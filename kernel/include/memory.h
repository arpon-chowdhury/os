#ifndef ON_MEMORY_H
#define ON_MEMORY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PMM_PAGE_SIZE UINT64_C(4096)
#define PMM_MAX_RANGES 256
/* Bootloader-independent copy. Every type other than usable stays reserved. */
struct boot_memory_range { uint64_t base, length; bool usable; };
struct boot_memory_map {
    const struct boot_memory_range *ranges;
    size_t count;
    uintptr_t hhdm_offset;
};
struct pmm_statistics {
    uint64_t map_bytes, reserved_bytes;
    uint64_t usable_frames, metadata_frames, allocated_frames, free_frames;
    uint64_t metadata_base;
};
/* One successful initialization; malformed maps fail closed without writes. */
bool pmm_init(const struct boot_memory_map *map);
/* Single frames, physical addresses, not zeroed. Failure preserves *physical. */
bool pmm_alloc_page(uint64_t *physical);
/* Reject unaligned, reserved, metadata, never-allocated, and double-free pages. */
bool pmm_free_page(uint64_t physical);
struct pmm_statistics pmm_get_statistics(void);
/* Borrow an HHDM pointer only for a currently allocated page; NULL otherwise.
   The caller must retain ownership for the duration of all accesses. */
void *pmm_page_pointer(uint64_t physical);
void pmm_boot_start(const struct boot_memory_map *map);
#endif
