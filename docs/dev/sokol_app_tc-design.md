# sokol_app_tc.h — design

Full replacement of `sokol_app.h` for TrussC ("graduation"), written as a
standalone sokol-style C header with zero TrussC dependencies. End state:
`sokol_app.h` is deleted from the tree and `sokol_app_tc.h` drives every
platform. The header should be readable/adoptable by upstream sokol users —
same license (zlib), same conventions, modifications and lineage documented.

## Why this exists (and why it can work now)

sokol_app is single-window by design. floooh's own multi-window attempt
(PR #437, ~2020) implemented `sapp_window` handles on all desktop platforms
but was abandoned on two blockers:

1. **Old sokol_gfx context model** — one `sg_context` per window with
   `sg_activate_context()` juggling and a single `sg_commit()`; multiple
   default-passes per frame "required rewriting the entire garbage
   collection code".
2. **Metal occlusion stall** — `[CAMetalLayer nextDrawable]` blocks ~1s for
   a fully occluded window, freezing the shared frame loop. Called
   "unfixable" at the time.

Both are dead in 2026:

1. The 2024 sokol_gfx rework made swapchains **per-pass parameters**
   (`sg_begin_pass{ .swapchain = ... }`). One sg instance, N windows,
   multiple commits per frame — officially supported (multiwindow-glfw
   sample).
2. TrussC's multi-window branch proved a **per-window vsync tick** model on
   macOS: each window's CADisplayLink posts ticks to the main run loop; an
   occluded window's link suspends (plus an explicit occlusionState gate
   before drawable acquisition), so the stalling call is simply never made.
   Verified live: close / fully cover / other virtual desktop — the other
   window keeps running at full rate, mixed 120/60 Hz works.

So the pitch is: **PR #437's API shape, on post-2024 sokol_gfx, with an
event-driven per-window tick loop instead of a pull-everything frame loop.**

## Lineage, naming, license

- Fork lineage of `sokol_app.h` (zlib). Keep the `sapp_` prefix and the
  existing single-window API as a compatibility surface so diffs against
  upstream stay legible and the ~130 existing `sapp_*` call sites in TrussC
  keep compiling during migration.
- Multi-window API aligned with #437 where it had one: `sapp_window`
  handle (sokol-gfx-style `{uint32_t id}`), `sapp_window_desc`,
  `sapp_event.window` field, `sapp_window_*()` variants of geometry/dpi
  queries. Handle-less legacy calls mean "the main window".
- File lives at `core/include/sokol/sokol_app_tc.h`, implementation via
  `SOKOL_APP_TC_IMPL` in per-platform TUs (ObjC for mac/iOS, C++ allowed
  for win, C for linux/web/android) — same compilation model as today's
  `sokol_impl.mm` / `sokol_impl.cpp`.
- The three existing TrussC patches to sokol_app.h (D3D11 skip_present,
  emscripten canvas keyboard, RGB10A2) carry over as native features.

## Core model

**The app owns the OS run loop.** `sapp_run(&desc)` enters `[NSApp run]` /
Win32 `GetMessage` loop / X11 poll loop / emscripten RAF registration.
There is no polled frame loop.

**Every window is a `sapp_window`, including the main one.** `sapp_run`
creates window #0 from `sapp_desc` (compat); more via
`sapp_create_window(&(sapp_window_desc){...})`. Single-window platforms
(web/iOS/Android) are the degenerate case: window #0 only,
`sapp_create_window` returns an invalid handle + log. Same header, same
API, explicit platform gap.

**Per-window vsync tick source** (the load-bearing idea):

| platform | tick source | notes |
|---|---|---|
| macOS | one CADisplayLink per window (NSView-based, macOS 14+) | auto-suspends on occlusion; explicit occlusionState gate as belt-and-braces |
| Windows | DXGI frame-latency waitable object per swapchain | real vsync pacing (fixes the WM_TIMER ~64 Hz cap in the current win branch); waits serviced via MsgWaitForMultipleObjectsEx in the message loop |
| Linux | timer paced to RandR refresh rate (clock_nanosleep) | best-effort; GLX has no per-window vsync callback |
| web | requestAnimationFrame | inherently the single window's tick |
| iOS | CADisplayLink | same as macOS |
| Android | Choreographer | last to port; EGL lifecycle is the hard part |

**Callbacks.** App-wide, like sokol_app (not per-window like early #437 —
floooh himself pivoted away from that):

- `frame_cb()` — legacy, fires on the MAIN window's tick (compat).
- `window_frame_cb(sapp_window)` — fires on any window's tick, including
  the main one. TrussC migrates to this everywhere; frame_cb is sugar.
- `event_cb(const sapp_event*)` — unchanged signature, `ev->window` added.
- Tick coalescing is depth-1 per window by construction (run loop is the
  queue; CADisplayLink / waitable object / WM paced sources never stack).

**Occlusion / lifecycle events:** occluded window = ticks stop = fps 0.
`SAPP_EVENTTYPE_SUSPENDED/RESUMED` (or a new OCCLUDED/UNOCCLUDED pair)
notify the app so it can drop work; no API needed to "handle" the stall —
it cannot occur.

**Swapchain bridging:** `sokol_glue_tc.h` gains
`sg_swapchain sglue_window_swapchain(sapp_window)`; backend-specific
handle getters (`sapp_window_metal_next_drawable()`,
`sapp_window_d3d11_render_view()`, ...) mirror today's main-window ones.
Drawable acquisition happens lazily inside the window's tick, after the
visibility gate — never for a non-ticking window.

**Threading:** everything on the main thread, same as sokol_app. A slow
window's draw delays others (accepted; documented).

## What stays OUT of the header (TrussC-side)

- loop modes / fps gating / update-vs-draw split — TrussC decides what to
  do with each tick (including skipping present via the existing
  skip_present mechanism, generalized per window).
- WindowContext switching, CoreEvents fan-out, App/Node lifecycle, dt.
  **Per-window dt** is a TrussC fix: move `updateDeltaTime` /
  `lastUpdateCallTime` into WindowContext, measured per tick. (This also
  fixes the projector-slow-motion bug currently latent in secondary
  windows.)
- IME / text editing (TrussC's tcxIME is native and stays so); the header
  exposes raw native handles (`sapp_window_macos_get_window()` etc.) as
  escape hatches, same as sokol_app does today.

sokol_imgui note: upstream supports `SOKOL_IMGUI_NO_SOKOL_APP`; tcxImGui
can feed events manually, so imgui does not chain us to sokol_app.

## Migration phases (each independently shippable; breakage between phases OK)

- **P0 — header skeleton + macOS driver.** New run loop owns the app; main
  window becomes window #0; the existing tcWindowMac.mm secondary-window
  code moves INTO the header impl (de-TrussC-ified: C API out, ObjC in);
  tcWindowMac.mm shrinks to a thin adapter (Window ↔ sapp_window, events →
  CoreEvents). TrussC.h main loop consumes window ticks. Per-window dt.
  Gate: all examples + multiWindowExample + hot-reload run; occlusion gate
  holds; mixed-Hz holds.
- **P1 — Windows driver.** Cannibalize feat/multi-window-win (DXGI flip
  swapchain, WndProc, DPI v2 — audited good) + sokol_app's win32 keyboard
  tables; DXGI waitable ticks replace WM_TIMER (kills the 64 Hz cap); main
  window unified. The win branch is reference source, not merged as-is.
- **P2 — Linux driver.** X11 + GLX shared context (multiwindow-glfw
  pattern), timer-paced ticks.
- **P3 — web driver.** RAF tick, HTML5 event callbacks, WebGL context;
  port the canvas-keyboard patch natively.
- **P4 — iOS driver.** UIWindow + CAMetalLayer + CADisplayLink + touch.
- **P5 — Android driver.** NativeActivity + ALooper + Choreographer + EGL
  surface lifecycle. Last on purpose; needs a real-device verification
  plan (rotation, pause/resume, surface recreate).
- **P6 — delete sokol_app.h**, update TRUSSC_MODIFICATIONS.md, retire the
  fork patches.

Keyboard keycode tables, clipboard, drag&drop, fullscreen, mouse
lock/cursor images are lifted from sokol_app per platform as each driver
ports (zlib allows it; keep attribution). The sapp-inventory
(docs/dev/sapp-inventory.md) is the authoritative checklist of what each
phase must provide.

## Risks

1. Long-tail input correctness per platform (IME interplay, dead keys,
   clipboard formats, dnd) — mitigated by lifting sokol_app's tables and
   by the inventory's "unused features" list (don't port what nothing
   calls).
2. Android EGL lifecycle — the one genuinely hairy port; scheduled last.
3. Mobile/web CI blindness — mac/win/linux/web are in CI, iOS/Android are
   not; regressions there surface late. Device checklist required.
4. Hot-reload: C API is DLL-friendly; per-window state lives behind the
   header's own internal singleton — needs the same host/guest awareness
   audit as WindowContext had.
