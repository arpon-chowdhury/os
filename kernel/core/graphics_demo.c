#include "graphics.h"
#include "arch.h"
/* Original geometry, font, and palette; this is a static drawing demonstration. */
static void rounded(struct graphics *g, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    int radius=10;
    if (w<20 || h<20) { graphics_fill(g,x,y,w,h,color);return; }
    for(int row=0;row<h;++row) {
        int inset=0, distance=row<radius ? radius-row-1 : (row>=h-radius ? row-(h-radius):0);
        if(distance) while((radius-inset)*(radius-inset)+distance*distance>radius*radius) ++inset;
        graphics_fill(g,x+inset,y+row,w-2*inset,1,color);
    }
}
bool graphics_demo(const struct kernel_framebuffer *fb) {
    struct graphics g;
    if (!graphics_init(&g,fb)) return false;
    int32_t w=fb->width,h=fb->height;
    unsigned int scale=w>=640 && h>=400 ? 2:1;
    graphics_clear(&g,0x0c1426);
    graphics_fill(&g,0,0,w,28,0x18253d);
    graphics_text(&g,16,7,"ON.",2,0x66d9cf);
    graphics_text(&g,w-150,10,"GRAPHICS / PHASE 1",1,0xdde8f5);
    int32_t panel_x=w/10,panel_y=h/6,panel_w=w*8/10,panel_h=h*3/5;
    rounded(&g,panel_x+4,panel_y+5,panel_w,panel_h,0x070d18);
    rounded(&g,panel_x,panel_y,panel_w,panel_h,0x18253d);
    graphics_text(&g,panel_x+16,panel_y+14,"ON. Canvas",scale,0xdde8f5);
    graphics_text(&g,panel_x+16,panel_y+14+18*scale,"A foundation for your space.",1,0x91a7c4);
    int32_t content_y=panel_y+40+12*scale;
    graphics_rect(&g,panel_x+16,content_y,panel_w-32,32,0x66d9cf);
    graphics_line(&g,panel_x+16,content_y+32,panel_x+panel_w-17,content_y,0x687cff);
    for (int i=0;i<3;++i) graphics_fill(&g,panel_x+24+i*36,content_y+10,20,12,
                                                i==0 ? 0x66d9cf : (i==1 ? 0x687cff:0xf0ba73));
    graphics_text(&g,panel_x+16,panel_y+panel_h-18,"Esc returns to terminal",1,0xdde8f5);
    int32_t dock_w=w>=640 ? 320:240,dock_x=(w-dock_w)/2,dock_y=h-48;
    rounded(&g,dock_x,dock_y,dock_w,36,0x243650);
    const char *labels[4]={"Flow","Pulse","Spaces","Studio"};
    for(int i=0;i<4;++i) {
        int32_t x=dock_x+12+i*(dock_w/4);
        graphics_rect(&g,x,dock_y+8,14,14,0x66d9cf);
        graphics_text(&g,x+18,dock_y+11,labels[i],1,0xdde8f5);
    }
    /* Deterministic pixel probe independent of text and the mouse position. */
    graphics_pixel(&g,8,h-8,0x38bdf8);
    arch_debug_puts("ON.: graphics demo ready; Escape returns to terminal.\n");
    return true;
}
