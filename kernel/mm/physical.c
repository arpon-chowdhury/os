#include "arch.h"
#include "memory.h"

/* A compact bitmap covers usable frames only, so MMIO holes and high reserved
   addresses consume no bitmap space. Range descriptors live in kernel BSS. */
struct frame_range { uint64_t base, frames, first_bit; };
static struct frame_range ranges[PMM_MAX_RANGES];
static size_t range_count;
static uint8_t *bitmap;
static uintptr_t direct_map;
static uint64_t cursor;
static bool ready;
static struct pmm_statistics stats;

static bool occupied(uint64_t bit) { return bitmap[bit / 8] & (1u << (bit % 8)); }
static void set_bit(uint64_t bit, bool used) {
    if (used) bitmap[bit / 8] |= 1u << (bit % 8);
    else bitmap[bit / 8] &= ~(1u << (bit % 8));
}
static bool metadata(uint64_t physical) {
    return physical >= stats.metadata_base &&
           physical - stats.metadata_base < stats.metadata_frames * PMM_PAGE_SIZE;
}
static bool find_bit(uint64_t physical, uint64_t *bit) {
    if (!ready || physical == 0 || physical % PMM_PAGE_SIZE || metadata(physical)) return false;
    for (size_t i = 0; i < range_count; ++i) {
        if (physical >= ranges[i].base &&
            (physical - ranges[i].base) / PMM_PAGE_SIZE < ranges[i].frames) {
            *bit = ranges[i].first_bit + (physical - ranges[i].base) / PMM_PAGE_SIZE;
            return true;
        }
    }
    return false;
}

bool pmm_init(const struct boot_memory_map *map) {
    uint64_t flags = arch_interrupt_save();
    bool ok = false;
    if (ready || !map || !map->ranges || !map->count ||
        map->count > PMM_MAX_RANGES || !map->hhdm_offset ||
        map->hhdm_offset % PMM_PAGE_SIZE) goto done;
    uint64_t previous_end = 0, total = 0, frames = 0;
    size_t count = 0;
    /* Validate the entire map before touching any physical memory. Accept only
       sorted, disjoint entries; gaps, unknown types and partial pages stay out. */
    for (size_t i = 0; i < map->count; ++i) {
        const struct boot_memory_range *entry = &map->ranges[i];
        if (!entry->length || entry->length > UINT64_MAX - entry->base ||
            entry->base < previous_end || entry->length > UINT64_MAX - total) goto done;
        uint64_t end = entry->base + entry->length;
        previous_end = end;
        total += entry->length;
        if (!entry->usable) continue;
        if (entry->base > UINT64_MAX - (PMM_PAGE_SIZE - 1)) goto done;
        uint64_t start = (entry->base + PMM_PAGE_SIZE - 1) & ~(PMM_PAGE_SIZE - 1);
        end &= ~(PMM_PAGE_SIZE - 1);
        if (start == 0) start = PMM_PAGE_SIZE; /* Permanently exclude frame zero. */
        if (start >= end) continue;
        if (end - 1 > UINTPTR_MAX - map->hhdm_offset) goto done;
        uint64_t length = (end - start) / PMM_PAGE_SIZE;
        ranges[count++] = (struct frame_range){start, length, frames};
        frames += length; /* Disjoint ranges bound the sum below 2^52 frames. */
    }
    uint64_t bytes = (frames + 7) / 8;
    uint64_t pages = (bytes + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;
    if (!pages || pages >= frames) goto done;
    size_t storage = 0;
    while (storage < count && ranges[storage].frames < pages) ++storage;
    if (storage == count) goto done;
    struct pmm_statistics initial = {
        .map_bytes = total,
        .reserved_bytes = total - frames * PMM_PAGE_SIZE,
        .usable_frames = frames,
        .metadata_frames = pages,
        .free_frames = frames - pages,
        .metadata_base = ranges[storage].base,
    };
    bitmap = (uint8_t *)(map->hhdm_offset + initial.metadata_base);
    /* Reserve entire metadata pages, including trailing bitmap padding. */
    for (uint64_t i = 0; i < pages * PMM_PAGE_SIZE; ++i) bitmap[i] = 0;
    for (uint64_t i = 0; i < pages; ++i) set_bit(ranges[storage].first_bit + i, true);
    range_count = count;
    direct_map = map->hhdm_offset;
    stats = initial;
    cursor = 0;
    ready = true;
    ok = true;
done:
    arch_interrupt_restore(flags);
    return ok;
}

bool pmm_alloc_page(uint64_t *physical) {
    uint64_t flags = arch_interrupt_save();
    bool ok = false;
    if (!ready || !physical || !stats.free_frames) goto done;
    uint64_t bit = cursor;
    for (uint64_t checked = 0; checked < stats.usable_frames; ++checked) {
        if (!occupied(bit)) {
            for (size_t i = 0; i < range_count; ++i) {
                if (bit >= ranges[i].first_bit && bit - ranges[i].first_bit < ranges[i].frames) {
                    *physical = ranges[i].base + (bit - ranges[i].first_bit) * PMM_PAGE_SIZE;
                    set_bit(bit, true);
                    --stats.free_frames;
                    ++stats.allocated_frames;
                    cursor = bit + 1 == stats.usable_frames ? 0 : bit + 1;
                    ok = true;
                    goto done;
                }
            }
        }
        bit = bit + 1 == stats.usable_frames ? 0 : bit + 1;
    }
done:
    arch_interrupt_restore(flags);
    return ok;
}

bool pmm_free_page(uint64_t physical) {
    uint64_t flags = arch_interrupt_save();
    uint64_t bit;
    bool ok = find_bit(physical, &bit) && occupied(bit);
    if (ok) {
        set_bit(bit, false);
        ++stats.free_frames;
        --stats.allocated_frames;
        /* Prefer reusing this frame and keep full allocation/free cycles linear. */
        cursor = bit;
    }
    arch_interrupt_restore(flags);
    return ok;
}

struct pmm_statistics pmm_get_statistics(void) {
    uint64_t flags = arch_interrupt_save();
    struct pmm_statistics result = stats;
    arch_interrupt_restore(flags);
    return result;
}

void *pmm_page_pointer(uint64_t physical) {
    uint64_t flags = arch_interrupt_save();
    uint64_t bit;
    void *result = find_bit(physical, &bit) && occupied(bit) ?
                   (void *)(direct_map + physical) : 0;
    arch_interrupt_restore(flags);
    return result;
}
