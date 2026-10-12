#include "arch.h"
#include "ui.h"
static struct ui desktop;
bool ui_demo_open(const struct kernel_framebuffer *fb) {
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
void ui_demo_keyboard(const struct keyboard_event *e) { ui_keyboard(&desktop, e); }
void ui_demo_mouse(const struct mouse_event *e) {
    uint64_t x, y;
    framebuffer_pointer_position(&x, &y);
    ui_mouse(&desktop, x, y, e);
}
void ui_demo_render(void) {
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
