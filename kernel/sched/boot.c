#include "task.h"
#include "arch.h"
#include "virtual.h"
#include "process.h"
#include "syscall.h"
static volatile uint64_t maintenance_wakes;
static void maintenance(void *argument) {
    (void)argument;
    for(;;) {++maintenance_wakes;(void)task_sleep(100);}
}
#if ON_TASK_TEST
static void require(bool ok) {
    if(!ok) {arch_debug_puts("ON.: TASK TEST FAILED; halted safely.\n");arch_halt();}
}
#if ON_TASK_TEST == 1 || ON_TASK_TEST == 3
static void empty(void *argument) {(void)argument;}
static bool count_is(size_t n) {struct task_info info[TASK_MAX];return task_list(info,TASK_MAX)==n;}
static bool memory_equal(struct pmm_statistics a,struct pmm_statistics b) {return a.free_frames==b.free_frames && a.allocated_frames==b.allocated_frames;}
#endif
#endif
#if ON_TASK_TEST == 1
static volatile unsigned completed[3];
static volatile uintptr_t locals[3];
extern bool x86_task_register_test(void);
static void worker(void *argument) {
    unsigned id=(uintptr_t)argument;
    volatile uint64_t local[4]={0x11110000+id,0x22220000+id,0x33330000+id,0x44440000+id};
    locals[id]=(uintptr_t)local;
    require(x86_task_register_test());
    uint64_t flags=arch_interrupt_save();task_yield();
    uint64_t after=arch_interrupt_save();require(!(after&0x200));arch_interrupt_restore(flags);
    require(task_sleep(3));
    require(local[0]==0x11110000+id && local[3]==0x44440000+id);
    ++completed[id];
}
static void tests(void) {
    struct pmm_statistics before=pmm_get_statistics();
    task_id ids[3];struct task_info info;
    for(unsigned i=0;i<3;++i) {
        ids[i]=task_create(i==0?"worker-a":i==1?"worker-b":"worker-c",worker,(void *)(uintptr_t)i);require(ids[i]!=0);
        require(task_snapshot(ids[i],&info));struct vmm_translation t;
        for(unsigned page=0;page<TASK_STACK_PAGES;++page) require(vmm_translate(info.stack_base+page*4096,&t) && t.permissions==VMM_WRITE);
        require(!vmm_translate(info.stack_base-4096,&t) && !vmm_translate(info.stack_base+TASK_STACK_BYTES,&t));
    }
    require(vmm_translate((uintptr_t)tests,&(struct vmm_translation){0}));
    for(unsigned iteration=0;iteration<2000 && !(completed[0]&&completed[1]&&completed[2]);++iteration) {
        task_yield();
        __asm__ volatile ("sti; hlt" : : : "memory");
    }
    require(completed[0]==1 && completed[1]==1 && completed[2]==1);
    require(locals[0]!=locals[1] && locals[1]!=locals[2] && locals[0]!=locals[2]);
    task_reap();require(count_is(1));require(memory_equal(before,pmm_get_statistics()));
    arch_debug_puts("ON.: TASK TEST real contexts, callee registers, stack locals, sleep, return/exit and reclamation passed.\n");
    task_id slots[TASK_MAX-1];
    for(unsigned i=0;i<TASK_MAX-1;++i) {slots[i]=task_create("capacity",empty,0);require(slots[i]!=0);}
    require(!task_create("overflow",empty,0));require(!task_terminate(task_current()));
    for(unsigned i=0;i<TASK_MAX-1;++i) require(task_terminate(slots[i]));
    task_reap();require(memory_equal(before,pmm_get_statistics()));
    task_id fresh=task_create("generation",empty,0);require(fresh && fresh!=slots[0]);
    require(!task_terminate(slots[0]) && !task_snapshot(slots[0],&info));
    require(task_terminate(fresh));task_reap();require(count_is(1));require(memory_equal(before,pmm_get_statistics()));
    require(!process_user_execution_enabled() && syscall_unavailable(0)==-ON_ENOSYS && arch_task_privilege_boundary());
    struct vmm_translation kernel;
    require(vmm_translate((uintptr_t)tests,&kernel) && !(kernel.permissions&VMM_USER));
    arch_debug_puts("ON.: TASK TEST capacity, stale IDs, termination, supervisor/NX guards and disabled user entry passed.\n");
}
#elif ON_TASK_TEST == 2
static void guard_fault(void *argument) {
    (void)argument;struct task_info info;require(task_snapshot(task_current(),&info));
    arch_debug_puts("ON.: TASK TEST controlled stack guard write begins.\n");
    *(volatile uint8_t *)(info.stack_base-4096)=1;
    require(false);
}
#elif ON_TASK_TEST == 3
static void tests(void) {
    struct pmm_statistics before=pmm_get_statistics();
    require(!task_create("rollback",empty,0));require(count_is(1));
    require(memory_equal(before,pmm_get_statistics()));
    task_id id=task_create("after-rollback",empty,0);require(id!=0);
    require(task_terminate(id));task_reap();require(memory_equal(before,pmm_get_statistics()));
    arch_debug_puts("ON.: TASK TEST partial stack allocation rollback and retry passed.\n");
}
#endif
void task_boot_start(void) {
    if(!task_init()) return;
    arch_debug_puts("ON.: cooperative scheduler ready; foreground task; ring-3 and syscalls disabled.\n");
#if ON_TASK_TEST
    arch_interrupt_restore(0x200);
#if ON_TASK_TEST == 1 || ON_TASK_TEST == 3
    tests();
#elif ON_TASK_TEST == 2
    require(task_create("guard-fault",guard_fault,0)!=0);task_yield();require(false);
#endif
#endif
    if(task_create("scheduler-monitor",maintenance,0)) arch_debug_puts("ON.: independent scheduler-monitor kernel task ready.\n");
    else arch_debug_puts("ON.: task stacks unavailable; foreground services retained.\n");
}
