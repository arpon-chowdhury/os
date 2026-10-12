#include "window.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void arch_debug_puts(const char *text) {(void)text;}
static struct ui u;
static unsigned char *pixels,*reference;
static size_t bytes;
static struct kernel_framebuffer fb;
static void mouse(int x,int y,bool left) {struct mouse_event e={.left=left};ui_mouse(&u,x,y,&e);}
static void key(unsigned code,bool shift) {struct keyboard_event e={.pressed=true,.scancode=code,.shift=shift};ui_keyboard(&u,&e);}
static bool equal_rect(struct ui_rect a,struct ui_rect b) {return a.x==b.x&&a.y==b.y&&a.w==b.w&&a.h==b.h;}
static void equivalent(void) {
    ui_render(&u);memcpy(reference,pixels+1,bytes);
    ui_invalidate(&u,(struct ui_rect){0,0,fb.width,fb.height});ui_render(&u);
    assert(!memcmp(reference,pixels+1,bytes));
    assert(pixels[0]==0xa5 && pixels[bytes+1]==0xa5);
    for(unsigned y=0;y<fb.height;++y)
        for(unsigned x=fb.width*4;x<fb.pitch;++x) assert(pixels[1+y*fb.pitch+x]==0xa5);
}
int main(void) {
    bytes=640*4*480+480*3;pixels=malloc(bytes+2);reference=malloc(bytes);assert(pixels&&reference);
    memset(pixels,0xa5,bytes+2);
    fb=(struct kernel_framebuffer){.address=pixels+1,.width=640,.height=480,.pitch=2563,.bpp=32,.memory_model=1,.red_size=8,.red_shift=16,.green_size=8,.green_shift=8,.blue_size=8};
    assert(ui_init(&u,&fb));u.desktop=true;u.shell_hover=u.shell_capture=-1;
    int a=ui_window(&u,"Control",(struct ui_rect){30,50,240,180});
    int b=ui_window(&u,"About ON.",(struct ui_rect){160,120,260,200});
    int c=ui_window(&u,"Palette",(struct ui_rect){340,180,240,180});
    u.surfaces[b].kind=1;u.surfaces[c].kind=2;
    assert(a==0 && b==1 && c==2);equivalent();
    assert(wm_hit(&u,360,200)==c);ui_focus(&u,b);assert(wm_hit(&u,360,200)==b);equivalent();
    assert(!wm_contains(u.surfaces[b].bounds,160,120));
    assert(wm_hit(&u,160,120)==a); /* rounded corner passes through to lower surface */
    ui_focus(&u,a);assert(u.order[u.count-1]==(unsigned)a);equivalent();
    key(0x40,false);assert(u.focus==b);key(0x40,true);assert(u.focus==a);equivalent();
    unsigned original_count=u.count;
    for(unsigned i=0;i<100;++i) {wm_minimize(&u,a);assert(u.focus!=a);wm_restore(&u,a);wm_close(&u,a);assert(!u.surfaces[a].visible&&u.surfaces[a].closed);wm_restore(&u,a);}
    assert(u.count==original_count);equivalent();
    struct ui_rect saved=u.surfaces[a].bounds;
    key(0x42,false);assert(u.surfaces[a].maximized && equal_rect(u.surfaces[a].bounds,wm_workarea(&u)));equivalent();
    wm_resize(&u,a,(struct ui_rect){0,0,1,1});ui_move(&u,a,50,60);assert(equal_rect(u.surfaces[a].bounds,wm_workarea(&u)));
    wm_minimize(&u,a);wm_restore(&u,a);assert(u.surfaces[a].maximized);equivalent();
    key(0x42,false);assert(!u.surfaces[a].maximized && equal_rect(u.surfaces[a].bounds,saved));equivalent();
    /* Actual resize capture at bottom/right; resizing cannot activate children. */
    assert(ui_button_add(&u,a,"Light",(struct ui_rect){12,40,200,28},UI_SET_LIGHT));
    u.surfaces[a].buttons[0].stretch=true;
    mouse(saved.x+saved.w-3,saved.y+saved.h-3,true);assert(u.resizing&&!u.dragging);
    mouse(saved.x+saved.w+57,saved.y+saved.h+37,true);
    mouse(saved.x+saved.w+57,saved.y+saved.h+37,false);
    assert(u.surfaces[a].bounds.w==saved.w+60 && u.surfaces[a].bounds.h==saved.h+40);
    assert(!u.resizing && u.capture_surface==-1 && !u.surfaces[a].activations);equivalent();
    wm_resize(&u,a,(struct ui_rect){30,50,240,180});
    mouse(267,227,true);mouse(639,479,true);mouse(639,479,false);
    assert(u.surfaces[a].bounds.x==30 && u.surfaces[a].bounds.y==50);
    assert(u.surfaces[a].bounds.w==610 && u.surfaces[a].bounds.h==354);equivalent();
    wm_resize(&u,a,(struct ui_rect){20,40,240,180});ui_focus(&u,a);
    mouse(21,90,true);assert(u.resizing && u.resize_edges==WM_LEFT);
    mouse(51,90,true);mouse(51,90,false);
    assert(u.surfaces[a].bounds.x==50 && u.surfaces[a].bounds.w==210);equivalent();
    mouse(80,41,true);assert(u.resizing && u.resize_edges==WM_TOP);
    mouse(80,61,true);mouse(80,61,false);
    assert(u.surfaces[a].bounds.y==60 && u.surfaces[a].bounds.h==160);equivalent();
    wm_resize(&u,a,(struct ui_rect){INT_MIN,INT_MAX,INT_MAX,INT_MIN});
    assert(u.surfaces[a].bounds.x>=0 && u.surfaces[a].bounds.y>=28 && u.surfaces[a].bounds.w<=640);equivalent();
    wm_resize(&u,a,(struct ui_rect){30,50,240,180});
    mouse(40,60,true);assert(u.dragging);wm_close(&u,a);assert(!u.dragging && u.capture_surface==-1);
    mouse(500,300,false);assert(u.surfaces[a].closed);equivalent();
    /* Closing a background window preserves the current focus. */
    ui_focus(&u,c);wm_close(&u,b);assert(u.focus==c);wm_restore(&u,b);equivalent();
    for(unsigned i=0;i<u.count;++i) wm_minimize(&u,i);
    assert(u.focus==-1);key(0x40,false);assert(u.focus==-1);wm_restore(&u,c);key(0x40,false);assert(u.focus==c);equivalent();
    uint64_t frames=u.frames,area=u.redraw_pixels;ui_render(&u);assert(frames==u.frames && area==u.redraw_pixels);
    ui_invalidate(&u,(struct ui_rect){8,8,2,2});ui_render(&u);assert(u.redraw_pixels-area==4);
    area=u.redraw_pixels;wm_minimize(&u,c);ui_render(&u);assert(u.redraw_pixels-area<640*480);
    wm_restore(&u,c);equivalent();
    for(unsigned i=u.count;i<UI_MAX_SURFACES;++i) assert(ui_window(&u,"Extra",(struct ui_rect){10,30,120,90})>=0);
    assert(ui_window(&u,"Overflow",(struct ui_rect){10,30,120,90})==-1);
    wm_resize(NULL,0,(struct ui_rect){0,0,0,0});wm_close(&u,99);wm_restore(&u,-1);wm_maximize(NULL,0);wm_cycle(NULL,false);
    equivalent();free(reference);free(pixels);
    puts("Window lifecycle, overlapping hit tests, cycling, all resize edges, maximize geometry, capture cancellation, fixed capacity, dirty/full equivalence and guards passed.");
}
