#ifndef ON_ADDRESS_SPACE_H
#define ON_ADDRESS_SPACE_H
#include "virtual.h"
/* Trusted kernel handle. Owns root, lower-half tables and all user frames.
   No borrowed physical frames, huge leaves, executable writable pages or aliases. */
struct address_space { uint64_t root; unsigned pages; };
#define USER_PAGE_LIMIT 16u
bool address_space_create(struct address_space *space);
bool address_space_map(struct address_space *space, uintptr_t address,
                       const void *data, size_t length, unsigned permissions);
bool address_space_translate(const struct address_space *space, uintptr_t address,
                             struct vmm_translation *result);
/* Owned anonymous memory service: permits active-root RW/NX mapping.
   Unmap releases the leaf and empty private tables, invalidating active TLBs. */
bool address_space_map_zero(struct address_space *space,uintptr_t address);
bool address_space_unmap(struct address_space *space,uintptr_t address);
bool address_space_copy_in(const struct address_space *space,void *out,uintptr_t user,size_t length);
bool address_space_copy_out(const struct address_space *space,uintptr_t user,const void *in,size_t length);
bool address_space_destroy(struct address_space *space); /* Inactive only. */
/* Architecture hooks. Activating zero selects the adopted kernel root. */
uint64_t arch_address_kernel_root(void);
uint64_t arch_address_current_root(void);
void arch_address_activate(uint64_t root);
void arch_address_invalidate(uint64_t root,uintptr_t address);
#endif
