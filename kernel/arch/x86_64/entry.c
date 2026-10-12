#include "limine_base.h"
#include "kernel.h"
#include "arch.h"

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t requests_start[] = LIMINE_REQUESTS_START_MARKER;
__attribute__((used, section(".limine_requests")))
static volatile uint64_t base_revision[] = LIMINE_BASE_REVISION(6);
__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0,
};
__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID, .revision = 0,
};
__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID, .revision = 0,
};
static struct boot_memory_range memory_ranges[PMM_MAX_RANGES];

/* Copy responses; no protocol structures escape into the portable allocator. */
static bool read_memory_map(struct boot_memory_map *map) {
    const struct limine_memmap_response *response = memmap_request.response;
    const struct limine_hhdm_response *hhdm = hhdm_request.response;
#if ON_PMM_TEST == 2
    response = 0; /* Controlled missing-response fallback. */
#elif ON_PMM_TEST == 3
    hhdm = 0;
#endif
    if (!response || !hhdm || !response->entries || !response->entry_count ||
        response->entry_count > PMM_MAX_RANGES ||
        hhdm->offset < UINT64_C(0xffff800000000000) || hhdm->offset % PMM_PAGE_SIZE) return false;
    for (size_t i = 0; i < response->entry_count; ++i) {
        const struct limine_memmap_entry *entry = response->entries[i];
        if (!entry) return false;
        memory_ranges[i] = (struct boot_memory_range){
            entry->base, entry->length, entry->type == LIMINE_MEMMAP_USABLE,
        };
    }
    *map = (struct boot_memory_map){memory_ranges, response->entry_count, hhdm->offset};
    return true;
}

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t requests_end[] = LIMINE_REQUESTS_END_MARKER;

__attribute__((noreturn)) void kmain(void) {
    arch_debug_init();
    arch_cpu_init();
    bool supported = LIMINE_BASE_REVISION_SUPPORTED(base_revision);
    struct kernel_framebuffer framebuffer;
    const struct kernel_framebuffer *screen = 0;
    /* Never inspect bootloader responses if base negotiation failed. */
    if (supported) {
        struct limine_framebuffer_response *response = framebuffer_request.response;
        if (response && response->framebuffer_count && response->framebuffers &&
            response->framebuffers[0]) {
            const struct limine_framebuffer *source = response->framebuffers[0];
            framebuffer.address = source->address;
            framebuffer.width = source->width;
            framebuffer.height = source->height;
            framebuffer.pitch = source->pitch;
            framebuffer.bpp = source->bpp;
            framebuffer.memory_model = source->memory_model;
            framebuffer.red_size = source->red_mask_size;
            framebuffer.red_shift = source->red_mask_shift;
            framebuffer.green_size = source->green_mask_size;
            framebuffer.green_shift = source->green_mask_shift;
            framebuffer.blue_size = source->blue_mask_size;
            framebuffer.blue_shift = source->blue_mask_shift;
            screen = &framebuffer;
        }
    }
    struct boot_memory_map memory;
    const struct boot_memory_map *map = supported && read_memory_map(&memory) ? &memory : 0;
    kernel_main(supported, screen, map);
}
