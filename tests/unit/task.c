/* Native policy/ownership model. Real x86 stack switching is verified in QEMU. */
#include "task.h"
#include "arch.h"
#include "process.h"
#include "syscall.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "../elf_fixture.h"
static bool enabled=true,fail_allocate;
static uint64_t ticks,switches,allocations,releases;
static void *stacks[TASK_MAX];
uint64_t arch_interrupt_save(void) {uint64_t f=enabled?0x200:0;enabled=false;return f;}
void arch_interrupt_restore(uint64_t f) {enabled=(f&0x200)!=0;}
uint64_t arch_system_ticks(void) {return ticks;}
void arch_debug_puts(const char *text) {(void)text;}
static char output[ON_WRITE_MAX];static size_t output_size;
void arch_debug_write(const char *text,size_t length) {assert(length<=sizeof(output));memcpy(output,text,length);output_size=length;}
__attribute__((noreturn)) void process_user_stop(int64_t status,const void *frame,uint64_t cr2) {(void)status;(void)frame;(void)cr2;abort();}
__attribute__((noreturn)) void arch_halt(void) {abort();}
bool arch_task_stack_allocate(unsigned slot,uintptr_t *base) {
    assert(!enabled && slot>0 && slot<TASK_MAX && !stacks[slot]);
    if(fail_allocate) {fail_allocate=false;return false;}
    stacks[slot]=malloc(TASK_STACK_BYTES);assert(stacks[slot]);
    memset(stacks[slot],0,TASK_STACK_BYTES);*base=(uintptr_t)stacks[slot];++allocations;return true;
}
void arch_task_stack_release(unsigned slot) {assert(!enabled && slot!=(task_current()&255) && stacks[slot]);free(stacks[slot]);stacks[slot]=0;++releases;}
uintptr_t arch_task_context(uintptr_t base,void (*start)(void)) {assert(start && !enabled);return base+TASK_STACK_BYTES-64;}
void arch_task_switch(uintptr_t *old_stack,uintptr_t new_stack) {assert(!enabled && old_stack && new_stack);*old_stack=1;++switches;}
void arch_task_activate(uint64_t root,uintptr_t top) {assert(!enabled);(void)top;arch_address_activate(root);}
__attribute__((noreturn)) void arch_user_enter(uintptr_t entry,uintptr_t stack) {(void)entry;(void)stack;abort();}
static const unsigned char user_code[]={0x31,0xc0,0xcd,0x80};
static void entry(void *arg) {(void)arg;abort();}
static struct task_info snapshot(task_id id) {struct task_info info;assert(task_snapshot(id,&info));return info;}
int main(void) {
    struct task_info info[TASK_MAX];
    assert(!task_create("before-init",entry,0));assert(!task_snapshot(256,info));task_yield();task_reap();assert(!task_current());
    assert(task_init() && !task_init());task_id boot=task_current();assert(boot==256 && enabled);
    assert(!task_create("null",0,0));assert(!task_sleep(1));assert(!task_terminate(boot));
    fail_allocate=true;assert(!task_create("fail",entry,0));assert(task_list(info,TASK_MAX)==1);
    task_id a=task_create("a",entry,0),b=task_create("b",entry,0);assert(a && b && a!=b);
    assert(snapshot(a).state==TASK_READY && snapshot(a).stack_base!=snapshot(b).stack_base);
    task_yield();assert(task_current()==a && snapshot(a).state==TASK_RUNNING && snapshot(boot).state==TASK_READY && enabled);
    task_tick();assert(snapshot(a).cpu_ticks==1);
    assert(!task_terminate(a));assert(task_sleep(5));assert(task_current()==b && snapshot(a).state==TASK_SLEEPING);
    assert(task_terminate(a));task_yield();assert(task_current()==boot && !task_snapshot(a,info));
    assert(task_terminate(b));task_reap();assert(task_list(info,TASK_MAX)==1 && releases==allocations);
    task_id fresh=task_create("fresh",entry,0);assert(fresh!=a && !task_terminate(a));
    task_yield();assert(task_current()==fresh);assert(task_sleep(2));assert(task_current()==boot);
    ticks=1;task_yield();assert(task_current()==boot && snapshot(fresh).state==TASK_SLEEPING);
    ticks=2;task_yield();assert(task_current()==fresh && snapshot(fresh).dispatches==2);
    ticks=UINT64_MAX;assert(!task_sleep(1));assert(snapshot(fresh).state==TASK_RUNNING);
    assert(task_sleep(0));assert(task_current()==boot);assert(task_terminate(fresh));task_reap();ticks=0;
    task_idle_mark(true);task_tick();task_idle_mark(false);task_tick();
    assert(snapshot(boot).idle_ticks==1 && snapshot(boot).cpu_ticks==1);
    task_id ids[TASK_MAX-1];
    for(unsigned i=0;i<TASK_MAX-1;++i) {ids[i]=task_create("long bounded task name beyond twenty four characters",entry,0);assert(ids[i]);assert(strlen(snapshot(ids[i]).name)==23);}
    assert(!task_create("full",entry,0));assert(task_list(info,TASK_MAX)==TASK_MAX);
    assert(!process_create("task-capacity",user_code,sizeof(user_code),0));
    assert(task_list(info,1)==1 && task_list(0,8)==0);
    for(unsigned i=0;i<TASK_MAX-1;++i) assert(task_terminate(ids[i]));
    task_reap();assert(task_list(info,TASK_MAX)==1 && allocations==releases && enabled);
    for(unsigned i=0;i<100;++i) {task_id id=task_create("reuse",entry,0);assert(id && id!=ids[0]);assert(task_terminate(id));task_reap();assert(!task_snapshot(id,info));}
    assert(allocations==releases && switches>=6 && !task_terminate(UINT32_MAX));
    enabled=false;task_yield();assert(!enabled);enabled=true;
    assert(process_user_execution_enabled());
    struct process_user_layout layout={.entry=0x400000,.stack_bottom=0x800000,.stack_top=0x804000};
    assert(process_user_layout_valid(&layout));layout.entry=layout.stack_bottom;assert(!process_user_layout_valid(&layout));
    layout.entry=UINTPTR_MAX;assert(!process_user_layout_valid(&layout));assert(!process_user_layout_valid(0));
    assert(syscall_unavailable(0)==-ON_ENOSYS);
    struct syscall_request request={.number=ON_SYS_WRITE,.arguments={UINT64_MAX,UINT64_MAX}};
    assert(syscall_unavailable(&request)==-ON_ENOSYS);
    task_id preempted=task_create("preempted",entry,0);
    uint64_t irq_flags=arch_interrupt_save(), released_before=releases;
    for(unsigned i=0;i<TASK_QUANTUM_TICKS-1;++i) {task_preempt();assert(task_current()==boot);}
    task_preempt();assert(task_current()==preempted && !enabled);
    arch_interrupt_restore(irq_flags);
    assert(task_sleep(2) && task_current()==boot);
    ticks=2;irq_flags=arch_interrupt_save();
    for(unsigned i=0;i<TASK_QUANTUM_TICKS;++i) task_preempt();
    assert(task_current()==preempted && releases==released_before);
    arch_interrupt_restore(irq_flags);task_yield();assert(task_current()==boot);
    assert(task_terminate(preempted));irq_flags=arch_interrupt_save();
    for(unsigned i=0;i<TASK_QUANTUM_TICKS;++i) task_preempt();
    assert(releases==released_before);arch_interrupt_restore(irq_flags);
    task_reap();assert(releases==released_before+1 && enabled);
    extern unsigned model_live;
    fail_allocate=true;
    assert(!process_create("stack-fail",user_code,sizeof(user_code),0) && !model_live);
    uint8_t image[FIXTURE_SIZE];elf_fixture(image);
    extern int model_fail_after;
    for(int budget=0;budget<16;++budget) {
        model_fail_after=budget;
        task_id loaded=process_load_elf("elf-rollback",image,sizeof(image));
        model_fail_after=-1;
        if(loaded) {assert(task_terminate(loaded));task_reap();}
        assert(!model_live && task_list(info,TASK_MAX)==1 && enabled);
    }
    fail_allocate=true;
    assert(!process_load_elf("elf-stack-fail",image,sizeof(image)) && !model_live);
    task_id loaded=process_load_elf("elf-retry",image,sizeof(image));assert(loaded);
    assert(task_terminate(loaded));task_reap();assert(!model_live);
    task_id user=process_create("user",user_code,sizeof(user_code),42);assert(user);
    assert(snapshot(user).address_root);
    struct vmm_translation tr;assert(process_translate(user,PROCESS_DATA,&tr));
    assert(*(uint64_t *)pmm_page_pointer(tr.physical)==42);
    task_yield();assert(task_current()==user && task_current_user());
    struct syscall_request call={.number=ON_SYS_ABI};assert(syscall_dispatch(&call,0)==1);
    call.number=UINT64_MAX;assert(syscall_dispatch(&call,0)==-ON_ENOSYS);
    call=(struct syscall_request){ON_SYS_WRITE,{1,PROCESS_DATA,8}};
    assert(syscall_dispatch(&call,0)==8 && output_size==8 && output[0]==42);
    call.arguments[1]=PROCESS_DATA+4092;assert(syscall_dispatch(&call,0)==-ON_EFAULT && output_size==8);
    call.arguments[1]=UINT64_MAX;assert(syscall_dispatch(&call,0)==-ON_EFAULT);
    call.arguments[2]=0;assert(syscall_dispatch(&call,0)==0 && !output_size);
    call.arguments[2]=ON_WRITE_MAX+1;assert(syscall_dispatch(&call,0)==-ON_E2BIG);
    call.arguments[0]=0;assert(syscall_dispatch(&call,0)==-ON_EBADF);
    const uint64_t update=99;assert(process_copy_out(PROCESS_DATA,&update,8));
    assert(!process_copy_out(PROCESS_CODE,&update,8));
    extern int model_fail_after;unsigned frames_before=model_live;
    model_fail_after=2;assert(process_memory_allocate(8192)==-ON_ENOMEM && model_live==frames_before);model_fail_after=-1;
    assert(process_memory_allocate(0)==-ON_EINVAL && process_memory_allocate(UINT64_MAX)==-ON_EINVAL);
    int64_t block=process_memory_allocate(8193);assert(block==PROCESS_HEAP_BASE);
    uint64_t zero=1;assert(process_copy_in(&zero,block,8) && zero==0);
    assert(process_memory_free(block,8192)==-ON_EINVAL);
    assert(process_memory_free(PROCESS_CODE,4096)==-ON_EINVAL);
    assert(process_memory_free(block,8193)==0 && process_memory_free(block,8193)==-ON_EINVAL && model_live==frames_before);
    assert(!process_copy_in(&zero,block,8));
    int64_t blocks[PROCESS_ALLOCATIONS];
    for(unsigned i=0;i<PROCESS_ALLOCATIONS;++i) {blocks[i]=process_memory_allocate(4096);assert(blocks[i]>0);}
    assert(process_memory_allocate(4096)==-ON_ENOMEM);
    for(unsigned i=0;i<PROCESS_ALLOCATIONS;++i) assert(process_memory_free(blocks[i],4096)==0);
    assert(process_memory_allocate(4096)>0); /* Cancellation also frees outstanding memory. */
    call.number=ON_SYS_YIELD;assert(syscall_dispatch(&call,0)==0 && task_current()==boot);
    assert(task_terminate(user));task_reap();
    struct process_result result;assert(process_result(user,&result) && result.finished && result.reclaimed && result.status==-1 && !model_live);
    assert(!process_translate(user,PROCESS_DATA,&tr));
    task_id reused_user=process_create("user-reuse",user_code,sizeof(user_code),43);assert(reused_user && reused_user!=user);
    assert(!process_result(user,&result));assert(task_terminate(reused_user));task_reap();assert(!model_live);
    assert(!process_create("bad",0,4,0) && !process_create("big",user_code,4097,0));
    task_fault_report();assert(snapshot(boot).state==TASK_FAULTED);task_diagnostics();
    puts("Task policy, states, round-robin, sleep deadlines, termination/reap, generations, capacity, allocation failure, IF preservation, accounting, process cancellation/rollback ELF transaction rollback and bounded syscall contracts passed.");
}
