#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "arch.h"
#include "heap.h"
static _Alignas(16) unsigned char arena[KHEAP_BOOT_BYTES + 32];
static uint64_t simulated_if = 0x200;
static bool locked;
extern bool heap_stress(void);
uint64_t arch_interrupt_save(void) {
    assert(!locked);
    locked = true;
    uint64_t flags = simulated_if;
    simulated_if = 0;
    return flags;
}
void arch_interrupt_restore(uint64_t flags) {
    assert(locked && !simulated_if);
    locked = false;
    simulated_if = flags & 0x200;
}
int main(void) {
    memset(arena, 0xa5, sizeof(arena));
    assert(!kmalloc(1) && !kheap_validate() && !kfree((void *)1) && kfree(0));
    assert(!kheap_init(0, KHEAP_BOOT_BYTES));
    assert(!kheap_init(arena + 1, KHEAP_BOOT_BYTES));
    assert(!kheap_init(arena, 16));
    assert(!kheap_init(arena, KHEAP_BOOT_BYTES - 1));
    assert(!kheap_init((void *)(UINTPTR_MAX & ~(uintptr_t)15), 128));
    for (size_t i = 0; i < sizeof(arena); ++i) assert(arena[i] == 0xa5);
    assert(kheap_init(arena + 16, KHEAP_BOOT_BYTES));
    assert(!kheap_init(arena, KHEAP_BOOT_BYTES));
    assert(heap_stress() && simulated_if == 0x200 && !locked);
    simulated_if = 0;
    assert(heap_stress() && !simulated_if && !locked);
    for (unsigned i = 0; i < 16; ++i) {
        assert(arena[i] == 0xa5);
        assert(arena[sizeof(arena) - 1 - i] == 0xa5);
    }
    /* An out-of-bounds metadata span must fail closed, not walk arbitrary RAM. */
    size_t span;
    memcpy(&span, arena + 16, sizeof(span));
    size_t corrupt = SIZE_MAX;
    memcpy(arena + 16, &corrupt, sizeof(corrupt));
    assert(!kheap_validate() && !kmalloc(1) && !kfree(arena + 64));
    memcpy(arena + 16, &span, sizeof(span));
    assert(kheap_validate());
    puts("Heap alignment, guards, invalid frees, overflow, coalescing, exhaustion, 40000 stress operations, metadata bounds, statistics, and IF preservation passed.");
}
