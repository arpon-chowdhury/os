#include <stddef.h>
#include <stdint.h>
#include "arch.h"
#include "virtual.h"
#include "exceptions.h"
#include "interrupts.h"
#include "task.h"

struct table_pointer { uint16_t limit; uint64_t base; } __attribute__((packed));
struct idt_gate {
    uint16_t low, selector;
    uint8_t ist, attributes;
    uint16_t middle;
    uint32_t high, reserved;
} __attribute__((packed));
struct task_state {
    uint32_t reserved0;
    uint64_t rsp[3], reserved1, ist[7], reserved2;
    uint16_t reserved3, iomap;
} __attribute__((packed));
_Static_assert(sizeof(struct table_pointer) == 10, "descriptor pointer ABI");
_Static_assert(sizeof(struct idt_gate) == 16, "IDT gate ABI");
_Static_assert(sizeof(struct task_state) == 104, "TSS ABI");
_Static_assert(offsetof(struct exception_frame, vector) == 120, "assembly frame ABI");
_Static_assert(offsetof(struct exception_frame, rip) == 136, "CPU frame ABI");
_Static_assert(sizeof(struct exception_frame) == 176, "IST frame ABI");

static uint64_t gdt[5] = {0, 0x00af9a000000ffff, 0x00cf92000000ffff, 0, 0};
static struct task_state tss;
static struct idt_gate idt[256] __attribute__((aligned(16)));
/* Separate mapped stacks for double fault, NMI, machine check, and other faults. */
uint8_t x86_exception_stacks[4][16384] __attribute__((aligned(16)));
static volatile unsigned int fatal_active;
extern void x86_load_gdt(const struct table_pointer *pointer);
extern const uintptr_t x86_exception_stubs[32];

void x86_idt_set_gate(unsigned int vector, uintptr_t handler, uint8_t ist) {
    idt[vector].low = handler;
    idt[vector].selector = 0x08;
    idt[vector].ist = ist;
    idt[vector].attributes = 0x8e;
    idt[vector].middle = handler >> 16;
    idt[vector].high = handler >> 32;
}

void arch_cpu_init(void) {
    __asm__ volatile ("cli" : : : "memory");
    /* No user selectors or DPL3 gates exist; explicitly disable SYSCALL too. */
    uint32_t efer_low,efer_high;
    __asm__ volatile ("rdmsr" : "=a"(efer_low), "=d"(efer_high) : "c"(0xc0000080));
    efer_low &= ~1u;
    __asm__ volatile ("wrmsr" : : "a"(efer_low), "d"(efer_high), "c"(0xc0000080) : "memory");
    for (unsigned int i = 0; i < 4; ++i) tss.ist[i] = (uintptr_t)&x86_exception_stacks[i][sizeof(x86_exception_stacks[i])];
    tss.rsp[0] = tss.ist[3];
    tss.iomap = sizeof(tss); /* No I/O permission bitmap. */
    uintptr_t base = (uintptr_t)&tss;
    uint64_t limit = sizeof(tss) - 1;
    gdt[3] = limit | ((base & 0xffffff) << 16) | (0x89ull << 40) |
             ((base & 0xff000000) << 32);
    gdt[4] = base >> 32;
    for (unsigned int vector = 0; vector < 32; ++vector) {
        x86_idt_set_gate(vector, x86_exception_stubs[vector],
                         vector == 8 ? 1 : vector == 2 ? 2 : vector == 18 ? 3 : 4);
    }
    const struct table_pointer gdtr = {sizeof(gdt) - 1, (uintptr_t)gdt};
    const struct table_pointer idtr = {sizeof(idt) - 1, (uintptr_t)idt};
    x86_load_gdt(&gdtr);
    __asm__ volatile ("lidt %0" : : "m"(idtr) : "memory");
    arch_debug_puts("ON.: GDT, TSS, and exception IDT ready; interrupts disabled.\n");
}

static void hex(const char *label, uint64_t value) {
    char text[19];
    static const char digits[] = "0123456789abcdef";
    text[0] = '0'; text[1] = 'x'; text[18] = '\0';
    for (unsigned int i = 0; i < 16; ++i) text[2 + i] = digits[(value >> ((15 - i) * 4)) & 15];
    arch_debug_puts(label);
    arch_debug_puts(text);
    arch_debug_puts("\n");
}

__attribute__((noreturn)) void x86_exception_fatal(const struct exception_frame *frame, uint64_t cr2) {
    __asm__ volatile ("cli" : : : "memory");
    if (fatal_active) {
        arch_debug_puts("ON.: nested fatal exception; halting.\n");
        arch_halt();
    }
    fatal_active = 1;
    static const char *const names[32] = {
        "Divide error", "Debug", "NMI", "Breakpoint", "Overflow", "Bound range",
        "Invalid opcode", "Device unavailable", "Double fault", "Reserved",
        "Invalid TSS", "Segment not present", "Stack fault", "General protection",
        "Page fault", "Reserved", "x87 floating point", "Alignment check",
        "Machine check", "SIMD floating point", "Virtualization", "Control protection",
        "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
        "Hypervisor injection", "VMM communication", "Security", "Reserved"
    };
    arch_debug_puts("ON.: FATAL CPU EXCEPTION: ");
    arch_debug_puts(frame->vector < 32 ? names[frame->vector] : "Unknown");
    arch_debug_puts("\n");
    task_fault_report();
    hex("handler_frame=", (uintptr_t)frame);
    hex("vector=", frame->vector);
    hex("error=", frame->error);
    hex("rip=", frame->rip);
    hex("cs=", frame->cs);
    hex("rflags=", frame->rflags);
    hex("rsp=", frame->rsp);
    hex("ss=", frame->ss);
    if (frame->vector == 14) hex("cr2=", cr2);
    hex("rax=", frame->rax); hex("rbx=", frame->rbx);
    hex("rcx=", frame->rcx); hex("rdx=", frame->rdx);
    hex("rsi=", frame->rsi); hex("rdi=", frame->rdi); hex("rbp=", frame->rbp);
    hex("r8=", frame->r8); hex("r9=", frame->r9); hex("r10=", frame->r10);
    hex("r11=", frame->r11); hex("r12=", frame->r12); hex("r13=", frame->r13);
    hex("r14=", frame->r14); hex("r15=", frame->r15);
    arch_debug_puts("ON.: fatal exception halted safely.\n");
    arch_halt();
}

#ifndef ON_EXCEPTION_TEST
#define ON_EXCEPTION_TEST 0
#endif
void arch_exception_test(void) {
#if ON_EXCEPTION_TEST != 0
    arch_debug_puts("ON.: controlled exception test begins.\n");
#if ON_EXCEPTION_TEST == 1
    __asm__ volatile (".global x86_test_fault; x86_test_fault: ud2");
#elif ON_EXCEPTION_TEST == 2
    __asm__ volatile ("xor %%edx, %%edx; mov $1, %%eax; xor %%ecx, %%ecx; .global x86_test_fault; x86_test_fault: div %%ecx" : : : "rax", "rcx", "rdx", "cc");
#elif ON_EXCEPTION_TEST == 3
    __asm__ volatile ("movw $0x28, %%ax; .global x86_test_fault; x86_test_fault: movw %%ax, %%ds" : : : "rax");
#elif ON_EXCEPTION_TEST == 4
    /* Request no low mappings, then perform a real supervisor write fault. */
    __asm__ volatile ("movabs $0x1000, %%rax; .global x86_test_fault; x86_test_fault: movb $1, (%%rax)" : : : "rax", "memory");
#elif ON_EXCEPTION_TEST == 5
    /* A #GP whose gate is absent becomes #DF during exception delivery. */
    idt[13].attributes = 0x0e;
    __asm__ volatile ("movw $0x28, %%ax; .global x86_test_fault; x86_test_fault: movw %%ax, %%ds" : : : "rax", "memory");
#elif ON_EXCEPTION_TEST == 6
    extern void x86_test_bad_stack(void) __attribute__((noreturn));
    x86_test_bad_stack();
#elif ON_EXCEPTION_TEST >= 7 && ON_EXCEPTION_TEST <= 9
    uint64_t page = 0;
    const uintptr_t alias = UINT64_C(0x0000400000000000);
    if (!pmm_alloc_page(&page) || !vmm_map(alias, page, VMM_WRITE)) {
        arch_debug_puts("ON.: VMM fault setup failed.\n");
        arch_halt();
    }
    *(uint8_t *)alias = 0xc3; /* RET, if execution were incorrectly permitted. */
#if ON_EXCEPTION_TEST == 7
    if (!vmm_protect(alias, 0)) arch_halt();
    __asm__ volatile (".global x86_test_fault; x86_test_fault: movb $1, (%0)" : : "r"(alias) : "memory");
#elif ON_EXCEPTION_TEST == 8
    /* Jump directly so the fault RIP is the attempted execution address. */
    __asm__ volatile (".global x86_test_fault; .set x86_test_fault, 0x0000400000000000; jmp *%0" : : "r"(alias) : "memory");
#else
    if (!vmm_unmap(alias, 0)) arch_halt();
    __asm__ volatile (".global x86_test_fault; x86_test_fault: movb $1, (%0)" : : "r"(alias) : "memory");
#endif
#else
#error Invalid ON_EXCEPTION_TEST
#endif
    arch_debug_puts("ON.: ERROR exception test returned.\n");
    arch_halt();
#endif
}
