#ifndef ON_GRAPHICS_H
#define ON_GRAPHICS_H
#include "framebuffer.h"
/* Foreground only, borrowed framebuffer storage. No allocation, RGB 0xRRGGBB.
   Signed coordinates clip to screen; nonpositive dimensions are no-ops. */
struct graphics { struct kernel_framebuffer fb; bool ready; int32_t clip_x, clip_y, clip_w, clip_h; };
void graphics_clip(struct graphics *g, int32_t x, int32_t y, int32_t w, int32_t h);
bool graphics_init(struct graphics *g, const struct kernel_framebuffer *fb);
void graphics_pixel(struct graphics *g, int32_t x, int32_t y, uint32_t color);
void graphics_fill(struct graphics *g, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
void graphics_rect(struct graphics *g, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
void graphics_line(struct graphics *g, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color);
void graphics_text(struct graphics *g, int32_t x, int32_t y, const char *text, unsigned int scale, uint32_t color);
void graphics_clear(struct graphics *g, uint32_t color);
bool graphics_demo(const struct kernel_framebuffer *fb);
#endif
