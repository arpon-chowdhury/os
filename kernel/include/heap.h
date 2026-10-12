#ifndef ON_HEAP_H
#define ON_HEAP_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define KHEAP_ALIGNMENT 16u
#define KHEAP_BOOT_BYTES (1024u * 1024u)
struct kheap_statistics {
    size_t arena_bytes, requested_bytes, allocated_bytes, free_bytes, metadata_bytes;
    size_t blocks, free_blocks, active_allocations, peak_requested_bytes;
    uint64_t allocation_count, free_count, failed_allocations, invalid_frees;
};
/* Initialize one writable, exclusively owned, 16-byte aligned arena. On failure
   no arena memory is written. Successful initialization cannot be repeated. */
bool kheap_init(void *arena, size_t bytes);
/* Zero size, overflow, exhaustion, or invalid alignment returns NULL.
   Alignment must be a nonzero power of two; at least 16-byte alignment is given.
   Memory is uninitialized. Caller must not access beyond requested size. */
void *kmalloc(size_t bytes);
void *kmalloc_aligned(size_t bytes, size_t alignment);
/* NULL is a successful no-op. Other pointers must exactly match a live
   allocation. Invalid/interior/double frees are rejected without dereferencing
   the supplied pointer. Stale pointers after address reuse cannot be detected. */
bool kfree(void *pointer);
bool kheap_validate(void);
struct kheap_statistics kheap_get_statistics(void);
void kheap_boot_start(void);
#endif
