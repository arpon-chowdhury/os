#include "ui.h"
#include "window.h"
#include "arch.h"
#include <stddef.h>
static void zero(void *object, size_t bytes) {
    volatile unsigned char *p = object;
    while (bytes--)
        *p++ = 0;
}
static const struct ui_tokens dark = {0x0c1426, 0x18253d, 0x243650, 0xdde8f5,
                                      0x91a7c4, 0x66d9cf, 0x687cff};
static const struct ui_tokens light = {0xf2f5fa, 0xffffff, 0xe4ebf4, 0x18253d,
                                       0x50627c, 0x166c65, 0x4354be};
static void copy(char *to, const char *from, size_t size) {
    size_t i = 0;
    if (from)
        while (i + 1 < size && from[i]) {
            to[i] = from[i];
            ++i;
        }
    to[i] = 0;
}
static bool contains(struct ui_rect r, int32_t x, int32_t y) {
    return x >= r.x && y >= r.y && (int64_t)x < (int64_t)r.x + r.w &&
           (int64_t)y < (int64_t)r.y + r.h;
}
static bool overlaps(struct ui_rect a, struct ui_rect b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}
void ui_invalidate(struct ui *u, struct ui_rect r) {
    if (!u || !u->graphics.ready || r.w <= 0 || r.h <= 0)
        return;
    int64_t right = (int64_t)r.x + r.w, bottom = (int64_t)r.y + r.h;
    if (r.x < 0)
        r.x = 0;
    if (r.y < 0)
        r.y = 0;
    if (right > (int64_t)u->graphics.fb.width)
        right = u->graphics.fb.width;
    if (bottom > (int64_t)u->graphics.fb.height)
        bottom = u->graphics.fb.height;
    if (right <= r.x || bottom <= r.y)
        return;
    r.w = right - r.x;
    r.h = bottom - r.y;
    /* Restart after each union so newly intersecting regions are merged too. */
    for (unsigned int i = 0; i < u->dirty_count;) {
        struct ui_rect d = u->dirty[i];
        if (overlaps(r, d)) {
            int right_edge = r.x + r.w > d.x + d.w ? r.x + r.w : d.x + d.w;
            int bottom_edge = r.y + r.h > d.y + d.h ? r.y + r.h : d.y + d.h;
            if (d.x < r.x)
                r.x = d.x;
            if (d.y < r.y)
                r.y = d.y;
            r.w = right_edge - r.x;
            r.h = bottom_edge - r.y;
            u->dirty[i] = u->dirty[--u->dirty_count];
            i = 0;
        } else
            ++i;
    }
    if (u->dirty_count == UI_MAX_DIRTY) {
        u->dirty_count = 0;
        r = (struct ui_rect){0, 0, u->graphics.fb.width, u->graphics.fb.height};
    }
    u->dirty[u->dirty_count++] = r;
}
static void all(struct ui *u) {
    ui_invalidate(u, (struct ui_rect){0, 0, u->graphics.fb.width, u->graphics.fb.height});
}
static struct ui_rect dock(struct ui *u) {
    int w = u->graphics.fb.width >= 640 ? 360 : 280;
    return (struct ui_rect){((int)u->graphics.fb.width - w) / 2, (int)u->graphics.fb.height - 44, w,
                            32};
}
static void tokens(struct ui *u) {
    bool daytime = u->clock_valid && u->minute >= 420 && u->minute < 1140;
    u->tokens = (u->mode == UI_LIGHT || (u->mode == UI_AUTO && daytime)) ? light : dark;
}
bool ui_init(struct ui *u, const struct kernel_framebuffer *fb) {
    if (!u)
        return false;
    zero(u, sizeof(*u));
    u->focus = u->hover_surface = u->hover_button = u->capture_surface = u->capture_button = -1;
    if (!graphics_init(&u->graphics, fb))
        return false;
    u->mode = UI_DARK;
    tokens(u);
    all(u);
    return true;
}
int ui_window(struct ui *u, const char *title, struct ui_rect r) {
    if (!u || !u->graphics.ready || u->count == UI_MAX_SURFACES || r.w < 120 || r.h < 90 ||
        r.w > (int32_t)u->graphics.fb.width || r.h > (int32_t)u->graphics.fb.height - 72)
        return -1;
    if (r.x < 0)
        r.x = 0;
    if (r.y < 28)
        r.y = 28;
    if (r.x > (int32_t)u->graphics.fb.width - r.w)
        r.x = u->graphics.fb.width - r.w;
    struct ui_rect area=wm_workarea(u);
    if(r.h>area.h) return -1;
    if (r.y > area.y+area.h-r.h) r.y=area.y+area.h-r.h;
    unsigned int id = u->count++;
    zero(&u->surfaces[id], sizeof(u->surfaces[id]));
    u->surfaces[id].visible = true;
    u->surfaces[id].bounds = r;
    copy(u->surfaces[id].title, title, sizeof(u->surfaces[id].title));
    u->order[id] = id;
    ui_invalidate(u, r);
    ui_focus(u, id);
    return id;
}
bool ui_button_add(struct ui *u, int id, const char *label, struct ui_rect r, unsigned int action) {
    if (!u || id < 0 || (unsigned int)id >= u->count)
        return false;
    struct ui_surface *s = &u->surfaces[id];
    if (s->button_count == UI_MAX_BUTTONS || r.x < 4 || r.y < 28 || r.w < 16 || r.h < 16 ||
        (int64_t)r.x + r.w > s->bounds.w - 4 || (int64_t)r.y + r.h > s->bounds.h - 4)
        return false;
    struct ui_button *b = &s->buttons[s->button_count++];
    *b = (struct ui_button){.bounds = r, .action = action};
    copy(b->label, label, sizeof(b->label));
    ui_invalidate(u, s->bounds);
    return true;
}
void ui_focus(struct ui *u, int id) {
    if (!u || id < 0 || (unsigned int)id >= u->count || !u->surfaces[id].visible)
        return;
    if (u->focus >= 0)
        wm_damage(u, u->focus);
    for (unsigned int i = 0; i < u->count; ++i)
        if (u->order[i] == (unsigned int)id) {
            for (unsigned int j = i; j + 1 < u->count; ++j)
                u->order[j] = u->order[j + 1];
            u->order[u->count - 1] = id;
            break;
        }
    u->focus = id;
    wm_damage(u, id);
    wm_chrome(u);
    arch_debug_puts("ON.: UI focus ");
    arch_debug_puts(u->surfaces[id].title);
    arch_debug_puts("\n");
}
void ui_move(struct ui *u, int id, int32_t x, int32_t y) {
    if (!u || id < 0 || (unsigned int)id >= u->count)
        return;
    struct ui_surface *s = &u->surfaces[id];
    if (s->closed || s->maximized) return;
    if (x < 0)
        x = 0;
    if (y < 28)
        y = 28;
    if (x > (int32_t)u->graphics.fb.width - s->bounds.w)
        x = u->graphics.fb.width - s->bounds.w;
    struct ui_rect area=wm_workarea(u);
    if (y > area.y+area.h-s->bounds.h) y=area.y+area.h-s->bounds.h;
    if (x == s->bounds.x && y == s->bounds.y)
        return;
    wm_damage(u, id);
    s->bounds.x = x;
    s->bounds.y = y;
    wm_damage(u, id);
    arch_debug_puts("ON.: UI window moved.\n");
}
void ui_theme(struct ui *u, enum ui_theme_mode mode) {
    if (!u || !u->graphics.ready || mode > UI_AUTO || mode < UI_LIGHT)
        return;
    u->mode = mode;
    tokens(u);
    all(u);
    arch_debug_puts(mode == UI_LIGHT  ? "ON.: UI theme Light.\n"
                    : mode == UI_DARK ? "ON.: UI theme Dark.\n"
                                      : "ON.: UI theme Auto.\n");
}
void ui_auto_time(struct ui *u, bool valid, unsigned int minute) {
    if (!u || !u->graphics.ready)
        return;
    u->clock_valid = valid && minute < 1440;
    u->minute = minute;
    struct ui_tokens old = u->tokens;
    tokens(u);
    if (old.canvas != u->tokens.canvas)
        all(u);
}
static struct ui_rect button_rect(struct ui_surface *s, unsigned int i) {
    struct ui_rect r = s->buttons[i].bounds;
    r.x += s->bounds.x;
    r.y += s->bounds.y;
    return r;
}
static void activate(struct ui *u, int id, int button) {
    if (id < 0 || (unsigned int)id >= u->count || button < 0 ||
        (unsigned int)button >= u->surfaces[id].button_count)
        return;
    struct ui_surface *s = &u->surfaces[id];
    struct ui_button *b = &s->buttons[button];
    ++s->activations;
    if (b->action == UI_WALLPAPER) { u->wallpaper ^= 1; all(u); }
    if (b->action >= 1 && b->action <= 3)
        ui_theme(u, b->action - 1);
    ui_invalidate(u, s->bounds);
    arch_debug_puts("ON.: UI activate ");
    arch_debug_puts(b->label);
    arch_debug_puts("\n");
}
void ui_keyboard(struct ui *u, const struct keyboard_event *e) {
    if (!u || !u->graphics.ready || !e || !e->pressed)
        return;
    if (u->desktop && desktop_keyboard(u, e)) return;
    if (e->scancode == 0x40) { /* F6 / Shift-F6 */
        wm_cycle(u,e->shift);return;
    }
    if (u->desktop && u->focus>=0) {
        if(e->scancode==0x41) {wm_minimize(u,u->focus);return;} /* F7 */
        if(e->scancode==0x42) {wm_maximize(u,u->focus);return;} /* F8 */
        if(e->scancode==0x43) {wm_close(u,u->focus);return;} /* F9 */
    }
    if (u->focus < 0) return;
    struct ui_surface *s = &u->surfaces[u->focus];
    if (e->character == '\t' && s->button_count) {
        s->selected = (s->selected + (e->shift ? s->button_count - 1 : 1)) % s->button_count;
        ui_invalidate(u, s->bounds);
    } else if (e->character == '\n' || e->character == ' ')
        activate(u, u->focus, s->selected);
    else if (e->character >= 32 && e->character <= 126) {
        s->last_key = e->character;
        ui_invalidate(u, s->bounds);
        arch_debug_puts("ON.: UI key routed to ");
        arch_debug_puts(s->title);
        arch_debug_puts("\n");
    }
}
void ui_mouse(struct ui *u, int32_t x, int32_t y, const struct mouse_event *e) {
    if (!u || !u->graphics.ready || !e)
        return;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x >= (int32_t)u->graphics.fb.width)
        x = u->graphics.fb.width - 1;
    if (y >= (int32_t)u->graphics.fb.height)
        y = u->graphics.fb.height - 1;
    if (u->desktop && desktop_mouse(u, x, y, e)) return;
    bool down = e->left && !u->left, up = !e->left && u->left;
    bool right_down = e->right && !u->right;
    u->mouse_x = x;
    u->mouse_y = y;
    u->left = e->left;
    u->right = e->right;
    bool was_geometry = u->dragging || u->resizing;
    if (u->resizing) {
        if(e->left) wm_pointer_resize(u,x,y);
        else u->resizing=false;
    }
    if (u->dragging) {
        if (e->left)
            ui_move(u, u->capture_surface, x - u->drag_x, y - u->drag_y);
        else
            u->dragging = false;
    }
    int hit = -1, button = -1;
    struct ui_rect d = dock(u);
    if (!contains(d, x, y))
        for (unsigned int n = u->count; n > 0; --n) {
            int id = u->order[n - 1];
            struct ui_surface *s = &u->surfaces[id];
            if (!s->visible || !(u->desktop ? wm_contains(s->bounds,x,y) : contains(s->bounds, x, y)))
                continue;
            hit = id;
            for (unsigned int i = 0; i < s->button_count; ++i)
                if (contains(button_rect(s, i), x, y)) {
                    button = i;
                    break;
                }
            break;
        }
    if (hit != u->hover_surface || button != u->hover_button) {
        if (u->hover_surface >= 0)
            ui_invalidate(u, u->surfaces[u->hover_surface].bounds);
        u->hover_surface = hit;
        u->hover_button = button;
        if (hit >= 0)
            ui_invalidate(u, u->surfaces[hit].bounds);
    }
    if (down) {
        u->capture_surface = hit;
        u->capture_button = button;
        if (!u->desktop && contains(d, x, y)) {
            u->dock_selection = (unsigned int)(x - d.x) / (d.w / 5);
            if (u->dock_selection > 4)
                u->dock_selection = 4;
            ui_invalidate(u, d);
            if (u->dock_selection == 4 && u->count)
                ui_focus(u, 0);
            arch_debug_puts("ON.: UI dock selected.\n");
        } else if (hit >= 0) {
            ui_focus(u, hit);
            if (button >= 0) {
                u->surfaces[hit].selected = button;
                ui_invalidate(u, u->surfaces[hit].bounds);
            }
            unsigned edges=u->desktop?wm_resize_edges(u,hit,x,y):0;
            if(edges) {
                u->resizing=true;u->resize_edges=edges;u->resize_start=u->surfaces[hit].bounds;
                u->drag_x=x;u->drag_y=y;
            } else if (!u->surfaces[hit].maximized && y < u->surfaces[hit].bounds.y + 24) {
                u->dragging = true;
                u->drag_x = x - u->surfaces[hit].bounds.x;
                u->drag_y = y - u->surfaces[hit].bounds.y;
            }
        }
    }
    if (right_down && hit >= 0)
        ui_focus(u, hit);
    if (up) {
        if (!was_geometry && hit == u->capture_surface && button == u->capture_button)
            activate(u, hit, button);
        u->capture_surface = u->capture_button = -1;
    }
}
static void rounded(struct graphics *g, struct ui_rect r, uint32_t color) {
    int radius = 8;
    for (int y = 0; y < r.h; ++y) {
        int dist = y < radius ? radius - y - 1 : (y >= r.h - radius ? y - (r.h - radius) : 0),
            inset = 0;
        if (dist)
            while ((radius - inset) * (radius - inset) + dist * dist > radius * radius)
                ++inset;
        graphics_fill(g, r.x + inset, r.y + y, r.w - 2 * inset, 1, color);
    }
}
static struct ui_rect clip_bounds(struct graphics *g) {
    return (struct ui_rect){g->clip_x, g->clip_y, g->clip_w, g->clip_h};
}
static void clip_child(struct graphics *g, struct ui_rect r) {
    struct ui_rect old = clip_bounds(g);
    int right = r.x + r.w < old.x + old.w ? r.x + r.w : old.x + old.w;
    int bottom = r.y + r.h < old.y + old.h ? r.y + r.h : old.y + old.h;
    int x = r.x > old.x ? r.x : old.x, y = r.y > old.y ? r.y : old.y;
    graphics_clip(g, x, y, right > x ? right - x : 0, bottom > y ? bottom - y : 0);
}
static void surface(struct ui *u, int id) {
    struct ui_surface *s = &u->surfaces[id];
    struct graphics *g = &u->graphics;
    struct ui_rect previous = clip_bounds(g);
    if(u->desktop) {
        struct ui_rect shadow=s->bounds;shadow.x+=4;shadow.y+=6;
        rounded(g,shadow,u->tokens.canvas);
    }
    clip_child(g, s->bounds);
    if(u->desktop) {
        rounded(g,s->bounds,id==u->focus?u->tokens.accent:u->tokens.muted);
        struct ui_rect inner=s->bounds;inner.x++;inner.y++;inner.w-=2;inner.h-=2;
        rounded(g,inner,u->tokens.surface);
    } else rounded(g, s->bounds, u->tokens.surface);
    graphics_fill(g, s->bounds.x + 8, s->bounds.y + 23, s->bounds.w - 16, 1,
                  id == u->focus ? u->tokens.accent : u->tokens.raised);
    struct ui_rect title_clip=clip_bounds(g);
    if(u->desktop) clip_child(g,(struct ui_rect){s->bounds.x+8,s->bounds.y+4,s->bounds.w-92,16});
    graphics_text(g, s->bounds.x + 12, s->bounds.y + 8, s->title, 1, u->tokens.ink);
    graphics_clip(g,title_clip.x,title_clip.y,title_clip.w,title_clip.h);
    if (s->bounds.h >= 140)
        graphics_text(g, s->bounds.x + 12, s->bounds.y + s->bounds.h - 16, "Esc: terminal", 1,
                      u->tokens.muted);
    if (u->desktop) {
        for(int control=0;control<3;++control) {
            int x=s->bounds.x+s->bounds.w-80+control*28;
            if(u->shell_hover==10+id*3+control)
                graphics_fill(g,x,s->bounds.y+2,24,20,u->tokens.raised);
            graphics_rect(g,x+2,s->bounds.y+3,20,18,u->tokens.muted);
            if(control==0) graphics_line(g,x+8,s->bounds.y+14,x+15,s->bounds.y+14,u->tokens.ink);
            else if(control==1) {
                graphics_rect(g,x+8,s->bounds.y+7,8,8,u->tokens.ink);
                if(s->maximized) graphics_rect(g,x+6,s->bounds.y+9,8,8,u->tokens.ink);
            } else {
                graphics_line(g,x+8,s->bounds.y+8,x+15,s->bounds.y+15,u->tokens.ink);
                graphics_line(g,x+15,s->bounds.y+8,x+8,s->bounds.y+15,u->tokens.ink);
            }
        }
        desktop_content(u,id);
        if(!s->maximized) {
            graphics_line(g,s->bounds.x+s->bounds.w-14,s->bounds.y+s->bounds.h-5,s->bounds.x+s->bounds.w-5,s->bounds.y+s->bounds.h-14,u->tokens.muted);
            graphics_line(g,s->bounds.x+s->bounds.w-9,s->bounds.y+s->bounds.h-5,s->bounds.x+s->bounds.w-5,s->bounds.y+s->bounds.h-9,u->tokens.muted);
        }
    }
    char count[24] = "Activations: ";
    unsigned int value = s->activations, n = 0;
    char digits[11];
    do {
        digits[n++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    unsigned int pos = 13;
    while (n)
        count[pos++] = digits[--n];
    count[pos] = 0;
    if (s->bounds.h >= 140 && !u->desktop)
        graphics_text(g, s->bounds.x + 12, s->bounds.y + s->bounds.h - 32, count, 1,
                      u->tokens.muted);
    char last[2] = {s->last_key ? s->last_key : ' ', 0};
    if (!u->desktop) graphics_text(g, s->bounds.x + s->bounds.w - 16, s->bounds.y + 8, last, 1, u->tokens.accent);
    for (unsigned int i = 0; i < s->button_count; ++i) {
        struct ui_rect b = button_rect(s, i);
        bool hover = u->hover_surface == id && u->hover_button == (int)i;
        rounded(g, b, hover ? u->tokens.surface : u->tokens.raised);
        if (hover)
            graphics_rect(g, b.x, b.y, b.w, b.h, u->tokens.focus);
        if (id == u->focus && i == s->selected)
            graphics_rect(g, b.x, b.y, b.w, b.h, u->tokens.accent);
        struct ui_rect parent = clip_bounds(g);
        clip_child(g, b);
        graphics_text(g, b.x + 8, b.y + (b.h - 7) / 2, s->buttons[i].label, 1, u->tokens.ink);
        graphics_clip(g, parent.x, parent.y, parent.w, parent.h);
    }
    graphics_clip(g, previous.x, previous.y, previous.w, previous.h);
}
void ui_render(struct ui *u) {
    if (!u || !u->graphics.ready || !u->dirty_count)
        return;
    struct graphics *g = &u->graphics;
    for (unsigned int i = 0; i < u->dirty_count; ++i) {
        struct ui_rect r = u->dirty[i];
        graphics_clip(g, r.x, r.y, r.w, r.h);
        graphics_clear(g, u->tokens.canvas);
        if (u->desktop) desktop_background(u);
        if (!u->desktop) {
        graphics_fill(g, 0, 0, g->fb.width, 28, u->tokens.surface);
        graphics_text(g, 12, 6, "ON.", 2, u->tokens.accent);
        graphics_text(g, g->fb.width - 180, 10,
                      u->mode == UI_AUTO
                          ? (u->clock_valid ? "Auto / local schedule" : "Auto / Dark fallback")
                          : "Retained UI foundation",
                      1, u->tokens.muted);
        }
        for (unsigned int n = 0; n < u->count; ++n) {
            int id = u->order[n];
            if (u->surfaces[id].visible && overlaps(r, (struct ui_rect){u->surfaces[id].bounds.x-6,u->surfaces[id].bounds.y-4,u->surfaces[id].bounds.w+12,u->surfaces[id].bounds.h+12}))
                surface(u, id);
        }
        if (!u->desktop) {
        struct ui_rect d = dock(u);
        rounded(g, d, u->tokens.raised);
        const char *labels[5] = {"Flow", "Pulse", "Spaces", "Studio", "Control"};
        for (unsigned int n = 0; n < 5; ++n) {
            int x = d.x + n * (d.w / 5);
            struct ui_rect parent = clip_bounds(g);
            clip_child(g, (struct ui_rect){x, d.y, d.w / 5, d.h});
            graphics_text(g, x + 6, d.y + 11, labels[n], 1, u->tokens.ink);
            graphics_clip(g, parent.x, parent.y, parent.w, parent.h);
            if (n == u->dock_selection)
                graphics_fill(g, x + 6, d.y + 25, d.w / 5 - 12, 2, u->tokens.accent);
        }
        } else desktop_overlay(u);
        u->redraw_pixels += (uint64_t)r.w * r.h;
    }
    graphics_clip(g, 0, 0, g->fb.width, g->fb.height);
    u->dirty_count = 0;
    ++u->frames;
}
