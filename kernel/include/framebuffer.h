#ifndef ON_FRAMEBUFFER_H
#define ON_FRAMEBUFFER_H
#include <stdbool.h>
#include <stdint.h>

/* Bootloader-independent descriptor; RGB memory model has value 1. */
struct kernel_framebuffer {
    void *address;
    uint64_t width, height, pitch;
    uint16_t bpp;
    uint8_t memory_model;
    uint8_t red_size, red_shift, green_size, green_shift, blue_size, blue_shift;
};

/* Returns false without writing any pixels if validation fails. */
bool framebuffer_show_welcome(const struct kernel_framebuffer *framebuffer);
/* Validated, clipped terminal drawing primitives; RGB colors use 0xRRGGBB. */
bool framebuffer_valid(const struct kernel_framebuffer *fb);
void framebuffer_fill(const struct kernel_framebuffer *fb, uint64_t x, uint64_t y,
                      uint64_t width, uint64_t height, uint32_t color);
void framebuffer_cell(const struct kernel_framebuffer *fb, unsigned int col,
                      unsigned int row, char ch, bool cursor);
/* Foreground-only pointer overlay; writes beneath it update saved pixels. */
bool framebuffer_pointer_init(const struct kernel_framebuffer *fb);
void framebuffer_pointer_move(int16_t dx, int16_t dy);
void framebuffer_pointer_position(uint64_t *x, uint64_t *y);
#endif
