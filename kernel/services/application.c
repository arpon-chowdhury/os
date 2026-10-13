#include "application.h"
#include "process.h"
#include "syscall.h"
#include "arch.h"
static struct application_window windows[ON_APP_WINDOWS];
static uint32_t generation;
struct application_window *application_windows(void) {return windows;}
void application_event(struct application_window *w,struct on_event e) {
    if(w && w->owner && e.type==ON_EVENT_CLOSE && w->count==ON_WINDOW_EVENTS) w->head=w->count=0;
    if(w && w->owner && w->count<ON_WINDOW_EVENTS) {
        w->events[(w->head+w->count)%ON_WINDOW_EVENTS]=e;++w->count;
    }
}
void application_cleanup(task_id owner) {
    uint64_t flags=arch_interrupt_save();
    for(unsigned i=0;i<ON_APP_WINDOWS;++i) if(windows[i].owner==owner) windows[i].closing=true;
    arch_interrupt_restore(flags);
}
int64_t application_request(uint64_t number,uint64_t handle,uintptr_t pointer) {
    uint64_t flags=arch_interrupt_save();int64_t result=-ON_EBADF;
    task_id owner=task_current();
    if(!task_current_user()) {result=-ON_EPERM;goto done;}
    if(number==ON_SYS_WINDOW_CREATE) {
        struct on_window_create spec;
        if(!process_copy_in(&spec,handle,sizeof(spec))) {result=-ON_EFAULT;goto done;}
        if(spec.width<120 || spec.width>640 || spec.height<90 || spec.height>480 || spec.title[23]) {result=-ON_EINVAL;goto done;}
        result=-ON_ENOMEM;
        for(unsigned i=0;i<ON_APP_WINDOWS;++i) if(!windows[i].owner) {
            if(generation==UINT32_MAX) break;
            windows[i]=(struct application_window){.owner=owner,.handle=++generation,.dirty=true,.spec=spec};
            result=windows[i].handle;break;
        }
        goto done;
    }
    for(unsigned i=0;i<ON_APP_WINDOWS;++i) {
        struct application_window *w=&windows[i];
        if(w->owner!=owner || w->handle!=handle || w->closing) continue;
        result=0;
        if(number==ON_SYS_WINDOW_CLOSE) w->closing=true;
        else if(number==ON_SYS_WINDOW_TITLE) {
            char title[24];
            if(!process_copy_in(title,pointer,sizeof(title))) result=-ON_EFAULT;
            else if(title[23]) result=-ON_EINVAL;
            else {for(unsigned j=0;j<24;++j) w->spec.title[j]=title[j];w->dirty=true;}
        } else if(number==ON_SYS_WINDOW_DRAW) {
            struct on_draw d;
            if(!process_copy_in(&d,pointer,sizeof(d))) result=-ON_EFAULT;
            else if(d.x<0 || d.y<0 || d.x>640 || d.y>480 || d.w<0 || d.h<0 || d.w>640 || d.h>480 || d.text[31] || d.color>0xffffff) result=-ON_EINVAL;
            else if(w->draw_count==ON_WINDOW_COMMANDS) result=-ON_E2BIG;
            else {w->draws[w->draw_count++]=d;w->dirty=true;}
        } else if(number==ON_SYS_WINDOW_EVENT) {
            struct on_event e=w->count?w->events[w->head]:(struct on_event){0};
            if(!process_copy_out(pointer,&e,sizeof(e))) result=-ON_EFAULT;
            else if(w->count) {arch_debug_puts(e.type==ON_EVENT_MOUSE?"ON.: native mouse consumed.\n":"ON.: native key consumed.\n");w->head=(w->head+1)%ON_WINDOW_EVENTS;--w->count;result=1;}
        } else result=-ON_ENOSYS;
        break;
    }
done:
    arch_interrupt_restore(flags);return result;
}
