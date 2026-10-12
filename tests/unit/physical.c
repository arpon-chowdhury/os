#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "arch.h"
#include "memory.h"

_Alignas(4096) static unsigned char arena[128 * 4096];
static uint64_t simulated_if = 0x200;
static unsigned int lock_depth;
uint64_t arch_interrupt_save(void) {
    assert(lock_depth == 0);
    ++lock_depth;
    uint64_t old = simulated_if;
    simulated_if = 0;
    return old;
}
void arch_interrupt_restore(uint64_t flags) {
    assert(lock_depth == 1 && simulated_if == 0);
    --lock_depth;
    simulated_if = flags & 0x200;
}
static void same_stats(struct pmm_statistics expected) {
    struct pmm_statistics actual = pmm_get_statistics();
    assert(memcmp(&expected, &actual, sizeof(actual)) == 0);
}
static void basic(void) {
    memset(arena, 0xa5, sizeof(arena));
    struct boot_memory_range entries[] = {
        {0, 4096, true}, {4096, 4096, false}, {8192, 7 * 4096, true},
        {9 * 4096, 3 * 4096, false}, {12 * 4096 + 1, 3 * 4096 - 1, true},
        {16 * 4096, 4 * 4096, true}, {UINT64_C(1) << 40, 4096, false},
    };
    struct boot_memory_map map = {entries, sizeof(entries) / sizeof(entries[0]), (uintptr_t)arena};
    assert(pmm_init(&map));
    struct pmm_statistics initial = pmm_get_statistics();
    assert(initial.usable_frames == 13 && initial.metadata_frames == 1 &&
           initial.allocated_frames == 0 && initial.free_frames == 12 && initial.metadata_base == 8192);
    assert(initial.map_bytes == 20 * 4096 - 1);
    assert(initial.reserved_bytes + initial.usable_frames * 4096 == initial.map_bytes);
    /* Initialization writes only the protected metadata frame, including padding. */
    for (size_t i = 0; i < sizeof(arena); ++i)
        if (i < 8192 || i >= 12288) assert(arena[i] == 0xa5);
    assert(!pmm_init(&map));
    for (uint64_t page = 0; page < sizeof(arena); page += 4096) {
        assert(!pmm_free_page(page));
        assert(pmm_page_pointer(page) == NULL);
    }
    same_stats(initial);
    assert(!pmm_alloc_page(NULL));
    assert(!pmm_free_page(UINT64_MAX));
    uint64_t pages[12];
    for (size_t i = 0; i < 12; ++i) {
        assert(pmm_alloc_page(&pages[i]));
        assert(pages[i] % 4096 == 0 && pages[i] != 0 && pages[i] != initial.metadata_base);
        assert((pages[i] >= 3 * 4096 && pages[i] < 9 * 4096) ||
               (pages[i] >= 13 * 4096 && pages[i] < 15 * 4096) ||
               (pages[i] >= 16 * 4096 && pages[i] < 20 * 4096));
        for (size_t j = 0; j < i; ++j) assert(pages[i] != pages[j]);
        unsigned char *ptr = pmm_page_pointer(pages[i]);
        assert(ptr == arena + pages[i]);
        memset(ptr, (int)i, 4096);
        assert(!pmm_free_page(pages[i] + 1));
    }
    struct pmm_statistics full = pmm_get_statistics();
    assert(full.free_frames == 0 && full.allocated_frames == 12);
    uint64_t failed = 123;
    assert(!pmm_alloc_page(&failed) && failed == 123);
    assert(!pmm_free_page((uintptr_t)(arena + pages[0])));
    assert(!pmm_free_page(UINT64_C(1) << 40));
    same_stats(full);
    /* Free while full, reuse exactly that frame, and exercise cursor wrapping. */
    assert(pmm_free_page(pages[5]));
    uint64_t reuse;
    assert(pmm_alloc_page(&reuse) && reuse == pages[5]);
    assert(!pmm_alloc_page(&failed));
    /* No boot map lifetime dependency remains. */
    memset(entries, 0, sizeof(entries));
    simulated_if = 0;
    for (size_t i = 0; i < 12; ++i) {
        assert(pmm_free_page(pages[i]));
        assert(!pmm_free_page(pages[i]));
        assert(pmm_page_pointer(pages[i]) == NULL);
        assert(simulated_if == 0);
    }
    same_stats(initial);
    assert(lock_depth == 0);
}
static void malformed(void) {
    memset(arena, 0x5a, sizeof(arena));
    uint64_t page = 321;
    assert(!pmm_alloc_page(&page) && page == 321);
    assert(!pmm_free_page(4096) && pmm_page_pointer(4096) == NULL);
    assert(!pmm_init(NULL));
    struct boot_memory_map map = {NULL, 1, (uintptr_t)arena};
    assert(!pmm_init(&map));
    struct boot_memory_range entries[] = {{4096, 4 * 4096, true}, {5 * 4096, 4096, false}};
    map.ranges = entries;
    map.count = 0; assert(!pmm_init(&map));
    map.count = PMM_MAX_RANGES + 1; assert(!pmm_init(&map));
    map.count = 2;
    map.hhdm_offset = 0; assert(!pmm_init(&map));
    map.hhdm_offset = (uintptr_t)arena + 1; assert(!pmm_init(&map));
    map.hhdm_offset = (uintptr_t)arena;
    entries[1].length = 0; assert(!pmm_init(&map));
    entries[1] = (struct boot_memory_range){8192, 4096, false}; assert(!pmm_init(&map));
    entries[1] = (struct boot_memory_range){0, 4096, false}; assert(!pmm_init(&map));
    entries[1] = (struct boot_memory_range){UINT64_MAX - 10, 20, false}; assert(!pmm_init(&map));
    entries[1] = (struct boot_memory_range){5 * 4096, 4096, false};
    entries[0].usable = false; assert(!pmm_init(&map));
    entries[0] = (struct boot_memory_range){4097, 4094, true}; assert(!pmm_init(&map));
    entries[0] = (struct boot_memory_range){4096, 4096, true}; assert(!pmm_init(&map));
    entries[0] = (struct boot_memory_range){4096, 4 * 4096, true};
    map.hhdm_offset = UINTPTR_MAX & ~(uintptr_t)4095; assert(!pmm_init(&map));
    struct pmm_statistics empty = {0}; same_stats(empty);
    for (size_t i = 0; i < sizeof(arena); ++i) assert(arena[i] == 0x5a);
    map.hhdm_offset = (uintptr_t)arena;
    assert(pmm_init(&map)); /* A failed initialization is safely retryable. */
    assert(simulated_if == 0x200 && lock_depth == 0);
}
static void sparse(void) {
    struct boot_memory_range entries[] = {
        {4096, 2 * 4096, true}, {UINT64_C(1) << 40, 2 * 4096, true},
    };
    struct boot_memory_map map = {entries, 2, (uintptr_t)arena};
    assert(pmm_init(&map));
    struct pmm_statistics stats = pmm_get_statistics();
    assert(stats.usable_frames == 4 && stats.metadata_frames == 1 && stats.free_frames == 3);
    uint64_t pages[3];
    for (size_t i = 0; i < 3; ++i) assert(pmm_alloc_page(&pages[i]));
    assert(pages[0] == 8192 && pages[1] == (UINT64_C(1) << 40) && pages[2] == pages[1] + 4096);
    for (size_t i = 0; i < 3; ++i) assert(pmm_free_page(pages[i]));
    same_stats(stats);
}
static void metadata_in_later_range(void) {
    struct boot_memory_range entries[] = {
        {4096, 4096, true}, {8192, UINT64_C(32768) * 4096, true},
    };
    struct boot_memory_map map = {entries, 2, (uintptr_t)arena};
    assert(pmm_init(&map));
    struct pmm_statistics stats = pmm_get_statistics();
    assert(stats.metadata_base == 8192 && stats.metadata_frames == 2);
    assert(stats.usable_frames == 32769 && stats.free_frames == 32767);
    assert(!pmm_free_page(8192) && !pmm_free_page(12288));
    uint64_t first, second;
    assert(pmm_alloc_page(&first) && first == 4096);
    assert(pmm_alloc_page(&second) && second == 16384);
    assert(pmm_free_page(first) && pmm_free_page(second));
    same_stats(stats);
}
static void mixed_operations(void) {
    struct boot_memory_range entry = {4096, sizeof(arena) - 4096, true};
    struct boot_memory_map map = {&entry, 1, (uintptr_t)arena};
    assert(pmm_init(&map));
    bool allocated[128] = {false};
    struct pmm_statistics initial = pmm_get_statistics();
    uint64_t allocated_count = 0;
    uint32_t random = 0x31415926;
    for (unsigned int iteration = 0; iteration < 10000; ++iteration) {
        random = random * 1664525u + 1013904223u;
        simulated_if = (iteration & 1) ? 0x200 : 0;
        uint64_t expected_if = simulated_if;
        if (random & 0x80000000u) {
            uint64_t physical = UINT64_MAX;
            if (pmm_alloc_page(&physical)) {
                size_t frame = physical / 4096;
                assert(frame < 128 && frame >= 2 && !allocated[frame]);
                allocated[frame] = true;
                ++allocated_count;
            } else {
                assert(physical == UINT64_MAX && allocated_count == initial.free_frames);
            }
        } else {
            size_t frame = (random >> 8) % 128;
            assert(pmm_free_page(frame * 4096) == allocated[frame]);
            if (allocated[frame]) { allocated[frame] = false; --allocated_count; }
        }
        struct pmm_statistics current = pmm_get_statistics();
        assert(current.allocated_frames == allocated_count);
        assert(current.free_frames + allocated_count == initial.free_frames);
        assert(simulated_if == expected_if && lock_depth == 0);
    }
    for (size_t frame = 0; frame < 128; ++frame)
        if (allocated[frame]) assert(pmm_free_page(frame * 4096));
    same_stats(initial);
}
int main(void) {
    void (*cases[])(void) = {basic, malformed, sparse, metadata_in_later_range, mixed_operations};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        pid_t child = fork();
        assert(child >= 0);
        if (child == 0) { cases[i](); _exit(0); }
        int status;
        assert(waitpid(child, &status, 0) == child);
        assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
    puts("PMM alignment, protected ranges, metadata, exhaustion, reuse, statistics, malformed maps, sparse RAM, and IF preservation passed.");
}
