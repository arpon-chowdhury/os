#include "ui.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void arch_debug_puts(const char *s) { (void)s; }
static struct ui u;
static unsigned char *buffer;
static struct kernel_framebuffer fb;
static void mouse(int x, int y, bool left) {
    struct mouse_event e = {.left = left};
    ui_mouse(&u, x, y, &e);
}
static void key(char ch) {
    struct keyboard_event e = {.pressed = true, .character = ch};
    ui_keyboard(&u, &e);
}
int main(void) {
    size_t size = 1283 * 200;
    buffer = malloc(size + 2);
    assert(buffer);
    memset(buffer, 0xa5, size + 2);
    fb = (struct kernel_framebuffer){.address = buffer + 1,
                                     .width = 320,
                                     .height = 200,
                                     .pitch = 1283,
                                     .bpp = 32,
                                     .memory_model = 1,
                                     .red_size = 8,
                                     .red_shift = 16,
                                     .green_size = 8,
                                     .green_shift = 8,
                                     .blue_size = 8};
    assert(!ui_init(NULL, &fb));
    assert(!ui_init(&u, NULL));
    assert(ui_window(&u, "bad", (struct ui_rect){0, 0, 120, 90}) == -1);
    assert(ui_init(&u, &fb));
    int a = ui_window(&u, "Control", (struct ui_rect){10, 30, 140, 110});
    assert(a == 0);
    assert(ui_button_add(&u, a, "Light", (struct ui_rect){8, 30, 100, 22}, 1));
    assert(!ui_button_add(&u, a, "bad", (struct ui_rect){INT_MAX, INT_MAX, 40, 30}, 0));
    int b = ui_window(&u, "Canvas", (struct ui_rect){100, 40, 140, 110});
    assert(b == 1);
    assert(ui_button_add(&u, b, "A very long button label", (struct ui_rect){8, 30, 100, 22}, 0));
    ui_render(&u);
    uint64_t old = u.redraw_pixels;
    ui_render(&u);
    assert(u.redraw_pixels == old);
    key('x');
    assert(u.surfaces[b].last_key == 'x' && !u.surfaces[a].last_key);
    struct keyboard_event cycle = {.pressed = true, .scancode = 0x40};
    ui_keyboard(&u, &cycle);
    assert(u.focus == a);
    struct keyboard_event released = {.character = 'z'};
    ui_keyboard(&u, &released);
    assert(!u.surfaces[a].last_key);
    ui_focus(&u, a);
    key('q');
    assert(u.surfaces[a].last_key == 'q');
    ui_render(&u);
    mouse(30, 68, true);
    mouse(30, 68, false);
    assert(u.mode == UI_LIGHT && u.surfaces[a].activations == 1);
    mouse(30, 68, true);
    mouse(319, 150, false);
    assert(u.surfaces[a].activations == 1);
    ui_theme(&u, UI_AUTO);
    assert(u.tokens.canvas == 0x0c1426);
    ui_auto_time(&u, true, 420);
    assert(u.tokens.canvas == 0xf2f5fa);
    ui_auto_time(&u, true, 1140);
    assert(u.tokens.canvas == 0x0c1426);
    ui_auto_time(&u, true, 600);
    assert(u.tokens.canvas == 0xf2f5fa);
    ui_auto_time(&u, true, 1440);
    assert(!u.clock_valid && u.tokens.canvas == 0x0c1426);
    ui_theme(&u, UI_DARK);
    ui_render(&u);
    ui_move(&u, a, 20, 35);
    ui_render(&u);
    unsigned char *reference = malloc(size);
    assert(reference);
    memcpy(reference, buffer + 1, size);
    ui_invalidate(&u, (struct ui_rect){0, 0, 320, 200});
    ui_render(&u);
    assert(!memcmp(reference, buffer + 1, size));
    old = u.redraw_pixels;
    ui_invalidate(&u, (struct ui_rect){3, 3, 2, 2});
    ui_render(&u);
    assert(u.redraw_pixels - old == 4);
    mouse(25, 40, true);
    mouse(55, 50, true);
    mouse(55, 50, false);
    assert(u.surfaces[a].bounds.x == 50);
    ui_move(&u, a, INT_MIN, INT_MAX);
    assert(u.surfaces[a].bounds.x == 0 && u.surfaces[a].bounds.y == 46);
    ui_render(&u);
    memcpy(reference, buffer + 1, size);
    ui_invalidate(&u, (struct ui_rect){0, 0, 320, 200});
    ui_render(&u);
    assert(!memcmp(reference, buffer + 1, size));
    for (int i = 0; i < 17; ++i)
        ui_invalidate(&u, (struct ui_rect){i * 3, 0, 1, 1});
    assert(u.dirty_count == 1 && u.dirty[0].w == 320);
    for (int i = 2; i < UI_MAX_SURFACES; ++i)
        assert(ui_window(&u, "Window", (struct ui_rect){0, 28, 120, 90}) == i);
    assert(ui_window(&u, "Overflow", (struct ui_rect){0, 28, 120, 90}) == -1);
    ui_invalidate(&u, (struct ui_rect){INT_MIN, INT_MIN, INT_MAX, INT_MAX});
    mouse(INT_MIN, INT_MAX, false);
    assert(buffer[0] == 0xa5 && buffer[size + 1] == 0xa5);
    for (size_t y = 0; y < 200; ++y)
        for (size_t x = 1280; x < 1283; ++x)
            assert(buffer[1 + y * 1283 + x] == 0xa5);
    /* Desktop composition must preserve dirty/full equivalence and idle behavior. */
    u.desktop=true;u.shell_hover=u.shell_capture=-1;
    ui_invalidate(&u,(struct ui_rect){0,0,320,200});ui_render(&u);
    mouse(110,50,true);mouse(130,60,true);mouse(130,60,false);ui_render(&u);
    memcpy(reference,buffer+1,size);
    ui_invalidate(&u,(struct ui_rect){0,0,320,200});ui_render(&u);
    assert(!memcmp(reference,buffer+1,size));
    uint64_t desktop_frames=u.frames;ui_render(&u);assert(u.frames==desktop_frames);
    struct keyboard_event menu={.pressed=true,.scancode=0x3b};ui_keyboard(&u,&menu);
    assert(u.launcher);key('z');assert(u.search_length==1);key('\b');assert(!u.search_length);
    key('\n');assert(!u.launcher && u.surfaces[0].visible);
    ui_render(&u);assert(buffer[0]==0xa5 && buffer[size+1]==0xa5);
    assert(ui_init(&u,&fb));u.desktop=true;u.shell_hover=u.shell_capture=-1;
    assert(ui_window(&u,"Control",(struct ui_rect){20,30,140,100})==0);
    mouse(87,40,true);mouse(87,40,false);
    assert(!u.surfaces[0].visible && u.surfaces[0].minimized && u.focus==-1);
    mouse(160,170,true);mouse(160,170,false);
    assert(u.surfaces[0].visible && !u.surfaces[0].minimized && u.focus==0);
    mouse(148,40,true);mouse(148,40,false);
    assert(u.surfaces[0].closed && !u.surfaces[0].visible && u.focus==-1);
    mouse(160,170,true);mouse(160,170,false);
    assert(u.surfaces[0].visible && !u.surfaces[0].closed);
    /* A release over a shell control must not steal a captured window press. */
    mouse(30,40,true);mouse(160,170,true);mouse(160,170,false);
    assert(!u.dragging && u.capture_surface==-1);
    ui_render(&u);
    free(reference);
    free(buffer);
    puts("Retained UI focus, routing, capture, themes, drag, dirty/full equivalence, bounds, and "
         "guards passed.");
}
