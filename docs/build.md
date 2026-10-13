# Building ON. (x86-64 target)

The current development host is an Apple M2 Mac running ARM64 macOS. The target
is Intel/AMD x86-64 PCs, independent of the host. This guide describes that
cross-build workflow; it does not imply Apple-only hardware support. ARM64 PCs
and potentially Apple Silicon Macs are future targets, with no ARM64 build or
implementation yet. See [the architecture roadmap](architecture.md).

## Dependencies

| Tool | Purpose | Local inspection |
| --- | --- | --- |
| Clang with x86-64 ELF target support | Freestanding kernel C compilation | Apple Clang 21.0.0 verified |
| LLVM LLD (`ld.lld`) | Link ELF64 rather than macOS Mach-O | 23.1.3; linking passed |
| GNU make 3.81 or newer | Build orchestration | System make available and tested |
| Host C compiler (`cc`) | Build Limine's ARM64 macOS image utility | Verified ARM64 Mach-O utility build |
| xorriso | Package BIOS/UEFI hybrid ISO | 1.5.8.pl02 verified |
| QEMU (`qemu-system-x86_64`) | Emulate x86-64 on ARM64 with TCG | 11.1.2; BIOS boot passed |
| curl, shasum, tar | Fetch, hash-check, and extract Limine | Available; fetch and hash check passed |
| Limine 12.9.3 binary distribution | Bootloader assets and host utility source | Downloaded and SHA-256 verified |

LLVM's `llvm-readelf` and `llvm-nm` are recommended for ELF inspection. NASM,
a separate assembler, a target libc, and an x86-64 GCC toolchain are not required.
Clang's integrated assembler also builds the GDT loader and exception stubs.

Homebrew 7.0.9 is installed under `/opt/homebrew`. The approved installation of
`lld`, `qemu`, and `xorriso` completed successfully, including their required
Homebrew dependencies. LLVM 23.1.3 was installed as an LLD dependency and
provides ELF inspection tools. The kernel still uses Apple Clang 21.

The zsh login profile (`~/.zprofile`) contains:

```sh
eval "$(/opt/homebrew/bin/brew shellenv zsh)"
```

A fresh zsh login finds Homebrew, LLD, QEMU, and xorriso. For the current Terminal
session, run the same `eval` command if needed. Installation ran with
`HOMEBREW_NO_SUDO=1`; no administrator commands were run during package
installation. Use ELF `ld.lld`, not Apple's `ld` or LLVM's Mach-O linker
`ld64.lld`.

## Commands

Run from the repository root, including if its absolute path contains spaces:

```sh
make check-tools
make check-deps
make objects
make kernel
make fetch-limine  # First setup only; skip if vendor/limine already exists
make iso
make run
make verify-boot
make verify-framebuffer
make test-framebuffer
make run DISPLAY=cocoa
```

`make` defaults to `kernel`, so linking never implicitly downloads a bootloader.
`make fetch-limine` is a deliberate download, not an installation. It obtains
[the official Limine 12.9.3 binary release](https://github.com/Limine-Bootloader/Limine/releases/tag/v12.9.3)
and checks its published SHA-256:

```text
9f42fe9ea2e84d71056529969b1d5c24ba9a2287ffcc01bcce58b0ef8aa98b1d
```

The script preserves the upstream distribution and license under `vendor/limine`.
It refuses to overwrite that directory. A failed extraction may leave partial
files; inspect them before approving any manual cleanup. Release versions and
checksums must be updated together. The digest checks integrity against the
release metadata; it is not a separate signature verification step.

Tool overrides are passed on the command line, for example after dependencies
have been installed with permission:

```sh
make kernel CC=/opt/homebrew/opt/llvm/bin/clang LD=/opt/homebrew/bin/ld.lld
make run QEMU=/opt/homebrew/bin/qemu-system-x86_64
```

Keep the same overrides for subsequent builds. Changes to compiler flags or
command-line overrides do not automatically invalidate existing objects; use a
new output directory for a fresh build without deleting anything:

```sh
make kernel BUILD=build/llvm CC=/opt/homebrew/opt/llvm/bin/clang LD=/opt/homebrew/bin/ld.lld
```

Use relative, space-free values for `BUILD` and `LIMINE`; the repository itself
can reside in a path with spaces. `BUILD` should stay beneath ignored `build/`.
The fetch script always uses `build/downloads` and `vendor/limine`.
No cleanup target is provided; no deletion is necessary for initial setup.

## Target selection

`ARCH` defaults to `x86_64`; `make objects ARCH=x86_64` preserves the existing
build target. Any other value fails immediately with an unsupported-target
error. The build includes portable sources from `kernel/core`, `kernel/mm`, and
`kernel/sched`, plus only `kernel/arch/$(ARCH)`. New shared components must be
added explicitly to the source list when implemented.

The target triple, CPU flags, linker script, ISO boot assets, and QEMU command
remain x86-64-specific. Selecting an architecture is not sufficient to port the
OS: future ports must supply those build rules and platform implementations.

## Build contract

Clang targets `x86_64-unknown-none-elf`, uses the kernel code model and no red zone,
and disables stack protection, PIC/PIE, floating-point/SIMD register use, and
unwind tables. C headers come from Clang's freestanding support; no macOS SDK or
host libc is linked. LLD links the objects directly as a static ELF with no
startup objects or standard libraries. Future code that requires routines such
as `memcpy` must provide freestanding implementations before use.

The linker places the kernel at `0xffffffff80000000`, retains the Limine marker
sequence, and page-aligns segments with read/write, read/execute, or read-only
permissions. Limine sets up the initial x86-64 execution environment and stack,
so a separate assembly startup file is unnecessary for this phase.

The image is `build/on.iso`, with ISO volume label `ON` and boot menu entry
`ON.`. It contains `boot/kernel.elf`, `boot/limine/limine.conf`, BIOS boot files,
the UEFI CD image, and `EFI/BOOT/BOOTX64.EFI`. The Limine image utility is compiled
for the host into `build/host/limine` and applies BIOS boot metadata only to the
newly generated ISO; it does not write to a physical disk.

`make run` boots via QEMU's BIOS with `q35`, `qemu64`, and software TCG emulation.
The same hybrid image also boots with x86-64 OVMF using `make run-uefi`
and is tested by `make verify-uefi`. Apple HVF cannot
accelerate an x86-64 guest on an ARM64 CPU.

QEMU runs without a graphical display. Its monitor occupies the terminal; type
`quit` to exit. Limine serial diagnostics are captured in `build/serial.log`.
Kernel diagnostics are mirrored to COM1 with bounded UART polling and also
to debug port `0xe9` in `build/debug.log`. A successful
handoff emits:

```text
ON.: Limine handoff OK; x86-64 kernel entered.
ON.: kernel version 0.1.0.
ON.: framebuffer welcome screen ready.
```

The kernel renders a simple welcome screen and then idles between PIT interrupts. `make run` is
headless by default; use `make run DISPLAY=cocoa` for a macOS display window.
Basic fatal CPU exceptions and legacy PIC/PIT IRQ handlers are implemented;
physical page allocation is also implemented. No scheduler, keyboard,
terminal, or desktop is implemented.
The QEMU debug port is not a real-hardware output interface.

## First verified boot procedure

All required tools and the five Limine assets are now available. The first
x86-64 BIOS boot passed on this Apple M2 Mac on **2026-10-13**. To reproduce it
from a zsh session with Homebrew in PATH:

```sh
make check-tools
make check-deps
make kernel
/opt/homebrew/opt/llvm/bin/llvm-readelf -h -l build/kernel.elf
/opt/homebrew/opt/llvm/bin/llvm-nm --undefined-only build/kernel.elf
make iso
make verify-boot BOOT_TIMEOUT=60
```

The verified run used explicit `LD=/opt/homebrew/opt/lld/bin/ld.lld`,
`XORRISO=/opt/homebrew/bin/xorriso`, and
`QEMU=/opt/homebrew/bin/qemu-system-x86_64` overrides because the agent's shell
had not loaded the login profile. These overrides remain useful in shells
without Homebrew in PATH.

The ELF must be x86-64, with `kmain` as the higher-half entry, no undefined
symbols, and separate executable and writable load segments. The ISO rules
match the upstream Limine template; the config location and `path` syntax match
[Limine 12.9.3 configuration](https://github.com/Limine-Bootloader/Limine/blob/v12.9.3/CONFIG.md).

`verify-boot` runs BIOS/QEMU TCG without a monitor, clears its own logs before
launch, and waits up to `BOOT_TIMEOUT` seconds (default 30). It passes only when
both the handoff and framebuffer-ready lines match, and terminates
its QEMU process on completion. Inspect `boot-test-debug.log`,
`boot-test-serial.log`, and `boot-test-qemu.log` beneath `BUILD` for failures.
A failure may mean a missing dependency, bootloader error, kernel fault, or a
slow TCG boot; increasing the timeout does not establish correctness.
This verifies initial handoff and completed render, not UEFI, real hardware,
or later kernel services. Run `make run` afterward for an interactive monitor session.

The verifier's success, unsupported-revision, timeout, early-exit, and stale-log
handling can be checked with a simulated QEMU process. Such checks validate the
verification script; they are not evidence that ON. actually booted.

## x86-64 UEFI boot with OVMF

QEMU 11.1.2 installed by Homebrew already provides EDK II/OVMF firmware.
No additional package or firmware download was needed. Its installed
`share/qemu/firmware/60-edk2-x86_64.json` declares the tested pair:

- Code: `/opt/homebrew/share/qemu/edk2-x86_64-code.fd`.
- Variables template: `/opt/homebrew/share/qemu/edk2-i386-vars.fd`.

Despite the variables filename, this template is paired with the x86-64 code
by QEMU's own firmware descriptor. `OVMF_CODE` and `OVMF_VARS` can be overridden
for a matching firmware pair on another host. Use space-free paths for these
Makefile values. This test uses the ordinary non-Secure-Boot firmware; Secure
Boot is not configured or verified.

```sh
make check-ovmf
make verify-uefi                # UEFI_TIMEOUT defaults to 60 seconds
make run-uefi DISPLAY=cocoa     # Interactive monitor: type quit to exit
make verify-boot                # BIOS regression check
make verify-framebuffer         # BIOS screen capture and no-VGA fallback
```

Both firmware paths use the same `build/on.iso` and kernel. The ISO already
contains the Limine UEFI CD boot image and `EFI/BOOT/BOOTX64.EFI`; no kernel,
linker, or bootloader configuration change was required for UEFI entry.
The UEFI command uses `q35`, `qemu64`, and TCG with two raw pflash drives:
unit 0 holds read-only firmware code, and unit 1 holds a writable local copy
of NVRAM. The installed code and variables template are never written.

`run-uefi` maintains `build/ovmf-vars.fd` between interactive runs.
`verify-uefi` starts from a fresh copy at `build/ovmf-test-vars.fd` on every test,
so previous boot selections cannot supply a false pass. It uses QMP stdio and
writes separate evidence files:

- `build/uefi-framebuffer-debug.log`: kernel handoff, version, and render status.
- `build/uefi-framebuffer-serial.log`: OVMF, Limine, and kernel COM1 output.
- `build/uefi-framebuffer-qemu.log`: emulator diagnostics.
- `build/welcome-uefi.png`: actual QEMU display capture.

Actual result on 2026-10-13: OVMF loaded and started its `UEFI QEMU DVD-ROM`
boot entry. The kernel logged successful Limine handoff, version 0.1.0, and
framebuffer readiness on both diagnostic channels. QMP captured the 1280x800
screen, visually confirmed as `WELCOME TO ON.` and `KERNEL 0.1.0`.
The first verifier attempts rejected the serial output because UEFI cursor
control sequences and Limine text preceded the first kernel line without a
newline. The verifier now removes ANSI controls and accepts that handoff text
within the current serial output. The corrected UEFI check passed; BIOS entry,
BIOS display, and BIOS no-VGA fallback also passed on the rebuilt ISO.

This verifies x86-64 UEFI boot in QEMU with this OVMF pair. Physical UEFI PCs,
Secure Boot, and ARM64 remain outside the verified scope.

## Framebuffer support and checks

The boot adapter requests the framebuffer using the
[official Limine framebuffer ABI](https://github.com/Limine-Bootloader/limine-protocol/blob/trunk/include/limine.h)
and copies its first framebuffer into a portable descriptor. The renderer
supports 32-bit RGB modes with three non-overlapping 8-bit channels and dimensions
from 320x200 through 8192x8192. It validates address, dimensions, pitch, bit depth,
memory model, channel masks, and arithmetic overflow before writing pixels.
Rows use the supplied pitch; unaligned addresses and pitch are handled with
byte writes. Unsupported modes are skipped without writing framebuffer memory.
Absent responses and unsupported base revisions are checked before dereferencing.
A missing or unsupported screen leaves debug-port and COM1 diagnostics working.

The welcome screen uses a small original 5x7 bitmap font with uppercase
letterforms. It shows `Welcome to ON.` and `Kernel 0.1.0` on a navy background.
The version comes from `kernel/include/version.h`. This is a boot screen, with
no input handling or desktop services.

```sh
make test-framebuffer
make verify-framebuffer BOOT_TIMEOUT=60
```

The native host test uses AddressSanitizer and UndefinedBehaviorSanitizer to
check guard bytes, row padding, unaligned buffers, swapped RGB channel order,
and malformed metadata rejected without writes. Sanitizer code is never linked
into the kernel. The QEMU test uses Python 3's standard library and QMP through
stdio to capture `build/welcome.png`; it checks kernel diagnostics on both the
debug port and COM1. It then boots with `-vga none` and checks the safe fallback.
The screenshot test checks capture completion; the actual PNG was also visually
inspected to confirm its contents. The QMP screendump requires PNG support in
QEMU (verified in the installed Homebrew build).

Actual results on 2026-10-13: the renderer safety tests passed; QEMU captured a
1280x800 welcome screen with version 0.1.0; the no-VGA boot reported
`ON.: framebuffer unavailable; screen skipped.` on both diagnostic channels.
The initial attempt to use a local QMP socket was denied by the execution
sandbox; switching to QMP stdio resolved it without changing macOS settings.

## Verification status

Verified on 2026-10-13:

- Homebrew 7.0.9 and persistent zsh login PATH configuration.
- LLD 23.1.3, QEMU 11.1.2, and xorriso 1.5.8.pl02 installation and execution.
- Limine 12.9.3 archive SHA-256 matches the pin and official release metadata.
- All required Limine assets and ARM64 Mach-O host utility compilation.
- Portable core and x86-64 sources compile with warnings treated as errors.
- ELF64 x86-64 executable links with `kmain` in the higher half.
- Four page-aligned load segments have RW, RX, R, and RW permissions; none is
  RWX. The last segment maps descriptor tables and exception stacks in data/BSS.
- `llvm-nm --undefined-only` reports no undefined symbols.
- ISO creation succeeds: 2,119 sectors (4,339,712 bytes).
- Limine BIOS installation succeeds. It reports converting the generated
  hybrid ISO's GPT metadata to MBR and marking partition 1 active for
  compatibility; all these writes are confined to `build/on.iso`.
- `make verify-boot BOOT_TIMEOUT=60` exits successfully with the exact kernel
  diagnostic in `build/boot-test-debug.log`:

```text
ON.: Limine handoff OK; x86-64 kernel entered.
ON.: kernel version 0.1.0.
ON.: framebuffer welcome screen ready.
```

`build/boot-test-serial.log` records Limine loading
`boot():/boot/kernel.elf`. The QEMU log contains a signal 15 termination message:
this is the verifier stopping its own emulator after success, not a boot error.
No kernel or bootloader error was observed. Current normal boot idles with
interrupts enabled and reports ticks reaching 100 and 200.

The ON. rebrand was also verified on 2026-10-13: the rebuilt `build/on.iso`
passed BIOS handoff, framebuffer capture, COM1 diagnostics, and no-VGA fallback
checks. The captured screen reads `WELCOME TO ON.` with `KERNEL 0.1.0`.

x86-64 UEFI execution with OVMF is verified as described above; real hardware
remains unverified. These results verify only
the x86-64 BIOS handoff and welcome rendering. The subsequent PIC/PIT checks
also verify repeated timer delivery under BIOS and UEFI; see the
[interrupt guide](interrupts.md). Physical frame allocation is also implemented; see the [memory guide](memory.md).
Virtual memory translation, mapping, permissions, and TLB invalidation are
implemented; see the [virtual memory guide](virtual-memory.md). PS/2 input, kernel scheduling and isolated user execution are also implemented;
see [multitasking](multitasking.md) and [user mode](user-mode.md). Bounded
syscalls, static ELF loading and bounded ATA storage are implemented; filesystem
services remain pending. See
[syscalls and loader](syscalls-and-loader.md) and [storage](storage.md) for build commands, verification and limits.

## Exception builds and tests

See [the exception guide](exceptions.md) for GDT/TSS/IDT layouts, fatal handling,
and the nine controlled CPU-fault cases. `make verify-exceptions` runs 18 tests
across BIOS and UEFI, with isolated images beneath `build/exceptions/`. Default
builds use injection mode 0; object paths now include `obj/mode-0-0-0-0/` (or the
selected test mode). Mode changes force re-linking to prevent fault injection
from leaking into a normal build when an output directory is reused.
All 12 fault tests and the normal boot/screen regressions passed on 2026-10-13.

## Upstream references

- [Limine protocol](https://github.com/Limine-Bootloader/limine-protocol/blob/trunk/PROTOCOL.md)
- [Official C template](https://github.com/Limine-Bootloader/limine-c-template)
- [Limine 12.9.3 configuration](https://github.com/Limine-Bootloader/Limine/blob/v12.9.3/CONFIG.md)

## Physical memory builds and tests

See [the memory guide](memory.md). `make test-memory` runs sanitized native
allocator tests. `make verify-memory` tests exhaustion/restoration and missing
map/HHDM fallbacks under BIOS and UEFI at 128 MiB and 256 MiB. Normal images use
`PMM_TEST=0`; test images use isolated build directories. Object modes encode
exception, PMM, VMM, and heap flags, and the `build-mode` record forces a link when
any of these changes. No test reclaims firmware or bootloader memory.

## Kernel heap builds and tests

See [the heap guide](heap.md). `make test-heap` runs sanitized native tests;
`make verify-heap` runs stress and partial-provisioning rollback under BIOS/UEFI.
Normal builds use `HEAP_TEST=0`; injected modes require isolated build directories.

## Foreground window checks

`make test-windows` runs native ASan/UBSan geometry, lifecycle, capacity and dirty-composition tests. `make verify-windows` boots BIOS and UEFI, exercises independent desktop windows, and captures PNGs under `$(BUILD)/windows/{bios,uefi}`. See [window management](window-management.md) for controls, ownership and limitations.

## Kernel task checks

`make test-tasks` uses native sanitizers and a modeled context-switch hook. `make verify-tasks` builds isolated `TASK_TEST=1`, `2`, `3` images and checks BIOS/UEFI real contexts, expected guard faults, and allocation rollback. Task mode is part of object/build-mode keys and must not be combined with other fault injections. See [multitasking](multitasking.md).

## Ring 3 phase checks

`make test-address-spaces` validates portable ownership/rollback code under native
sanitizers. `make verify-user` builds isolated USER_TEST=1/2 images and verifies
CPL3/CR3, permissions, user fault containment and real allocation rollback under
BIOS/UEFI. `make verify-user-load` uses USER_TEST=3 for GUI regressions under two
sustained user workers. These fixtures load trusted embedded bytes only; normal
boot starts no applications. See [contracts and results](user-mode.md).

## Static application and syscall checks

`make apps` builds independent ON. ABI ELF64 test executables. `make test-elf`
runs the sanitized parser/population suite. `make verify-apps` checks syscall
entry/return, pointers, ELF validation, faults, repeated launches and rollback
under BIOS/UEFI. `make verify-app-load` runs desktop/window regressions with two
loaded syscall-issuing applications. Images are isolated in `build/apps-test/`;
normal boot has no embedded or automatically launched user application. See
[implementation, actual results and limits](syscalls-and-loader.md).

## Storage phase checks

`make test-storage` runs sanitized ATA protocol and block service tests.
`make verify-storage` builds isolated `STORAGE_TEST=1..5` images, creates
disposable raw disks below `build/storage/`, and verifies BIOS/UEFI I/O, flush,
reboot persistence, failure containment, slave-only selection and desktop stress.
Installed OVMF code and variable templates are read-only inputs. The default
QEMU q35 AHCI boot controller is unsupported; tests attach `piix3-ide` alongside
it. Normal images never enable disk writes. See [storage](storage.md) for actual
results and the filesystem integrity work still required.
