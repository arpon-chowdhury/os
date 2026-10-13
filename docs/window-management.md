# Foreground GUI window management

Type `desktop` in the terminal to open three independent retained windows:
Control changes the appearance/wallpaper, About ON. explains the current kernel
architecture and shortcuts, and Palette displays the live semantic theme colors.
They share one foreground event/render loop; they are not application processes.
The original `ui` and `graphics` demos remain available. Escape restores the
terminal, its retained cells, keyboard input, and command prompt.

## Controls

| Input | Behavior |
| --- | --- |
| Click window body/title | Focus and raise the topmost hit window |
| Drag title | Move within the desktop work area |
| Drag any edge/corner | Resize, respecting minimum content size and screen bounds |
| Title minus | Minimize that window |
| Title square | Maximize / restore saved position and size |
| Title cross | Close that window |
| Per-window dock item | Raise a visible window or reopen a minimized/closed window |
| F6 / Shift-F6 | Next / previous visible window in creation order |
| F7 / F8 / F9 | Minimize / maximize-or-restore / close the focused window |
| F1 | Toggle the searchable foreground-tool launcher |
| Tab / Shift-Tab; Enter / Space | Select and activate focused window buttons |
| Escape | Return to the terminal |

Dock indicators distinguish the focused window (accent bar), another visible
window (thin muted bar), a minimized window (outlined mark), and a closed window
(dim label without a running mark). Dock entries are scene-owned tools, not a
process list. Launcher search filters real window titles; Enter opens the first
match, and mouse selection opens that result. Flow, Pulse, Spaces and Studio
remain explicitly labeled planned.

## Architecture and ownership

`kernel/include/window.h` and `kernel/core/window.c` own work-area bounds,
geometry changes, resize capture, lifecycle transitions, rounded hit testing,
focus cycling, and shadow-aware invalidation. `ui.c` owns retained records,
z-order, focused input routing and clipped composition. `desktop.c` owns shell
layout and foreground-tool content. `ui_demo.c` constructs the scene, and
`terminal.c` owns mode switching and reads task diagnostics. Window lifetimes
do not own task, address-space, process or application lifetimes; the preemptive
scheduler is a separate kernel module.

Each window independently stores visibility, minimized/closed/maximized state,
normal geometry, child controls and content identity. Maximize saves the exact
normal geometry; minimize and dock reopening preserve maximized state. Clicking
the square or F8 then restores the saved normal geometry. Closing a background
window preserves the existing focus. Closing/minimizing the focused window
selects the frontmost remaining visible one. Hidden windows never receive hits
or keyboard input. Geometry capture is cancelled when its window closes, when
focus cycles, or when the launcher opens.

Hit testing traverses front-to-back and excludes rounded corner gaps and shadow
pixels. Input uses press/release capture identities; a resize/drag release cannot
activate a child button. Geometry arithmetic uses bounded 64-bit intermediates.
Minimum dimensions preserve fixed child bounds; Control buttons stretch with the
window width. Resizing/maximizing a window never resizes another window.

## Memory and drawing

There are eight scene-owned window records, eight bounded buttons per window,
sixteen dirty rectangles and bounded titles/search text. Capacity exhaustion
returns failure. No per-window pixel buffers, heap allocations, shared ownership,
or freeable pointers are introduced. Windows retain opaque display lists; the
validated boot framebuffer is borrowed and must remain mapped/writable throughout
the mode's lifetime. The kernel's existing physical/virtual/heap allocators remain
independent of window management.

Close removes a window from input/composition and retains its fixed record for
reopening. It does not consume another slot on reopen. Scene reset (`ui_init`)
clears all records. IDs are valid only during that scene and must not be reused
across resets. The capacity is eight total records per scene, including closed
records; there is no arbitrary dynamic create/destroy/reclaim API yet. Repeated
close/reopen/minimize/maximize operations require no allocation and cannot leak
or fragment a window-buffer heap because no such heap exists.

Rounded borders, title controls, resize grips and offset opaque shadows use the
original ON. font and theme tokens. Damage includes both old/new geometry and
shadow footprints; focus/lifecycle changes also invalidate dock indicators.
Launcher changes invalidate its bounded panel. Dirty compositing rebuilds affected
regions back-to-front, including wallpaper and overlapping windows. Dirty overflow
falls back to full-screen damage; idle rendering performs no writes. Existing
pointer-overlay save/restore behavior remains in the framebuffer backend.

Limitations: CPU rasterization into the front buffer can tear. There is no
per-window raster cache, alpha shadow, double buffering, vsync, GPU driver, modal
window, snapping, animation, persistent layout, application registry or process isolation. Preemptive kernel tasks are now implemented
separately; see [multitasking](multitasking.md). Only the built-in foreground tools are provided.
At compact resolutions the dock may overlay the bottom of a window, and content
is clipped when a window is too small to display it all. Physical hardware and
full accessibility remain unverified.

## Verification

`make test-windows` runs native ASan/UBSan checks for overlapping and rounded
hit tests, z-order, forward/reverse cycling, all resize edges, child-size limits,
maximized geometry and restoration, minimize while maximized, repeated close/
reopen, capture cancellation, background-window close, no-visible-window focus,
capacity limits, idle rendering, bounded damage, and padded/guarded framebuffer
storage. It compares dirty output byte-for-byte with full redraws after geometry,
focus and lifecycle changes, including shadows and the window dock. It also
checks that edge resizing at screen boundaries keeps the opposite edge fixed.

`make verify-windows` boots the real kernel in QEMU BIOS and UEFI and sends PS/2
input. It covers desktop theme/wallpaper behavior, independent window switching,
mouse resize with exact bounds checks, maximize/minimize/restore, independent
close, dock and filtered-launcher reopening, screenshots, Escape, and subsequent
terminal `mem`/`heap` commands. Captures are generated under
`$(BUILD)/windows/{bios,uefi}`. Build artifacts are ignored by Git.

## Recorded results — 2026-10-13

The final image in `build/windows-final` passed `verify-windows` under BIOS and
UEFI, including exact Control resize bounds and maximized/restored geometry,
independent About/Palette close, dock/launcher reopening, Escape and terminal
memory commands. The final nine native ASan/UBSan suites passed: window manager,
retained UI, graphics, framebuffer, terminal, keyboard, mouse, physical memory,
and heap. Dirty/full framebuffer equality, guarded/padded pitch, repeated window
lifecycles and fixed capacity passed natively. Overlap and resized captures were
visually inspected; QEMU screenshots show actual kernel rendering.

The full regression matrix exited 0 with 52 QEMU boot/check cases: retained UI,
desktop, graphics, terminal, keyboard and mouse (two each), framebuffer/fallback
(two), extra UEFI boot (one), BIOS smoke (one), virtual memory (two), heap
stress/rollback (four), physical-memory modes/RAM sizes (twelve), and nine CPU
exception cases on both firmwares (eighteen). After the final screen-edge resize
refinement, final-image BIOS/UEFI window checks and retained UI/desktop
compatibility checks passed again. The default kernel is byte-for-byte identical
to the tested final kernel. No final test failures remain.

Logs: `build/windows-final-verification.log`, `build/windows-final-native.log`,
`build/windows-final-compatibility.log` and `build/windows-regressions.log`.
Final images are in `build/windows-final/windows/{bios,uefi}`; useful captures
include `desktop-dark.png`, `windows-overlap.png`, `windows-resized.png`,
`windows-maximized.png`, `windows-restored.png`, and `windows-final.png`.

An initial compile detected an unused old full-screen damage helper; it was
removed before verification. No host settings, dependency installs, physical
disks or privileged host operations were used. Real PC hardware is unverified.
