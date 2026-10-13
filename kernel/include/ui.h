#ifndef ON_UI_H
#define ON_UI_H
#include "graphics.h"
#include "keyboard.h"
#include "mouse.h"
#define UI_MAX_SURFACES 8
#define UI_MAX_BUTTONS 8
#define UI_MAX_DIRTY 16
struct ui_rect {
    int32_t x, y, w, h;
};
enum ui_theme_mode { UI_LIGHT, UI_DARK, UI_AUTO };
struct ui_tokens {
    uint32_t canvas, surface, raised, ink, muted, accent, focus;
};
enum ui_action { UI_ACTIVATE, UI_SET_LIGHT, UI_SET_DARK, UI_SET_AUTO, UI_WALLPAPER };
struct ui_button {
    struct ui_rect bounds;
    char label[24];
    unsigned int action;
    bool stretch;
};
struct ui_surface {
    bool visible, retired;
    bool minimized, closed, maximized;
    struct ui_rect restore_bounds;
    unsigned int kind;
    struct ui_rect bounds;
    char title[24];
    struct ui_button buttons[UI_MAX_BUTTONS];
    unsigned int button_count;
    unsigned int selected, activations;
    char last_key;
};
/* Retained opaque display lists, single foreground owner, no allocation.
   Bounds are in pixels. Child button bounds are relative to the surface. */
struct ui {
    void (*application_content)(struct ui *, int);
    struct graphics graphics;
    struct ui_surface surfaces[UI_MAX_SURFACES];
    unsigned int order[UI_MAX_SURFACES], count;
    int focus, hover_surface, hover_button, capture_surface, capture_button;
    int32_t mouse_x, mouse_y, drag_x, drag_y;
    bool left, right, dragging, resizing;
    unsigned int resize_edges;
    struct ui_rect resize_start;
    enum ui_theme_mode mode;
    bool clock_valid;
    unsigned int minute;
    struct ui_tokens tokens;
    struct ui_rect dirty[UI_MAX_DIRTY];
    unsigned int dirty_count;
    uint64_t frames, redraw_pixels;
    unsigned int dock_selection;
    bool desktop, launcher, rtc_valid;
    unsigned int rtc_minute;
    unsigned int wallpaper;
    int shell_hover, shell_capture;
    char search[32];
    unsigned int search_length;
};
bool ui_init(struct ui *ui, const struct kernel_framebuffer *fb);
int ui_window(struct ui *ui, const char *title, struct ui_rect bounds);
bool ui_button_add(struct ui *ui, int window, const char *label, struct ui_rect bounds,
                   unsigned int action);
void ui_invalidate(struct ui *ui, struct ui_rect bounds);
void ui_focus(struct ui *ui, int window);
void ui_move(struct ui *ui, int window, int32_t x, int32_t y);
void ui_theme(struct ui *ui, enum ui_theme_mode mode);
/* Auto adapts to supplied local civil time (07:00–19:00 Light), otherwise Dark.
   No RTC/host settings are read. The caller must supply a valid time service. */
void ui_auto_time(struct ui *ui, bool valid, unsigned int minute);
void ui_keyboard(struct ui *ui, const struct keyboard_event *event);
void ui_mouse(struct ui *ui, int32_t x, int32_t y, const struct mouse_event *event);
void ui_render(struct ui *ui);
bool desktop_open(const struct kernel_framebuffer *fb);
void desktop_background(struct ui *u);
void desktop_overlay(struct ui *u);
void desktop_content(struct ui *u, int id);
bool desktop_mouse(struct ui *u, int32_t x, int32_t y, const struct mouse_event *e);
bool desktop_keyboard(struct ui *u, const struct keyboard_event *e);
bool ui_demo_open(const struct kernel_framebuffer *fb);
void ui_demo_keyboard(const struct keyboard_event *event);
void ui_demo_mouse(const struct mouse_event *event);
void ui_demo_render(void);
void ui_demo_close(void);
#endif
