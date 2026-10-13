#ifndef ON_PROCESS_H
#define ON_PROCESS_H
#include <stdbool.h>
#include <stdint.h>
#include "task.h"
#include "address_space.h"
#define PROCESS_CODE 0x400000ull
#define PROCESS_DATA 0x600000ull
#define PROCESS_STACK 0x800000ull
#define PROCESS_STACK_PAGES 2u
/* One trusted, embedded page of RX machine code, one RW/NX data page and
   guarded user stack. This is not an executable loader or application ABI. */
struct process_result {
    task_id id;
    bool finished, reclaimed;
    int64_t status;
    uint64_t vector, error, rip, rsp, cs, ss, rflags, cr2, kernel_frame, root;
};
#define PROCESS_HEAP_BASE 0x10000000ull
#define PROCESS_HEAP_PAGES 64u
#define PROCESS_ALLOCATIONS 4u
/* Kernel-owned ELF byte span; returns 0 with no publication on failure. */
task_id process_load_elf(const char *name,const void *image,size_t length);
bool process_copy_in(void *out,uintptr_t user,size_t length);
bool process_copy_out(uintptr_t user,const void *in,size_t length);
int64_t process_memory_allocate(uint64_t bytes);
int64_t process_memory_free(uintptr_t base,uint64_t bytes);
task_id process_create(const char *name,const void *code,size_t length,uint64_t value);
bool process_result(task_id id,struct process_result *result);
bool process_translate(task_id id,uintptr_t address,struct vmm_translation *result);
void process_boot_test(void);
void application_boot_test(void);
__attribute__((noreturn)) void process_finish(const struct process_result *result);
__attribute__((noreturn)) void process_user_stop(int64_t status,const void *frame,uint64_t cr2);
void arch_user_kernel_stack(uintptr_t top);
uintptr_t x86_user_exception_stack(void);
__attribute__((noreturn)) void arch_user_enter(uintptr_t entry,uintptr_t stack);

struct process_user_layout { uintptr_t entry, stack_bottom, stack_top; };
bool process_user_layout_valid(const struct process_user_layout *layout);
bool process_user_execution_enabled(void);
#endif
