#include <on/application.h>
#include <on/syscall.h>
static struct on_window_create spec={300,180,"Native sample"};
static struct on_draw drawing={12,16,0,0,0x72c9ff,"Hello from Ring 3"};
static struct on_event event;
static char title[24]="Input received";
void _start(void) {
    if(on_call(ON_SYS_ABI,0,0)!=ON_SYSCALL_ABI_VERSION) on_call(ON_SYS_EXIT,2,0);
    int64_t window=on_call(ON_SYS_WINDOW_CREATE,(uintptr_t)&spec,0);
    if(window<0) on_call(ON_SYS_EXIT,1,0);
    if(on_call(ON_SYS_WINDOW_DRAW,window,0)!=-ON_EFAULT ||
       on_call(ON_SYS_WINDOW_CLOSE,UINT32_MAX,0)!=-ON_EBADF) on_call(ON_SYS_EXIT,3,0);
    on_call(ON_SYS_WINDOW_DRAW,window,(uintptr_t)&drawing);
    for(;;) {
        if(on_call(ON_SYS_WINDOW_EVENT,window,(uintptr_t)&event)>0) {
            if(event.type==ON_EVENT_KEY && event.character=='c') __asm__ volatile("ud2");
            if(event.type==ON_EVENT_CLOSE || (event.type==ON_EVENT_KEY && event.character=='q')) break;
            on_call(ON_SYS_WINDOW_TITLE,window,(uintptr_t)title);

        }
        on_call(ON_SYS_YIELD,0,0);
    }
    on_call(ON_SYS_EXIT,0,0);
    for(;;) {}
}
