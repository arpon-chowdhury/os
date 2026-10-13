#include "arch.h"
#include "ui.h"
#include "application.h"
#include "process.h"
#include "window.h"
static struct ui desktop;
static int app_ids[ON_APP_WINDOWS]={-1,-1,-1,-1};
static bool app_close_sent[ON_APP_WINDOWS];
static uint32_t app_handles[ON_APP_WINDOWS];
extern const unsigned char native_sample_start[],native_sample_end[];
static void app_content(struct ui *u,int id) {
    for(unsigned i=0;i<ON_APP_WINDOWS;++i) {
        uint64_t flags=arch_interrupt_save();
        struct application_window snapshot=application_windows()[i];
        bool match=app_ids[i]==id && snapshot.owner && !snapshot.closing;
        arch_interrupt_restore(flags);
        if(!match) continue;
        struct ui_surface *s=&u->surfaces[id];struct graphics *g=&u->graphics;
        int cx=g->clip_x,cy=g->clip_y,cw=g->clip_w,ch=g->clip_h;
        int x=s->bounds.x+4,y=s->bounds.y+24;
        int left=x>cx?x:cx,top=y>cy?y:cy;
        int right=s->bounds.x+s->bounds.w-4,bottom=s->bounds.y+s->bounds.h-4;
        if(right>cx+cw) right=cx+cw;
        if(bottom>cy+ch) bottom=cy+ch;
        graphics_clip(g,left,top,right>left?right-left:0,bottom>top?bottom-top:0);
        for(unsigned j=0;j<snapshot.draw_count;++j) {
            struct on_draw *d=&snapshot.draws[j];
            graphics_fill(g,x+d->x,y+d->y,d->w,d->h,d->color);
            graphics_text(g,x+d->x,y+d->y,d->text,1,d->color);
        }
        graphics_clip(g,cx,cy,cw,ch);
    }
}
static void app_pump(void) {
    uint64_t flags=arch_interrupt_save();struct application_window *ws=application_windows();
    for(unsigned i=0;i<ON_APP_WINDOWS;++i) {
        struct application_window *w=&ws[i];int id=app_ids[i];
        if(!w->owner) continue;
        if(w->closing) {
            if(id>=0) {wm_close(&desktop,id);desktop.surfaces[id].retired=true;wm_chrome(&desktop);}
            arch_debug_puts("ON.: native window cleaned.\n");
            *w=(struct application_window){0};app_handles[i]=0;continue;
        }
        if(app_handles[i]!=w->handle) {
            struct ui_rect r={80+(int)i*32,80+(int)i*24,w->spec.width,w->spec.height};
            if(id>=0) {desktop.surfaces[id]=(struct ui_surface){.visible=true,.bounds=r};wm_resize(&desktop,id,r);ui_focus(&desktop,id);}
            else id=ui_window(&desktop,w->spec.title,r);
            if(id<0) {w->closing=true;continue;}
            app_ids[i]=id;app_handles[i]=w->handle;app_close_sent[i]=false;
            arch_debug_puts("ON.: native application window ready.\n");
        }
        if(desktop.surfaces[id].closed) {
            if(!app_close_sent[i]) application_event(w,(struct on_event){.type=ON_EVENT_CLOSE});
            app_close_sent[i]=true;
            continue;
        }
        app_close_sent[i]=false;
        if(w->dirty) {
            for(unsigned j=0;j<24;++j) desktop.surfaces[id].title[j]=w->spec.title[j];
            wm_damage(&desktop,id);wm_chrome(&desktop);w->dirty=false;
        }
    }
    arch_interrupt_restore(flags);
}
bool ui_demo_open(const struct kernel_framebuffer *fb) {
    uint64_t flags=arch_interrupt_save();
    struct application_window *ws=application_windows();
    for(unsigned i=0;i<ON_APP_WINDOWS;++i) {
        if(ws[i].owner) task_terminate(ws[i].owner);
        ws[i]=(struct application_window){0};app_ids[i]=-1;app_handles[i]=0;
    }
    arch_interrupt_restore(flags);
    if (!ui_init(&desktop, fb))
        return false;
    int width = fb->width >= 640 ? 280 : 160, height = fb->height >= 400 ? 180 : 100;
    int control = ui_window(&desktop, "Control",
                            (struct ui_rect){fb->width / 8, fb->height / 5, width, height});
    for (int i = 0; i < 3; ++i)
        ui_button_add(&desktop, control,
                      i == 0   ? "Light"
                      : i == 1 ? "Dark"
                               : "Auto",
                      (struct ui_rect){12, 30 + i * (height >= 180 ? 36 : 22), width - 24,
                                       height >= 180 ? 28 : 18},
                      i + 1);
    int canvas = ui_window(&desktop, "Canvas",
                           (struct ui_rect){fb->width / 2, fb->height / 3, width, height});
    ui_button_add(&desktop, canvas, "Activate", (struct ui_rect){12, 40, width - 24, 28}, 0);
    ui_render(&desktop);
    arch_debug_puts("ON.: retained UI ready; Escape returns to terminal.\n");
    return true;
}
void ui_demo_keyboard(const struct keyboard_event *e) {
    if(desktop.desktop && e->pressed && e->scancode==0x3f) {
        if(!process_load_elf("Native sample",native_sample_start,native_sample_end-native_sample_start))
            arch_debug_puts("ON.: native application launch failed.\n");
        return;
    }
    bool shell_key=desktop.launcher || (e->scancode>=0x3b && e->scancode<=0x43);
    ui_keyboard(&desktop,e);
    if(shell_key) return;
    uint64_t flags=arch_interrupt_save();
    for(unsigned i=0;i<ON_APP_WINDOWS;++i) if(app_ids[i]==desktop.focus)
        application_event(&application_windows()[i],(struct on_event){.type=ON_EVENT_KEY,.code=e->scancode,.character=(unsigned char)e->character,.flags=e->pressed|(e->shift<<1)});
    arch_interrupt_restore(flags);
}
void ui_demo_mouse(const struct mouse_event *e) {
    uint64_t x, y;
    framebuffer_pointer_position(&x, &y);
    ui_mouse(&desktop, x, y, e);
    uint64_t flags=arch_interrupt_save();
    int hit=wm_hit(&desktop,x,y);
    for(unsigned i=0;i<ON_APP_WINDOWS;++i) if(app_ids[i]==hit && hit==desktop.focus) {
        struct ui_rect r=desktop.surfaces[hit].bounds;
        if(x>=(uint64_t)(r.x+4) && y>=(uint64_t)(r.y+24))
            application_event(&application_windows()[i],(struct on_event){.type=ON_EVENT_MOUSE,.x=(int)x-r.x-4,.y=(int)y-r.y-24,.flags=e->left|(e->right<<1)});
    }
    arch_interrupt_restore(flags);
}
void ui_demo_render(void) {
    app_pump();
    if (desktop.desktop) {
        static uint64_t checked;
        uint64_t now=arch_system_ticks()/100;
        if(now!=checked) {
            checked=now;unsigned minute=0;
            bool valid=arch_clock_minute(&minute);
            if(valid!=desktop.rtc_valid || minute!=desktop.rtc_minute)
                ui_invalidate(&desktop,(struct ui_rect){0,0,desktop.graphics.fb.width,28});
            /* RTC is firmware time, not a configured local-time schedule. */
            desktop.rtc_valid=valid;desktop.rtc_minute=minute;
        }
    }
    ui_render(&desktop);
}

bool desktop_open(const struct kernel_framebuffer *fb) {
    if (!ui_demo_open(fb)) return false;
    desktop.desktop = true;
    desktop.application_content=app_content;
    desktop.shell_hover = desktop.shell_capture = -1;
    struct ui_surface *about=&desktop.surfaces[1];
    about->button_count=0;about->kind=1;
    const char *name="About ON.";
    for(unsigned i=0;i<10;++i) about->title[i]=name[i];
    int palette=ui_window(&desktop,"Palette",(struct ui_rect){fb->width/3,fb->height/2,fb->width>=640?280:160,fb->height>=400?180:100});
    if(palette>=0) desktop.surfaces[palette].kind=2;
    ui_focus(&desktop,0);
    struct ui_surface *s = &desktop.surfaces[0];
    s->button_count = 0;
    int step = s->bounds.h >= 180 ? 30 : 17;
    const char *labels[] = {"Light", "Dark", "Auto (Dark fallback)", "Change wallpaper"};
    for (int i=0;i<4;++i) {
        ui_button_add(&desktop, 0, labels[i], (struct ui_rect){12,28+i*step,s->bounds.w-24,step>=30?step-2:16}, i<3 ? (unsigned)i+1 : UI_WALLPAPER);
        if(s->button_count) s->buttons[s->button_count-1].stretch=true;
    }
    ui_invalidate(&desktop,(struct ui_rect){0,0,fb->width,fb->height});
    ui_render(&desktop);
    arch_debug_puts("ON.: desktop ready.\n");
    return true;
}

void ui_demo_close(void) {
    uint64_t flags=arch_interrupt_save();
    for(unsigned i=0;i<ON_APP_WINDOWS;++i) {
        struct application_window *w=&application_windows()[i];
        if(w->owner) task_terminate(w->owner);
        w->closing=true;
    }
    app_pump();arch_interrupt_restore(flags);
}
