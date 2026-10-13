#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "terminal.h"
#include "arch.h"
#include "heap.h"
#include "memory.h"
#include "task.h"
size_t task_list(struct task_info *info,size_t capacity) {(void)info;(void)capacity;return 0;}
void task_diagnostics(void) {}
bool graphics_demo(const struct kernel_framebuffer *fb) { (void)fb;return true; }
bool desktop_open(const struct kernel_framebuffer *fb) { (void)fb;return true; }
bool ui_demo_open(const struct kernel_framebuffer *fb) { (void)fb;return true; }
void ui_demo_keyboard(const struct keyboard_event *e) { (void)e; }
void ui_demo_mouse(const struct mouse_event *e) { (void)e; }
void ui_demo_render(void) {}
static char display[12][26];
static unsigned int cursor_count;
static char commands[2048];
bool framebuffer_valid(const struct kernel_framebuffer *fb) { return fb && fb->address && fb->width==320 && fb->height==200; }
void framebuffer_fill(const struct kernel_framebuffer *fb,uint64_t x,uint64_t y,uint64_t w,uint64_t h,uint32_t color) {
    (void)fb;(void)x;(void)y;(void)w;(void)h;(void)color;
    memset(display,' ',sizeof(display));
}
void framebuffer_cell(const struct kernel_framebuffer *fb,unsigned int x,unsigned int y,char ch,bool cursor) {
    (void)fb;assert(x<26 && y<12);display[y][x]=ch; if(cursor) ++cursor_count;
}
void arch_debug_puts(const char *s) { assert(strlen(commands)+strlen(s)<sizeof(commands));strcat(commands,s); }
uint64_t arch_system_ticks(void) { return 123; }
struct pmm_statistics pmm_get_statistics(void) { return (struct pmm_statistics){.free_frames=99}; }
struct kheap_statistics kheap_get_statistics(void) { return (struct kheap_statistics){.arena_bytes=1024}; }
static void type(const char *text) {
    while(*text) { struct keyboard_event e={.character=*text++,.pressed=true}; terminal_handle_event(&e); }
}
static bool contains(const char *text) {
    char flat[12*27+1];unsigned int n=0;
    for(unsigned int y=0;y<12;++y) { for(unsigned int x=0;x<26;++x) flat[n++]=display[y][x];flat[n++]='\n'; }
    flat[n]=0;return strstr(flat,text)!=NULL;
}
int main(void) {
    assert(!terminal_init(NULL));type("help\n");terminal_tick(50);
    struct kernel_framebuffer fb={.address=display,.width=320,.height=200};
    assert(terminal_init(&fb)); assert(contains("on> "));
    type("echo Ab!x\b\n");assert(contains("Ab!"));assert(strstr(commands,"ON.: command echo\n"));
    type("\b\b\b\b");assert(contains("on> "));
    type("bad\n");assert(contains("Unknown command."));
    type("clear\n");assert(!contains("Unknown")); assert(!memcmp(display[0],"on> ",4));
    type("mem\nheap\nticks\nversion\nhelp\n");assert(contains("help clear echo version"));
    for(unsigned int i=0;i<40;++i) type("echo scroll\n");
    assert(contains("scroll"));assert(contains("on> "));
    type("clear\n");
    for(unsigned int i=0;i<400;++i) type("a");
    for(unsigned int i=0;i<400;++i) type("\b");
    assert(!memcmp(display[0],"on> ",4));assert(display[0][4]==' ');
    struct keyboard_event release={.character='z',.pressed=false}; terminal_handle_event(&release);assert(display[0][4]==' ');
    unsigned int old=cursor_count; terminal_tick(100);assert(cursor_count>old);
    type("graphics\n");
    struct keyboard_event escape={.scancode=1,.pressed=true};
    terminal_handle_event(&escape);type("echo restored\n");assert(contains("restored"));
    puts("Terminal commands, editing, wrapping, scrolling, cursor, bounds, and fallback passed.");
}
