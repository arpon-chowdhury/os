#include "exceptions.h"
#include "syscall.h"
#include "process.h"
#include "arch.h"
static bool valid_return(const struct exception_frame *f) {
    struct vmm_translation code,stack;
    if(f->cs!=0x2b || f->ss!=0x33 || f->rip<4096 || f->rip>=0x800000000000ull ||
       f->rsp<4096 || f->rsp>=0x800000000000ull ||
       !process_translate(task_current(),f->rip,&code) || code.permissions!=(VMM_USER|VMM_EXEC)) return false;
    /* Both a mapped stack pointer and its one-past-end empty position are safe. */
    bool mapped=process_translate(task_current(),f->rsp,&stack) && stack.permissions==(VMM_USER|VMM_WRITE);
    return mapped || (process_translate(task_current(),f->rsp-1,&stack) && stack.permissions==(VMM_USER|VMM_WRITE));
}
void x86_syscall_interrupt(struct exception_frame *f) {
    if(!task_current_user() || (f->cs&3)!=3) arch_halt();
    if(!valid_return(f)) process_user_stop(-ON_EFAULT,f,0);
    struct syscall_request request={f->rax,{f->rdi,f->rsi,f->rdx,f->r10,f->r8,f->r9}};
    /* Private kernel stack, interrupts enabled for bounded service work. Memory
       ownership/copy operations save IF across their own atomic transactions. */
    arch_interrupt_restore(0x200);
    int64_t result=syscall_dispatch(&request,f);
    (void)arch_interrupt_save();
    if(!valid_return(f)) process_user_stop(-ON_EFAULT,f,0);
    f->rax=result;
    f->rflags=(f->rflags&0xcd5)|0x202; /* Arithmetic flags/DF, IF; no IOPL/NT/VM/TF. */
}
