#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "graphics.h"
static unsigned char *bytes;
static size_t pitch=1283;
static uint32_t at(unsigned int x,unsigned int y) {
    unsigned char *p=bytes+1+y*pitch+x*4;
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16;
}
int main(void) {
    size_t size=pitch*200;
    bytes=malloc(size+2);assert(bytes);memset(bytes,0xa5,size+2);
    struct kernel_framebuffer fb={.address=bytes+1,.width=320,.height=200,.pitch=pitch,
        .bpp=32,.memory_model=1,.red_size=8,.red_shift=16,.green_size=8,.green_shift=8,.blue_size=8};
    struct graphics g;
    assert(!graphics_init(NULL,&fb));assert(!graphics_init(&g,NULL));
    graphics_clear(&g,0);assert(bytes[1]==0xa5);
    assert(graphics_init(&g,&fb));graphics_clear(&g,0);
    graphics_pixel(&g,1,1,0x123456);assert(at(1,1)==0x123456);
    graphics_fill(&g,-2,-2,4,4,0xabcdef);assert(at(0,0)==0xabcdef && at(2,2)==0);
    graphics_rect(&g,10,10,5,5,0xffffff);assert(at(10,10)==0xffffff && at(12,12)==0);
    graphics_rect(&g,INT_MAX,INT_MAX,INT_MAX,INT_MAX,1);
    graphics_fill(&g,INT_MIN,INT_MIN,INT_MAX,INT_MAX,1);assert(at(0,0)==0xabcdef);
    graphics_fill(&g,0,0,0,20,1);graphics_pixel(&g,-1,-1,1);
    graphics_clear(&g,0);
    graphics_line(&g,INT_MIN,100,INT_MAX,100,0x66d9cf);assert(at(0,100)==0x66d9cf && at(319,100)==0x66d9cf);
    graphics_line(&g,160,INT_MIN,160,INT_MAX,0x123456);assert(at(160,0)==0x123456 && at(160,199)==0x123456);
    graphics_line(&g,INT_MIN,INT_MIN,INT_MAX,INT_MAX,0xffffff);assert(at(0,0)==0xffffff && at(199,199)==0xffffff);
    const int endpoints[8][2]={{319,125},{190,199},{130,199},{0,125},{0,75},{130,0},{190,0},{319,75}};
    for(int i=0;i<8;++i) {
        int x=endpoints[i][0],y=endpoints[i][1];
        graphics_line(&g,160,100,x,y,0xffffff);
        graphics_line(&g,x,y,160,100,0xffffff);
        assert(at(x,y)==0xffffff);
    }
    graphics_line(&g,-100,-100,-1,-1,1);
    graphics_clear(&g,0);graphics_text(&g,0,0,"A",1,0xffffff);assert(at(1,0)==0xffffff && at(0,0)==0);
    graphics_clear(&g,0);graphics_rect(&g,-2,4,5,5,0xffffff);
    assert(at(0,6)==0 && at(2,6)==0xffffff);
    graphics_clear(&g,0);const char unsupported[2]={(char)200,0};
    graphics_text(&g,0,0,unsupported,1,0xffffff);
    assert(at(2,6)==0xffffff && at(0,6)==0);
    graphics_text(&g,-3,-3,"Clipped",8,0xffffff);
    graphics_text(&g,0,0,NULL,1,1);graphics_text(&g,0,0,"x",0,1);graphics_text(&g,0,0,"x",UINT_MAX,1);
    for(unsigned int c=32;c<128;++c) { char text[2]={(char)c,0};graphics_text(&g,310,192,text,2,0xffffff); }
    fb.red_shift=0;fb.blue_shift=16;assert(graphics_init(&g,&fb));
    graphics_pixel(&g,3,3,0x123456);assert(at(3,3)==0x563412);
    for(size_t y=0;y<200;++y) for(size_t x=1280;x<pitch;++x) assert(bytes[1+y*pitch+x]==0xa5);
    assert(bytes[0]==0xa5 && bytes[size+1]==0xa5);
    fb.bpp=24;assert(!graphics_init(&g,&fb));graphics_pixel(&g,0,0,1);
    free(bytes);
    puts("Graphics clipping, extreme coordinates, lines, rectangles, font, masks, pitch, and guards passed.");
}
