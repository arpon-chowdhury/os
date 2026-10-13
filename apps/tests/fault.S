#include "on/syscall.h"
.text
.global _start
_start:
    movq $4096,%rdi
    movq $ON_SYS_MEM_ALLOC,%rax
    int $ON_SYSCALL_VECTOR
#if APP_FAULT == 1
    movabsq $0xffffffff80000000,%rax
    movb (%rax),%al
#elif APP_FAULT == 2
    xorq %rsp,%rsp
    movq $ON_SYS_ABI,%rax
    int $ON_SYSCALL_VECTOR
#else
#error Invalid APP_FAULT
#endif
    ud2
.section .note.GNU-stack,"",@progbits
.data
.quad 1
