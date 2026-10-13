#ifndef ON_PUBLIC_SYSCALL_H
#define ON_PUBLIC_SYSCALL_H
/* ABI v1: INT 0x81, RAX number/result, RDI/RSI/RDX/R10/R8/R9 arguments.
   All other general registers preserved; arithmetic flags/DF preserved, IF=1.
   Errors are negative. Legacy INT 0x80 remains exit-only, outside this ABI. */
#define ON_SYSCALL_ABI_VERSION 1
#define ON_SYSCALL_VECTOR 0x81
#define ON_SYS_ABI 0
#define ON_SYS_YIELD 1
#define ON_SYS_EXIT 2
#define ON_SYS_WRITE 3
#define ON_SYS_MEM_ALLOC 4
#define ON_SYS_MEM_FREE 5
#define ON_EPERM 1
#define ON_E2BIG 7
#define ON_ENOEXEC 8
#define ON_EBADF 9
#define ON_ENOMEM 12
#define ON_EFAULT 14
#define ON_EINVAL 22
#define ON_ENOSYS 38
#define ON_WRITE_MAX 256
#define ON_ALLOC_MAX 32768
#endif
