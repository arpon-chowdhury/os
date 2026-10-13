#include "process.h"
#include "exceptions.h"
#include "arch.h"
__attribute__((noreturn)) void process_user_stop(int64_t status,const void *saved,uint64_t cr2) {
    const struct exception_frame *f=saved;
    if((f->cs&3)!=3 || !task_current_user()) arch_halt();
    struct process_result result={.id=task_current(),.status=status,
        .vector=f->vector,.error=f->error,.rip=f->rip,.rsp=f->rsp,
        .cs=f->cs,.ss=f->ss,.rflags=f->rflags,.cr2=cr2,.kernel_frame=(uintptr_t)f};
    process_finish(&result);
}

__attribute__((noreturn)) void arch_user_enter(uintptr_t entry,uintptr_t stack) {
    (void)arch_interrupt_save();
    struct vmm_translation code,data;
    struct task_info task;
    if(!task_current_user() || !task_snapshot(task_current(),&task) ||
       task.address_root!=arch_address_current_root() || (stack&15) ||
       entry<4096 || stack<4096 || stack>=0x800000000000ull ||
       !process_translate(task_current(),entry,&code) || code.permissions!=(VMM_USER|VMM_EXEC) ||
       !process_translate(task_current(),stack-1,&data) || data.permissions!=(VMM_USER|VMM_WRITE) ||
       x86_user_exception_stack()!=task.stack_base+TASK_STACK_BYTES) arch_halt();
    extern void x86_user_iret(uintptr_t,uintptr_t) __attribute__((noreturn));
    x86_user_iret(entry,stack);
}
