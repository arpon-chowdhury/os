# MyOS

MyOS is a custom x86-64 operating system written from scratch in C with minimal
assembly. Development takes place on an Apple M2 Mac (ARM64); the target machine
is x86-64, so host and target tooling must remain separate.

This repository currently contains only project scaffolding. There is no kernel
implementation, bootable image, build system, or executable test suite yet.

## Project goals

- Build a small, understandable kernel with clearly separated subsystems.
- Use C for core logic and assembly only where hardware interfaces require it.
- Keep architecture-specific code separate from portable kernel code.
- Integrate an existing bootloader through a documented boot protocol; the
  bootloader and protocol will be selected before implementation begins.
- Develop and test on an emulated x86-64 machine before attempting real hardware.
- Keep builds reproducible and document design decisions and testing procedures.

## Repository structure

```text
MyOS/
├── boot/
│   ├── config/             Bootloader configuration and image-layout inputs
│   └── protocol/           Boot protocol definitions and integration adapters
├── kernel/
│   ├── arch/
│   │   └── x86_64/         CPU-specific C, minimal assembly, and linker scripts
│   ├── core/               Kernel entry and architecture-independent core logic
│   ├── include/            Private kernel headers
│   ├── mm/                 Memory management
│   └── sched/              Task scheduling
├── drivers/
│   └── include/            Device drivers and their private interfaces
├── libs/
│   └── include/            Freestanding support libraries and their headers
├── include/
│   └── myos/               Shared project interfaces used across components
├── scripts/                Future build, image, emulator, and tooling scripts
├── tests/
│   ├── unit/               Host-side tests for code suitable for isolated testing
│   └── integration/        Boot and subsystem tests in an x86-64 emulator
└── docs/                   Architecture notes, decisions, and developer guides
```

Each leaf directory currently contains an empty `.gitkeep` file. Git does not
track empty directories, so these files preserve the scaffold. They are markers,
not source code or build inputs, and can be removed when real files are added.

Generated output will go in `build/`, which is ignored and will be created by
future build tooling. Third-party downloads will go in the ignored `vendor/`
directory; future tooling must record their versions and integrity information.

## Development phases

1. **Repository foundation (current):** establish directory boundaries, Git
   hygiene, project goals, and the development roadmap.
2. **Toolchain and boot planning:** choose the bootloader and boot protocol,
   define the memory and image layout, and document the cross-compilation and
   emulator setup. No software installation is part of the current phase.
3. **First boot:** add a minimal entry path, linker script, image build, and
   basic diagnostic output; verify execution in an x86-64 emulator.
4. **CPU and memory foundation:** implement descriptor tables, interrupts,
   exception handling, physical memory allocation, and virtual memory.
5. **Drivers and support libraries:** add foundational device support and the
   freestanding routines needed by the kernel.
6. **Tasks and higher-level services:** introduce scheduling, process boundaries,
   system calls, and storage/filesystem support incrementally.
7. **Hardening and validation:** expand automated tests, document interfaces,
   improve reproducibility, and assess supported real hardware.

## Development on Apple M2

The host is ARM64 macOS; MyOS targets x86-64. Future builds will need a compiler
that emits freestanding x86-64 ELF objects, a compatible linker, and any tools
required by the chosen bootloader. Future execution tests will need x86-64
emulation, such as QEMU, rather than native execution on the M2 CPU.

Do not assume the default macOS compiler/linker produces suitable kernel
artifacts. Exact tool versions, installation steps, and build commands will be
documented when the toolchain is selected. There are no build or test commands
to run at this stage.

## Files in this initial scaffold

- `README.md`: project goals, directory responsibilities, phased roadmap, and
  host/target development constraints.
- `.gitignore`: excludes macOS metadata, editor-local files, generated build
  artifacts, logs, and future downloaded dependencies.
- `boot/config/.gitkeep`: preserves the bootloader configuration directory.
- `boot/protocol/.gitkeep`: preserves the boot protocol integration directory.
- `kernel/arch/x86_64/.gitkeep`: preserves the target architecture directory.
- `kernel/core/.gitkeep`: preserves the common kernel core directory.
- `kernel/include/.gitkeep`: preserves the private kernel header directory.
- `kernel/mm/.gitkeep`: preserves the memory management directory.
- `kernel/sched/.gitkeep`: preserves the scheduling directory.
- `drivers/include/.gitkeep`: preserves the driver tree and private header directory.
- `libs/include/.gitkeep`: preserves the support library tree and header directory.
- `include/myos/.gitkeep`: preserves the shared project header directory.
- `scripts/.gitkeep`: preserves the build and development script directory.
- `tests/unit/.gitkeep`: preserves the host-side unit test directory.
- `tests/integration/.gitkeep`: preserves the emulator integration test directory.
- `docs/.gitkeep`: preserves the documentation directory.

Git initialization also creates `.git/`, Git's internal metadata directory. Git
manages its files; they are not project source files and must not be edited by hand.
