#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "framebuffer.h"

int main(void) {
    const size_t width = 320, height = 200, pitch = width * 4 + 3;
    const size_t bytes = pitch * height;
    unsigned char *memory = malloc(bytes + 2);
    assert(memory);
    memset(memory, 0xa5, bytes + 2);
    struct kernel_framebuffer fb = {
        .address = memory + 1, .width = width, .height = height, .pitch = pitch,
        .bpp = 32, .memory_model = 1,
        .red_size = 8, .red_shift = 16, .green_size = 8, .green_shift = 8,
        .blue_size = 8, .blue_shift = 0,
    };
    assert(framebuffer_show_welcome(&fb));
    assert(memory[0] == 0xa5 && memory[bytes + 1] == 0xa5);
    for (size_t row = 0; row < height; ++row)
        for (size_t column = width * 4; column < pitch; ++column)
            assert(memory[1 + row * pitch + column] == 0xa5);
    assert(memory[1] == 42 && memory[2] == 23 && memory[3] == 15);
    fb.red_shift = 0; fb.blue_shift = 16;
    assert(framebuffer_show_welcome(&fb));
    assert(memory[1] == 15 && memory[2] == 23 && memory[3] == 42);

    memset(memory, 0xa5, bytes + 2);
    struct kernel_framebuffer bad = fb;
    bad.address = NULL; assert(!framebuffer_show_welcome(&bad));
    bad = fb; bad.bpp = 24; assert(!framebuffer_show_welcome(&bad));
    bad = fb; bad.memory_model = 0; assert(!framebuffer_show_welcome(&bad));
    bad = fb; bad.width = 0; assert(!framebuffer_show_welcome(&bad));
    bad = fb; bad.height = 0; assert(!framebuffer_show_welcome(&bad));
    bad = fb; bad.width = 8193; assert(!framebuffer_show_welcome(&bad));
    bad = fb; bad.pitch = width * 4 - 1; assert(!framebuffer_show_welcome(&bad));
    bad = fb; bad.pitch = UINT64_MAX; assert(!framebuffer_show_welcome(&bad));
    bad = fb; bad.address = (void *)(UINTPTR_MAX - 10); assert(!framebuffer_show_welcome(&bad));
    bad = fb; bad.red_shift = 25; assert(!framebuffer_show_welcome(&bad));
    bad = fb; bad.red_shift = 4; assert(!framebuffer_show_welcome(&bad));
    bad = fb; bad.red_size = 5; assert(!framebuffer_show_welcome(&bad));
    assert(!framebuffer_show_welcome(NULL));
    for (size_t i = 0; i < bytes + 2; ++i) assert(memory[i] == 0xa5);
    free(memory);
    puts("Framebuffer guards, padding, channel order, and invalid-mode checks passed.");
}
