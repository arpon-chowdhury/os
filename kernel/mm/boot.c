#include "arch.h"
#include "memory.h"

static void log_number(const char *label, uint64_t value) {
    static const char digits[] = "0123456789abcdef";
    char text[20] = "0x0000000000000000\n";
    for (unsigned int i = 0; i < 16; ++i) text[2 + i] = digits[(value >> ((15 - i) * 4)) & 15];
    arch_debug_puts(label);
    arch_debug_puts(text);
}

#if ON_PMM_TEST == 1
static void require(bool condition) {
    if (!condition) {
        arch_debug_puts("ON.: PMM TEST FAILED; halted safely.\n");
        arch_halt();
    }
}
static bool usable_page(const struct boot_memory_map *map, uint64_t physical) {
    if (!physical || physical % PMM_PAGE_SIZE) return false;
    for (size_t i = 0; i < map->count; ++i) {
        const struct boot_memory_range *entry = &map->ranges[i];
        if (entry->usable && physical >= entry->base &&
            physical - entry->base <= entry->length &&
            entry->length - (physical - entry->base) >= PMM_PAGE_SIZE) return true;
    }
    return false;
}
static void selftest(const struct boot_memory_map *map) {
    struct pmm_statistics initial = pmm_get_statistics();
    require(initial.free_frames > 3 && initial.allocated_frames == 0);
    require(!pmm_free_page(0) && !pmm_free_page(UINT64_MAX));
    for (uint64_t i = 0; i < initial.metadata_frames; ++i)
        require(!pmm_free_page(initial.metadata_base + i * PMM_PAGE_SIZE));
    for (size_t i = 0; i < map->count; ++i)
        if (!map->ranges[i].usable)
            require(!pmm_free_page(map->ranges[i].base & ~(PMM_PAGE_SIZE - 1)));
    uint64_t page, second;
    require(!pmm_alloc_page(0));
    require(pmm_alloc_page(&page) && usable_page(map, page));
    require(!pmm_free_page(page + 1));
    require(pmm_free_page(page) && !pmm_free_page(page));
    require(pmm_page_pointer(page) == 0);
    require(pmm_alloc_page(&second) && second == page && pmm_free_page(second));
    require(pmm_get_statistics().free_frames == initial.free_frames);
    arch_debug_puts("ON.: PMM TEST alignment, reserved protection, and reuse passed.\n");

    /* Use owned frames as a temporary linked list, avoiding a fixed test-array
       limit. Touch both ends of every page through the reported HHDM mapping. */
    uint64_t head = 0, count = 0;
    while (pmm_alloc_page(&page)) {
        require(usable_page(map, page));
        uint64_t *words = pmm_page_pointer(page);
        require(words != 0);
        words[0] = head;
        words[PMM_PAGE_SIZE / sizeof(uint64_t) - 1] = page ^ UINT64_C(0x55aa55aa55aa55aa);
        head = page;
        ++count;
        require(count <= initial.free_frames);
    }
    struct pmm_statistics exhausted = pmm_get_statistics();
    require(count == initial.free_frames && exhausted.free_frames == 0 &&
            exhausted.allocated_frames == count);
    page = UINT64_MAX;
    require(!pmm_alloc_page(&page) && page == UINT64_MAX);
    log_number("pmm_test_exhausted_frames=", count);
    while (head) {
        require(count > 0);
        uint64_t *words = pmm_page_pointer(head);
        require(words != 0);
        require(words[PMM_PAGE_SIZE / sizeof(uint64_t) - 1] ==
                (head ^ UINT64_C(0x55aa55aa55aa55aa)));
        uint64_t next = words[0];
        require(pmm_free_page(head) && !pmm_free_page(head));
        head = next;
        --count;
    }
    require(count == 0);
    struct pmm_statistics restored = pmm_get_statistics();
    require(restored.free_frames == initial.free_frames && restored.allocated_frames == 0 &&
            restored.metadata_frames == initial.metadata_frames);
    /* Allocation still works after exhaustion and full restoration. */
    require(pmm_alloc_page(&page) && pmm_free_page(page));
    log_number("pmm_test_restored_frames=", restored.free_frames);
    arch_debug_puts("ON.: PMM TEST exhaustion and full restoration passed.\n");
}
#endif

void pmm_boot_start(const struct boot_memory_map *map) {
    if (!pmm_init(map)) {
        arch_debug_puts("ON.: physical memory unavailable; allocator disabled.\n");
#if ON_PMM_TEST == 1
        require(false);
#elif ON_PMM_TEST == 2 || ON_PMM_TEST == 3
        uint64_t physical = UINT64_MAX;
        struct pmm_statistics empty = pmm_get_statistics();
        if (pmm_alloc_page(&physical) || physical != UINT64_MAX || pmm_free_page(0) ||
            pmm_page_pointer(0) || empty.map_bytes || empty.free_frames) {
            arch_debug_puts("ON.: PMM TEST FAILED; halted safely.\n");
            arch_halt();
        }
        arch_debug_puts("ON.: PMM TEST missing-response fallback passed.\n");
#endif
        return;
    }
#if ON_PMM_TEST == 2 || ON_PMM_TEST == 3
    arch_debug_puts("ON.: PMM TEST FAILED; unexpected initialization.\n");
    arch_halt();
#endif
    struct pmm_statistics current = pmm_get_statistics();
    arch_debug_puts("ON.: physical memory ready; 4096-byte frames.\n");
    log_number("pmm_map_bytes=", current.map_bytes);
    log_number("pmm_reserved_bytes=", current.reserved_bytes);
    log_number("pmm_usable_frames=", current.usable_frames);
    log_number("pmm_metadata_frames=", current.metadata_frames);
    log_number("pmm_metadata_base=", current.metadata_base);
    log_number("pmm_allocated_frames=", current.allocated_frames);
    log_number("pmm_free_frames=", current.free_frames);
#if ON_PMM_TEST == 1
    selftest(map);
#endif
}
