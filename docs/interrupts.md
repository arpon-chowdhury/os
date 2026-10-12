# x86-64 hardware interrupts

Normal boot draws the existing welcome screen, runs any configured exception
injection, installs IRQ gates, initializes the legacy PIC/PIT, then sleeps with
`sti; hlt; cli`. The interrupt shadow prevents a lost wakeup between enabling
interrupts and halting. Fatal halt still uses `cli; hlt`.

## PIC and IRQ entry

The bootstrap CPU disables its local APIC through IA32_APIC_BASE when CPUID
reports APIC support. Limine can leave LAPIC enabled with LINT0 masked; merely
unmasking the PIC then leaves IRQ0 pending without waking the CPU. This is a
single-CPU legacy-PC implementation, with no APIC/IOAPIC or SMP support.

The cascaded 8259 PICs use 8086 mode with master vectors 32–39 and slave vectors
40–47. All lines begin masked. Only IRQ0 is enabled at boot; keyboard IRQ1 and
all other device IRQs stay masked. The cascade IRQ2 opens only when a slave
handler is registered. PIC initialization writes include I/O waits.

`irq_entry.S` supplies 16 stubs with synthetic error codes, saves all 15 general
registers other than RSP, clears DF, and aligns the stack before calling C.
It restores the original stack and registers and returns with `iretq`, which
restores the interrupted flags, including DF and IF. IRQs use the interrupted
kernel stack (IST=0); fatal exception stacks and diagnostics are unchanged.
The frame layout matches `exception_frame` and its existing ABI assertions.
The kernel is compiled without a red zone or SIMD register use.

`x86_irq_register(irq, handler)` requires IF clear, rejects invalid lines and
the cascade line, and unmasks a registered line. A NULL handler masks it.
Handlers run with IF clear and must remain bounded without enabling interrupts.
Unexpected unhandled IRQs are masked. Normal completion sends EOI to the slave
first for IRQ8–15, then the master. In-service checks suppress EOI for spurious
IRQ7 and send only master EOI for spurious IRQ15.

## PIT and ticks

PIT channel 0 uses mode 2, binary counting, and divisor 11932 from an input
clock of approximately 1193182 Hz: approximately 100 Hz, not an exact wall clock.
IRQ0 increments a volatile, naturally aligned `uint64_t` counter initialized
to zero. `arch_system_ticks()` returns a native 64-bit snapshot on the bootstrap
CPU. Unsigned arithmetic wraps modulo 2^64. There is no SMP synchronization,
scheduler, keyboard input, timer callback queue, or wall-clock API.

Serial/debugcon messages at thresholds 100 and 200 come from the foreground
idle loop, keeping UART polling out of the IRQ handler. Logging stops after
those two messages; timer interrupts and the counter continue. The framebuffer
is not modified by interrupt handling.

## Verification

```sh
make verify-interrupts
make verify-boot
make verify-exceptions
make test-framebuffer
```

`verify-interrupts` runs BIOS display capture, BIOS no-VGA fallback, and UEFI
capture. Normal Python boot checks require initialization plus both tick
thresholds in fresh debugcon and COM1 logs before capturing the welcome screen.
This exercises repeated delivery, EOI, returning IRQ entry, and continued idle
execution. `verify-boot` also waits for tick 200. Exception injections run before
IRQ initialization, preserving the saved-IF contract of all six existing tests.

Artifacts live in `build/`: `welcome.png`, `welcome-uefi.png`, firmware-specific
logs, `interrupt-verification.log`, and `exceptions/<case>/` evidence. PIC slave
and spurious handling are implemented but are not hardware-injected by this
suite. Physical hardware and timer accuracy against an external clock remain
unverified.

On 2026-10-13, `make verify-interrupts verify-boot test-framebuffer
verify-exceptions` passed under QEMU q35/qemu64/TCG on the Apple M2 host. BIOS
and UEFI reached both tick thresholds, the no-VGA fallback passed, and all 12
fatal fault checks passed. The UEFI welcome image and all 12 exception captures
matched the normal BIOS welcome PNG byte for byte. The linked normal kernel
had no undefined symbols.
