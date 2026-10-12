#include "arch.h"
#include "virtual.h"
#if ON_VMM_TEST
static void require(bool condition) {
    if (!condition) {
        arch_debug_puts("ON.: VMM TEST FAILED; halted safely.\n");
        arch_halt();
    }
}
static void selftest(void) {
    uintptr_t address = UINT64_C(0x0000400000000000);
    struct vmm_translation before, after;
    uint64_t initial = pmm_get_statistics().free_frames, page = 0, second = 0, output = UINT64_MAX;
    require(vmm_translate((uintptr_t)selftest, &before));
    require(!vmm_map((uintptr_t)selftest & ~UINT64_C(4095), 0, VMM_EXEC));
    require(!vmm_unmap((uintptr_t)selftest & ~UINT64_C(4095), &output));
    require(output == UINT64_MAX);
    require(!vmm_translate(UINT64_C(0x800000000000), &after));
    require(!vmm_map(address + 1, 0, VMM_WRITE));
    require(!vmm_map(address, 1, VMM_WRITE));
    require(!vmm_map(address, 0, 8));
    require(pmm_alloc_page(&page) && pmm_alloc_page(&second));
    require(vmm_map(address, page, VMM_WRITE));
    require(vmm_map(address + 4096, second, VMM_WRITE));
    require(!vmm_map(address, second, VMM_WRITE));
    volatile uint64_t *alias = (volatile uint64_t *)address;
    *alias = UINT64_C(0x123456789abcdef0);
    require(*(uint64_t *)pmm_page_pointer(page) == *alias);
    require(vmm_translate(address + 123, &after) && after.physical == page + 123 &&
        after.page_size == 4096 && after.permissions == VMM_WRITE);
    require(vmm_protect(address, VMM_EXEC));
    require(vmm_translate(address, &after) && after.permissions == VMM_EXEC);
    require(vmm_protect(address, VMM_WRITE | VMM_USER));
    require(vmm_translate(address, &after) && after.permissions == (VMM_WRITE | VMM_USER));
    require(vmm_unmap(address, &output) && output == page);
    require(!vmm_translate(address, &after) && !vmm_unmap(address, 0));
    require(vmm_map(address, second, VMM_WRITE));
    *alias = UINT64_C(0xfedcba9876543210);
    require(*(uint64_t *)pmm_page_pointer(second) == *alias &&
        *(uint64_t *)pmm_page_pointer(page) == UINT64_C(0x123456789abcdef0));
    require(vmm_unmap(address, 0) && vmm_unmap(address + 4096, 0));
    require(pmm_free_page(page) && pmm_free_page(second));
    require(pmm_get_statistics().free_frames == initial);
    require(vmm_translate((uintptr_t)selftest, &after) && before.physical == after.physical &&
        before.permissions == after.permissions && before.page_size == after.page_size);
    /* Cover separate PTs, PDs, PDPTs, PML4 slots, and canonical upper half. */
    const uintptr_t scattered[] = {address, address + (UINT64_C(1) << 21),
        address + (UINT64_C(1) << 30), address + (UINT64_C(1) << 39),
        UINT64_C(0xffffc00000000000)};
    require(pmm_alloc_page(&page));
    for (unsigned i = 0; i < sizeof(scattered) / sizeof(scattered[0]); ++i)
        require(vmm_map(scattered[i], page, VMM_WRITE));
    for (unsigned i = 0; i < sizeof(scattered) / sizeof(scattered[0]); ++i) {
        require(vmm_translate(scattered[i] + 4095, &after) && after.physical == page + 4095);
        require(vmm_unmap(scattered[i], 0));
    }
    require(pmm_free_page(page) && pmm_get_statistics().free_frames == initial);
    /* Fail after allocating two of three required tables; no published leaf,
       no leaked table frames, and the top-level slot must be reusable. */
    uint64_t head = 0;
    while (pmm_get_statistics().free_frames > 2) {
        require(pmm_alloc_page(&page));
        *(uint64_t *)pmm_page_pointer(page) = head;
        head = page;
    }
    require(!vmm_map(address, 0, VMM_WRITE));
    require(pmm_get_statistics().free_frames == 2 && !vmm_translate(address, &after));
    while (head) {
        uint64_t next = *(uint64_t *)pmm_page_pointer(head);
        require(pmm_free_page(head));
        head = next;
    }
    require(pmm_get_statistics().free_frames == initial);
    require(vmm_map(address, 0, VMM_WRITE) && vmm_unmap(address, 0));
    require(pmm_get_statistics().free_frames == initial);
    arch_debug_puts("ON.: VMM TEST allocation rollback passed.\n");
    arch_debug_puts("ON.: VMM TEST translation, permissions, remap TLB, and reclamation passed.\n");
}
#endif
void vmm_boot_start(const struct boot_memory_map *map) {
    if (!vmm_init(map)) {
        arch_debug_puts("ON.: virtual memory unavailable; boot mappings retained.\n");
#if ON_VMM_TEST
        require(false);
#endif
        return;
    }
    arch_debug_puts("ON.: virtual memory ready; boot mappings retained.\n");
#if ON_VMM_TEST
    selftest();
#endif
}
