#ifndef ON_APPLICATION_ABI_H
#define ON_APPLICATION_ABI_H
#include <stdint.h>
#define ON_WINDOW_TITLE 24
#define ON_WINDOW_COMMANDS 16
#define ON_WINDOW_EVENTS 16
#define ON_APP_WINDOWS 4
#define ON_SYS_WINDOW_CREATE 6
#define ON_SYS_WINDOW_TITLE 7
#define ON_SYS_WINDOW_DRAW 8
#define ON_SYS_WINDOW_EVENT 9
#define ON_SYS_WINDOW_CLOSE 10
/* Fixed-width copied messages. Coordinates are client-relative pixels. */
struct on_window_create { uint32_t width,height; char title[ON_WINDOW_TITLE]; };
struct on_draw { int32_t x,y,w,h; uint32_t color; char text[32]; };
enum on_event_type { ON_EVENT_KEY=1, ON_EVENT_MOUSE, ON_EVENT_CLOSE };
struct on_event { uint32_t type,code; int32_t x,y; uint32_t flags,character; };
#ifndef ON_KERNEL
static inline int64_t on_call(uint64_t n,uint64_t a,uint64_t b) {
    register uint64_t result __asm__("rax")=n;
    __asm__ volatile("int $0x81":"+a"(result):"D"(a),"S"(b):"memory","cc");
    return (int64_t)result;
}
#endif
#endif
