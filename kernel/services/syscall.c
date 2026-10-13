#include "syscall.h"
#include "process.h"
#include "arch.h"
#include "application.h"
int64_t syscall_dispatch(const struct syscall_request *r,const void *frame) {
    if(!r) return -ON_EINVAL;
    if(!task_current_user()) return -ON_EPERM;
    switch(r->number) {
    case ON_SYS_ABI: return ON_SYSCALL_ABI_VERSION;
    case ON_SYS_YIELD: task_yield();return 0;
    case ON_SYS_EXIT:
        if(!frame) return -ON_EINVAL;
        process_user_stop((int64_t)r->arguments[0],frame,0);
    case ON_SYS_WRITE: {
        uint64_t fd=r->arguments[0],length=r->arguments[2];
        if(fd!=1 && fd!=2) return -ON_EBADF;
        if(length>ON_WRITE_MAX) return -ON_E2BIG;
        char bytes[ON_WRITE_MAX];
        if(!process_copy_in(bytes,r->arguments[1],length)) return -ON_EFAULT;
        arch_debug_write(bytes,length);return length;
    }
    case ON_SYS_MEM_ALLOC: return process_memory_allocate(r->arguments[0]);
    case ON_SYS_MEM_FREE: return process_memory_free(r->arguments[0],r->arguments[1]);
    case ON_SYS_WINDOW_CREATE: case ON_SYS_WINDOW_TITLE: case ON_SYS_WINDOW_DRAW:
    case ON_SYS_WINDOW_EVENT: case ON_SYS_WINDOW_CLOSE:
        return application_request(r->number,r->arguments[0],r->arguments[1]);
    default: return -ON_ENOSYS;
    }
}
