#include <stddef.h>
#include "arch.h"
#include "interrupts.h"
#include "io.h"
#include "keyboard.h"
#include "terminal.h"
#include "mouse.h"

#define PIC_MASTER 0x20
#define PIC_SLAVE 0xa0
#define PIT_DIVISOR 11932u /* Approximately 100 Hz; rounded hardware divisor. */
static x86_irq_handler handlers[X86_IRQ_COUNT];
static uint16_t mask = 0xffff;
static volatile uint64_t system_ticks;
extern const uintptr_t x86_irq_stubs[X86_IRQ_COUNT];

static void write_mask(void) {
    /* Keep the cascade open only while at least one slave line is enabled. */
    if ((mask & 0xff00) != 0xff00) mask &= ~(1u << 2);
    else mask |= 1u << 2;
    outb(PIC_MASTER + 1, mask);
    outb(PIC_SLAVE + 1, mask >> 8);
}

bool x86_irq_register(unsigned int irq, x86_irq_handler handler) {
    uint64_t flags;
    __asm__ volatile ("pushfq; popq %0" : "=r"(flags));
    if (irq >= X86_IRQ_COUNT || irq == 2 || (flags & 0x200)) return false;
    handlers[irq] = handler;
    if (handler) mask &= ~(1u << irq);
    else mask |= 1u << irq;
    write_mask();
    return true;
}

void x86_irq_dispatch(const struct exception_frame *frame) {
    if (frame->vector < X86_IRQ_BASE || frame->vector >= X86_IRQ_BASE + X86_IRQ_COUNT) return;
    unsigned int irq = frame->vector - X86_IRQ_BASE;
    /* Spurious IRQ7 gets no EOI; spurious IRQ15 acknowledges only the cascade. */
    if (irq == 7 || irq == 15) {
        uint16_t port = irq == 7 ? PIC_MASTER : PIC_SLAVE;
        outb(port, 0x0b); /* Read in-service register. */
        if (!(inb(port) & 0x80)) {
            if (irq == 15) outb(PIC_MASTER, 0x20);
            return;
        }
    }
    if (handlers[irq]) handlers[irq](frame);
    else {
        mask |= 1u << irq;
        write_mask();
    }
    if (irq >= 8) outb(PIC_SLAVE, 0x20);
    outb(PIC_MASTER, 0x20);
}

static void timer_irq(const struct exception_frame *frame) {
    (void)frame;
    ++system_ticks;
}

uint64_t arch_system_ticks(void) {
    /* Aligned native 64-bit read on the single boot CPU. */
    return system_ticks;
}

void arch_interrupt_init(void) {
    __asm__ volatile ("cli" : : : "memory");
    /* Limine may leave LAPIC enabled with LINT0 masked. This first subsystem
       uses legacy PIC delivery exclusively, so disable the bootstrap LAPIC. */
    uint32_t eax = 1, ebx, ecx, edx;
    __asm__ volatile ("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    if (edx & (1u << 9)) {
        uint32_t low, high;
        __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(0x1b));
        low &= ~((1u << 11) | (1u << 10)); /* APIC global and x2APIC enable. */
        __asm__ volatile ("wrmsr" : : "a"(low), "d"(high), "c"(0x1b) : "memory");
    }
    for (unsigned int irq = 0; irq < X86_IRQ_COUNT; ++irq)
        x86_idt_set_gate(X86_IRQ_BASE + irq, x86_irq_stubs[irq], 0);
    outb(PIC_MASTER + 1, 0xff);
    outb(PIC_SLAVE + 1, 0xff);
    outb(PIC_MASTER, 0x11); io_wait();
    outb(PIC_SLAVE, 0x11); io_wait();
    outb(PIC_MASTER + 1, X86_IRQ_BASE); io_wait();
    outb(PIC_SLAVE + 1, X86_IRQ_BASE + 8); io_wait();
    outb(PIC_MASTER + 1, 4); io_wait();
    outb(PIC_SLAVE + 1, 2); io_wait();
    outb(PIC_MASTER + 1, 1); io_wait();
    outb(PIC_SLAVE + 1, 1); io_wait();
    mask = 0xffff;
    write_mask();
    system_ticks = 0;
    outb(0x43, 0x34); /* Channel 0, low/high divisor, mode 2, binary. */
    outb(0x40, PIT_DIVISOR & 0xff);
    outb(0x40, PIT_DIVISOR >> 8);
    (void)x86_irq_register(0, timer_irq);
    arch_debug_puts("ON.: PIC IRQs ready; PIT ~100 Hz; only IRQ0 unmasked.\n");
}

__attribute__((noreturn)) void arch_idle(void) {
    unsigned int reports = 0;
    for (;;) {
        /* STI's interrupt shadow makes the enable-and-sleep sequence atomic. */
        __asm__ volatile ("sti; hlt; cli" : : : "memory");
        /* Foreground drawing and commands must not stall IRQ1 or the PIT. */
        __asm__ volatile ("sti" : : : "memory");
        struct keyboard_event event;
        while (keyboard_poll(&event)) {
            terminal_handle_event(&event);
            if (event.pressed && event.character) {
                char text[2] = {event.character, 0};
                arch_debug_puts("ON.: key ");
                if (event.character == '\n') arch_debug_puts("ENTER");
                else if (event.character == '\b') arch_debug_puts("BACKSPACE");
                else arch_debug_puts(text);
                arch_debug_puts("\n");
            }
        }
        struct mouse_event mouse;
        while (mouse_poll(&mouse)) {
            framebuffer_pointer_move(mouse.dx, mouse.dy);
            terminal_mouse(&mouse);
            /* Foreground diagnostics also make QEMU input tests observable. */
            arch_debug_puts("ON.: mouse ");
            if (mouse.dx > 0) arch_debug_puts("X+ ");
            else if (mouse.dx < 0) arch_debug_puts("X- ");
            if (mouse.dy > 0) arch_debug_puts("Y+ ");
            else if (mouse.dy < 0) arch_debug_puts("Y- ");
            arch_debug_puts(mouse.left ? "L1 " : "L0 ");
            arch_debug_puts(mouse.right ? "R1\n" : "R0\n");
        }
        uint64_t ticks = arch_system_ticks();
        terminal_tick(ticks);
        if (reports < 2 && ticks >= (uint64_t)(reports + 1) * 100) {
            arch_debug_puts(reports == 0 ? "ON.: system ticks reached 100.\n" :
                                         "ON.: system ticks reached 200.\n");
            ++reports;
        }
        __asm__ volatile ("cli" : : : "memory");
    }
}
