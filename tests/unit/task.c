/* Native policy/ownership model. Real x86 stack switching is verified in QEMU. */
#include "task.h"
#include "arch.h"
#include "process.h"
#include "syscall.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static bool enabled=true,fail_allocate;
static uint64_t ticks,switches,allocations,releases;
static void *stacks[TASK_MAX];
uint64_t arch_interrupt_save(void) {uint64_t f=enabled?0x200:0;enabled=false;return f;}
void arch_interrupt_restore(uint64_t f) {enabled=(f&0x200)!=0;}
uint64_t arch_system_ticks(void) {return ticks;}
void arch_debug_puts(const char *text) {(void)text;}
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
    assert(task_list(info,1)==1 && task_list(0,8)==0);
    for(unsigned i=0;i<TASK_MAX-1;++i) assert(task_terminate(ids[i]));
    task_reap();assert(task_list(info,TASK_MAX)==1 && allocations==releases && enabled);
    for(unsigned i=0;i<100;++i) {task_id id=task_create("reuse",entry,0);assert(id && id!=ids[0]);assert(task_terminate(id));task_reap();assert(!task_snapshot(id,info));}
    assert(allocations==releases && switches>=6 && !task_terminate(UINT32_MAX));
    enabled=false;task_yield();assert(!enabled);enabled=true;
    assert(!process_user_execution_enabled());
    struct process_user_layout layout={.entry=0x400000,.stack_bottom=0x800000,.stack_top=0x804000};
    assert(process_user_layout_valid(&layout));layout.entry=layout.stack_bottom;assert(!process_user_layout_valid(&layout));
    layout.entry=UINTPTR_MAX;assert(!process_user_layout_valid(&layout));assert(!process_user_layout_valid(0));
    assert(syscall_unavailable(0)==-ON_ENOSYS);
    struct syscall_request request={.number=ON_SYS_WRITE,.arguments={UINT64_MAX,UINT64_MAX}};
    assert(syscall_unavailable(&request)==-ON_ENOSYS);
    task_fault_report();assert(snapshot(boot).state==TASK_FAULTED);task_diagnostics();
    puts("Task policy, states, round-robin, sleep deadlines, termination/reap, generations, capacity, allocation failure, IF preservation, accounting and disabled process/syscall contracts passed.");
}
