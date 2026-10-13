# Phase 4: bounded storage and disk I/O

ON. OS has a lightweight ATA PIO driver and a kernel-only block service. This
phase provides raw sector I/O on disposable QEMU disks. It does not implement a
filesystem, file operations, disk syscalls, application storage or a file manager.
Normal boot leaves all devices read-only and the existing desktop intact.

## Supported target and ownership

The x86 adapter recognizes Intel PIIX3/PIIX4 compatibility IDE controllers
(PCI IDs 8086:7010 and 8086:7111) on root PCI bus 0. It verifies the IDE class
and compatibility programming interface, enables I/O decoding with a 16-bit
PCI command write, and disables bus mastering. It does not rewrite PCI status
W1C bits or interpret arbitrary BARs. Only the first recognized controller's
primary channel (`0x1f0`, alternate status/control `0x3f6`) is used. Two fixed
slots represent master and slave. The verified controller is QEMU `piix3-ide`
attached alongside q35's existing boot CD-ROM controller.

IDENTIFY DEVICE rejects ATAPI, missing LBA support, zero/invalid capacity and
reported logical sector sizes other than 512 bytes. LBA28 capacity is bounded
by 2^28 sectors; only that reported window is exposed, even if hardware has a
larger LBA48 capacity. Write enable requires validated FLUSH CACHE support.
A failed/unsupported IDENTIFY does not publish a device. An expected IDENTIFY
abort from a missing master does not hide a valid slave. A stuck channel is
quarantined. No device means a diagnostic and continued desktop boot.

`kernel/arch/x86_64/storage.c` owns PCI/port I/O. The portable driver is
`kernel/drivers/storage/ata.c`; discovery glue/test fixtures live beside it.
`kernel/services/block.c` owns policy, serialization and caller buffers.
Definitions are in `kernel/include/ata.h` and `block.h`. No public user ABI or
physical address capability was added. Kernel callers supply trusted buffers;
this interface is not a user-pointer validation substitute.

## Request bounds and failure behavior

Read/write requests accept 1–8 sectors (512–4096 bytes), a valid device slot,
exact matching buffer length and an in-capacity LBA range. Subtraction-based
bounds reject overflow and end-of-disk crossing before hardware commands.
The driver independently enforces LBA28 and transfer limits. Callers receive
`BLOCK_OK`, `INVALID`, `ABSENT`, `BUSY`, `READ_ONLY`, `IO` or `TIMEOUT`.

The service uses one fixed 4 KiB supervisor bounce buffer and a single shared
controller lock. Writes snapshot the full input before commands; reads validate
and complete into the bounce buffer before modifying any caller byte. An error
after partial read leaves the caller buffer unchanged. The buffer is cleared
before unlock. No allocation occurs on the I/O path. Concurrent/reentrant
requests return BUSY rather than deadlocking. Request owners must keep their
kernel buffers alive until return.

Writes start disabled, including for writable backends. Trusted kernel code may
explicitly enable one device through `block_set_writable`, if flush is supported.
A successful write includes completion of ATA FLUSH CACHE, not just acceptance
of the data words. Explicit flush is available as well. Normal boot has no path
that enables writes; only controlled storage test images exercise them now.
A transport or flush error quarantines both devices on the channel. No automatic
retry, reset or reopening of a partly failed write occurs until reboot.

The transport uses IDENTIFY (`0xec`), READ SECTORS (`0x20`), WRITE SECTORS (`0x30`)
and FLUSH CACHE (`0xe7`). It transfers 256 16-bit words per logical sector,
checks BSY/DRQ/ERR/DF and final completion, and uses four alternate-status reads
after selection/commands/transfers. nIEN disables ATA interrupts; PIC masks stay
unchanged. Polling has a hard 100,000-iteration ceiling plus a 200-tick deadline
(about two seconds per wait at the current PIT rate), including tick wraparound.
A whole request can contain several waits; this is not a two-second total-request
promise. Polling never changes caller IF, and locks disable interrupts only
briefly. Runtime callers should use process context with interrupts enabled;
IRQ handlers must not perform synchronous disk I/O. Scheduler timer preemption
and queued input continue during runtime polling. Boot discovery precedes the
foreground idle loop and remains iteration-bounded if IF is clear.

Supported emulation details were checked against [QEMU's PIIX controller](https://github.com/qemu/qemu/blob/master/hw/ide/piix.c)
and [ATA emulation](https://github.com/qemu/qemu/blob/master/hw/ide/core.c).
The narrow controller scope follows [Intel's archived chipset documentation](https://www.intel.com/design/archives/chipsets/440/index.htm).

## Verification commands

```sh
make test-storage
make verify-storage
```

Native ASan/UBSan tests model port registers, the full transfer data path,
controller errors and a reentrant backend. They cover absent/ATAPI devices,
slave selection, capability/sector-size rejection, read/write bounds, data-word
packing, multi-sector transfers, flush, missing DRQ, ERR/DF, iteration and
wrapping-tick deadlines, read-only policy, immutable write snapshots, BUSY
reentry, no partial read publication, flush failure and channel quarantine.
The block test runs separate read-error and flush-error scenarios.

The QEMU harness creates fresh 2 MiB deterministic raw images strictly beneath
`build/storage/`. It rejects symlink image/output paths, never accepts a host
physical disk, and copies OVMF variable data to test output. Each mode runs under
BIOS and UEFI:

| Mode | Verification |
| --- | --- |
| 1 | First/last sector reads; invalid requests; default write refusal; 32 eight-sector write/read/flush rounds; neighboring sectors; full desktop/window/theme/input regression during a background disk reader |
| 2 | Reboot the same raw image, read the persisted eight-sector payload, verify neighbors and full-disk contents |
| 3 | No supported controller/disk: ABSENT and continued boot |
| 4 | Backend rejects every write: caller receives failure, channel quarantines and desktop boot continues; full image remains unchanged |
| 5 | Slave-only disk, no master: discovery, first/last reads, eight-sector write/read/flush and default/master absence |

Every attached image is compared byte-for-byte after QEMU exits. Only sectors
32–39 may change in successful write modes; all other bytes must match the
original. Mode 4 must leave the entire image unchanged. `results.json` records
the final ten successful configurations.

QEMU `ide-hd` refuses attaching a read-only block node. Therefore modes 2/4 use
the documented [blkdebug error injector](https://www.qemu.org/docs/master/devel/testing/blkdebug.html)
to return EROFS for every write while permitting attachment and reads. This
verifies guest handling of a write-rejecting backend; it is not a claim that
QEMU IDE hardware reports a read-only capability. Mode 2 never enables guest
writes. Mode 4 deliberately enables the controlled fixture and proves the
transport failure does not modify the raw image.

## Actual results — 2026-10-13

All **14 native ASan/UBSan suites** passed: the existing twelve suites
(tasks, address spaces, ELF, PMM, heap, framebuffer, graphics, keyboard, mouse,
terminal, UI and windows) plus ATA and block I/O. The block suite ran both read-
and flush-error scenarios. All **82 distinct QEMU firmware/configuration checks**
passed, excluding exploratory failures and duplicate reruns:

| Checks | Count | Evidence |
| --- | --- | --- |
| Storage modes 1–5, each BIOS/UEFI, including disk/desktop stress and persistence | 10 | `build/storage-final-verification.log`, `build/storage/results.json` |
| Existing Ring 3, task contexts/guards/rollback and ELF/syscall modes, including desktop load | 18 | `build/storage-process-regression.log` |
| Existing exceptions, PMM at 128/256 MiB, paging and heap | 36 | `build/storage-foundation-regression.log` |
| Existing windows, desktop, UI, terminal, input, graphics, framebuffer/fallback and boot | 18 | `build/storage-desktop-regression.log` |

Each final matrix command exited 0. Native storage evidence is in
`build/storage-native-final.log` and the native prerequisite output of the final
storage matrix. Existing native evidence is in the regression logs. A subsequent
normal-image BIOS/UEFI boot recheck is in `build/storage-final-boot.log` and is not
counted twice. `build/build-mode` is `0-0-0-0-0-0-0-0`: no test injection in the
normal ISO.

The UEFI screenshot `build/storage/uefi/mode-1/windows-final.png` was visually
inspected and retains multiple windows, theme controls, wallpaper, dock and
launcher. Its final task report shows the background storage reader repeatedly
dispatched while foreground input/window interactions completed. Failed read-only
attachment and exploratory slave detection runs are described below; they are
not included in the final pass count.
Known-good syscall/ELF revision `99e5dc2` was preserved before implementation as
`checkpoint/syscalls-99e5dc2`. No host macOS setting, installation or physical
disk was modified. Initial read-only attachment failed at QEMU startup and was
replaced by the write-error injector described above. The slave-only test exposed
an expected IDENTIFY abort being treated as a channel failure; detection now
rejects that missing device and continues to the valid slave. Temporary diagnosis
code was removed, and the affected storage matrix was rerun.

## Limits and next release gates

This is single-CPU QEMU TCG x86-64 storage, not general hardware compatibility.
PIIX3 is verified; PIIX4 recognition is implemented but not QEMU-tested. Secondary
channels, native-mode PCI IDE, AHCI/SATA, NVMe, USB disks, virtio, DMA, LBA48,
4 KiB logical sectors, hotplug and controller recovery are unsupported. Unsupported
controllers, including default q35 AHCI, are skipped. There is no disk selection
UI, partition parser, filesystem mount, user storage syscall or app-loading source.
The block service serializes I/O and may return BUSY under contention; there is
no queue, fairness guarantee or asynchronous service yet. PIO is appropriate for
the small current bound, not a high-throughput design.

A successful flush is the device's acknowledgment and is verified across QEMU
restarts; physical power-loss durability and real hardware are unverified.
Multi-sector writes are not atomic. Failed writes/flushes may have changed some
requested sectors even though the API returns an error. Quarantine prevents
further I/O but does not repair on-disk contents. No filesystem integrity claim
is made. Phase 5 must add validated metadata, transactional safe file operations,
recovery and corruption/power-failure tests before persistent file operations
become a release capability. Phase 4 stops here. Optional features and Phase 8
release work remain untouched.
