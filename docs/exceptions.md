# x86-64 CPU exceptions

ON. installs its own GDT, TSS, and IDT on the bootstrap CPU before framebuffer
initialization. All CPU exception vectors 0–31 have fatal handlers. Maskable
interrupts stay disabled during setup and controlled fault tests. Normal boot
then installs PIC IRQ gates at vectors 32–47 and enables PIT delivery; see the
[interrupt guide](interrupts.md). Vectors 48–255 remain non-present. Multitasking,
user mode, and fault recovery remain future work.

## Descriptor tables and entry

The GDT contains a null descriptor, ring-0 64-bit code (selector `0x08`), ring-0
data (`0x10`), and a 64-bit TSS (`0x18`, occupying two slots). The assembly loader
reloads CS with a far return, reloads the data segments, and loads TR. The TSS
has no I/O bitmap. Its RSP0 exists only as a placeholder; there is no privilege
transition or task switching in the current kernel.

Every exception gate is a ring-0 interrupt gate. Four aligned, 16 KiB static
stacks provide IST1 for double fault, IST2 for NMI, IST3 for machine check, and
IST4 for the other exceptions. They are mapped in the kernel's RW data/BSS
segment. All exception entries switch stacks, so fatal handling does not need
the interrupted kernel stack to be usable.

`kernel/arch/x86_64/exceptions.S` supplies 32 entry stubs. CPU-pushed error codes
for vectors 8, 10–14, 17, 21, 29, and 30 are preserved; other stubs supply zero.
The common entry saves all 15 general registers other than RSP, captures CR2,
clears DF, and aligns the stack to the System V C calling convention. RSP and
SS come from the hardware IST frame. C layout assertions check the frame,
GDT/IDT pointer, gate, and TSS sizes against the assembly contract.

The implementation follows the descriptor-table, long-mode exception, and IST
contracts described in the [Intel system programming manuals](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html).
The loader and exception assembly are built by Clang's integrated assembler;
NASM and a separate assembler installation are unnecessary.

## Fatal handling

Every delivered exception, including breakpoint/debug/NMI, is fatal at this
stage. The handler prints its name, vector, error code, RIP, CS, RFLAGS, original
RSP/SS, general registers, and handler-frame address to debugcon and COM1. Page
faults also print CR2. There is no dynamic allocation, stack unwind, framebuffer
redraw, or attempt to resume execution. The last welcome screen remains visible.
The final path disables interrupts and repeatedly halts. A nested-fatal guard
prints a short message and halts rather than trying another full report. UART
polling remains bounded, so a missing or unresponsive serial port cannot stall
fault reporting indefinitely.

This is a single-CPU foundation. NMI and machine-check gates have dedicated
stacks but have not been independently injected in QEMU. The stacks have no
guard pages yet. Corruption of descriptor tables, exception stacks, mappings,
or the diagnostic code itself can still defeat reporting or cause a triple
fault; this is not a general recovery mechanism.

## Controlled QEMU tests

Run with the normal tool PATH, or the documented command-line tool overrides:

```sh
make verify-exceptions
make verify-boot
make verify-framebuffer
make verify-uefi
```

The exception suite builds separate `build/exceptions/<case>/on.iso` images.
Each image renders the welcome screen and initializes physical memory before injecting a real CPU exception;
both BIOS and OVMF boot paths are tested. The normal `build/on.iso` defaults to
`EXCEPTION_TEST=0` and contains no test fault label. Nonzero injection modes
require an isolated `BUILD` beneath `build/`. Object paths include both exception and PMM modes,
and a mode switch forces re-linking, even within one timestamp second on GNU
make 3.81. Reusing a directory across modes was checked by switching
1 → 0 → 1 → 0 and inspecting the resulting kernel symbols.

| Mode / case | Trigger | Verified vector / error |
| --- | --- | --- |
| 1 / `ud` | `ud2` | 6 / 0 |
| 2 / `de` | Integer division by zero in assembly | 0 / 0 |
| 3 / `gp` | Load DS with out-of-range selector `0x28` | 13 / `0x28` |
| 4 / `pf` | Supervisor write to unmapped virtual address `0x1000` | 14 / 2; CR2 = `0x1000` |
| 5 / `df` | Make the #GP gate non-present, then trigger #GP | 8 / 0 on the separate double-fault stack |
| 6 / `stack` | Set RSP to zero, then execute `ud2` | 6 / 0; saved RSP = 0 |
| 7 / `ro` | Write after protecting a VMM alias read-only | 14 / 3 |
| 8 / `nx` | Execute from an NX VMM alias | 14 / 17 |
| 9 / `unmapped` | Write after unmapping an accessed VMM alias | 14 / 2 |

The VMM cases check CR2 = `0x400000000000`; NX execution also checks RIP at
that address. See [virtual memory](virtual-memory.md) for ownership and TLB rules.

The verifier checks debugcon and COM1, exact vector/error, saved RIP against the
fault instruction symbol, CS/SS, disabled IF, and handler-frame placement within
the expected IST stack. It checks divide operands and page-fault CR2 where
applicable. QMP register inspection must show `HLT=1` while the emulator remains
running; a reboot, triple fault/shutdown, missing diagnostic, or returned test
fails verification. QMP then captures the retained framebuffer. Each process
is stopped by the verifier after collecting evidence.

Logs, register snapshots, build output, and screenshots remain beneath each
case directory, with distinct BIOS and UEFI filenames. `LLVM_NM`, `OVMF_CODE`,
`OVMF_VARS`, and `UEFI_TIMEOUT` are configurable. The suite currently uses the
same q35/qemu64/TCG configuration as the normal boot checks.

## Actual results

On 2026-10-13, all six cases passed under both BIOS and UEFI: **12 fault tests**.
Every saved frame and halt-state check passed. All 12 captured PNGs matched the
normal welcome screenshot byte for byte. Normal BIOS entry, BIOS screen capture,
BIOS no-VGA fallback, and OVMF entry/screen capture also passed after installation
of the descriptor tables. Linking produced no undefined symbols and distinct
RW/RX/R/RW segments for requests, code, constants, and data/BSS.

A rapid injection-mode switch exposed GNU make 3.81's timestamp granularity:
the prior image could remain linked if the mode changed within one second.
The build now forces a link whenever the stored mode differs; the repeated
mode-switch check passed. No kernel boot or fault-handler error was observed
in the final QEMU checks. Physical hardware remains unverified.
