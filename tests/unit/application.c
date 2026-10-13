#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "application.h"
#include "syscall.h"
static task_id owner=1;
uint64_t arch_interrupt_save(void) {return 0;}
void arch_interrupt_restore(uint64_t f) {(void)f;}
void arch_debug_puts(const char *s) {(void)s;}
task_id task_current(void) {return owner;}
bool task_current_user(void) {return owner!=0;}
bool process_copy_in(void *out,uintptr_t p,size_t n) {if(p<4096) return false;memcpy(out,(void *)p,n);return true;}
bool process_copy_out(uintptr_t p,const void *in,size_t n) {if(p<4096) return false;memcpy((void *)p,in,n);return true;}
int main(void) {
    struct on_window_create s={300,180,"Test"};
    assert(application_request(ON_SYS_WINDOW_CREATE,0,0)==-ON_EFAULT);
    s.width=UINT32_MAX;assert(application_request(ON_SYS_WINDOW_CREATE,(uintptr_t)&s,0)==-ON_EINVAL);s.width=300;
    int64_t h=application_request(ON_SYS_WINDOW_CREATE,(uintptr_t)&s,0);assert(h>0);
    owner=2;assert(application_request(ON_SYS_WINDOW_CLOSE,h,0)==-ON_EBADF);owner=1;
    struct on_draw d={.color=0xffffff};
    d.x=-1;assert(application_request(ON_SYS_WINDOW_DRAW,h,(uintptr_t)&d)==-ON_EINVAL);d.x=0;
    for(unsigned i=0;i<ON_WINDOW_COMMANDS;++i) assert(!application_request(ON_SYS_WINDOW_DRAW,h,(uintptr_t)&d));
    assert(application_request(ON_SYS_WINDOW_DRAW,h,(uintptr_t)&d)==-ON_E2BIG);
    struct application_window *w=application_windows();
    for(unsigned i=0;i<ON_WINDOW_EVENTS+4;++i) application_event(w,(struct on_event){.type=ON_EVENT_KEY,.code=i});
    assert(w->count==ON_WINDOW_EVENTS);
    assert(application_request(ON_SYS_WINDOW_EVENT,h,0)==-ON_EFAULT);assert(w->count==ON_WINDOW_EVENTS);
    struct on_event e;
    for(unsigned i=0;i<ON_WINDOW_EVENTS;++i) {assert(application_request(ON_SYS_WINDOW_EVENT,h,(uintptr_t)&e)==1);assert(e.code==i);}
    assert(application_request(ON_SYS_WINDOW_EVENT,h,(uintptr_t)&e)==0);
    for(unsigned i=0;i<ON_WINDOW_EVENTS;++i) application_event(w,(struct on_event){.type=ON_EVENT_MOUSE});
    application_event(w,(struct on_event){.type=ON_EVENT_CLOSE});
    assert(w->count==1);assert(application_request(ON_SYS_WINDOW_EVENT,h,(uintptr_t)&e)==1);assert(e.type==ON_EVENT_CLOSE);
    application_cleanup(owner);assert(w->closing);assert(application_request(ON_SYS_WINDOW_EVENT,h,(uintptr_t)&e)==-ON_EBADF);
    puts("application ownership, bounds, queue overflow, invalid pointers and cleanup passed");
}
