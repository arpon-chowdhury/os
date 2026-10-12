#include "graphics.h"
#include "font.h"
bool graphics_init(struct graphics *g, const struct kernel_framebuffer *fb) {
    if (!g) return false;
    g->ready = false;
    if (!framebuffer_valid(fb)) return false;
    g->fb = *fb;
    g->ready = true;
    graphics_clip(g,0,0,fb->width,fb->height);
    return true;
}
void graphics_clip(struct graphics *g, int32_t x, int32_t y, int32_t w, int32_t h) {
    if (!g || !g->ready) return;
    int64_t right=(int64_t)x+(w>0?w:0), bottom=(int64_t)y+(h>0?h:0);
    int64_t left=x<0?0:x, top=y<0?0:y;
    if(left>(int64_t)g->fb.width) left=g->fb.width;
    if(top>(int64_t)g->fb.height) top=g->fb.height;
    if(right>(int64_t)g->fb.width) right=g->fb.width;
    if(bottom>(int64_t)g->fb.height) bottom=g->fb.height;
    g->clip_x=left;g->clip_y=top;
    g->clip_w=right>left?right-left:0;g->clip_h=bottom>top?bottom-top:0;
}
void graphics_pixel(struct graphics *g, int32_t x, int32_t y, uint32_t color) {
    if (!g || !g->ready || x < 0 || y < 0) return;
    if(x<g->clip_x || y<g->clip_y || x>=g->clip_x+g->clip_w || y>=g->clip_y+g->clip_h) return;
    framebuffer_fill(&g->fb, x, y, 1, 1, color);
}
static void fill(struct graphics *g, int64_t x, int64_t y, int64_t w, int64_t h, uint32_t color) {
    if (!g || !g->ready || w <= 0 || h <= 0) return;
    int64_t right = x + w, bottom = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (right > (int64_t)g->fb.width) right = g->fb.width;
    if (bottom > (int64_t)g->fb.height) bottom = g->fb.height;
    if (x<g->clip_x) x=g->clip_x;
    if (y<g->clip_y) y=g->clip_y;
    if (right>g->clip_x+g->clip_w) right=g->clip_x+g->clip_w;
    if (bottom>g->clip_y+g->clip_h) bottom=g->clip_y+g->clip_h;
    if (right <= x || bottom <= y) return;
    framebuffer_fill(&g->fb, x, y, right-x, bottom-y, color);
}
void graphics_fill(struct graphics *g, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    fill(g,x,y,w,h,color);
}
void graphics_rect(struct graphics *g, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    fill(g,x,y,w,1,color); fill(g,x,(int64_t)y+h-1,w,1,color);
    fill(g,x,y,1,h,color); fill(g,(int64_t)x+w-1,y,1,h,color);
}
static unsigned int outside(int64_t x, int64_t y, int64_t w, int64_t h) {
    return (x<0 ? 1u:0u) | (x>=w ? 2u:0u) | (y<0 ? 4u:0u) | (y>=h ? 8u:0u);
}
/* Differences of int32 endpoints fit unsigned 32 bits; their product fits
   uint64_t even at INT32_MIN/MAX. Avoid signed overflow and FP/runtime helpers. */
static int64_t ratio(int64_t a, int64_t b, int64_t divisor) {
    bool negative = (a<0) ^ (b<0) ^ (divisor<0);
    uint64_t ua = a<0 ? -a:a, ub=b<0 ? -b:b, ud=divisor<0 ? -divisor:divisor;
    uint64_t value=ua*ub/ud;
    return negative ? -(int64_t)value : (int64_t)value;
}
void graphics_line(struct graphics *g, int32_t ax, int32_t ay, int32_t bx, int32_t by, uint32_t color) {
    if (!g || !g->ready) return;
    int64_t x0=ax,y0=ay,x1=bx,y1=by,w=g->fb.width,h=g->fb.height;
    bool accepted=false;
    for (unsigned int i=0;i<16;++i) {
        unsigned int a=outside(x0,y0,w,h), b=outside(x1,y1,w,h);
        if (!(a|b)) { accepted=true; break; }
        if (a&b) return;
        unsigned int code=a ? a:b;
        int64_t x,y;
        if (code&12) {
            if (y1==y0) return;
            y=(code&4) ? 0:h-1;
            x=x0+ratio(x1-x0,y-y0,y1-y0);
        } else {
            if (x1==x0) return;
            x=(code&1) ? 0:w-1;
            y=y0+ratio(y1-y0,x-x0,x1-x0);
        }
        if (a) { x0=x;y0=y; } else { x1=x;y1=y; }
    }
    if (!accepted) return;
    int64_t dx=x1>x0 ? x1-x0:x0-x1, dy=-(y1>y0 ? y1-y0:y0-y1);
    int64_t sx=x0<x1 ? 1:-1, sy=y0<y1 ? 1:-1, error=dx+dy;
    for (;;) {
        graphics_pixel(g,x0,y0,color);
        if (x0==x1 && y0==y1) break;
        int64_t twice=2*error;
        if (twice>=dy) { error+=dy;x0+=sx; }
        if (twice<=dx) { error+=dx;y0+=sy; }
    }
}
void graphics_text(struct graphics *g, int32_t x, int32_t y, const char *text, unsigned int scale, uint32_t color) {
    if (!g || !g->ready || !text || scale<1 || scale>8) return;
    int64_t px=x,py=y;
    for (;*text;++text) {
        if (*text=='\n') { px=x;py+=8*scale; if(py>=(int64_t)g->fb.height) break; continue; }
        if (px>=(int64_t)g->fb.width || py>=(int64_t)g->fb.height) break;
        uint8_t glyph[7]; font_glyph((unsigned char)*text,glyph);
        for(unsigned int r=0;r<7;++r) for(unsigned int c=0;c<5;++c)
            if (glyph[r] & (1u<<(4-c))) fill(g,px+c*scale,py+r*scale,scale,scale,color);
        px+=6*scale;
    }
}
void graphics_clear(struct graphics *g, uint32_t color) {
    if (g && g->ready) fill(g,0,0,g->fb.width,g->fb.height,color);
}
