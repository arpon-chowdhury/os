#include "window.h"
#include "arch.h"
#include <limits.h>
static bool valid(struct ui *u,int id) {
    return u && u->graphics.ready && id>=0 && (unsigned)id<u->count && !u->surfaces[id].retired;
}
static void number(unsigned value) {
    char digits[11];unsigned n=0;
    do {digits[n++]='0'+value%10;value/=10;} while(value);
    char text[11];unsigned at=0;while(n) text[at++]=digits[--n];text[at]=0;
    arch_debug_puts(text);
}
static void report(struct ui *u,int id) {
    struct ui_surface *s=&u->surfaces[id];
    arch_debug_puts("ON.: window bounds ");arch_debug_puts(s->title);arch_debug_puts(" ");
    number(s->bounds.x);arch_debug_puts(",");number(s->bounds.y);arch_debug_puts(",");
    number(s->bounds.w);arch_debug_puts(",");number(s->bounds.h);arch_debug_puts("\n");
}
struct ui_rect wm_workarea(struct ui *u) {
    if(!u || !u->graphics.ready) return (struct ui_rect){0,0,0,0};
    int bottom=u->desktop && u->graphics.fb.height>=400?76:44;
    return (struct ui_rect){0,28,u->graphics.fb.width,(int)u->graphics.fb.height-28-bottom};
}
void wm_chrome(struct ui *u) {
    if(u && u->desktop) ui_invalidate(u,(struct ui_rect){(int)u->graphics.fb.width/2-144,(int)u->graphics.fb.height-76,288,64});
}
void wm_damage(struct ui *u,int id) {
    if(!valid(u,id)) return;
    struct ui_rect r=u->surfaces[id].bounds;
    if(u->desktop) {r.x-=6;r.y-=4;r.w+=12;r.h+=12;}
    ui_invalidate(u,r);
}
bool wm_contains(struct ui_rect r,int32_t x,int32_t y) {
    if(x<r.x || y<r.y || (int64_t)x>= (int64_t)r.x+r.w || (int64_t)y>= (int64_t)r.y+r.h) return false;
    int dx=x-r.x,dy=y-r.y;
    if(dx>=r.w-8) dx=r.w-1-dx;
    if(dy>=r.h-8) dy=r.h-1-dy;
    if(dx<8 && dy<8) {dx=7-dx;dy=7-dy;return dx*dx+dy*dy<=64;}
    return true;
}
int wm_hit(struct ui *u,int32_t x,int32_t y) {
    if(!u || !u->graphics.ready) return -1;
    for(unsigned n=u->count;n>0;--n) {
        int id=u->order[n-1];struct ui_surface *s=&u->surfaces[id];
        if(s->visible && !s->closed && wm_contains(s->bounds,x,y)) return id;
    }
    return -1;
}
void wm_cancel_capture(struct ui *u) {
    if(!u) return;
    u->capture_surface=u->capture_button=-1;
    u->dragging=u->resizing=false;
    u->resize_edges=0;u->shell_capture=-1;
}
static void hide(struct ui *u,int id,bool close) {
    if(!valid(u,id)) return;
    struct ui_surface *s=&u->surfaces[id];
    wm_damage(u,id);s->visible=false;s->closed=close;s->minimized=!close;
    if(u->capture_surface==id) wm_cancel_capture(u);
    if(u->hover_surface==id) u->hover_surface=u->hover_button=-1;
    if(u->focus==id) {
        u->focus=-1;
        for(unsigned n=u->count;n>0;--n) {
            int next=u->order[n-1];
            if(u->surfaces[next].visible) {ui_focus(u,next);break;}
        }
    }
    wm_chrome(u);
    arch_debug_puts(close?"ON.: window closed ":"ON.: window minimized ");
    arch_debug_puts(s->title);arch_debug_puts("\n");
    arch_debug_puts(close?"ON.: desktop window closed.\n":"ON.: desktop window minimized.\n");
}
void wm_minimize(struct ui *u,int id) {if(valid(u,id)&&!u->surfaces[id].closed) hide(u,id,false);}
void wm_close(struct ui *u,int id) {hide(u,id,true);}
void wm_restore(struct ui *u,int id) {
    if(!valid(u,id)) return;
    struct ui_surface *s=&u->surfaces[id];
    s->visible=true;s->minimized=s->closed=false;
    wm_damage(u,id);ui_focus(u,id);wm_chrome(u);
    arch_debug_puts("ON.: window restored.\n");
}
static int clamp64(int64_t v,int lo,int hi) {return v<lo?lo:v>hi?hi:(int)v;}
static void minimum(struct ui_surface *s,int *width,int *height) {
    int minw=120,minh=90;
    for(unsigned i=0;i<s->button_count;++i) {
        struct ui_button *b=&s->buttons[i];
        int right=b->bounds.x+(b->stretch?16:b->bounds.w)+4;
        int bottom=b->bounds.y+b->bounds.h+4;
        if(right>minw) minw=right;
        if(bottom>minh) minh=bottom;
    }
    *width=minw;*height=minh;
}
void wm_resize(struct ui *u,int id,struct ui_rect r) {
    if(!valid(u,id) || u->surfaces[id].closed || u->surfaces[id].maximized) return;
    struct ui_surface *s=&u->surfaces[id];
    int minw,minh;minimum(s,&minw,&minh);
    struct ui_rect area=wm_workarea(u);
    if(minw>area.w || minh>area.h) return;
    r.w=clamp64(r.w,minw,area.w);r.h=clamp64(r.h,minh,area.h);
    r.x=clamp64(r.x,0,area.w-r.w);r.y=clamp64(r.y,area.y,area.y+area.h-r.h);
    if(r.x==s->bounds.x && r.y==s->bounds.y && r.w==s->bounds.w && r.h==s->bounds.h) return;
    wm_damage(u,id);s->bounds=r;
    for(unsigned i=0;i<s->button_count;++i) if(s->buttons[i].stretch) s->buttons[i].bounds.w=r.w-2*s->buttons[i].bounds.x;
    wm_damage(u,id);
    report(u,id);
    arch_debug_puts("ON.: window resized.\n");
}
void wm_maximize(struct ui *u,int id) {
    if(!valid(u,id)||!u->surfaces[id].visible) return;
    struct ui_surface *s=&u->surfaces[id];wm_cancel_capture(u);wm_damage(u,id);
    if(s->maximized) {s->bounds=s->restore_bounds;s->maximized=false;}
    else {
        s->restore_bounds=s->bounds;s->maximized=true;
        s->bounds=wm_workarea(u);
    }
    for(unsigned i=0;i<s->button_count;++i) if(s->buttons[i].stretch) s->buttons[i].bounds.w=s->bounds.w-2*s->buttons[i].bounds.x;
    wm_damage(u,id);ui_focus(u,id);wm_chrome(u);
    report(u,id);
    arch_debug_puts(s->maximized?"ON.: window maximized.\n":"ON.: window geometry restored.\n");
}
void wm_cycle(struct ui *u,bool reverse) {
    if(!u || !u->graphics.ready) return;
    /* Stable creation order makes forward/reverse traversal predictable. */
    int start=u->focus>=0?u->focus:(reverse?0:(int)u->count-1);
    for(unsigned step=1;step<=u->count;++step) {
        int id=(start+(reverse?-(int)step:(int)step)+(int)u->count)%(int)u->count;
        if(u->surfaces[id].visible) {wm_cancel_capture(u);ui_focus(u,id);break;}
    }
}
unsigned wm_resize_edges(struct ui *u,int id,int32_t x,int32_t y) {
    if(!valid(u,id)||u->surfaces[id].maximized) return 0;
    struct ui_rect r=u->surfaces[id].bounds;
    unsigned edges=0;
    if(x<r.x+5) edges|=WM_LEFT;else if(x>=r.x+r.w-5) edges|=WM_RIGHT;
    if(y<r.y+4) edges|=WM_TOP;else if(y>=r.y+r.h-5) edges|=WM_BOTTOM;
    return edges;
}
void wm_pointer_resize(struct ui *u,int32_t x,int32_t y) {
    if(!u || !u->resizing || !valid(u,u->capture_surface)) return;
    struct ui_rect r=u->resize_start;
    int64_t dx=(int64_t)x-u->drag_x,dy=(int64_t)y-u->drag_y;
    int minw,minh;minimum(&u->surfaces[u->capture_surface],&minw,&minh);
    struct ui_rect area=wm_workarea(u);
    if(u->resize_edges&WM_RIGHT) r.w=clamp64((int64_t)r.w+dx,minw,area.w-r.x);
    if(u->resize_edges&WM_BOTTOM) r.h=clamp64((int64_t)r.h+dy,minh,area.y+area.h-r.y);
    if(u->resize_edges&WM_LEFT) {r.x=clamp64((int64_t)r.x+dx,0,r.x+r.w-minw);r.w=clamp64((int64_t)u->resize_start.x+u->resize_start.w-r.x,1,INT_MAX);}
    if(u->resize_edges&WM_TOP) {r.y=clamp64((int64_t)r.y+dy,28,r.y+r.h-minh);r.h=clamp64((int64_t)u->resize_start.y+u->resize_start.h-r.y,1,INT_MAX);}
    wm_resize(u,u->capture_surface,r);
}
