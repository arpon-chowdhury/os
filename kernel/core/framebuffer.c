#include <stddef.h>
#include <stdint.h>
#include "framebuffer.h"
#include "version.h"
#include "font.h"

bool framebuffer_valid(const struct kernel_framebuffer *fb) {
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

static void raw_pixel(const struct kernel_framebuffer *fb, uint64_t x, uint64_t y, uint32_t color) {
    if (x >= fb->width || y >= fb->height) return;
    volatile uint8_t *address = (volatile uint8_t *)fb->address + y * fb->pitch + x * 4;
    /* Byte stores tolerate unaligned addresses and non-word-aligned pitch. */
    for (unsigned int byte = 0; byte < 4; ++byte) address[byte] = (uint8_t)(color >> (byte * 8));
}

#define POINTER_W 12u
#define POINTER_H 16u
static struct kernel_framebuffer pointer_fb;
static bool pointer_active;
static uint64_t pointer_x, pointer_y;
static uint32_t saved[POINTER_H][POINTER_W];
static bool pointer_shape(unsigned int x, unsigned int y) {
    return y < 12 ? x <= y / 2 : (x >= 3 && x <= 5);
}
static uint32_t pointer_color(unsigned int x, unsigned int y) {
    bool edge = x == 0 || (y < 12 && x == y/2) || y == 15;
    return rgb(&pointer_fb, edge ? 15 : 255, edge ? 23 : 255, edge ? 42 : 255);
}
static void pixel(const struct kernel_framebuffer *fb, uint64_t x, uint64_t y, uint32_t color) {
    if (pointer_active && fb->address == pointer_fb.address && x >= pointer_x && y >= pointer_y &&
        x - pointer_x < POINTER_W && y - pointer_y < POINTER_H) {
        unsigned int dx = x - pointer_x, dy = y - pointer_y;
        saved[dy][dx] = color;
        if (pointer_shape(dx, dy)) color = pointer_color(dx, dy);
    }
    raw_pixel(fb, x, y, color);
}
static void pointer_restore(void) {
    for (unsigned int y=0;y<POINTER_H;++y) for(unsigned int x=0;x<POINTER_W;++x)
        raw_pixel(&pointer_fb,pointer_x+x,pointer_y+y,saved[y][x]);
}
static void pointer_draw(void) {
    for (unsigned int y=0;y<POINTER_H;++y) for(unsigned int x=0;x<POINTER_W;++x) {
        if (pointer_x+x >= pointer_fb.width || pointer_y+y >= pointer_fb.height) continue;
        volatile uint8_t *p=(volatile uint8_t *)pointer_fb.address+(pointer_y+y)*pointer_fb.pitch+(pointer_x+x)*4;
        uint32_t value=0;
        for(unsigned int byte=0;byte<4;++byte) value |= (uint32_t)p[byte] << (8*byte);
        saved[y][x]=value;
        if(pointer_shape(x,y)) raw_pixel(&pointer_fb,pointer_x+x,pointer_y+y,pointer_color(x,y));
    }
}
bool framebuffer_pointer_init(const struct kernel_framebuffer *fb) {
    if (pointer_active) pointer_restore();
    pointer_active=false;
    if (!framebuffer_valid(fb)) return false;
    pointer_fb=*fb;
    pointer_x=fb->width/2; pointer_y=fb->height/2;
    pointer_draw(); pointer_active=true;
    return true;
}
void framebuffer_pointer_move(int16_t dx, int16_t dy) {
    if (!pointer_active) return;
    pointer_restore();
    int64_t x=(int64_t)pointer_x+dx, y=(int64_t)pointer_y-dy;
    if(x<0) x=0;
    if(y<0) y=0;
    if(x>(int64_t)pointer_fb.width-POINTER_W) x=pointer_fb.width-POINTER_W;
    if(y>(int64_t)pointer_fb.height-POINTER_H) y=pointer_fb.height-POINTER_H;
    pointer_x=x; pointer_y=y;
    pointer_draw();
}
void framebuffer_pointer_position(uint64_t *x, uint64_t *y) {
    if(x) *x=pointer_x;
    if(y) *y=pointer_y;
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
        uint8_t glyph[7];
        font_glyph((unsigned char)ch, glyph);
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
    if (!framebuffer_valid(fb)) return false;
    uint32_t background = rgb(fb, 15, 23, 42);
    for (uint64_t y = 0; y < fb->height; ++y)
        for (uint64_t x = 0; x < fb->width; ++x) pixel(fb, x, y, background);
    unsigned int scale = fb->width >= 640 ? 3 : 2;
    uint64_t y = fb->height / 2 - 28;
    text(fb, "Welcome to ON.", y, scale, rgb(fb, 226, 232, 240));
    text(fb, "Kernel " ON_KERNEL_VERSION, y + 48, 2, rgb(fb, 56, 189, 248));
    return true;
}

void framebuffer_fill(const struct kernel_framebuffer *fb, uint64_t x, uint64_t y,
                      uint64_t width, uint64_t height, uint32_t color) {
    if (!framebuffer_valid(fb) || x >= fb->width || y >= fb->height) return;
    if (width > fb->width - x) width = fb->width - x;
    if (height > fb->height - y) height = fb->height - y;
    uint32_t packed = rgb(fb, color >> 16, color >> 8, color);
    for (uint64_t dy = 0; dy < height; ++dy)
        for (uint64_t dx = 0; dx < width; ++dx) pixel(fb, x + dx, y + dy, packed);
}

void framebuffer_cell(const struct kernel_framebuffer *fb, unsigned int col,
                      unsigned int row, char ch, bool cursor) {
    uint64_t x = (uint64_t)col * 12, y = (uint64_t)row * 16;
    framebuffer_fill(fb, x, y, 12, 16, 0x0f172a);
    uint8_t glyph[7];
    font_glyph((unsigned char)ch, glyph);
    for (unsigned int r=0;r<7;++r) for (unsigned int c=0;c<5;++c)
        if (glyph[r] & (1u << (4-c))) framebuffer_fill(fb,x+c*2,y+r*2,2,2,0xe2e8f0);
    if (cursor) framebuffer_fill(fb,x,y+14,10,2,0x38bdf8);
}
