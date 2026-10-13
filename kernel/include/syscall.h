#ifndef ON_SYSCALL_H
#define ON_SYSCALL_H
#include <stdint.h>
#define ON_SYSCALL_ABI_VERSION 1u
/* Reserved future x86-64 ABI: RAX number/result, RDI/RSI/RDX/R10/R8/R9 args;
   RCX/R11 clobbered; negative error results. Hardware entry remains disabled. */
enum on_syscall_number { ON_SYS_ABI=0, ON_SYS_YIELD=1, ON_SYS_EXIT=2, ON_SYS_WRITE=3 };
#define ON_ENOSYS 38
struct syscall_request { uint64_t number, arguments[6]; };
/* Fail-closed preparation hook: no user pointers dereferenced, no dispatch. */
int64_t syscall_unavailable(const struct syscall_request *request);
#endif
