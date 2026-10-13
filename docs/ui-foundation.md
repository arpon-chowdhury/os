# Retained UI foundation

Cooperative kernel tasks now run separately from the foreground UI; see [multitasking](multitasking.md).

Phase 3 builds on this preserved demonstration; see [interactive desktop](desktop.md) for the implemented shell, lifecycle controls and current limits.

ON. remains the operating system's official name. Feature labels are Flow,
Pulse, Spaces, Studio and Control. This layer is a foreground UI demonstration,
with no application processes, multitasking, filesystem, or dependency on host
appearance settings.

## What is implemented

`kernel/include/ui.h` and `kernel/core/ui.c` provide retained window surfaces,
child buttons, theme tokens, focus/z-order, pointer capture, title-bar dragging,
a centered dock and bounded dirty-region compositing. `ui_demo.c` constructs
Control and Canvas sample windows. Type `ui` in the terminal to open the scene;
Escape redraws the retained terminal cells and restores its prompt. The original
`graphics` demo and all console commands remain available separately.

Surfaces are retained opaque display lists: bounds, labels, children, selected
control, activation count and last routed character. They are not allocated
pixel buffers. A dirty region is cleared and recomposed back-to-front from
these records, then the dock is drawn above the windows. Rounded corner gaps
show the underlying surface. The shared framebuffer backend preserves the
software mouse pointer during these writes.

There are at most eight surfaces, eight buttons per surface and sixteen dirty
rectangles, with fixed storage and bounded labels. Excess surface/button
creation fails without changing existing objects. Dirty overflow collapses to
a full-screen invalidation. No UI allocation is required, so the UI does not
consume the boot heap or depend on it being available. The framebuffer mapping
must remain writable and alive; APIs are owned by one foreground consumer.

## Focus and input routing

Left click focuses and raises the topmost hit window. Right click focuses it
without activating a button. Title-bar press captures the pointer; movement
moves the captured window, clamped between the top bar and dock area. Release
ends capture. Pressing a button captures its identity; release activates it
only when still over the same button on the same topmost surface. Hover and
selection states redraw only affected surfaces. There is no click-through to
covered buttons.

Keyboard characters route only to the focused window and update its retained
last-character indicator. Tab/Shift-Tab select the next/previous button;
Enter/Space activate it. F6 cycles visible windows and raises the newly focused
one. Escape belongs to the terminal's mode controller and exits the UI before
window handlers see it. PS/2 IRQ handlers still enqueue events only; the
foreground loop drains them, routes them, and renders once after the queues.

The dock selects one of five standalone labels and draws a selection indicator.
Control also focuses the existing Control sample surface. Flow, Pulse, Spaces
and Studio do not launch applications. Canvas's Activate button demonstrates
activation counting rather than a real application action. Button actions
currently support generic activation and Light/Dark/Auto selection; a future
application callback/lifetime model is not claimed here.

## Dirty redraw contract

`ui_invalidate` clips signed rectangles to the display, merges intersecting
regions transitively and falls back to a full redraw when the queue fills.
Movement invalidates both the old and new bounds. Focus changes invalidate both
windows. Theme changes invalidate the complete scene. An idle `ui_render` is a
no-op. Small caller invalidations remain small.

The graphics layer now has an explicit clip rectangle. All graphics pixel,
fill, outline, line, text and clear operations respect it. Surface rendering
intersects this clip with the surface bounds; button labels intersect it again
with their parent button. Full-window labels cannot spill into neighbors.
The clip is restored after each component and reset after a composite pass.
The UI updates only dirty framebuffer regions; pointer movement independently
restores and redraws its small saved rectangle.

`frames` counts render batches and `redraw_pixels` sums the area of composited
dirty rectangles. It is not a hardware performance counter or a count of every
pixel store: overlapping drawing commands within a region may store pixels more
than once. Composition is CPU rasterization directly into the front buffer, so
it can tear. It has no alpha blending, per-window raster cache, double buffering,
vsync, GPU driver, or damage-aware font raster cache.

## Theme tokens and Auto

Theme modes use shared semantic tokens for canvas, surface, raised surface,
primary ink, secondary ink, accent and focus. Light uses `#F2F5FA` canvas and
`#18253D` ink; Dark uses `#0C1426` canvas and `#DDE8F5` ink. Colors follow the
original identity in [gui-design.md](gui-design.md). Geometry and bitmap font
remain original. The windows, dock and controls use tokens consistently.

Auto uses local-time input supplied through `ui_auto_time`: 07:00 inclusive to
19:00 exclusive resolves to Light; other times resolve to Dark. A missing or
invalid clock value resolves to Dark. Changing the supplied time updates tokens
and invalidates the scene if its resolved palette changes. Native tests verify
schedule boundaries and invalid-clock fallback.

The kernel has no RTC/local-time service, so the interactive demo's Auto mode
truthfully displays a Dark fallback. It does not read host macOS appearance,
change host settings, infer civil time from PIT ticks, persist preferences, or
pretend to have a working clock. Integration with an actual local-time service
is future work. Labels and font scaling are still limited to original bitmap
ASCII; high-DPI typography and contrast/accessibility validation require more
work.

## Verification

`make test-ui` uses ASan/UBSan to check creation limits, invalid child bounds,
focus and keyboard ownership, press/release capture, dragging and screen bounds,
Auto schedule transitions, dirty queue overflow, guard bytes and padded pitch.
It compares dirty-render output byte-for-byte with a full redraw after window
movement/composition changes, checks that idle rendering changes no counters,
and confirms a two-by-two invalidation composites only four pixels of region
area. It also covers long child labels and extreme mouse coordinates.

`make verify-ui` tests the actual QEMU PS/2 input path under BIOS and UEFI:
opening the UI, mouse activation of Light, screenshot palette checks, keyboard
Dark/Auto selection, title dragging, F6 focus and character routing, dock input,
Escape, and subsequent terminal commands. Captures are in `build/ui/` (or under
an explicitly selected BUILD). `build/ui-final-verification.log` records the
final focus-enabled run built in `build/ui-final/`; the full existing regression
matrix is in `build/ui-regressions.log`.

## Remaining limits and next work

This is a basic retained UI compositor, not a complete desktop/window manager.
No resizing, minimize/close controls, modal dialogs, text editing fields,
application callbacks, general surface removal, persistence or concurrent UI
clients are implemented. Hit-testing uses rectangular surface bounds even at
rounded corners. The dock and top bar are simple reusable renderer patterns
with fixed demo labels, rather than dynamically managed application registries.
Input queues can still drop newest events under overload; an arbitrary lost
button release is not automatically reconstructed. Real PC hardware remains
unverified.

Next: formalize surface/component lifetimes and callback actions, add reusable
layout and window resize/close controls, and test pointer capture cancellation
and keyboard accessibility. Add a trusted local-time service for Auto before
claiming autonomous day/night adaptation. Keep scheduling and full applications
as separate phases.

## Recorded results — 2026-10-13

The full regression command exited successfully: 49 QEMU boots/checks, including
UI (2), the graphics demo (2), terminal (2), keyboard (2), mouse (2), framebuffer
and no-VGA fallback (2), BIOS smoke (1), nine exception cases on both firmwares
(18), physical-memory modes/RAM sizes (12), virtual memory (2), and heap
stress/rollback (4). Eight native ASan/UBSan suites passed: UI, graphics,
framebuffer, terminal, keyboard, mouse, physical memory and heap.

The final focus-enabled UI was rebuilt separately in `build/ui-final` and passed
BIOS and UEFI again after the final input/hover refinements. Dark and Light
captures were visually inspected; both palettes and Escape restoration were
also checked automatically. Dirty/full render equivalence and Auto schedule
adaptation were verified natively; autonomous clock-driven Auto adaptation is
not available in the running kernel. No final test failures remain.

An initial freestanding link failed because large aggregate zero-initialization
emitted a call to an unavailable `memset`. Bounded explicit initialization fixed
that failure without introducing a hosted runtime or installing dependencies.
No macOS settings, physical partitions or privileged operations were changed.
