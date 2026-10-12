#include "arch.h"
#include "heap.h"

struct block {
    size_t span, requested;
    uintptr_t user;
    struct block *previous, *next;
};
#define HEADER ((sizeof(struct block) + KHEAP_ALIGNMENT - 1) & ~(size_t)(KHEAP_ALIGNMENT - 1))
static uintptr_t start, end;
static struct block *first;
static struct kheap_statistics counters;
static bool ready;

/* Check bounds before dereferencing each header; never trust client pointers.
   Contiguous spans and backward links bound traversal even if links corrupt. */
static bool valid(void) {
    if (!ready) return false;
    size_t requested = 0;
    uintptr_t cursor = start;
    struct block *previous = 0, *block = first;
    while (cursor < end) {
        if ((uintptr_t)block != cursor || end - cursor < HEADER) return false;
        if (block->span < HEADER + KHEAP_ALIGNMENT || block->span % KHEAP_ALIGNMENT ||
            block->span > end - cursor || block->previous != previous) return false;
        uintptr_t limit = cursor + block->span;
        requested += block->requested;
        if (block->user) {
            if (block->user % KHEAP_ALIGNMENT || block->user < cursor + HEADER ||
                block->user >= limit || !block->requested ||
                block->requested > limit - block->user) return false;
        } else if (block->requested || (previous && !previous->user)) return false;
        if ((uintptr_t)block->next != (limit == end ? 0 : limit)) return false;
        previous = block;
        block = block->next;
        cursor = limit;
    }
    return cursor == end && !block && requested == counters.requested_bytes;
}
bool kheap_init(void *arena, size_t bytes) {
    uint64_t flags = arch_interrupt_save();
    uintptr_t base = (uintptr_t)arena;
    bool ok = !ready && base && !(base % KHEAP_ALIGNMENT) &&
        bytes >= HEADER + KHEAP_ALIGNMENT && !(bytes % KHEAP_ALIGNMENT) &&
        bytes <= UINTPTR_MAX - base;
    if (ok) {
        start = base;
        end = base + bytes;
        first = arena;
        *first = (struct block){.span = bytes};
        ready = true;
    }
    arch_interrupt_restore(flags);
    return ok;
}
void *kmalloc_aligned(size_t bytes, size_t alignment) {
    uint64_t flags = arch_interrupt_save();
    void *result = 0;
    if (!bytes || !alignment || (alignment & (alignment - 1)) || !valid()) goto fail;
    if (alignment < KHEAP_ALIGNMENT) alignment = KHEAP_ALIGNMENT;
    for (struct block *block = first; block; block = block->next) {
        if (block->user) continue;
        uintptr_t payload = (uintptr_t)block + HEADER;
        if (payload > UINTPTR_MAX - (alignment - 1)) continue;
        uintptr_t user = (payload + alignment - 1) & ~(uintptr_t)(alignment - 1);
        size_t padding = user - (uintptr_t)block;
        if (padding > block->span || bytes > block->span - padding) continue;
        size_t used = padding + bytes;
        if (used > SIZE_MAX - (KHEAP_ALIGNMENT - 1)) continue;
        used = (used + KHEAP_ALIGNMENT - 1) & ~(size_t)(KHEAP_ALIGNMENT - 1);
        if (used > block->span) continue;
        if (block->span - used >= HEADER + KHEAP_ALIGNMENT) {
            struct block *tail = (struct block *)((uintptr_t)block + used);
            *tail = (struct block){.span = block->span - used,
                .previous = block, .next = block->next};
            if (tail->next) tail->next->previous = tail;
            block->next = tail;
            block->span = used;
        }
        block->user = user;
        block->requested = bytes;
        counters.requested_bytes += bytes;
        if (counters.requested_bytes > counters.peak_requested_bytes)
            counters.peak_requested_bytes = counters.requested_bytes;
        ++counters.allocation_count;
        result = (void *)user;
        goto done;
    }
fail:
    ++counters.failed_allocations;
done:
    arch_interrupt_restore(flags);
    return result;
}
void *kmalloc(size_t bytes) { return kmalloc_aligned(bytes, KHEAP_ALIGNMENT); }
static void merge_next(struct block *block) {
    struct block *next = block->next;
    block->span += next->span;
    block->next = next->next;
    if (block->next) block->next->previous = block;
}
bool kfree(void *pointer) {
    uint64_t flags = arch_interrupt_save();
    bool ok = !pointer;
    if (!pointer) goto done;
    if (!valid()) goto fail;
    for (struct block *block = first; block; block = block->next) {
        if (block->user != (uintptr_t)pointer) continue;
        counters.requested_bytes -= block->requested;
        block->user = 0;
        block->requested = 0;
        if (block->next && !block->next->user) merge_next(block);
        if (block->previous && !block->previous->user) merge_next(block->previous);
        ++counters.free_count;
        ok = true;
        goto done;
    }
fail:
    ++counters.invalid_frees;
done:
    arch_interrupt_restore(flags);
    return ok;
}
bool kheap_validate(void) {
    uint64_t flags = arch_interrupt_save();
    bool result = valid();
    arch_interrupt_restore(flags);
    return result;
}
struct kheap_statistics kheap_get_statistics(void) {
    uint64_t flags = arch_interrupt_save();
    struct kheap_statistics result = counters;
    if (valid()) {
        result.arena_bytes = end - start;
        for (struct block *block = first; block; block = block->next) {
            ++result.blocks;
            result.metadata_bytes += HEADER;
            if (block->user) {
                ++result.active_allocations;
                result.allocated_bytes += block->span - HEADER;
            } else {
                ++result.free_blocks;
                result.free_bytes += block->span - HEADER;
            }
        }
    }
    arch_interrupt_restore(flags);
    return result;
}
