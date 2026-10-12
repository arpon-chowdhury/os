#ifndef ON_ARCH_H
#define ON_ARCH_H

#include <stdint.h>
#include <stdbool.h>

/* Implemented by the selected architecture; debug output may be emulator-only. */
void arch_debug_init(void);
void arch_cpu_init(void);
void arch_exception_test(void);
void arch_interrupt_init(void);
void arch_keyboard_init(void);
bool arch_mouse_init(void);
/* Save IF and disable local interrupts; restore the prior IF state. */
uint64_t arch_interrupt_save(void);
void arch_interrupt_restore(uint64_t flags);
uint64_t arch_system_ticks(void);
bool arch_clock_minute(unsigned int *minute);
__attribute__((noreturn)) void arch_idle(void);
const char *arch_name(void);
void arch_debug_puts(const char *text);
__attribute__((noreturn)) void arch_halt(void);

#endif
