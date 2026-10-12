/* Single foreground desktop: shell controls never represent application processes. */
#include "ui.h"
#include "window.h"
#include "arch.h"
static struct ui_rect launcher(struct ui *u) {
    int w=u->graphics.fb.width < 640 ? 288 : 448;
    int h=u->graphics.fb.height < 400 ? 124 : 256;
    return (struct ui_rect){((int)u->graphics.fb.width-w)/2,(int)u->graphics.fb.height-h-52,w,h};
}
static void panel(struct ui *u,struct ui_rect r,uint32_t c) {
    graphics_fill(&u->graphics,r.x+8,r.y,r.w-16,r.h,c);
    graphics_fill(&u->graphics,r.x,r.y+8,r.w,r.h-16,c);
    for(int y=0;y<8;++y) {
        int inset= y<2?6:y<4?3:1;
        graphics_fill(&u->graphics,r.x+inset,r.y+y,r.w-2*inset,1,c);
        graphics_fill(&u->graphics,r.x+inset,r.y+r.h-y-1,r.w-2*inset,1,c);
    }
}
void desktop_background(struct ui *u) {
    struct graphics *g=&u->graphics;
    /* Procedural original wallpaper: deterministic, no filesystem or imported assets. */
    int lo=g->clip_y, hi=lo+g->clip_h;
    for(int y=lo;y<hi;++y) {
        int x=(int)((int64_t)y*g->fb.width/g->fb.height);
        if(u->wallpaper) x=(int)g->fb.width-x;
        graphics_fill(g,x,y,g->fb.width-x,1,u->tokens.raised);
        graphics_fill(g,x+g->fb.width/5,y,g->fb.width,1,u->tokens.surface);
    }
    graphics_text(g,32,g->fb.height/2-32,"ON.",4,u->tokens.accent);
    graphics_text(g,32,g->fb.height/2+12,"A little space to begin.",1,u->tokens.muted);
}
static bool inside(struct ui_rect r,int x,int y) {
    return x>=r.x && y>=r.y && x<r.x+r.w && y<r.y+r.h;
}
static bool matches(struct ui *u,int id) {
    const char *name=u->surfaces[id].title;
    for(unsigned i=0;i<u->search_length;++i) {
        char c=u->search[i],n=name[i];
        if(!n) return false;
        if(c>='A'&&c<='Z') c+=32;
        if(n>='A'&&n<='Z') n+=32;
        if(c!=n) return false;
    }
    return true;
}
static int target(struct ui *u,int x,int y) {
    int w=u->graphics.fb.width,h=u->graphics.fb.height;
    if(x>=64 && x<152 && y<28) return 0;
    if(inside((struct ui_rect){w/2-140,h-44,280,32},x,y)) {
        int t=(x-(w/2-140))/93+1;return t>3?3:t;
    }
    if(u->launcher) {
        struct ui_rect r=launcher(u);
        if(inside(r,x,y)) {
            int row=0,step=r.h<200?20:32;
            for(unsigned id=0;id<u->count;++id) if(matches(u,id)) {
                if(r.y+72+(row+1)*step<=r.y+r.h-8 && y>=r.y+72+row*step && y<r.y+72+(row+1)*step) return 60+id;
                ++row;
            }
            return 4;
        }
        return 5;
    }
    if(u->count && inside((struct ui_rect){w/2-140,h-76,280,28},x,y)) {
        int id=(x-(w/2-140))*u->count/280;
        return 40+(id>=(int)u->count?(int)u->count-1:id);
    }
    int id=wm_hit(u,x,y);
    if(id>=0) {
        struct ui_rect r=u->surfaces[id].bounds;
        if(y>=r.y+2 && y<r.y+22 && x>=r.x+r.w-80) {
            int c=(x-(r.x+r.w-80))/28;
            return 10+id*3+(c>2?2:c);
        }
    }
    return -1;
}
static void restore(struct ui *u,int id) {
    if(id<0 || (unsigned)id>=u->count) return;
    wm_restore(u,id);
    if(u->launcher) {u->launcher=false;ui_invalidate(u,launcher(u));}
    arch_debug_puts("ON.: desktop ");arch_debug_puts(u->surfaces[id].title);arch_debug_puts(" opened.\n");
}
static void launcher_toggle(struct ui *u) {
    wm_cancel_capture(u);u->launcher=!u->launcher;
    ui_invalidate(u,launcher(u));wm_chrome(u);
    arch_debug_puts("ON.: desktop launcher toggled.\n");
}
bool desktop_mouse(struct ui *u,int32_t x,int32_t y,const struct mouse_event *e) {
    int t=target(u,x,y);
    if(t!=u->shell_hover) {
        if(u->shell_hover>=10 && u->shell_hover<34) wm_damage(u,(u->shell_hover-10)/3);
        u->shell_hover=t;
        if(t>=10 && t<34) wm_damage(u,(t-10)/3);
        ui_invalidate(u,(struct ui_rect){0,0,u->graphics.fb.width,28});wm_chrome(u);
        if(u->launcher) ui_invalidate(u,launcher(u));
    }
    bool down=e->left&&!u->left,up=!e->left&&u->left;
    if(u->dragging || u->resizing || u->capture_surface>=0) return false;
    if(t<0 && u->shell_capture<0) return false;
    if(down) u->shell_capture=t;
    if(up && t==u->shell_capture) {
        if(t==0 || t==1) launcher_toggle(u);
        else if(t==2) restore(u,0);
        else if(t==3) ui_theme(u,u->mode==UI_LIGHT?UI_DARK:UI_LIGHT);
        else if(t==5) {u->launcher=false;ui_invalidate(u,launcher(u));}
        else if(t>=60) restore(u,t-60);
        else if(t>=40) restore(u,t-40);
        else if(t>=10 && t<34) {
            int id=(t-10)/3,control=(t-10)%3;
            if(control==0) wm_minimize(u,id);
            else if(control==1) wm_maximize(u,id);
            else wm_close(u,id);
        }
    }
    if(up) u->shell_capture=-1;
    u->left=e->left;u->right=e->right;
    return true;
}
bool desktop_keyboard(struct ui *u,const struct keyboard_event *e) {
    if(e->scancode==0x3b) {launcher_toggle(u);return true;}
    if(!u->launcher) return false;
    if(e->character=='\n') {
        for(unsigned id=0;id<u->count;++id) if(matches(u,id)) {restore(u,id);break;}
    } else if(e->character=='\b' && u->search_length) u->search[--u->search_length]=0;
    else if(e->character>=32 && e->character<=126 && u->search_length<sizeof(u->search)-1) {
        u->search[u->search_length++]=e->character;u->search[u->search_length]=0;
    }
    ui_invalidate(u,launcher(u));return true;
}
void desktop_content(struct ui *u,int id) {
    struct graphics *g=&u->graphics;struct ui_surface *s=&u->surfaces[id];
    int x=s->bounds.x+12,y=s->bounds.y+36;
    int cx=g->clip_x,cy=g->clip_y,cw=g->clip_w,ch=g->clip_h;
    int top=s->bounds.y+28>cy?s->bounds.y+28:cy;
    int bottom=s->bounds.y+s->bounds.h-20<cy+ch?s->bounds.y+s->bounds.h-20:cy+ch;
    graphics_clip(g,cx,top,cw,bottom>top?bottom-top:0);
    if(s->kind==1) {
        graphics_text(g,x,y,"ON. / foreground desktop",1,u->tokens.ink);
        graphics_text(g,x,y+20,"Independent GUI windows",1,u->tokens.muted);
        graphics_text(g,x,y+40,"One kernel input/render loop",1,u->tokens.muted);
        graphics_text(g,x,y+60,"No processes or scheduler",1,u->tokens.muted);
        if(s->bounds.h>=160) {
            graphics_text(g,x,y+84,"F6 switch / Shift-F6 previous",1,u->tokens.muted);
            graphics_text(g,x,y+104,"F7 min / F8 max / F9 close",1,u->tokens.muted);
        }
    } else if(s->kind==2) {
        const char *names[]={"Canvas","Surface","Raised","Ink","Accent","Focus"};
        uint32_t colors[]={u->tokens.canvas,u->tokens.surface,u->tokens.raised,u->tokens.ink,u->tokens.accent,u->tokens.focus};
        int cell=(s->bounds.w-24)/3;
        for(int i=0;i<6;++i) {
            int px=x+(i%3)*cell,py=y+(i/3)*44;
            graphics_fill(g,px,py,cell-8,24,colors[i]);
            graphics_rect(g,px,py,cell-8,24,u->tokens.muted);
            graphics_text(g,px,py+30,names[i],1,u->tokens.ink);
        }
        if(s->bounds.h>=160) graphics_text(g,x,y+96,"Live semantic theme tokens",1,u->tokens.muted);
    }
    graphics_clip(g,cx,cy,cw,ch);
}
void desktop_overlay(struct ui *u) {
    struct graphics *g=&u->graphics;int w=g->fb.width,h=g->fb.height;
    graphics_fill(g,0,0,w,28,u->tokens.surface);
    graphics_text(g,12,6,"ON.",2,u->tokens.accent);
    graphics_text(g,68,10,"Menu [F1]",1,u->shell_hover==0?u->tokens.accent:u->tokens.ink);
    char clock[]="RTC 00:00";
    clock[4]='0'+u->rtc_minute/60/10;clock[5]='0'+u->rtc_minute/60%10;
    clock[7]='0'+u->rtc_minute%60/10;clock[8]='0'+u->rtc_minute%10;
    if(w>=640) {
        graphics_text(g,w-310,10,"PS/2 input | CPU online",1,u->tokens.muted);
        graphics_text(g,w-132,10,u->rtc_valid?clock:"Clock unavailable",1,u->tokens.muted);
    } else graphics_text(g,w-150,10,"Clock unavailable",1,u->tokens.muted);
    struct ui_rect d={w/2-140,h-44,280,32};panel(u,d,u->tokens.raised);
    const char *labels[]={"Launcher","Control","Appearance"};
    for(int i=0;i<3;++i) {
        int x=d.x+i*93;
        if(u->shell_hover==i+1) panel(u,(struct ui_rect){x+2,d.y+2,89,28},u->tokens.surface);
        graphics_rect(g,x+6,d.y+10,8,8,u->tokens.accent);
        if(i==0) {graphics_line(g,x+10,d.y+10,x+10,d.y+17,u->tokens.accent);graphics_line(g,x+6,d.y+14,x+13,d.y+14,u->tokens.accent);}
        else if(i==1) graphics_fill(g,x+9,d.y+13,2,2,u->tokens.accent);
        else graphics_fill(g,x+7,d.y+11,3,6,u->tokens.accent);
        graphics_text(g,x+20,d.y+12,labels[i],1,u->tokens.ink);
    }
    if(u->count) {
        struct ui_rect tasks={d.x,h-76,280,28};panel(u,tasks,u->tokens.raised);
        for(unsigned id=0;id<u->count;++id) {
            struct ui_surface *s=&u->surfaces[id];
            int x=tasks.x+id*280/u->count,ww=(id+1)*280/u->count-id*280/u->count;
            if(u->shell_hover==40+(int)id) panel(u,(struct ui_rect){x+2,tasks.y+2,ww-4,24},u->tokens.surface);
            int cx=g->clip_x,cy=g->clip_y,cw=g->clip_w,ch=g->clip_h;
            int left=x>cx?x:cx,right=x+ww<cx+cw?x+ww:cx+cw;
            graphics_clip(g,left,cy,right>left?right-left:0,ch);
            graphics_text(g,x+6,tasks.y+6,s->title,1,s->closed?u->tokens.muted:u->tokens.ink);
            if(s->visible) graphics_fill(g,x+6,tasks.y+20,ww-12,id== (unsigned)u->focus?3:1,id==(unsigned)u->focus?u->tokens.accent:u->tokens.muted);
            else if(s->minimized) graphics_rect(g,x+ww/2-4,tasks.y+19,8,3,u->tokens.muted);
            graphics_clip(g,cx,cy,cw,ch);
        }
    }
    if(!u->launcher) return;
    struct ui_rect r=launcher(u);panel(u,r,u->tokens.surface);
    graphics_text(g,r.x+16,r.y+16,"Explore ON.",2,u->tokens.ink);
    panel(u,(struct ui_rect){r.x+12,r.y+40,r.w-24,24},u->tokens.raised);
    graphics_text(g,r.x+20,r.y+48,u->search_length?u->search:"Search foreground tools...",1,u->tokens.muted);
    int row=0,step=r.h<200?20:32;
    for(unsigned id=0;id<u->count;++id) if(matches(u,id)) {
        int y=r.y+72+row*step;
        if(y+step<=r.y+r.h-8) {
            panel(u,(struct ui_rect){r.x+12,y,r.w-24,step-2},u->shell_hover==60+(int)id?u->tokens.raised:u->tokens.canvas);
            graphics_text(g,r.x+20,y+8,u->surfaces[id].title,1,u->tokens.ink);
        }
        ++row;
    }
    if(!row) graphics_text(g,r.x+16,r.y+84,"No available tools match",1,u->tokens.muted);
    if(r.h>200) {
        graphics_text(g,r.x+16,r.y+184,"Flow / Pulse / Spaces / Studio: planned",1,u->tokens.muted);
        graphics_text(g,r.x+16,r.y+208,"Enter opens tool | Esc: terminal",1,u->tokens.muted);
        graphics_text(g,r.x+16,r.y+232,"Foreground windows; no app processes",1,u->tokens.muted);
    }
}
