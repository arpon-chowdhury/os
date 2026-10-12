#include "arch.h"
#include "heap.h"
#include "virtual.h"
#define HEAP_BASE UINT64_C(0xffffd00000000000)
#define HEAP_PAGES (KHEAP_BOOT_BYTES / PMM_PAGE_SIZE)
static uint64_t frames[HEAP_PAGES];
static bool boot_ready;
#if ON_HEAP_TEST
extern bool heap_stress(void);
static void require(bool condition) {
    if (!condition) {
        arch_debug_puts("ON.: HEAP TEST FAILED; halted safely.\n");
        arch_halt();
    }
}
#endif
static bool provision(void) {
    if (boot_ready) return false;
    size_t mapped = 0;
    for (; mapped < HEAP_PAGES; ++mapped) {
#if ON_HEAP_TEST == 2
        if (mapped == 3) goto rollback; /* Controlled partial provisioning failure. */
#endif
        uint64_t physical;
        if (!pmm_alloc_page(&physical)) goto rollback;
        if (!vmm_map(HEAP_BASE + mapped * PMM_PAGE_SIZE, physical, VMM_WRITE)) {
            pmm_free_page(physical);
            goto rollback;
        }
        frames[mapped] = physical;
    }
    if (!kheap_init((void *)HEAP_BASE, KHEAP_BOOT_BYTES)) goto rollback;
    boot_ready = true;
    return true;
rollback:
    while (mapped) {
        --mapped;
        /* These mappings belong exclusively to this provisioning call. */
        if (!vmm_unmap(HEAP_BASE + mapped * PMM_PAGE_SIZE, 0)) arch_halt();
        if (!pmm_free_page(frames[mapped])) arch_halt();
        frames[mapped] = 0;
    }
    return false;
}
void kheap_boot_start(void) {
#if ON_HEAP_TEST
    struct pmm_statistics before = pmm_get_statistics();
#endif
    if (!provision()) {
        arch_debug_puts("ON.: kernel heap unavailable; existing services retained.\n");
#if ON_HEAP_TEST == 1
        require(false);
#elif ON_HEAP_TEST == 2
        struct pmm_statistics after = pmm_get_statistics();
        struct vmm_translation translation;
        require(before.free_frames == after.free_frames && before.allocated_frames == after.allocated_frames);
        for (unsigned i = 0; i < HEAP_PAGES; ++i)
            require(!vmm_translate(HEAP_BASE + i * PMM_PAGE_SIZE, &translation));
        require(!kmalloc(1) && !kheap_validate() && !kheap_get_statistics().arena_bytes);
        arch_debug_puts("ON.: HEAP TEST provisioning rollback passed.\n");
#endif
        return;
    }
    arch_debug_puts("ON.: kernel heap ready; 1 MiB, supervisor writable NX.\n");
#if ON_HEAP_TEST == 1
    struct pmm_statistics provisioned = pmm_get_statistics();
    struct vmm_translation translation;
    for (unsigned i = 0; i < HEAP_PAGES; ++i)
        require(vmm_translate(HEAP_BASE + i * PMM_PAGE_SIZE, &translation) &&
            translation.physical == frames[i] && translation.permissions == VMM_WRITE);
    require(before.free_frames - provisioned.free_frames == HEAP_PAGES + 3);
    require(heap_stress());
    require(pmm_get_statistics().free_frames == provisioned.free_frames);
    arch_debug_puts("ON.: HEAP TEST alignment, coalescing, exhaustion, statistics, and stress passed.\n");
#elif ON_HEAP_TEST == 2
    require(false);
#endif
}
