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
#endif
