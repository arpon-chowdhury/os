#ifndef ON_X86_64_INTERRUPTS_H
#define ON_X86_64_INTERRUPTS_H
#include <stdbool.h>
#include "exceptions.h"
#define X86_IRQ_BASE 32
#define X86_IRQ_COUNT 16
/* Handlers run with IF clear; keep them bounded and do not enable interrupts. */
typedef void (*x86_irq_handler)(const struct exception_frame *frame);
/* Registration changes require IF clear; a NULL handler masks the line. */
bool x86_irq_register(unsigned int irq, x86_irq_handler handler);
void x86_irq_dispatch(struct exception_frame *frame);
void x86_idt_set_gate(unsigned int vector, uintptr_t handler, uint8_t ist);
#endif
