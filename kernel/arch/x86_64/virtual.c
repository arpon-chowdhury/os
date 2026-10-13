#include "arch.h"
#include "virtual.h"
#define PRESENT UINT64_C(1)
#define WRITE UINT64_C(2)
#define USER UINT64_C(4)
#define HUGE UINT64_C(128)
#define NX (UINT64_C(1) << 63)
#define ADDRESS UINT64_C(0x000ffffffffff000)
static uint64_t root, physical_mask;
static uintptr_t hhdm;
static bool ready, nx_enabled, owned[512];
static struct boot_memory_range ranges[PMM_MAX_RANGES];
static size_t range_count;
static bool canonical(uint64_t address) {
    return address <= UINT64_C(0x00007fffffffffff) || address >= UINT64_C(0xffff800000000000);
}
static uint64_t *table(uint64_t physical) {
    if (physical & ~physical_mask || physical > UINTPTR_MAX - hhdm - (PMM_PAGE_SIZE - 1)) return 0;
    for (size_t i = 0; i < range_count; ++i)
        if (physical >= ranges[i].base && physical - ranges[i].base <= ranges[i].length &&
            ranges[i].length - (physical - ranges[i].base) >= PMM_PAGE_SIZE)
            return (uint64_t *)(hhdm + physical);
    return 0;
}
static unsigned index_at(uintptr_t address, unsigned level) {
    return (address >> (12 + 9 * level)) & 511;
}
static void invalidate(uintptr_t address) {
    __asm__ volatile ("invlpg (%0)" : : "r"(address) : "memory");
}
static bool valid(uintptr_t address, unsigned permissions) {
    return ready && canonical(address) && !(address & 4095) &&
        !(permissions & ~(VMM_WRITE | VMM_USER | VMM_EXEC)) &&
        (!(permissions & VMM_USER) || address < UINT64_C(0x0000800000000000)) &&
        (nx_enabled || (permissions & VMM_EXEC));
}
static uint64_t bits(unsigned permissions) {
    return PRESENT | ((permissions & VMM_WRITE) ? WRITE : 0) |
        ((permissions & VMM_USER) ? USER : 0) | ((permissions & VMM_EXEC) ? 0 : NX);
}
bool vmm_init(const struct boot_memory_map *map) {
    uint64_t flags = arch_interrupt_save(), cr4, efer;
    bool ok = false;
    if (ready || !map || !map->ranges || !map->count || map->count > PMM_MAX_RANGES ||
        !pmm_get_statistics().usable_frames || !canonical(map->hhdm_offset) ||
        map->hhdm_offset < UINT64_C(0xffff800000000000) ||
        (map->hhdm_offset & 4095)) goto done;
    uint64_t end = 0;
    for (size_t i = 0; i < map->count; ++i) {
        if (!map->ranges[i].length || map->ranges[i].base < end ||
            map->ranges[i].length > UINT64_MAX - map->ranges[i].base) goto done;
        end = map->ranges[i].base + map->ranges[i].length;
    }
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    if (cr4 & ((UINT64_C(1) << 12) | (UINT64_C(1) << 17))) goto done; /* LA57 is unsupported. */
    uint32_t a, b, c, d;
    __asm__ volatile ("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000000), "c"(0));
    unsigned width = 36;
    if (a >= 0x80000008) {
        __asm__ volatile ("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000008), "c"(0));
        width = a & 255;
    }
    if (width < 32 || width > 52) goto done;
    physical_mask = ((UINT64_C(1) << width) - 1) & ADDRESS;
    __asm__ volatile ("rdmsr" : "=a"(a), "=d"(d) : "c"(0xc0000080));
    efer = ((uint64_t)d << 32) | a;
    nx_enabled = (efer & (UINT64_C(1) << 11)) != 0;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(root));
    root &= ADDRESS;
    hhdm = map->hhdm_offset;
    range_count = map->count;
    for (size_t i = 0; i < range_count; ++i) ranges[i] = map->ranges[i];
    if (!table(root)) goto done;
    uint64_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= UINT64_C(1) << 16; /* Enforce read-only leaves in supervisor mode. */
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");
    cr4 &= ~(UINT64_C(1) << 7); /* No inherited global TLB entries in process roots. */
    __asm__ volatile ("mov %0, %%cr4" : : "r"(cr4) : "memory");
    ready = true;
    ok = true;
done:
    arch_interrupt_restore(flags);
    return ok;
}
bool vmm_translate(uintptr_t address, struct vmm_translation *result) {
    uint64_t flags = arch_interrupt_save();
    bool ok = false;
    unsigned permissions = VMM_WRITE | VMM_USER | VMM_EXEC;
    uint64_t physical = root;
    if (!ready || !canonical(address) || !result) goto done;
    for (int level = 3; level >= 0; --level) {
        uint64_t *entries = table(physical);
        if (!entries) goto done;
        uint64_t entry = entries[index_at(address, level)];
        if (!(entry & PRESENT) || ((entry & ADDRESS) & ~physical_mask)) goto done;
        if (!(entry & WRITE)) permissions &= ~VMM_WRITE;
        if (!(entry & USER)) permissions &= ~VMM_USER;
        if (entry & NX) permissions &= ~VMM_EXEC;
        if (!level || (entry & HUGE)) {
            if (level == 3) goto done;
            uint64_t size = UINT64_C(1) << (12 + 9 * level);
            /* Bit 12 is PAT for huge leaves, not part of their address. */
            if (level && (entry & ADDRESS & (size - 1) & ~UINT64_C(4096))) goto done;
            *result = (struct vmm_translation){(entry & ADDRESS & ~(size - 1)) |
                (address & (size - 1)), size, permissions};
            ok = true;
            goto done;
        }
        physical = entry & ADDRESS;
    }
done:
    arch_interrupt_restore(flags);
    return ok;
}
bool vmm_map(uintptr_t address, uint64_t physical, unsigned permissions) {
    uint64_t flags = arch_interrupt_save();
    bool ok = false;
    uint64_t *parents[3];
    uint64_t allocated[3];
    unsigned count = 0, slot = index_at(address, 3);
    uint64_t *entries = table(root);
    if (!valid(address, permissions) || (physical & ~physical_mask) || !entries ||
        (!owned[slot] && entries[slot])) goto done;
    for (int level = 3; level > 0; --level) {
        uint64_t *entry = &entries[index_at(address, level)];
        if (!*entry) {
            uint64_t page;
            if (!pmm_alloc_page(&page)) goto rollback;
            uint64_t *child = pmm_page_pointer(page);
            if (!child || (page & ~physical_mask)) { pmm_free_page(page); goto rollback; }
            for (unsigned i = 0; i < 512; ++i) child[i] = 0;
            parents[count] = entry;
            allocated[count++] = page;
            *entry = page | PRESENT | WRITE | USER;
        }
        if (!(*entry & PRESENT) || (*entry & HUGE)) goto rollback;
        entries = table(*entry & ADDRESS);
        if (!entries) goto rollback;
    }
    if (entries[index_at(address, 0)]) goto rollback;
    entries[index_at(address, 0)] = physical | bits(permissions);
    owned[slot] = true;
    invalidate(address);
    ok = true;
    goto done;
rollback:
    while (count) { --count; *parents[count] = 0; invalidate(address); pmm_free_page(allocated[count]); }
done:
    arch_interrupt_restore(flags);
    return ok;
}
static bool change(uintptr_t address, unsigned permissions, uint64_t *physical, bool remove) {
    uint64_t flags = arch_interrupt_save();
    bool ok = false;
    uint64_t *path[4], pages[4];
    if (!valid(address, remove ? VMM_EXEC : permissions) || !owned[index_at(address, 3)]) goto done;
    pages[3] = root;
    for (int level = 3; level >= 0; --level) {
        uint64_t *entries = table(pages[level]);
        if (!entries) goto done;
        path[level] = &entries[index_at(address, level)];
        if (!(*path[level] & PRESENT) || (level && (*path[level] & HUGE))) goto done;
        if (level) pages[level - 1] = *path[level] & ADDRESS;
    }
    uint64_t old = *path[0] & ADDRESS;
    *path[0] = remove ? 0 : old | bits(permissions);
    invalidate(address);
    if (remove) {
        for (unsigned level = 0; level < 3; ++level) {
            uint64_t *entries = table(pages[level]);
            bool empty = true;
            for (unsigned i = 0; i < 512; ++i) if (entries[i]) { empty = false; break; }
            if (!empty) break;
            *path[level + 1] = 0;
            invalidate(address);
            pmm_free_page(pages[level]);
        }
        if (!*path[3]) owned[index_at(address, 3)] = false;
        if (physical) *physical = old;
    }
    ok = true;
done:
    arch_interrupt_restore(flags);
    return ok;
}
bool vmm_protect(uintptr_t address, unsigned permissions) { return change(address, permissions, 0, false); }
bool vmm_unmap(uintptr_t address, uint64_t *physical) { return change(address, 0, physical, true); }

/* Process roots own no boot lower-half mappings. Shared kernel tables are
   copied only as supervisor PML4 entries on every switch, including new stacks. */
uint64_t arch_address_kernel_root(void) {return ready && nx_enabled ? root : 0;}
uint64_t arch_address_current_root(void) {
    uint64_t value;__asm__ volatile ("mov %%cr3,%0" : "=r"(value));return value&ADDRESS;
}
void arch_address_activate(uint64_t process_root) {
    uint64_t selected=process_root?process_root:root;
    if(!ready) {if(process_root) arch_halt();return;}
    if(process_root) {
        uint64_t *p=pmm_page_pointer(process_root),*kernel=table(root);
        if(!p || !kernel) arch_halt();
        for(unsigned i=256;i<512;++i) p[i]=kernel[i]&~USER;
    }
    /* PCID and global pages are disabled: every switch flushes stale mappings. */
    __asm__ volatile ("mov %0,%%cr3" : : "r"(selected) : "memory");
}
