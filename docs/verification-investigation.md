# Verification timeout investigation — 2026-10-13

## Original user-mode failure

`build/user-verification-final.log` records successful sanitized address-space
and task tests, followed by a BIOS `USER_TEST=1` timeout. The traceback points to
line 109 of the historical framebuffer harness: the boot polling loop waiting
for `ON.: system ticks reached 200.` in **both debugcon and COM1**. It did not
time out in a QMP command or a screenshot operation. The enclosing Make target
stopped before its UEFI mode-1 or mode-2 invocations.

The original failure log is timestamped 12:20:39; its referenced QEMU logs were
overwritten at 14:23:20 before this investigation. Those replacement logs show
all 21 isolated user faults and full reclamation passing. They cannot identify
which individual guest operation stalled in the original failed run.

Fresh runs pass with the existing 60-second limit. Initial recorded user-mode
diagnostic arrival times were 2.29/4.05 seconds for mode 1 BIOS/UEFI and
2.30/4.05 seconds for mode 2. There is no reproduced kernel regression,
scheduler deadlock, or unexpected user fault. An environmental delay remains
possible but **unproven**; the original failure cannot be conclusively classified
from the surviving evidence. No timeout was increased and no kernel protection
was changed to make these runs pass.

## Confirmed filesystem fixture failure and correction

The first filesystem rerun failed BIOS mode 7. Preserved debugcon output ends
with `ON.: FS TEST FAILED; halted safely.` The fixture enabled block writes but
omitted mode 7 from the writable ONFS mount condition. Opening the fixture file
for writing failed before reaching the injected backend write error.

Mode 7 now requests a writable mount, matching its existing block permission.
The test still requires the injected write to return IO without publishing a
byte count, poisoned-volume reads to fail, the controller to be quarantined,
handles to close, and unmount to succeed. The host verifies the complete image
remains unchanged. Corruption and recovery modes retain read-only mounts.

## Harness correction

The harness now records boot diagnostic timing and the actual QEMU command.
Timeout messages name the awaited diagnostic and missing output channels.
User selftest failures, filesystem selftest failures, and unexpected fatal
kernel exceptions fail immediately rather than becoming generic timeouts.
Unique failure directories preserve debugcon, COM1, QEMU stderr, command, and
failure metadata when an output directory is reused.

An intentional one-second timeout verified the negative path. A subsequent
successful run in the same directory left every archived file's SHA-256
unchanged. A simulated QEMU selftest failure verified immediate failure and
archival. These are expected negative checks, not regression-suite passes.

## Scope and limitations

This validates the existing Phase 10 filesystem work and corrects its fixture;
Phase 11 was not started. Host macOS settings, firmware inputs, timeout defaults,
and memory protection are unchanged. Guest tests use QEMU TCG, disposable disks,
and copied UEFI variables; they do not establish behavior on physical hardware.
The original user-mode timeout's underlying cause remains unproven.

Exact regression results and evidence locations follow below.

## Final regression results

All 21 targets passed (exit 0), comprising **100 QEMU checks** and the native
ASan/UBSan tests invoked by these targets. The first filesystem run failed mode
7 as described above; the complete corrected rerun passed all 18 cases.

| Make target | QEMU checks | Result | Log under `build/timeout-investigation/` |
| --- | ---: | --- | --- |
| `verify-user` | 4 | PASS (exit 0) | `verify-user-final.log` |
| `verify-user-load` | 2 | PASS (exit 0) | `verify-user-load.log` |
| `verify-fs` | 18 | PASS (exit 0) | `verify-fs-final.log` |
| `verify-storage` | 10 | PASS (exit 0) | `verify-storage.log` |
| `verify-boot` | 1 | PASS (exit 0) | `verify-boot.log` |
| `verify-framebuffer` | 2 | PASS (exit 0) | `verify-framebuffer.log` |
| `verify-uefi` | 1 | PASS (exit 0) | `verify-uefi.log` |
| `verify-exceptions` | 18 | PASS (exit 0) | `verify-exceptions.log` |
| `verify-memory` | 12 | PASS (exit 0) | `verify-memory.log` |
| `verify-virtual` | 2 | PASS (exit 0) | `verify-virtual.log` |
| `verify-heap` | 4 | PASS (exit 0) | `verify-heap.log` |
| `verify-tasks` | 6 | PASS (exit 0) | `verify-tasks.log` |
| `verify-apps` | 4 | PASS (exit 0) | `verify-apps.log` |
| `verify-app-load` | 2 | PASS (exit 0) | `verify-app-load.log` |
| `verify-keyboard` | 2 | PASS (exit 0) | `verify-keyboard.log` |
| `verify-terminal` | 2 | PASS (exit 0) | `verify-terminal.log` |
| `verify-mouse` | 2 | PASS (exit 0) | `verify-mouse.log` |
| `verify-graphics` | 2 | PASS (exit 0) | `verify-graphics.log` |
| `verify-ui` | 2 | PASS (exit 0) | `verify-ui.log` |
| `verify-desktop` | 2 | PASS (exit 0) | `verify-desktop.log` |
| `verify-windows` | 2 | PASS (exit 0) | `verify-windows.log` |

Machine-readable results: `build/timeout-investigation/consolidated-results.json`.
The original batch results (including the failed filesystem attempt) remain in
`results.json`; final filesystem/user reruns are in `final-results.json`.
Filesystem disk-content evidence is in the directory named by
`build/filesystem/latest-run.txt`. Screenshots and boot timings accompany each
target’s debugcon and COM1 logs in its existing build output directory.

Desktop, dock/launcher restore, themes, wallpaper, focus, dragging, resize,
maximize/restore, minimize/close, keyboard and mouse all passed under both
firmware paths, including sustained Ring 3 load, ELF syscall load, and background
filesystem reads. Native filesystem tests also passed 18 interrupted/torn commit
cases and 1000 metadata mutations.

The initial Python bytecode check encountered the host’s external cache write
restriction; rerunning with an explicit bytecode output under `build/` passed.
This required no host setting change.
