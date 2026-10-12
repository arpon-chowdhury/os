# ON. graphics foundation and GUI direction

Status: Phase 1 design record; the subsequent implemented retained UI is
documented in [ui-foundation.md](ui-foundation.md). The audit below records the
Phase 1 baseline. This document separates the implemented foundation from the
proposed desktop. ON. owns its C/assembly implementation, bitmap font, geometry,
palette, and demo assets. Windows and macOS inform interaction goals only; no
proprietary code, fonts, icons, wallpapers, or other assets are imported.

## Repository audit

| Area | Actual capability | Current limit |
| --- | --- | --- |
| Build | Makefile selects freestanding x86-64 clang/LLD, higher-half ELF, Limine hybrid ISO; native tests use host sanitizers | x86-64 only; external tools/assets must already be available |
| Boot | Limine BIOS and x86-64 OVMF UEFI handoff; protocol revision checked before reading responses | QEMU verified; real PC compatibility is unverified |
| Framebuffer | Boot adapter copies address, dimensions, pitch and RGB masks; validated 32-bit RGB writes, clipped primitives, pointer overlay | 320x200 through 8192x8192; each RGB channel is eight bits with nonoverlapping masks; no 16/24-bit modes, GPU driver, display switching, or acceleration |
| Graphics | Pixel, fill, outline, clipped integer line, clear and scaled bitmap text through a borrowed framebuffer context | Direct front-buffer drawing; no buffering, vsync, alpha compositing, damage tracking, or Unicode shaping |
| Font | Original ON. 5x7 ASCII glyphs with upper/lowercase, digits and punctuation; nearest-neighbor integer scale 1–8 | Bitmap typography; no vector font engine or antialiasing |
| CPU/interrupts | GDT/TSS/IDT, fatal exception reports/IST stacks, legacy PIC/PIT ~100 Hz | Bootstrap CPU only; no scheduler, SMP execution, or APIC interrupt subsystem |
| Memory | Protected physical frame allocator, four-level virtual mappings and permissions, fixed 1 MiB kernel heap with failure rollback | No process address spaces or user-space runtime; existing boot mappings are retained |
| Input | IRQ1 translated set-1 US keyboard, Shift, Enter, Backspace; IRQ12 standard PS/2 mouse and button state; fixed event queues | No USB, wheel, layout switching, IME, or input ownership between applications; newest events dropped when queues fill |
| Terminal | Bounded command line, wrapping, scrolling, blinking cursor; help/clear/echo/version/ticks/mem/heap and graphics command | Single foreground consumer; no history, pipes, ANSI, or serial input shell |
| Demo | Static original scene with top bar, rounded panel, floating centered dock and placeholder names; Escape restores terminal cells | No functional windows, dock launches, click actions, or desktop services |
| Themes/services | Proposed below | No persistent preferences, RTC service, theme scheduling, filesystem, multitasking, Flow, Pulse, Spaces or Studio implementation |

The current source boundaries are `kernel/arch/x86_64/entry.c` for Limine
translation, `framebuffer.c` for validated pixel storage and pointer composition,
`font.c` for glyph data, `graphics.c` for drawing primitives, `graphics_demo.c`
for the static scene, and `terminal.c` for console state and demo switching.
All drawing is foreground work. IRQ handlers route/enqueue input only.

## Original identity

The operating system's official name remains **ON.** Built-in feature names are
**Flow**, **Pulse**, **Spaces**, **Studio**, and **Control**, without an OS-name
prefix. Use these standalone names in GUI labels, menus, documentation, and
future development plans. This naming convention does not add implementations
or change the existing four demo dock entries.

ON. should feel calm, deliberate, and useful. The wordmark is the literal `ON.`;
the period suggests a ready state rather than another platform's emblem.
A geometric ring-and-point symbol may be developed later using original paths.
The Phase 1 demo uses text and elementary geometry rather than third-party icons.

Use a coherent eight-pixel spacing rhythm, with four-pixel optical adjustments.
Keep interactive targets at least 40 pixels in a later scalable UI, with visible
focus, adequate contrast, keyboard access, and labels where icons are ambiguous.
The demo's small bitmap labels are foundation diagnostics, not the final
accessibility or typography implementation.

| Token | Dark proposal / demo | Light proposal |
| --- | --- | --- |
| Canvas | `#0C1426` | `#F2F5FA` |
| Surface | `#18253D` | `#FFFFFF` |
| Raised surface / dock | `#243650` | `#E4EBF4` |
| Primary ink | `#DDE8F5` | `#18253D` |
| Secondary ink | `#91A7C4` | `#50627C` |
| ON. sea accent | `#66D9CF` | `#166C65` |
| Focus / secondary accent | `#687CFF` | `#4354BE` |
| Warm attention | `#F0BA73` | `#815216` |

Contrast must be measured for final text sizes and states before shipping a
functional desktop. Avoid conveying status through color alone.

## Proposed desktop interaction

A minimal top system bar carries ON., active workspace/context, and a compact
status area. A centered bottom dock floats above the canvas with breathing room
on each side, showing pinned/running tools, labels on focus, and a distinct
running indicator. The top bar is not a global application menu in this phase.

Later windows should support familiar explicit minimize/maximize/close controls,
keyboard focus and switching, drag movement, edge snapping, and workspace
organization. Use an original title-bar layout with textual/tool-tip labels;
do not copy traffic-light assets or another OS's exact control arrangement.
Rounded surfaces should have restrained shadows, clear active/inactive states,
and predictable resize boundaries. Density and typography should adapt to
resolution and eventual display scale, rather than assuming a Retina display.

The default hierarchy is canvas → window/content surface → controls → focus.
Visual transparency and animation are optional future enhancements; readability
and responsiveness take priority. A future font family must be original or have
an explicit redistributable license. Phase 1 uses only the original bitmap font.

## Proposed Light, Dark, and Auto

Theme selection will eventually use shared semantic tokens rather than
hardcoded colors in individual applications. Light and Dark are explicit user
choices. Auto should follow a user-configured local-time schedule once a clock,
timezone/preferences service, and persistent settings exist. If those are
unavailable, use a deterministic Dark fallback and show the reason in settings;
never infer host macOS appearance or change host settings.

No adaptive theme engine is implemented here. The static demo intentionally
renders the Dark proposal. There is no claim of a real wall-clock time or
persistent setting, and PIT ticks are not a substitute for civil time.

## Future ON. experiences

These are design placeholders, not applications or services:

- **Flow:** focused work and task navigation, with a directional original mark.
- **Pulse:** system/activity overview, using an original rhythm motif.
- **Spaces:** workspace organization with a distinct tiled motif.
- **Studio:** creation tools and previews, using an original frame motif.

Their names appear only as noninteractive text in the demo dock. Their eventual
APIs and process/resource boundaries require separate phases.

## Graphics API and safety contract

`graphics_init` copies a validated descriptor and borrows its mapped storage.
The caller must keep it mapped and writable for the context lifetime. Failed
initialization leaves the context inert; a zero-initialized context is inert.
All primitives accept `0xRRGGBB`; the backend packs channels according to the
framebuffer masks and performs unaligned byte stores using the actual pitch.
Padding bytes are untouched. An uninitialized C object is not a valid context.

Pixel and rectangle coordinates are signed 32-bit values; clipping uses 64-bit
arithmetic, with nonpositive sizes treated as no-ops. Outlines retain the true
rectangle edges rather than adding a false outline on a clipping boundary.
Lines are clipped before Bresenham rasterization so extreme endpoints do not
cause billions of off-screen iterations. Clipping uses bounded iteration and
unsigned 64-bit products for extreme int32 endpoint differences, without FP or
compiler runtime division helpers. Integer intersection rounding may move a
clipped endpoint by one pixel.

Text is a NUL-terminated borrowed byte string, transparent over the background,
with newline support, fixed six-by-eight glyph advance, integer scale 1–8, and
`?` substitution for unsupported bytes. Text beyond the right/bottom edge is
stopped; it does not wrap. Use rectangles/clear to erase an area before redraw.
There is no general text shaping, dynamic font loading, or retained scene API.

Graphics writes use the same pointer-aware pixel backend as terminal rendering.
Saved pointer pixels update when drawing occurs beneath it, so moving the arrow
restores the new scene. Direct writes outside this backend bypass that guarantee.
The demo suspends terminal painting and consumes keyboard events until Escape;
mouse movement and the timer continue. Escape redraws the saved terminal cells
and resumes its prompt without resetting memory allocators or hardware.

## Verification and next boundary

`make test-graphics` checks guards, padded/unaligned pitch, reversed RGB masks,
negative/extreme coordinates, outline interiors, line endpoints and octants,
text clipping/scales, and invalid descriptors using ASan/UBSan.
`make verify-graphics` injects the graphics command in BIOS and UEFI QEMU,
checks actual captured pixel colors for canvas/bar/panel/pixel drawing, saves
`graphics-demo.png`, exits with Escape, and verifies subsequent terminal input.
Existing input, pointer-restoration, framebuffer fallback, CPU fault, physical
memory, virtual memory, heap, and timer suites remain required regressions.
QEMU captures prove the implemented scene, not future desktop behavior.

The recommended next phase is a small retained UI/surface model with explicit
focus and input routing, theme tokens, and dirty-region redraw tests. Start with
one movable/resizable test window and keyboard-accessible controls inside the
existing foreground loop. Keep a terminal escape path. Scheduling, processes,
filesystems, GPU acceleration and complete desktop services need independent
plans and are outside Phase 1.

## Recorded Phase 1 results — 2026-10-13

The build and full regression command completed with exit status 0 on the
existing Apple M2 host toolchain, using QEMU TCG and the installed OVMF images.
There were 47 successful QEMU boots/checks: graphics (2), terminal (2), keyboard
(2), mouse (2), framebuffer including no-VGA fallback (2), BIOS boot smoke (1),
nine exception cases under both firmwares (18), three physical-memory modes at
two RAM sizes under both firmwares (12), virtual-memory checks (2), and heap
stress/rollback (4). Seven native ASan/UBSan suites passed: graphics, framebuffer,
terminal, keyboard, mouse, physical memory, and heap. The expanded focused
graphics tests also passed after adding all line octants, unsupported glyphs,
and clipped-outline interior assertions. No failures remained in these runs.

The graphics QEMU checks verified captured pixels and post-Escape terminal
commands under both BIOS and UEFI. The BIOS capture was also visually inspected.
Logs are `build/graphics-regressions.log` and
`build/graphics-focused-tests.log`; scene captures are
`build/graphics/bios/graphics-demo.png` and
`build/graphics/uefi/graphics-demo.png`. These generated artifacts are ignored
by Git and must be recreated for a new checkout.

No dependencies were installed, macOS settings changed, physical partitions
modified, or privileged operations performed. Theme adaptation, compositor
behavior, real hardware, and future ON. applications remain unimplemented or
unverified as described above.
