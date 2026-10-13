#ifndef ON_APPLICATION_H
#define ON_APPLICATION_H
#define ON_KERNEL
#include <on/application.h>
#include "task.h"
struct application_window {
    task_id owner; uint32_t handle; bool closing, dirty;
    struct on_window_create spec;
    struct on_draw draws[ON_WINDOW_COMMANDS]; unsigned draw_count;
    struct on_event events[ON_WINDOW_EVENTS]; unsigned head,count;
};
int64_t application_request(uint64_t number,uint64_t handle,uintptr_t pointer);
void application_cleanup(task_id owner);
/* Foreground desktop adapter; caller holds interrupt exclusion. */
struct application_window *application_windows(void);
void application_event(struct application_window *w,struct on_event event);
#endif
