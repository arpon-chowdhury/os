#include <stddef.h>
#include "terminal.h"
#include "arch.h"
#include "memory.h"
#include "heap.h"
#include "version.h"
#include "graphics.h"
#include "ui.h"
#include "task.h"
#define MAX_COLS 160u
#define MAX_ROWS 64u
#define LINE_BYTES 256u
static struct kernel_framebuffer screen;
static bool active, demo_active, ui_active;
static unsigned int cols, rows, column, row, length;
static uint64_t blink_phase;
static char cells[MAX_ROWS][MAX_COLS], line[LINE_BYTES];

static void cursor(bool visible) {
    if (active && !demo_active) framebuffer_cell(&screen, column, row, cells[row][column], visible);
}
static void clear(void) {
    for (unsigned int y=0;y<rows;++y) for(unsigned int x=0;x<cols;++x) cells[y][x]=' ';
    column=row=0;
    framebuffer_fill(&screen,0,0,screen.width,screen.height,0x0f172a);
}
static void newline(void) {
    column=0;
    if (++row < rows) return;
    row=rows-1;
    for(unsigned int y=0;y<rows-1;++y) for(unsigned int x=0;x<cols;++x) cells[y][x]=cells[y+1][x];
    for(unsigned int x=0;x<cols;++x) cells[row][x]=' ';
    for(unsigned int y=0;y<rows;++y) for(unsigned int x=0;x<cols;++x)
        framebuffer_cell(&screen,x,y,cells[y][x],false);
}
static void put(char ch) {
    cursor(false);
    if(ch=='\n') { newline(); return; }
    if(ch=='\r') { column=0; return; }
    if(ch<' ' || ch>'~') return;
    cells[row][column]=ch;
    framebuffer_cell(&screen,column,row,ch,false);
    if(++column==cols) newline();
}
void terminal_write(const char *text) {
    if(!active || !text) return;
    cursor(false);
    while(*text) put(*text++);
    cursor(true);
}
static bool equal(const char *a,const char *b) {
    while(*a && *a==*b) { ++a; ++b; }
    return *a==*b;
}
static void number(uint64_t value) {
    char digits[21]; unsigned int n=0;
    do { digits[n++]=(char)('0'+value%10); value/=10; } while(value);
    while(n) put(digits[--n]);
}
static void metric(const char *name,uint64_t value) {
    terminal_write(name); number(value); put('\n');
}
static void command(void) {
    line[length]=0;
    char *name=line;
    while(*name==' ') ++name;
    char *args=name;
    while(*args && *args!=' ') ++args;
    if(*args) { *args++=0; while(*args==' ') ++args; }
    arch_debug_puts("ON.: command "); arch_debug_puts(name); arch_debug_puts("\n");
    if(!*name) return;
    if(equal(name,"help")) terminal_write("help clear echo version ticks mem heap graphics ui desktop tasks\n");
    else if(equal(name,"desktop")) { ui_active=desktop_open(&screen);demo_active=ui_active; }
    else if(equal(name,"ui")) { ui_active=ui_demo_open(&screen);demo_active=ui_active; }
    else if(equal(name,"graphics")) demo_active=graphics_demo(&screen);
    else if(equal(name,"tasks")) {
        struct task_info info[TASK_MAX];size_t n=task_list(info,TASK_MAX);
        terminal_write("Preemptive kernel and user tasks\n");
        static const char *states[]={"free","ready","running","sleeping","zombie","faulted"};
        for(size_t i=0;i<n;++i) {
            number(info[i].id);put(' ');terminal_write(info[i].name);terminal_write(" [");terminal_write(states[info[i].state]);terminal_write(info[i].address_root?"; user]\n":"; kernel]\n");
            metric("  Dispatches: ",info[i].dispatches);
            metric("  CPU samples (~100 Hz): ",info[i].cpu_ticks);
            metric("  Idle samples: ",info[i].idle_ticks);
        }
        task_diagnostics();
    }
    else if(equal(name,"clear")) clear();
    else if(equal(name,"echo")) { terminal_write(args); put('\n'); }
    else if(equal(name,"version")) terminal_write("ON. " ON_KERNEL_VERSION " (x86-64)\n");
    else if(equal(name,"ticks")) metric("PIT ticks (~100 Hz): ",arch_system_ticks());
    else if(equal(name,"mem")) {
        struct pmm_statistics stats=pmm_get_statistics();
        metric("Free frames: ",stats.free_frames);
        metric("Allocated frames: ",stats.allocated_frames);
        metric("Frame bytes: ",PMM_PAGE_SIZE);
    } else if(equal(name,"heap")) {
        struct kheap_statistics stats=kheap_get_statistics();
        metric("Arena bytes: ",stats.arena_bytes);
        metric("Free bytes: ",stats.free_bytes);
        metric("Active allocations: ",stats.active_allocations);
    } else terminal_write("Unknown command. Type help.\n");
}
bool terminal_init(const struct kernel_framebuffer *fb) {
    active=false; demo_active=false;ui_active=false;
    if(!framebuffer_valid(fb)) return false;
    screen=*fb;
    cols=(unsigned int)(fb->width/12); if(cols>MAX_COLS) cols=MAX_COLS;
    rows=(unsigned int)(fb->height/16); if(rows>MAX_ROWS) rows=MAX_ROWS;
    active=true; length=0; blink_phase=0;
    clear();
    terminal_write("Welcome to ON.\nInteractive terminal - type help\n\non> ");
    arch_debug_puts("ON.: framebuffer terminal ready.\n");
    return true;
}
void terminal_handle_event(const struct keyboard_event *event) {
    if(!active || !event || !event->pressed) return;
    if(demo_active) {
        if(event->scancode==1) {
            bool was_ui=ui_active;demo_active=false;ui_active=false;
            framebuffer_fill(&screen,0,0,screen.width,screen.height,0x0f172a);
            for(unsigned int y=0;y<rows;++y) for(unsigned int x=0;x<cols;++x)
                framebuffer_cell(&screen,x,y,cells[y][x],false);
            terminal_write("on> ");
            arch_debug_puts(was_ui?"ON.: retained UI closed; terminal restored.\n":"ON.: graphics demo closed; terminal restored.\n");
        } else if(ui_active) ui_demo_keyboard(event);
        return;
    }
    if(!event->character) return;
    cursor(false);
    char ch=event->character;
    if(ch=='\n') {
        put('\n'); command(); length=0;
        if(!demo_active) terminal_write("on> ");
    } else if(ch=='\b') {
        if(length) {
            --length;
            if(column) --column;
            else if(row) { --row; column=cols-1; }
            cells[row][column]=' ';
            framebuffer_cell(&screen,column,row,' ',false);
        }
    } else if(ch>=' ' && ch<='~' && length<LINE_BYTES-1 && length<cols*(rows-2)-4) {
        line[length++]=ch; put(ch);
    }
    cursor(true);
}
void terminal_mouse(const struct mouse_event *event) {
    if(ui_active) ui_demo_mouse(event);
}
void terminal_tick(uint64_t ticks) {
    if(ui_active) { ui_demo_render();return; }
    if(!active) return;
    uint64_t phase=ticks/50;
    if(phase!=blink_phase) { blink_phase=phase; cursor((phase&1)==0); }
}
