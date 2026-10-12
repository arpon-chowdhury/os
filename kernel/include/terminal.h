#ifndef ON_TERMINAL_H
#define ON_TERMINAL_H
#include "framebuffer.h"
#include "keyboard.h"
#include "mouse.h"
/* Single foreground consumer. No allocation; never call from IRQ handlers.
   A failed framebuffer initialization leaves all terminal operations inert. */
bool terminal_init(const struct kernel_framebuffer *fb);
void terminal_write(const char *text);
void terminal_handle_event(const struct keyboard_event *event);
void terminal_mouse(const struct mouse_event *event);
void terminal_tick(uint64_t ticks);
#endif
