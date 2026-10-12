#ifndef ON_X86_64_EXCEPTIONS_H
#define ON_X86_64_EXCEPTIONS_H
#include <stdint.h>

/* Shared exception/IRQ entry layout, including the long-mode hardware frame. */
struct exception_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;
    uint64_t vector, error, rip, cs, rflags, rsp, ss;
};
__attribute__((noreturn)) void x86_exception_fatal(const struct exception_frame *frame, uint64_t cr2);
#endif
