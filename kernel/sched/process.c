#include "process.h"
#include "syscall.h"
bool process_user_layout_valid(const struct process_user_layout *p) {
    const uintptr_t limit=UINT64_C(0x0000800000000000);
    return p && p->entry>=4096 && p->entry<limit &&
        p->stack_bottom>=4096 && !(p->stack_bottom&4095) &&
        p->stack_top>p->stack_bottom && p->stack_top<limit && !(p->stack_top&4095) &&
        (p->entry<p->stack_bottom || p->entry>=p->stack_top);
}
bool process_user_execution_enabled(void) {return false;}
int64_t syscall_unavailable(const struct syscall_request *request) {(void)request;return -ON_ENOSYS;}
