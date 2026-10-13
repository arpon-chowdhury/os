#include "process.h"
#include "syscall.h"
#include "arch.h"
#include "elf.h"
bool process_user_layout_valid(const struct process_user_layout *p) {
    const uintptr_t limit=UINT64_C(0x0000800000000000);
    return p && p->entry>=4096 && p->entry<limit &&
        p->stack_bottom>=4096 && !(p->stack_bottom&4095) &&
        p->stack_top>p->stack_bottom && p->stack_top<limit && !(p->stack_top&4095) &&
        (p->entry<p->stack_bottom || p->entry>=p->stack_top);
}
struct allocation {uintptr_t base;uint64_t bytes;unsigned pages;};
struct process {
    struct address_space space;
    struct process_result result;
    uintptr_t entry;
    struct allocation allocations[PROCESS_ALLOCATIONS];
};
static struct process processes[TASK_MAX-1];
bool process_user_execution_enabled(void) {return arch_address_kernel_root()!=0;}
static void enter(void *argument) {
    struct process *p=argument;
    arch_user_enter(p->entry,PROCESS_STACK+PROCESS_STACK_PAGES*4096);
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
        p->entry=PROCESS_CODE;
        for(unsigned n=0;n<PROCESS_ALLOCATIONS;++n) p->allocations[n]=(struct allocation){0};
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
task_id process_load_elf(const char *name,const void *image,size_t length) {
    uint64_t flags=arch_interrupt_save();task_id id=0;struct elf_plan plan;
    if(!process_user_execution_enabled() || !elf64_validate(image,length,&plan)) goto done;
    for(unsigned i=0;i<TASK_MAX-1;++i) if(!processes[i].space.root) {
        struct process *p=&processes[i];
        if(!address_space_create(&p->space)) goto done;
        if(!elf64_populate(&p->space,image,length,&p->entry)) goto rollback;
        for(unsigned n=0;n<PROCESS_ALLOCATIONS;++n) p->allocations[n]=(struct allocation){0};
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
static struct process *current_process(void) {
    if(!task_current_user()) return 0;
    task_id id=task_current();
    for(unsigned i=0;i<TASK_MAX-1;++i) if(processes[i].result.id==id &&
       processes[i].space.root==arch_address_current_root()) return &processes[i];
    return 0;
}
bool process_copy_in(void *out,uintptr_t user,size_t length) {
    uint64_t flags=arch_interrupt_save();struct process *p=current_process();
    bool ok=p && address_space_copy_in(&p->space,out,user,length);
    arch_interrupt_restore(flags);return ok;
}
bool process_copy_out(uintptr_t user,const void *in,size_t length) {
    uint64_t flags=arch_interrupt_save();struct process *p=current_process();
    bool ok=p && address_space_copy_out(&p->space,user,in,length);
    arch_interrupt_restore(flags);return ok;
}
int64_t process_memory_allocate(uint64_t bytes) {
    uint64_t flags=arch_interrupt_save();int64_t result=-ON_ENOMEM;struct process *p=current_process();
    if(!p) {result=-ON_EPERM;goto done;}
    if(!bytes || bytes>ON_ALLOC_MAX) {result=-ON_EINVAL;goto done;}
    unsigned pages=(bytes+4095)/4096,record=PROCESS_ALLOCATIONS;
    for(unsigned i=0;i<PROCESS_ALLOCATIONS;++i) if(!p->allocations[i].base) {record=i;break;}
    if(record==PROCESS_ALLOCATIONS) goto done;
    for(unsigned start=0;start+pages<=PROCESS_HEAP_PAGES;++start) {
        uintptr_t base=PROCESS_HEAP_BASE+start*4096;bool free=true;
        for(unsigned i=0;i<pages;++i) {
            struct vmm_translation tr;
            if(address_space_translate(&p->space,base+i*4096,&tr)) {free=false;break;}
        }
        if(!free) continue;
        unsigned mapped=0;
        for(;mapped<pages;++mapped) if(!address_space_map_zero(&p->space,base+mapped*4096)) break;
        if(mapped!=pages) {
            while(mapped) {--mapped;if(!address_space_unmap(&p->space,base+mapped*4096)) arch_halt();}
            goto done;
        }
        p->allocations[record]=(struct allocation){base,bytes,pages};result=base;break;
    }
done:
    arch_interrupt_restore(flags);return result;
}
int64_t process_memory_free(uintptr_t base,uint64_t bytes) {
    uint64_t flags=arch_interrupt_save();int64_t result=-ON_EINVAL;struct process *p=current_process();
    if(!p) {result=-ON_EPERM;goto done;}
    for(unsigned i=0;i<PROCESS_ALLOCATIONS;++i) if(p->allocations[i].base==base && base && p->allocations[i].bytes==bytes) {
        for(unsigned page=0;page<p->allocations[i].pages;++page)
            if(!address_space_unmap(&p->space,base+page*4096)) arch_halt();
        p->allocations[i]=(struct allocation){0};result=0;break;
    }
done:
    arch_interrupt_restore(flags);return result;
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
