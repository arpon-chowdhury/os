#include "heap.h"
#define REQUIRE(condition) do { if (!(condition)) return false; } while (0)
static bool check(const unsigned char *p, size_t size, unsigned char value) {
    for (size_t i = 0; i < size; ++i) if (p[i] != value) return false;
    return true;
}
static void fill(unsigned char *p, size_t size, unsigned char value) {
    for (size_t i = 0; i < size; ++i) p[i] = value;
}
bool heap_stress(void) {
    struct kheap_statistics initial = kheap_get_statistics();
    REQUIRE(kheap_validate() && !initial.active_allocations && initial.free_blocks == 1);
    REQUIRE(!kmalloc(0) && !kmalloc(SIZE_MAX) && !kmalloc_aligned(1, 0) &&
        !kmalloc_aligned(1, 3) && !kmalloc_aligned(1, (size_t)1 << (sizeof(size_t) * 8 - 1)));
    REQUIRE(kfree(0) && !kfree((void *)(uintptr_t)1) && !kfree((void *)UINTPTR_MAX));
    for (size_t alignment = 1; alignment <= 65536; alignment *= 2) {
        unsigned char *p = kmalloc_aligned(37, alignment);
        REQUIRE(p && !((uintptr_t)p % alignment) && !((uintptr_t)p % KHEAP_ALIGNMENT));
        fill(p, 37, 0xa5);
        REQUIRE(!kfree(p + 1) && check(p, 37, 0xa5) && kfree(p) && !kfree(p));
    }
    void *a = kmalloc(1024), *b = kmalloc(1024), *c = kmalloc(1024), *d = kmalloc(1024);
    REQUIRE(a && b && c && d);
    REQUIRE(kfree(b) && kfree(a) && kfree(c));
    void *combined = kmalloc(3072);
    REQUIRE(combined == a && kfree(combined) && kfree(d));
    void *whole = kmalloc(initial.free_bytes);
    REQUIRE(whole && !kmalloc(1) && kfree(whole));

    static unsigned char *pointers[128];
    static size_t sizes[128];
    static unsigned char values[128];
    uint32_t random = 0x13579bdf;
    for (unsigned iteration = 0; iteration < 20000; ++iteration) {
        random = random * 1664525u + 1013904223u;
        unsigned slot = (random >> 16) & 127;
        if (pointers[slot]) {
            REQUIRE(check(pointers[slot], sizes[slot], values[slot]));
            REQUIRE(kfree(pointers[slot]));
            pointers[slot] = 0;
        } else {
            sizes[slot] = 1 + ((random >> 3) & 8191);
            size_t alignment = (size_t)16 << ((random >> 24) & 7);
            pointers[slot] = kmalloc_aligned(sizes[slot], alignment);
            if (pointers[slot]) {
                REQUIRE(!((uintptr_t)pointers[slot] % alignment));
                values[slot] = (unsigned char)(random >> 8);
                fill(pointers[slot], sizes[slot], values[slot]);
                for (unsigned j = 0; j < 128; ++j) {
                    if (j == slot || !pointers[j]) continue;
                    uintptr_t x = (uintptr_t)pointers[slot], y = (uintptr_t)pointers[j];
                    REQUIRE(x + sizes[slot] <= y || y + sizes[j] <= x);
                }
            }
        }
        REQUIRE(kheap_validate());
        struct kheap_statistics current = kheap_get_statistics();
        size_t requested = 0, live = 0;
        for (unsigned j = 0; j < 128; ++j) if (pointers[j]) {
            requested += sizes[j];
            ++live;
            if (!(iteration & 255)) REQUIRE(check(pointers[j], sizes[j], values[j]));
        }
        REQUIRE(current.requested_bytes == requested && current.active_allocations == live &&
            current.arena_bytes == current.metadata_bytes + current.free_bytes + current.allocated_bytes);
    }
    for (unsigned j = 0; j < 128; ++j) if (pointers[j]) {
        REQUIRE(check(pointers[j], sizes[j], values[j]) && kfree(pointers[j]));
        pointers[j] = 0;
    }
    void *head = 0;
    size_t count = 0;
    void *page;
    while ((page = kmalloc(1024))) {
        *(void **)page = head;
        head = page;
        REQUIRE(++count <= initial.arena_bytes / 1024);
    }
    REQUIRE(count > 0 && !kmalloc(1024));
    while (head) {
        void *next = *(void **)head;
        REQUIRE(kfree(head));
        head = next;
    }
    struct kheap_statistics final = kheap_get_statistics();
    REQUIRE(kheap_validate() && final.blocks == 1 && final.free_blocks == 1 &&
        !final.active_allocations && !final.requested_bytes && !final.allocated_bytes &&
        final.free_bytes == initial.free_bytes && final.metadata_bytes == initial.metadata_bytes &&
        final.allocation_count - initial.allocation_count == final.free_count - initial.free_count &&
        final.failed_allocations > initial.failed_allocations && final.invalid_frees > initial.invalid_frees);
    whole = kmalloc(initial.free_bytes);
    REQUIRE(whole && kfree(whole));
    return true;
}
