#include "process.h"
#include "syscall.h"
#include "arch.h"
bool process_user_layout_valid(const struct process_user_layout *p) {
    const uintptr_t limit=UINT64_C(0x0000800000000000);
    return p && p->entry>=4096 && p->entry<limit &&
        p->stack_bottom>=4096 && !(p->stack_bottom&4095) &&
        p->stack_top>p->stack_bottom && p->stack_top<limit && !(p->stack_top&4095) &&
        (p->entry<p->stack_bottom || p->entry>=p->stack_top);
}
struct process {
    struct address_space space;
    struct process_result result;
};
static struct process processes[TASK_MAX-1];
bool process_user_execution_enabled(void) {return arch_address_kernel_root()!=0;}
static void enter(void *argument) {
    (void)argument;
    arch_user_enter(PROCESS_CODE,PROCESS_STACK+PROCESS_STACK_PAGES*4096);
}
static void cleanup(void *argument) {
    struct process *p=argument;
    if(!address_space_destroy(&p->space)) arch_halt();
    /* Cancellation is distinct from a user exit or fault. */
    if(!p->result.finished) {p->result.finished=true;p->result.status=-1;}
    p->result.reclaimed=true;
}
task_id process_create(const char *name,const void *code,size_t length,uint64_t value) {
    uint64_t flags=arch_interrupt_save();task_id id=0;
    if(!code || !length || length>4096 || !process_user_execution_enabled()) goto done;
    for(unsigned i=0;i<TASK_MAX-1;++i) if(!processes[i].space.root) {
        struct process *p=&processes[i];
        if(!address_space_create(&p->space)) goto done;
        if(!address_space_map(&p->space,PROCESS_CODE,code,length,VMM_USER|VMM_EXEC) ||
           !address_space_map(&p->space,PROCESS_DATA,&value,sizeof(value),VMM_USER|VMM_WRITE)) goto rollback;
        for(unsigned page=0;page<PROCESS_STACK_PAGES;++page)
            if(!address_space_map(&p->space,PROCESS_STACK+page*4096,0,0,VMM_USER|VMM_WRITE)) goto rollback;
        id=task_create_isolated(name,enter,p,p->space.root,cleanup);
        if(!id) goto rollback;
        p->result=(struct process_result){.id=id,.root=p->space.root};break;
rollback:
        if(!address_space_destroy(&p->space)) arch_halt();
        break;
    }
done:
    arch_interrupt_restore(flags);return id;
}
bool process_result(task_id id,struct process_result *out) {
    uint64_t flags=arch_interrupt_save();bool ok=false;
    if(id && out) for(unsigned i=0;i<TASK_MAX-1;++i) if(processes[i].result.id==id) {*out=processes[i].result;ok=true;break;}
    arch_interrupt_restore(flags);return ok;
}
bool process_translate(task_id id,uintptr_t a,struct vmm_translation *out) {
    uint64_t flags=arch_interrupt_save();bool ok=false;
    if(id) for(unsigned i=0;i<TASK_MAX-1;++i) if(processes[i].result.id==id && processes[i].space.root) {
        ok=address_space_translate(&processes[i].space,a,out);break;
    }
    arch_interrupt_restore(flags);return ok;
}
int64_t syscall_unavailable(const struct syscall_request *request) {(void)request;return -ON_ENOSYS;}

__attribute__((noreturn)) void process_finish(const struct process_result *result) {
    (void)arch_interrupt_save();
    if(!result || !task_current_user() || result->id!=task_current()) arch_halt();
    for(unsigned i=0;i<TASK_MAX-1;++i) if(processes[i].result.id==result->id &&
       processes[i].space.root==arch_address_current_root()) {
        processes[i].result=*result;
        processes[i].result.root=processes[i].space.root;
        processes[i].result.finished=true;processes[i].result.reclaimed=false;
        task_exit();
    }
    arch_halt();
}
