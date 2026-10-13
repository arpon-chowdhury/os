#include "on/syscall.h"
.text
.global _start
_start:
    movq $8192,%rdi
    movq $ON_SYS_MEM_ALLOC,%rax
    int $ON_SYSCALL_VECTOR
    testq %rax,%rax
    js 3f
    movq %rax,%r12
1:
    movq $1000000,%rcx
2:
    incq (%r12)
    decq %rcx
    jnz 2b
    movq $ON_SYS_YIELD,%rax
    int $ON_SYSCALL_VECTOR
    jmp 1b
3:
    movq $111,%rdi
    movq $ON_SYS_EXIT,%rax
    int $ON_SYSCALL_VECTOR
    ud2
.section .note.GNU-stack,"",@progbits
.data
.quad 1
