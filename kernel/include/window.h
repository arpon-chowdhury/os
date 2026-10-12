#ifndef ON_WINDOW_H
#define ON_WINDOW_H
#include "ui.h"
/* Foreground window management; no process, scheduler or allocation ownership.
   IDs index scene-owned records and are valid until ui_init resets the scene. */
enum wm_edge { WM_LEFT=1, WM_RIGHT=2, WM_TOP=4, WM_BOTTOM=8 };
struct ui_rect wm_workarea(struct ui *u);
void wm_damage(struct ui *u, int id);
void wm_chrome(struct ui *u);
bool wm_contains(struct ui_rect r, int32_t x, int32_t y);
int wm_hit(struct ui *u, int32_t x, int32_t y);
void wm_minimize(struct ui *u, int id);
void wm_close(struct ui *u, int id);
void wm_restore(struct ui *u, int id);
void wm_maximize(struct ui *u, int id);
void wm_resize(struct ui *u, int id, struct ui_rect bounds);
void wm_cycle(struct ui *u, bool reverse);
void wm_cancel_capture(struct ui *u);
void wm_pointer_resize(struct ui *u, int32_t x, int32_t y);
unsigned int wm_resize_edges(struct ui *u, int id, int32_t x, int32_t y);
#endif
