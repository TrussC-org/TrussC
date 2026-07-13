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
  - **P0a DONE (commit 6d73d9a2):** secondary windows live in the header
    behind the C API; tcWindowMac.mm is the thin adapter.
  - **P0b DONE:** the header implements sokol_app.h's public API on macOS
    (`sapp_run` owns `[NSApp run]`; the main window is window #0 on the
    same view/delegate/tick machinery). sokol_app.h is included
    declarations-only on macOS — its mac implementation is no longer
    compiled. Key decisions: TrussC.h / tcHotReloadHost.h stayed UNTOUCHED
    (the ~52 sapp_* call sites now resolve to the header's shims); the main
    window keeps upstream's occlusion model (display link + ~60Hz fallback
    NSTimer, frame_cb never stops) while secondary windows keep the
    tick-gate model; drawable acquisition is lazy in sapp_get_swapchain()
    with per-tick caching (repeated calls per frame are now safe, unlike
    upstream); quit = upstream's cancellable performClose dance, plus
    `[NSApp terminate:]` on main-window close so the app exits even with
    secondary windows open; RGB10A2 + framebufferOnly=false + Cmd-keyup
    monitor + Cmd+V paste event + cursor table + drag&drop ported per
    docs/dev/sapp-mac-impl-spec.md (the implementation contract).
    Deliberate deviation: sapp_dpi_scale() before sapp_run() returns the
    main screen scale (upstream: 0).
- **P1 — Windows driver.** Cannibalize feat/multi-window-win (DXGI flip
  swapchain, WndProc, DPI v2 — audited good) + sokol_app's win32 keyboard
  tables; DXGI waitable ticks replace WM_TIMER (kills the 64 Hz cap); main
  window unified. The win branch is reference source, not merged as-is.
  - **P1 code COMPLETE — full CI green on all platforms (mac/win/linux/
    web/android incl. 108 example builds, addon tests, core tests 3/3 and
    the HotReload smoke test on Windows):** the header implements
    the sapp_* API on Windows (win32 + D3D11), mirroring the mac P0b shim
    strategy — platform/win/sokol_impl.cpp includes sokol_app.h
    declarations-only, TrussC.h untouched; NEW tcWindowWin.cpp adapter
    mirrors tcWindowMac.mm (BGRA8 secondary swapchains, persistent D3D11
    views instead of per-tick drawables). Implementation contract:
    docs/dev/sapp-win32-impl-spec.md + docs/dev/multi-window-win-audit.md.
    Key decisions: every swapchain (main RGB10A2, secondary BGRA8) is
    flip-discard with DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT
    (flag carried through every ResizeBuffers; RTV-unbind + Flush before
    resizing); the run loop blocks in MsgWaitForMultipleObjectsEx on the
    per-window waitables — a tick that ends without a Present (skip_present,
    occluded, minimized) holds its waitable "credit" and is timer-re-paced
    until the next real Present, so neither busy-spin nor waitable
    starvation can occur. The waitable is an AUTO-RESET object with exactly
    ONE consumer: the run loop's MsgWait consumes the signal and records it
    in the window's `waitable_ready` flag (handle→window map; MsgWait
    resets only the one satisfied handle), and the due-check reads that
    flag — never re-wait the handle (the original code did, stalling
    rendering after the first frame; caught on real hardware, cb28ad56 —
    CI is compile+headless only, the render loop needs a machine). During
    a modal size/move loop the blocked run loop's role passes to the
    WM_TIMER handler, which polls the waitable itself. Win11 DWM note:
    Present never returns DXGI_STATUS_OCCLUDED there, so the SUSPENDED/
    RESUMED occlusion path is dormant on modern Windows (hidden windows
    pause naturally when their waitable goes quiet); kept for other
    configs; resize coalesced per tick (never in WM_SIZE);
    WM_ENTERSIZEMOVE arms a ~64Hz WM_TIMER that keeps ALL windows ticking
    through the modal drag; scancode-indexed keytable lifted verbatim
    (fixes the win branch's key=vk gap) + WM_CHAR with surrogate pairs;
    quit = WM_CLOSE dance + PostQuitMessage; skip_present HONORED on
    Present (TrussC flicker patch); per-monitor-v2 DPI with the
    create-then-resize dance; DXGI_PRESENT_TEST occlusion poll for
    secondary windows (SUSPENDED/RESUMED). Runtime VERIFIED on real Win11
    hardware 2026-07-13: ~60fps, CPU <1%, resize/fullscreen/minimize/
    second-window/clean-exit/drag-render all pass.
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
