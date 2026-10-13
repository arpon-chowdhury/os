#if ON_USER_TEST == 1
.section .rodata
.global user_busy_start,user_busy_end
user_busy_start:
    movq $0x600000,%rbx
    movq (%rbx),%r12
    movq $30000000,%rcx
1:
    incq 8(%rbx)
    decq %rcx
    jnz 1b
    cmpq $30000000,8(%rbx)
    jne 2f
    cmpq %r12,(%rbx)
    jne 2f
    movq %r12,%rax
    int $0x80
2:
    ud2
user_busy_end:
#endif
.section .note.GNU-stack,"",@progbits
