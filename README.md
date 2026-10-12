# ON.

The display name is **ON.**; filenames, C identifiers, and shared headers use
`on` / `ON`. The boot image is `build/on.iso` with ISO volume label `ON`.

ON. is a general-purpose PC operating system written from scratch in C with
minimal assembly. Intel and AMD x86-64 computers are the first target, followed
by ARM64 PCs and potentially Apple Silicon Macs. ON. is not an Apple-only OS.

Development currently happens on an Apple M2 Mac. This ARM64 macOS **host**
cross-compiles an x86-64 ELF kernel; QEMU's TCG backend emulates the **target**
CPU. The host does not determine the operating system's supported hardware.
Only the x86-64 target is implemented today. See the
[architecture roadmap](docs/architecture.md) for port boundaries and milestones.

The current implementation is a minimal Limine boot entry: negotiate base
revision 6, initialize a supported Limine framebuffer, draw a welcome screen
with kernel version 0.1.0, emit debug-port and COM1 diagnostics, and idle between PIT timer interrupts.
Basic x86-64 fatal exception handling installs a GDT, TSS, and IDT; see the
[exception guide](docs/exceptions.md). The first legacy PIC/PIT interrupt subsystem provides IRQ dispatch and a 64-bit
system tick counter; see the [interrupt guide](docs/interrupts.md). Physical memory
management allocates protected 4 KiB frames from the Limine map; see the
[memory guide](docs/memory.md). Four-level x86-64 virtual memory provides translation,
mapping, permissions, and TLB invalidation while preserving boot mappings; see the
[virtual memory guide](docs/virtual-memory.md). Scheduling, keyboard input, other
drivers, and user space are future work.

## Build and run

Dependencies must already be available. No target installs software, and ordinary
build targets do not download dependencies. See [the build guide](docs/build.md)
for tool requirements, Apple Silicon setup, overrides, and verification status.

```sh
make help
make objects       # Compile only; works with the available Apple Clang
make kernel        # Requires ELF LLD (ld.lld)
make fetch-limine  # Explicit, checksum-pinned project-local dependency download
make iso           # Requires Limine binaries, a host C compiler, and xorriso
make run           # Requires qemu-system-x86_64; type quit in its monitor to exit
make verify-boot   # Bounded BIOS handoff and welcome-rendering check
make verify-framebuffer # Save the QEMU screen and test the no-VGA fallback
make test-framebuffer   # Native renderer memory safety tests
make test-memory        # Native physical allocator safety tests
make verify-virtual     # BIOS/UEFI virtual memory and rollback tests
make verify-memory      # Controlled BIOS/UEFI allocation and fallback tests
make run DISPLAY=cocoa # View the BIOS welcome screen in a macOS window
make verify-uefi        # Verify x86-64 OVMF entry and capture the welcome screen
make verify-exceptions  # Nine fatal fault cases, each under BIOS and UEFI
make verify-interrupts  # Timer progress, welcome captures, and no-VGA fallback
make run-uefi DISPLAY=cocoa # View the UEFI welcome screen
```

Successful boot should write `ON.: Limine handoff OK; x86-64 kernel entered.`
to `build/debug.log` and `build/serial.log`, followed by the version and
framebuffer status, then physical memory statistics. The kernel reports system ticks reaching 100 and 200, then continues sleeping
between interrupts. The welcome screen stays visible.
Kernel linking, ISO creation, and the QEMU BIOS handoff were verified on this
Apple M2 Mac on 2026-10-13. `make verify-boot` captured the same message in
`build/boot-test-debug.log`. x86-64 UEFI boot with QEMU's bundled OVMF firmware
and the welcome screen were also verified. Real hardware remains unverified.

## Project goals

- Keep the kernel small, understandable, and primarily written in C.
- Separate architecture-specific code from portable kernel logic.
- Use an existing bootloader and document its protocol contract.
- Test in an emulator before attempting supported real hardware.
- Pin dependencies and keep generated files separate from source.

## Project structure and files

- `Makefile`: freestanding compilation, ELF linking, ISO packaging, explicit
  dependency fetching, tool checks, and QEMU commands.
- `kernel/core/main.c`: portable kernel logic using the architecture interface.
- `kernel/mm/physical.c`, `boot.c`, and `kernel/include/memory.h`: physical page
  allocation, reserved-memory protection, statistics, and isolated boot tests.
- `kernel/core/framebuffer.c`: validated RGB framebuffer and bitmap welcome text.
- `kernel/include/version.h`: the kernel version displayed and logged at boot.
- `kernel/include/{arch,kernel}.h`: architecture services and kernel entry contract.
- `kernel/arch/x86_64/entry.c`: Limine markers, revision check, and ELF entry.
- `kernel/arch/x86_64/cpu.c` and `exceptions.S`: GDT/TSS/IDT setup, CPU exception
  entry, fatal diagnostics, and isolated fault injection.
- `kernel/arch/x86_64/interrupts.c`, `interrupts.h`, and `irq_entry.S`: legacy PIC
  remapping, returning IRQ entry/dispatch, PIT programming, and the tick counter.
- `kernel/arch/x86_64/platform.c`: x86-64 debug port/COM1 output and CPU halt.
  Limine provides the initial stack and long-mode environment.
- `kernel/arch/x86_64/linker.ld`: higher-half ELF layout, entry symbol, retained
  Limine markers, and separate writable/executable/read-only segments.
- `boot/protocol/limine_base.h`: small attributed subset of the official protocol
  header, containing base-revision, request-boundary, framebuffer, memory-map,
  and HHDM definitions.
- `boot/config/limine.conf`: automatic ON. selection and kernel path.
- `scripts/fetch-limine.sh`: explicit Limine 12.9.3 download with SHA-256 checking;
  refuses to overwrite an existing dependency directory.
- `scripts/verify-boot.sh`: bounded BIOS handoff and welcome-rendering check.
- `scripts/verify-framebuffer.py`: QEMU BIOS/UEFI screenshots, serial checks, and BIOS no-VGA fallback.
- `tests/unit/physical.c`: sanitized allocator validation and protection tests.
- `tests/unit/framebuffer.c`: host renderer validation and memory guard tests.
- `docs/build.md`: dependencies, first-boot procedure, and verification limits.
- `docs/memory.md`: physical allocator contracts, statistics, and controlled tests.
- `docs/interrupts.md`: PIC/PIT contracts, tick API, and verification.
- `docs/exceptions.md`: exception contracts, test cases, evidence, and limitations.
- `docs/architecture.md`: target roadmap, portable interfaces, and platform boundaries.
- `.gitignore`: excludes generated artifacts, downloaded dependencies, local editor
  settings, logs, and macOS metadata.
- `.gitkeep` files: empty markers retaining the original scaffold directories.
  They have no role in compilation.

Reserved directories remain available for future work:

```text
boot/{config,protocol}/    Bootloader configuration and protocol integration
kernel/arch/x86_64/        CPU-specific code and linker script
kernel/{core,include}/    Kernel core and private interfaces
kernel/mm/                Physical and virtual memory boot support
kernel/sched/             Future scheduling
drivers/include/          Future drivers and private interfaces
libs/include/             Future freestanding libraries and headers
include/on/             Shared interfaces across components
scripts/                  Build and development helpers
tests/{unit,integration}/  Future host and emulator tests
docs/                     Developer guides and architecture decisions
```

All generated objects, dependency files, ELF/map files, host utilities, ISO staging
files, images, downloads, and emulator logs live in ignored `build/`. Downloaded
Limine files and their upstream license live in ignored `vendor/limine/`.
Git manages repository metadata in `.git/`.

## Development phases

1. **Repository foundation:** directory boundaries, Git hygiene, and roadmap.
2. **Build and boot integration (verified):** freestanding toolchain configuration,
   Limine protocol, linker layout, ISO packaging, and emulator workflow. Compilation
   and linking are verified.
3. **First boot validation (verified):** ELF segments, ISO, BIOS boot handoff, and
   diagnostic output passed under QEMU TCG on Apple M2.
4. **CPU and memory foundation (current):** fatal exceptions, legacy PIC/PIT interrupts,
   physical page allocation, and virtual memory management are implemented.
5. **Drivers and libraries:** foundational device support and freestanding routines.
6. **Tasks and services:** scheduling, process boundaries, system calls, and storage.
7. **x86-64 PC hardening:** automated tests, reproducible builds, interface
   documentation, and validation on selected Intel and AMD computers.
8. **ARM64 PC port (future):** reuse portable subsystems and implement ARM64 CPU
   and supported PC platform services after the x86-64 foundation is established.
9. **Apple Silicon evaluation (potential):** assess a separate platform port;
   ARM64 CPU support alone does not provide Apple hardware support.
