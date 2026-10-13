#ifndef ON_TASK_H
#define ON_TASK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define TASK_MAX 8u
#define TASK_QUANTUM_TICKS 5u
#define TASK_STACK_PAGES 4u
#define TASK_STACK_BYTES (TASK_STACK_PAGES * 4096u)
typedef uint32_t task_id;
typedef void (*task_entry)(void *argument);
enum kernel_task_state { TASK_FREE, TASK_READY, TASK_RUNNING, TASK_SLEEPING, TASK_ZOMBIE, TASK_FAULTED };
struct task_info {
    task_id id;
    enum kernel_task_state state;
    char name[24];
    uintptr_t stack_base;
    uint64_t wake_tick, dispatches, cpu_ticks, idle_ticks;
    uint64_t address_root;
};
/* Single bootstrap CPU, timer preemption; never call scheduling/lifecycle APIs
   from IRQs or while holding allocator/device locks.
   IDs include generations; stale IDs cannot target a reused slot. */
bool task_init(void);
/* Entry/argument are trusted and borrowed; callers own argument/resource
   lifetime through termination. Only task stacks are automatically reclaimed. */
task_id task_create(const char *name, task_entry entry, void *argument);
/* Trusted process service only: root and argument live until cleanup. */
task_id task_create_isolated(const char *name, task_entry entry, void *argument,
                             uint64_t root, void (*cleanup)(void *));
bool task_current_user(void);
task_id task_current(void);
bool task_snapshot(task_id id, struct task_info *info);
size_t task_list(struct task_info *info, size_t capacity);
bool task_terminate(task_id id); /* Other tasks only; deferred reclamation. */
void task_reap(void);
void task_yield(void);
bool task_sleep(uint64_t ticks);
__attribute__((noreturn)) void task_exit(void);
/* Bounded IRQ0 accounting only; never switches contexts. */
void task_tick(void);
/* IRQ0 tail only, interrupts disabled, after PIC EOI; no reclamation. */
void task_preempt(void);
void task_idle_mark(bool idle);
void task_fault_report(void);
void task_boot_start(void);
void task_diagnostics(void);
/* Architecture-owned supervisor/NX stacks and C ABI callee-saved context. */
bool arch_task_stack_allocate(unsigned slot, uintptr_t *base);
void arch_task_stack_release(unsigned slot);
bool arch_task_privilege_boundary(void);
uintptr_t arch_task_context(uintptr_t base, void (*start)(void));
void arch_task_switch(uintptr_t *old_stack, uintptr_t new_stack);
void arch_task_activate(uint64_t root, uintptr_t kernel_stack_top);
#endif
