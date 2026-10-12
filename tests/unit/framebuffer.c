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

    framebuffer_fill(&fb, width-2, height-2, UINT64_MAX, UINT64_MAX, 0x123456);
    for (unsigned int ch=32;ch<127;++ch) framebuffer_cell(&fb,(ch-32)%26,(ch-32)/26,(char)ch,ch==126);
    framebuffer_cell(&fb, UINT32_MAX, UINT32_MAX, 'x', true);
    assert(memory[0] == 0xa5 && memory[bytes+1] == 0xa5);
    for (size_t row=0;row<height;++row)
        for(size_t column=width*4;column<pitch;++column)
            assert(memory[1+row*pitch+column] == 0xa5);
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
    framebuffer_fill(&bad,0,0,320,200,0);
    framebuffer_cell(&bad,0,0,'x',true);
    framebuffer_cell(NULL,0,0,'x',true);
    for (size_t i = 0; i < bytes + 2; ++i) assert(memory[i] == 0xa5);
    fb.red_shift=16;fb.blue_shift=0;
    framebuffer_fill(&fb,0,0,width,height,0x123456);
    assert(framebuffer_pointer_init(&fb));
    uint64_t px,py;framebuffer_pointer_position(&px,&py);assert(px==160 && py==100);
    framebuffer_fill(&fb,px,py,12,16,0xabcdef);
    framebuffer_pointer_move(30,-20);
    size_t old=1+100*pitch+160*4;
    assert(memory[old]==0xef && memory[old+1]==0xcd && memory[old+2]==0xab);
    framebuffer_pointer_move(-32768,32767);
    framebuffer_pointer_position(&px,&py);assert(px==0 && py==0);
    framebuffer_pointer_move(32767,-32768);
    framebuffer_pointer_position(&px,&py);assert(px==width-12 && py==height-16);
    assert(!framebuffer_pointer_init(NULL));
    assert(memory[0]==0xa5 && memory[bytes+1]==0xa5);
    for(size_t y=0;y<height;++y) for(size_t x=width*4;x<pitch;++x) assert(memory[1+y*pitch+x]==0xa5);
    free(memory);
    puts("Framebuffer guards, padding, channel order, and invalid-mode checks passed.");
}
