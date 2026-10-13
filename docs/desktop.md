# Interactive ON. desktop

Run `desktop` from the terminal; Escape restores its retained cells and prompt.
`ui` and `graphics` remain separate foundation demos. F1 toggles the application
menu/launcher. Type a window-title prefix in its search field, Backspace edits,
and Enter opens the first matching foreground tool. Search misses show no available tool.
The launcher intentionally labels Flow, Pulse, Spaces and Studio unavailable.

The floating dock contains Launcher, Control and Appearance. Control restores
its retained window after minimize or close. A second dock row manages
Control, About ON. and the live Palette window independently. Appearance switches Light
and Dark; Control also exposes Auto and Change wallpaper. Two original geometric
wallpapers are rasterized procedurally, without external assets or file loading.
Close hides and marks the retained tool closed; opening Control recreates its
visible state, retaining appearance choices. Minimize preserves the same tool.
These are foreground GUI surfaces, not tasks or application processes.
[Cooperative kernel tasks](multitasking.md) run separately from window management.

Title bars drag and focus windows, with minus/square/cross controls for
minimize, maximize/restore and close. All edges and corners resize windows. Window buttons retain Tab/Shift-Tab and Enter/Space access. F6 cycles
visible windows; Shift-F6 reverses switching, and F7/F8/F9 minimize,
maximize/restore and close the focused window. See [window management](window-management.md). Shell controls capture press/release identities, block input
behind the launcher, and have hover feedback. The framebuffer's existing saved
pointer overlay and dirty-region compositor remain responsible for cursor and
screen updates. Idle rendering performs no writes. Wallpaper rendering respects
the dirty clip; movement recomposes old and new regions.

The top bar displays read-only guest CMOS RTC hours/minutes when stable and
valid; otherwise it displays Clock unavailable. It explicitly labels this RTC
firmware time: no timezone, date, persistence or host appearance integration.
RTC polling is bounded, once per approximate PIT second, with interrupts saved
and restored. Auto still uses the established local-time API and Dark fallback;
RTC firmware time is deliberately kept separate from that local-time input.
PS/2 input and CPU online labels report this boot's active infrastructure; they
are informational, without simulated network, battery or service controls.

Implementation boundaries: `desktop.c` owns shell layout, wallpaper, search and
shell input; `ui.c` retains surfaces, controls, damage, focus and compositing;
`window.c` owns geometry and lifecycle rules;
`ui_demo.c` owns foreground mode construction; `clock.c` reads architecture RTC;
`terminal.c` owns entry/exit. Storage is fixed and bounded, with no UI allocation.
Original ON. bitmap typography and semantic theme tokens are shared throughout.

Limitations: no bitmap wallpaper decoder/filesystem, persistent preferences,
real application system, process isolation, preemptive scheduling, translucency, animation,
double buffering, vsync, GPU acceleration, Unicode or scalable font rendering.
Small displays use compact layouts. Full accessibility remains future work. Only QEMU x86-64 is tested, not physical PCs.

## Verification

`make verify-desktop` exercises real PS/2 events under BIOS and UEFI: entry,
Light selection, wallpaper change, dragging, minimize, search miss/edit/match,
close, dock restore, Escape and subsequent mem/heap terminal commands. PNG
captures and boot logs are generated in `build/desktop/{bios,uefi}`. Native UI
sanitizer tests also check desktop dirty/full render equivalence, idle rendering,
search editing and guarded framebuffer storage. See the recorded results below.

## Recorded desktop-shell baseline results — 2026-10-13

These record the preceding shell implementation. Current expanded window-manager
results are in [window-management.md](window-management.md).

The final image in `build/desktop-final` passed desktop interaction verification
under BIOS and UEFI. Both palettes, wallpaper changes, launcher results and
window lifecycle were exercised; Dark BIOS and Light UEFI launcher captures
were visually inspected, including the running guest RTC clock. Final native
ASan/UBSan suites passed for UI, graphics, framebuffer, terminal, keyboard,
mouse, physical memory and heap. Expanded UI lifecycle/capture checks passed
again in `build/desktop-final-ui-tests.log`.

The complete regression matrix exited 0 with 50 QEMU boots/checks: retained
UI, graphics, terminal, keyboard and mouse (two each), framebuffer/fallback
(two), additional UEFI boot (one), BIOS smoke (one), virtual memory (two), heap
stress/rollback (four), physical-memory modes/RAM sizes (twelve), and nine CPU
exception cases on both firmwares (eighteen). Final retained UI BIOS/UEFI checks
also passed separately against the final image. No final failures remain.

Logs: `build/desktop-final-verification.log`, `build/desktop-final-native.log`,
`build/desktop-final-ui-tests.log`, `build/desktop-final-foundation.log` and
`build/desktop-regressions.log`. Final screenshots are in
`build/desktop-final/desktop/{bios,uefi}`. Generated artifacts are ignored by
Git and must be regenerated on a fresh checkout.

During development, the first desktop verifier referenced an unavailable image
size variable, and the terminal unit-test link needed a stub for the new mode
entry. Both harness issues were corrected; subsequent checks passed. No host
macOS settings, installs, physical disks or privileged operations were changed.
