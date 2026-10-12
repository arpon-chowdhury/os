#include <stddef.h>
#include <stdint.h>
#include "framebuffer.h"
#include "version.h"

/* Original 5x7 bitmap glyphs; lowercase uses the same letterforms. */
static const uint8_t letters[26][7] = {
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30},
    {14,17,16,16,16,17,14}, {30,17,17,17,17,17,30},
    {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17},
    {14,4,4,4,4,4,14}, {7,2,2,2,2,18,12},
    {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17},
    {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16},
    {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4},
    {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4},
    {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31},
};
static const uint8_t digits[10][7] = {
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
    {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14},
};
static const uint8_t dot[7] = {0,0,0,0,0,12,12};

static bool valid(const struct kernel_framebuffer *fb) {
    if (!fb || !fb->address || fb->bpp != 32 || fb->memory_model != 1 ||
        fb->width < 320 || fb->height < 200 ||
        fb->width > 8192 || fb->height > 8192 ||
        fb->red_size != 8 || fb->green_size != 8 || fb->blue_size != 8 ||
        fb->red_shift > 24 || fb->green_shift > 24 || fb->blue_shift > 24 ||
        fb->pitch < fb->width * 4 || fb->pitch > UINT64_MAX / fb->height) {
        return false;
    }
    uint32_t red = 255u << fb->red_shift;
    uint32_t green = 255u << fb->green_shift;
    uint32_t blue = 255u << fb->blue_shift;
    if ((red & green) || (red & blue) || (green & blue)) return false;
    uint64_t bytes = fb->pitch * fb->height;
    return bytes <= UINTPTR_MAX - (uintptr_t)fb->address;
}

static uint32_t rgb(const struct kernel_framebuffer *fb, uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << fb->red_shift) |
           ((uint32_t)g << fb->green_shift) |
           ((uint32_t)b << fb->blue_shift);
}

static void pixel(const struct kernel_framebuffer *fb, uint64_t x, uint64_t y, uint32_t color) {
    if (x >= fb->width || y >= fb->height) return;
    volatile uint8_t *address = (volatile uint8_t *)fb->address + y * fb->pitch + x * 4;
    /* Byte stores tolerate unaligned addresses and non-word-aligned pitch. */
    for (unsigned int byte = 0; byte < 4; ++byte) address[byte] = (uint8_t)(color >> (byte * 8));
}

static void text(const struct kernel_framebuffer *fb, const char *string,
                 uint64_t y, unsigned int scale, uint32_t color) {
    uint64_t length = 0;
    while (string[length]) ++length;
    if (length > fb->width / (6 * scale)) return;
    uint64_t x = (fb->width - length * 6 * scale) / 2;
    for (uint64_t i = 0; i < length; ++i) {
        char ch = string[i];
        if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
        const uint8_t *glyph = 0;
        if (ch >= 'A' && ch <= 'Z') glyph = letters[ch - 'A'];
        else if (ch >= '0' && ch <= '9') glyph = digits[ch - '0'];
        else if (ch == '.') glyph = dot;
        if (!glyph) continue;
        for (unsigned int row = 0; row < 7; ++row) {
            for (unsigned int col = 0; col < 5; ++col) {
                if (!(glyph[row] & (1u << (4 - col)))) continue;
                for (unsigned int dy = 0; dy < scale; ++dy)
                    for (unsigned int dx = 0; dx < scale; ++dx)
                        pixel(fb, x + (i * 6 + col) * scale + dx,
                              y + row * scale + dy, color);
            }
        }
    }
}

bool framebuffer_show_welcome(const struct kernel_framebuffer *fb) {
    if (!valid(fb)) return false;
    uint32_t background = rgb(fb, 15, 23, 42);
    for (uint64_t y = 0; y < fb->height; ++y)
        for (uint64_t x = 0; x < fb->width; ++x) pixel(fb, x, y, background);
    unsigned int scale = fb->width >= 640 ? 3 : 2;
    uint64_t y = fb->height / 2 - 28;
    text(fb, "Welcome to ON.", y, scale, rgb(fb, 226, 232, 240));
    text(fb, "Kernel " ON_KERNEL_VERSION, y + 48, 2, rgb(fb, 56, 189, 248));
    return true;
}
