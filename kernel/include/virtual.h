#ifndef ON_VIRTUAL_H
#define ON_VIRTUAL_H
#include "memory.h"
#define VMM_WRITE 1u
#define VMM_USER 2u
#define VMM_EXEC 4u
struct vmm_translation { uint64_t physical, page_size; unsigned permissions; };
/* Single bootstrap CPU. Adopt current four-level CR3; never reclaim boot tables.
   Mutations use initially empty PML4 slots only. Frames remain caller-owned.
   Failed calls preserve outputs and mappings. No huge-page splitting. */
bool vmm_init(const struct boot_memory_map *map);
bool vmm_translate(uintptr_t address, struct vmm_translation *result);
bool vmm_map(uintptr_t address, uint64_t physical, unsigned permissions);
bool vmm_protect(uintptr_t address, unsigned permissions);
bool vmm_unmap(uintptr_t address, uint64_t *physical);
void vmm_boot_start(const struct boot_memory_map *map);
#endif
