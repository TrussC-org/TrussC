# TrussC Modifications to Sokol Headers

This file documents all modifications made to sokol headers for TrussC.
When updating from upstream, these changes need to be re-applied.

Search for `tettou771` or `Modified by` or `[TrussC` to find all modified sections.

**Upstream base:** https://github.com/floooh/sokol commit `e7a48ed6` (2026-11-03); sokol_app_tc.h remains the permanent fork with only the compatibility sync below

**Shader compiler:** sokol-tools-bin commit
`11d0cf678105d614d675e6d9bd2aaf3eeff12f8c` (2026-08-29T14:15:01Z), the
owner-selected master HEAD used by CI. The host hashes are recorded in
`core/cmake/trussc_shaders.cmake`; update the commit and all five hashes together
and check the generated shaders against these vendored headers. This compiler
preserves the `metal_sim` and `wgsl` outputs required by TrussC.

---

## macOS Async dialog quit preflight (#1117, PR #1254)

`sapp_run` creates `_sapp_tc_application`, an `NSApplication` subclass whose
`terminate:` calls the existing `applicationShouldTerminate:` gate before
AppKit termination. Menu Quit/Cmd+Q and SIGTERM request quit through sokol's
deferred path. In `applicationWillFinishLaunching:`, after AppKit installs its
default handlers, the delegate replaces the `kCoreEventClass` /
`kAEQuitApplication` handler only when NSApp is `_sapp_tc_application`.
Dock Quit/ordinary quit Apple events call `[NSApp terminate:nil]` through this
handler, bypassing AppKit's earlier sheet dismissal/refusal in its default
Apple-event handler.
All use Runtime's existing exit veto and accepted-quit dialog cancellation
before AppKit can dismiss an alert or refuse a confirm/save sheet. A veto
returns without entering AppKit termination. After acceptance, AppKit's second
delegate query sees ordered quit and idempotent Runtime shutdown, so it neither
asks the veto twice nor repeats cancellation. Session-end Apple Events retain
their existing query/commit policy: the synchronous handler leaves the event
as `currentAppleEvent` while `terminate:` reads its quit reason. If termination
returns (veto or deferral), the handler sets `keyErrorNumber` to `userCanceledErr`
in a requested reply; a `typeNull` reply is left untouched. This also reports a
canceled session end to loginwindow. Last-window-close policy is unchanged.
`sharedApplication` returns an existing application unchanged, so app code that
creates `NSApplication` before `runApp` keeps a plain `NSApplication` and loses
this preflight. TrussC itself creates no application earlier; apps should not
either.
The override ignores nested `terminate:` calls while the outer call runs,
including sheet-close animations; its guard resets on veto so a later quit works.
A session-end request that arrives inside an exit handler's own event loop is
also ignored; if that ordinary quit is then vetoed, the session end is lost
(rare interleaving; documented, not guarded).

## Directory Structure

```
sokol/
├── sokol_app_tc.h       # TrussC-owned fork: full sapp_* implementation on
│                        #   every platform + multi-window API (sokol_app.h
│                        #   no longer exists in this tree — see below)
├── sokol_gfx.h          # Modified (Metal: swapchain store-action hint,
│                        #   uniform-buffer auto-grow, upload-only frame
│                        #   synchronization, mid-frame flush)
├── sokol_glue.h         # Modified (1 patch)
├── sokol_log.h          # Untouched
├── TRUSSC_MODIFICATIONS.md
└── util/
    └── sokol_gl_tc.h    # Forked from upstream util/sokol_gl.h + TrussC modifications
```

**Note:** `sokol_audio.h` is intentionally not included. TrussC's audio backend is
miniaudio (see `tc/sound/tcAudio_impl.cpp` for the rationale).

Matches upstream directory layout: core headers at root, utility headers in `util/`.

**Note:** `sokol_imgui.h` has been moved to `addons/tcxImGui/src/sokol_imgui.h`. It carries TrussC patches (the `simgui_tc_*` multi-context API, marked `[TrussC]`) on upstream `e7a48ed6` (#713 Q3/Q4), documented in [`addons/tcxImGui/src/imgui/TRUSSC_MODIFICATIONS.md`](../../../addons/tcxImGui/src/imgui/TRUSSC_MODIFICATIONS.md) together with the Dear ImGui patches. When updating sokol, update that copy as well (see How to Update Sokol below).

---

## sokol_app_tc.h (replaces sokol_app.h)

**sokol_app.h was deleted from this tree (2026-07, "sokol_app graduation").**
`sokol_app_tc.h` is a self-contained TrussC-owned fork that implements the
complete `sapp_*` public API (declarations absorbed verbatim, zlib license
kept) plus the multi-window API on every platform: macOS (multi-window,
NSWindow+CAMetalLayer+CADisplayLink), Windows (multi-window, HWND+D3D11+DXGI
waitable), desktop Linux (multi-window, X11+GLX), GLES3 Linux / Raspberry Pi
(single-window, X11+EGL), web (single-window, rAF + WebGPU/WebGL2), iOS
(single-window, UIWindowScene+Metal) and Android (single-window,
NativeActivity+EGL+Choreographer). Design + per-platform behavioral contracts:
`docs/dev/sokol_app_tc-design.md` and `docs/dev/sapp-*-impl-spec.md`.

It is NOT updated from upstream by 3-way merge — it is a permanent fork;
upstream sokol_app changes are cherry-picked deliberately when wanted.

The former sokol_app.h patches below are **native behavior** of
sokol_app_tc.h now (kept here as historical record of what differs from
upstream semantics):

### Win32 skipped-present input deadlines (#1012)

Positive-interval Win32 windows enable the shared input wake flag. The run loop
clears each window's delivery flag before draining messages, then resets only
the receiving windows' skipped-present deadlines before checking tick admission.
The existing frame latency credit checks still apply.

### Win32 keep-screen-on power-command refusal (#1041)

The Windows-only `sapp_tc_win32_set_keep_screen_on` seam stores one process-wide
flag, updated on the host main thread alongside `SetThreadExecutionState` and
cleared by the existing disable/normal-cleanup path. The `WM_SYSCOMMAND` handler
refuses `SC_SCREENSAVE` and `SC_MONITORPOWER` when that flag is set or any app
window is fullscreen. This applies to every TrussC HWND. Windows does not ask
TrussC when another app is in front, so screen-saver suppression has that limit.
The existing fullscreen policy is unchanged.

### X11 secondary swap interval pacing (#1263)

GLX and EGL secondary windows keep native GL swap interval 0 because they
share the main window's context. Store the requested interval separately
(nonpositive requests mean 1) and admit a tick/present on every Nth main
presentation credit or fallback refresh-timer tick. Interval 1 and the default
retain their existing cadence; main-window pacing is unchanged. A partial count
continues when the main is minimized/absent and the fallback timer takes over.
No new thread or timer is added. The extracted `windowTick/x11-pacing-test`
checks both backends; real gdm GLX and Raspberry Pi EGL verification uses the
gallery's `swapInterval 2` preset at approximately 30 fps on a 60 Hz display.

### Upstream e7a48ed6 compatibility sync (#713, Q5/Q6/Q7)

Rename `sapp_get_swapchain` to `sapp_acquire_swapchain` without an alias;
add `sapp_swapchain.invalid`, `SAPP_PIXELFORMAT_RGBA16F`, and rename
`SBGRA8` to `SBGR8A8` for the upstream glue. Unavailable native drawables
propagate `invalid`. D3D11 main and secondary depth storage uses
`D32_FLOAT_S8X24_UINT`, matching gfx's upstream depth format. RGB10A2,
skip-present and multi-window behavior stay in the fork. No new swapchain
options or Vulkan support are enabled. iOS already links CoreGraphics.

### X11 idle drawable and context during non-window GPU work (#239)

`sapp_tc_gl_activate_idle()` reuses the drawable created with the hidden X11
window; creating a second EGL surface for that window fails with EGL_BAD_ALLOC.
`_sapp_tc_x11_make_current(nullptr)` selects this idle drawable. Native close
selects the closing window before callbacks. Host-turn and cleanup entry, plus
shared queue and maintenance work, ensure a context is current before GL work,
selecting idle only when EGL/GLX reports no current context. This avoids idle
surface switches every frame. Window teardown and runtime shutdown still force
idle activation at disposal boundaries. Other backends keep the
internal ensure-context seam as a no-op, like idle activation.

### X11 refresh estimate ignores presents after a skipped tick (#998)

`_sapp_tc.refresh_period` (the EMA of present intervals, GLX and EGL) is fed
only by a present whose previous tick also presented (`last_tick_presented`
in the window record). After a skipped tick (`skip_present`, as setFps below
the display rate produces) the interval spans the app's own frame rate, not the
display; under a compositor whose swap does not block it used to teach the
estimate a 30 Hz display and slow the interval-zero secondaries to ~30 fps.
Tested by the throttled-main case of `core/tests/windowTick/x11-pacing-test`.

### macOS borderless windows can become key

A borderless NSWindow answers NO to `canBecomeKeyWindow`, so it never got
keyboard input (ESC in a clicked borderless window closed another window).
Every window is now created as `_sapp_tc_window`, an NSWindow subclass that
returns YES from `canBecomeKeyWindow` and `canBecomeMainWindow`. Titled windows
already returned YES, so decorated windows behave as before.

### macOS first click

`_sapp_tc_view` answers YES to `acceptsFirstMouse:`, so the click that activates
a window also reaches the app, as on Windows and Linux (upstream sokol_app and
GLFW do the same). This covers clicks between the app's own windows (a palette
click picks a color at once) and the click that brings the app back from
another app.

### macOS failed-startup termination notification (#239)

The failed-startup cleanup call supplies an NSApplicationWillTerminateNotification
with NSApp as its object, matching AppKit's nonnull delegate contract and avoiding
clang 21's `-Wnonnull` diagnostic. Normal termination and cleanup order are unchanged.

### macOS launch host-turn observer depth (#239)

The host-turn observer seeds its existing nesting depth from
CFRunLoopCopyCurrentMode at installation. `applicationDidFinishLaunching:` may
run before the first run-loop entry; assuming depth one then misclassified every
outer iteration as nested and prevented reload polling and process queues (MCP
included) from running. Installation inside a running loop still starts at one.
The returned mode is released after sampling. Native tick/init admission and
nested-loop arbitration are unchanged.

### Creation sizes in pixelPerfect mode (#1018)

The internal `_tc_pixel_width` / `_tc_pixel_height` descriptor fields preserve
positive framebuffer requests until the native target's render DPI is known.
Zero keeps the existing logical/default path. Win32 folds conversion into its
existing single DPI resize; X11 GLX and GLES3/EGL convert before XCreateWindow;
macOS uses one setContentSize before showing the window. The pure C/C++ helper
in `util/sokol_window_size.h` is also used by TrussC's resize dispatcher, so
macOS main and secondary retain the same double point sizes. Normal and
hot-reload launchers share descriptor construction; creation keeps the existing
handling of invalid sizes. Native pixel conversion checks the same logical-size
representability limit as the resize dispatcher before applying the request.

### Windows console UTF-8 input and output (#754)

`console_utf8` saves `orig_output_codepage` and `orig_input_codepage`, then
sets both console code pages to UTF-8 (output first). The two success flags
are independent: `_sapp_tc_win32_restore_console()` restores only the code
pages that were successfully set, each to its own original value. Preserve
this behavior and the descriptor comments when importing upstream changes.

Outside sokol, `TrussC.h`'s host-only `ConsoleCPCtrlGuard` saves both values
for Ctrl+C / Ctrl+Break / console close and one `atexit` hook. The guard's
destructor restores both values before disarming the exit fallback: on normal
shutdown this repeats sokol's restore; if an exception escapes `sapp_run()` and the caller
catches it, this is the only restore. `abort()`, `TerminateProcess` and external
kills cannot restore the console. Console input still uses `std::getline(std::cin)`.

### Windows secondary D3D11 borrowed views (#910)

`sapp_window_d3d11_render_view()`,
`sapp_window_d3d11_resolve_view()` and
`sapp_window_d3d11_depth_stencil_view()` return the window's existing
borrowed views even outside its native tick. These pointers remain valid until
that window's resize or destruction; the accessors acquire or retain nothing.
This permits lazy secondary capture during foreign-window execution. Metal
keeps its native-tick restriction for secondary drawable acquisition.

### D3D11 device loss (#258)

All three Present paths (main, secondary, and occlusion test) and ResizeBuffers
classify DEVICE_REMOVED / DEVICE_RESET using the shared, headless-testable
`util/sokol_d3d11_device_loss.h`. Other failures and DXGI status codes do not
trigger device loss. The first loss across the shared device logs the triggering
HRESULT and GetDeviceRemovedReason, then routes SAPP_EVENTTYPE_TC_DEVICE_LOST
(with `sapp_event.device_lost_reason`) to the main app callback. The latch is set
before callbacks so reentry and other windows cannot notify again.

TrussC offers `events().deviceLost`: cancellation keeps the main loop running;
the default logs an error and exits with code 1. After loss, windows use timer
pacing, no longer wait for dead frame-latency signals, and stop presenting or
resizing the failed swapchains. App callbacks continue if cancellation opts
into handling the failure. Resources are not recreated. The first tick still
updates dimensions and resizes before app initialization. Loss detected before
initialization is logged and latched, preserving the removal reason. Its pending
event is delivered once immediately after the init callback returns, before the
first frame; a default failure exit skips that frame. Pending state is cleared
before dispatch so reentry cannot deliver the event twice.

### 1. Skip Present (D3D11 flickering fix)

**Purpose:** Add `sapp_skip_present()` function to skip the next present call, fixing D3D11 flickering in event-driven rendering.

**Changes:**
- Added `skip_present` flag to `_sapp_t` struct
- Added `sapp_skip_present()` function declaration and implementation
- Added skip_present check to all backends: D3D11, WGL, Android EGL, Linux GLX, Linux EGL, WGPU, Vulkan
- Metal (macOS/iOS): not needed (no flickering observed)

### 2. Keyboard Events on Canvas (Emscripten)

**Purpose:** Register keyboard events on canvas element instead of window, allowing other page elements (like Monaco editor in TrussSketch) to handle keyboard events independently.

**Changes:**
- Changed `EMSCRIPTEN_EVENT_TARGET_WINDOW` to `_sapp.html5_canvas_selector` for keydown/keyup/keypress callbacks (register and unregister)
- Canvas must have `tabindex` attribute to receive focus

### 3. 10-bit Color Output (Metal / D3D11)

**Purpose:** Use RGB10A2 (10-bit per channel, 2-bit alpha) instead of BGRA8 for the default framebuffer on Metal and D3D11. Reduces color banding on 10-bit displays. Same 32-bit bandwidth as BGRA8, no performance impact. WebGL unchanged.

**Changes:**
- Added `SAPP_PIXELFORMAT_RGB10A2` to `sapp_pixel_format` enum
- macOS Metal: `CAMetalLayer.pixelFormat` = `MTLPixelFormatRGB10A2Unorm` (layer + MSAA texture)
- iOS Metal: same (CAMetalLayer + MSAA texture)
- D3D11: `DXGI_FORMAT_R10G10B10A2_UNORM` (swap chain, MSAA, resize)
- `sapp_color_format()` returns `SAPP_PIXELFORMAT_RGB10A2` for Metal/D3D11

### 4. iOS Custom View Controller + Orientation Control

**Purpose:** Replace `UIViewController` with custom `_sapp_ios_view_ctrl` to support runtime orientation changes via `sapp_ios_set_supported_orientations()`.

**Changes:**
- Added `_sapp_ios_view_ctrl` @interface/@implementation with `supportedInterfaceOrientations` override
- Added `supported_orientations` field to iOS state struct (default: `UIInterfaceOrientationMaskAll`)
- Changed `[[UIViewController alloc] init]` to `[[_sapp_ios_view_ctrl alloc] init]`
- Initialized `supported_orientations` in iOS setup
- Added `sapp_ios_set_supported_orientations()` public function (declaration + implementation)
- iOS 16+: calls `setNeedsUpdateOfSupportedInterfaceOrientations`

### 5. iOS Immersive Mode

**Purpose:** Support hiding status bar and home indicator for fullscreen experiences.

**Changes:**
- Added non-static `_sapp_ios_immersive_mode` global (accessible from `tcPlatform_ios.mm`)
- Added `prefersStatusBarHidden` and `prefersHomeIndicatorAutoHidden` overrides to `_sapp_ios_view_ctrl`

### 6. iOS View Bounds

**Purpose:** Use view bounds instead of `UIScreen.mainScreen.bounds` for window size calculation, consistent with macOS behavior and avoiding timing issues where screen bounds may not match the actual view layout.

**Changes:**
- In `_sapp_ios_update_dimensions()`: use `_sapp.ios.view.bounds` instead of `screen.bounds`
- Falls back to screen bounds if view is not yet laid out (width/height < 1.0)
- Pass `view_rect` instead of `screen_rect` to framebuffer update functions

### 7. iOS Drawable Dimension Readback

**Purpose:** Always read back actual Metal drawable dimensions to ensure `sapp_width()`/`sapp_height()` matches the Metal render pass, preventing scissor rect exceeding render pass bounds.

**Changes:**
- In `_sapp_ios_mtl_update_framebuffer_dimensions()`: after setting drawable size, read back `layer.drawableSize` and update `framebuffer_width`/`framebuffer_height`

### 8a. Android BACK Key No Shutdown

**Purpose:** Suppress upstream BACK-key shutdown. Upstream `_sapp_android_key_event` reacts to `AKEYCODE_BACK` by calling `_sapp_android_shutdown()`, which destroys the EGL surface and invokes `cleanup_cb` immediately. Under screen pinning (lock-task mode), `ANativeActivity_finish()` is blocked by the OS, so the process keeps running but the EGL context is gone — the app appears frozen with the last frame on screen. We consume the event without shutting down. Long-term fix: implement `SAPP_EVENTTYPE_QUIT_REQUESTED` for Android (already supported on macOS/Windows/Linux — see line 158 support table).

**Changes:**
- In `_sapp_android_key_event()`: replaced `_sapp_android_shutdown()` with a comment explaining the issue; just `return true` to consume the event

### 8. Android DPI Scale

**Purpose:** Use actual display density from `AConfiguration` instead of framebuffer/window ratio for `sapp_dpi_scale()`. Android baseline is 160dpi (mdpi), so `dpi_scale = density / 160`. This makes behavior consistent with macOS/Windows.

**Changes:**
- Added `#include <android/configuration.h>`
- In Android `_sapp_android_update_dimensions()`: query `AConfiguration_getDensity()` and compute scale from actual density, falling back to fb/win ratio if unavailable

### 14. CGBitmapInfo Enum Cast (Silence C++20 Warning)

**Purpose:** Silence `-Wdeprecated-enum-enum-conversion` introduced by C++20 in the macOS dock-tile image setup. The original code OR-ed two CoreGraphics constants of different enum types (`CGImageAlphaInfo` and `CGImageByteOrderInfo`).

**Changes:**
- In `_sapp_macos_set_dock_tile()` CGImage creation: explicitly cast both operands to `CGBitmapInfo` (the destination type, a `uint32_t` typedef) before the bitwise OR. No behavioral change.

---

## sokol_gfx.h

### Whole-attachment MSAA resolve with a scissored final draw (GL/GLES, #861)

**Purpose:** `glBlitFramebuffer` respects the draw scissor. Resolving after a
clipped draw must replace the whole destination, including pixels outside the
last draw's rectangle.

**Changes (marked `[TrussC]`, in `_sg_gl_end_render_pass`):**
- Disable `GL_SCISSOR_TEST` once before the first resolve blit and re-enable it
  after all color-attachment resolves, before GLES framebuffer invalidation.
  Passes without resolves do not change the enable state. The existing V3D
  pre-resolve flush stays before the first resolve.
- `_sg_gl_cache_t` does not cache scissor enable. `_sg_gl_reset_state_cache`
  enables it unconditionally, while begin-pass and apply-scissor only set the
  rectangle. Restoring the enabled state preserves this invariant and leaves
  the rectangle untouched; no cache field or pipeline invalidation is needed.
- This loop contains the only GL backend `glBlitFramebuffer` call. TrussC's
  GL readbacks use `glReadPixels` (not scissor-clipped); Fbo version copies and
  mip generation use destination-sized draw-pass scopes with full scissors.

Regression: `allCoreTests msaaResolveScissor --gpu-check` checks every pixel
after a 1x1-scissored draw at samples 1/4, with and without mips, plus copyTo,
subsequent draw clipping and empty clipped CLEAR passes. It fails on `ca0568bd`
and passes with this patch on GLCORE and GLES3.

### BC texture support on iOS (#645)

**Purpose:** Allow HAP playback on iOS GPUs that report BC texture support.

**Changes (marked `[TrussC]`, based on trussc-mac's experiment in #645):**
- `_sg_mtl_pixel_format()` maps BC1–BC7 on iOS as well as macOS. The iOS
  mappings are guarded by `@available(iOS 16.4, *)` so existing projects
  targeting 16.3 compile without availability warnings.
- `_sg_mtl_init_caps()` registers BC sampling/filtering on iOS only under
  `@available(iOS 16.4, *)` when `supportsBCTextureCompression` returns true.
  macOS keeps its existing BC support; iOS ETC/ASTC support is unchanged.
- The trusscli iOS preset now defaults to 16.4. Devices without BC support
  retain the existing HAP load failure. This is a local patch; no upstream
  PR is planned at this time.

### Swapchain pass store-action hint (Metal)

**Purpose:** Support TrussC's swapchain pass suspend/resume (Fbo / shadow /
reflection passes mid-frame, issue #191). A resumed swapchain pass uses
`SG_LOADACTION_LOAD`, but upstream hardcodes the Metal swapchain store actions
to `MultisampleResolve` (MSAA color) and `DontCare` (depth/stencil), so the loaded
content would be undefined.

**Changes (in `_sg_mtl_begin_render_pass`, swapchain branch, marked `[TrussC]`):**
- MSAA color: when `action->depth.store_action == SG_STOREACTION_STORE` (the
  "this pass may be suspended/resumed" hint that `RenderTarget::beginPass()`
  sets for a resumable pass, in `tc/app/tcGlobal.cpp`), use
  `MTLStoreActionStoreAndMultisampleResolve` instead of `MultisampleResolve`.
- Depth: honor `action->depth.store_action` via `_sg_mtl_store_action()`
  instead of hardcoded `DontCare` (default `DONTCARE` keeps upstream behavior).
- Stencil (#713 follow-up r2): likewise honor `action->stencil.store_action`.
  `RenderTarget::beginPass()` stores stencil on resumable depth/stencil targets
  and loads it on continuation, for both Fbo and swapchain passes. Initial
  passes retain their configured stencil clear action. This preserves masks
  across nested Fbo drawing and immediate `Texture::loadData()` copies.

Passes without the hint (including passes begun by plain sokol users) behave
exactly as upstream. GL and WebGPU already honor the stencil pass actions for
swapchains and Fbos; D3D11 clears only for CLEAR and retains storage at pass end.

### Mid-frame Metal flush for Fbo readback (#270)

**Purpose:** synchronous macOS/iOS Fbo readback must not end the frame, rewind
sokol_gl, or present a partially rendered swapchain.

**Changes (marked `[TrussC]`):**
- `sg_tc_mtl_flush()` requires no active pass, submits the current Metal
  command buffer and waits for completion, then clears its pointer. The next
  pass lazily creates/enqueues a fresh buffer, after the caller's readback blit.
  Other backends provide a no-op with the same between-passes precondition.
- Flush does not notify commit listeners, advance `frame_index`, collect
  deferred resources, rotate uniform slots, or reset `cur_ub_offset` / the
  uniform base pointer. Subsequent draws append uniforms in the same slot;
  existing uniform auto-grow and deferred retirement keep working.
- Reuses #599's `_sg_mtl_ensure_command_buffer()` and unconditional final
  commit; #270 adds no separate helper or conditional buffer-acquisition path.
  Each sokol command buffer acquires/releases exactly one permit. Flush waits for
  its submission (and earlier submissions on the same queue), so extra
  buffers cannot let a later frame overwrite an in-flight uniform slot.
  Flush never manually signals the semaphore. The separate, synchronous
  readback blit does not participate in sokol's semaphore.
- Unmanaged Sokol drawables retain the ordinary `pending_drawables` contract.
  TrussC window drawables use the owner-authorized records described below.
  A mid-frame flush never presents either kind. This applies to macOS/iOS.

The shared macOS/iOS Fbo caller (`platform/metal/tcFboReadback.h`, #641)
suspends an open swapchain pass and resumes with the existing LOAD/store
behavior after its same-queue blit has completed. Byte
readback rejects non-RGBA8 formats before touching render state. `copyTo()`
and `save()` remain synchronous through the same path.

Regression tests: `core/tests/mtlFlush` (dummy bookkeeping on Linux/Windows;
real offscreen Metal submission and uniform state on macOS), and
`core/tests/fboReadback --gpu-check` (TrussC window, including FullscreenShader,
float reads, copy and save). Mac display/Metal validation and post-implementation
review remain required by the Decision.

### Per-frame uniform buffer auto-grow (Metal)

**Purpose:** Upstream sizes the Metal per-frame uniform ring buffer once at
`sg_setup()` (`sg_desc.uniform_buffer_size`, default 4MB ≈ 8k draw calls) and
only `SOKOL_ASSERT`s on overflow — in release builds the assert compiles out
and uniforms are silently corrupted (flipped/black frames). TrussC grows the
ring on demand instead, so `WindowSettings::reserveUniformBuffer` becomes an
optional optimization rather than a correctness requirement.

**Changes (marked `[TrussC modification]`):**
- New `_SG_LOGITEM_XMACRO(METAL_UNIFORM_BUFFER_GROWN, ...)`.
- New `_sg_mtl_grow_uniform_buffer()` (before `_sg_mtl_apply_uniforms`): on
  overflow, allocate a doubled `MTLBuffer` for the CURRENT rotate slot, retire
  the old one through the normal deferred-release queue (draws already encoded
  captured the old binding, and the buffer outlives in-flight command buffers),
  reset `cur_ub_offset` to 0, and
  log a warning with old/new sizes.
- `_sg_mtl_apply_uniforms()`: overflow check + grow call before the memcpy.
- `_sg_mtl_begin_pass()`: at first pass of a frame, if this rotate slot's
  buffer is smaller than `ub_size` (grown while the slot was in flight),
  replace it — safe because the inflight-frame semaphore was just acquired.
- `#include <stdio.h>` next to `<string.h>` (snprintf for the log message).

**Pending-draw correction (#859):** `_sg_mtl_apply_uniforms()` binds the current
buffer AND offset only to the uploaded block's vertex, fragment or compute
slot. Pass start and growth no longer blanket-bind all slots to offset zero.
Already-applied blocks of the same draw keep their old buffer and offset;
the existing deferred-release queue retains each generation through submission,
including repeated growth within a pass/frame and prefix submissions. No copying
or new pool policy is needed. `_sg_mtl_resize_uniform_buffer()` allocates before
retiring/replacing storage, both on growth and rotate-slot catch-up. Allocation
or retirement-slot failure leaves storage/cursors intact and rejects the draw
(or catch-up pass) with the existing buffer-creation Error. Checked doubling
and aligned int cursor arithmetic reject unrepresentable uploads without writes
(`<limits.h>` added). Existing growth Warning and upload statistics remain.

`core/tests/metalUniformGrowth` runs the real PBR replay, forces six growths
between a draw's vertex and fragment blocks in three passes, and compares 18
complete readbacks with large-reserve references over six frames per phase.
Its private platform-TU wrapper also checks compute slots and allocation rollback.
`test_backend.py` compiles these exact production functions against GNU
Objective-C mocks on Linux; it verifies binding bytes and transactional failure,
not native Metal pixels or GPU lifetime. Air execution with `MTL_DEBUG_LAYER=1`
and base/head render-regression comparisons remain required.

WebGPU uniform growth is recorded in G9 below. Vulkan uniform handling remains upstream.

### Upload-only frame synchronization (Metal, #599 / #332)

**Purpose:** End frames on ticks that record offscreen work or uploads without
drawing the screen (#332). A commit without a pass must participate in the
in-flight semaphore before rotating uniform slots or collecting resources.

**Changes (marked `[TrussC modification]`):**
- `_sg_mtl_ensure_command_buffer()` shares the first-pass command-buffer creation,
  semaphore wait and completion-handler signal with `_sg_mtl_commit()`.
- `_sg_mtl_commit()` unconditionally ensures a command buffer and commits it,
  even when no pass created one or #270's flush cleared the previous buffer.
  Existing pass frames reuse their command buffer and semaphore slot.
  Retained/unretained-reference settings remain respected.
- Between ensuring the buffer and committing it, present unmanaged pending
  drawables and ready managed records once. Unready managed records survive
  foreign commits. Acquisition never depends on either list being nonempty.
  Garbage collection, uniform-ring rotation/reset, and frame advancement still
  belong only to the real commit; flush neither duplicates nor bypasses them.

### G5b. Managed drawable authorization seam (#821 / #864)

The existing `sg_tc_mtl_register_drawable`, `sg_tc_mtl_authorize_drawable`,
`sg_tc_mtl_cancel_drawables`, and `sg_tc_mtl_command_buffer` seam retains
attachment generations and allows presentation only after owner authorization.
Unready records survive foreign commits; prefix submissions never present.
Reapplied to e7a48ed6 together with G5/G5c. Mid-frame `sg_tc_mtl_flush` now
finishes an open upstream blit encoder before submitting and waiting. Prefix
submission and native capture command-buffer access finish that encoder too. G4's
shared timed acquisition and unconditional final commit are retained; new
upstream transfer paths use that same acquisition helper.

### G9. WebGPU uniform growth (#859)

Retain transactional uniform storage growth, generation-based uniform bind
group rebuilding, `WGPU_UNIFORM_BUFFER_GROWN` and `WGPU_UNIFORM_UPLOAD_FAILED`.
The e7a48ed6 merge also retains upstream d556445a's compact `bgl_index`
dynamic-offset mapping for sparse shader slots. The TrussC binding-number
snapshot continues to build each uniform bind group against its generation.

### G10. WebGPU bind-group failure handling

Retain the once-only `bindgroup_error_logged` diagnostic. Upstream 6a7d3ee7
now supplies the matching valid-only cache insertion and FAILED-group discard;
the duplicated local branches are dropped in favor of upstream's implementation.
G1/G2/G3/G3b/G6/G8 and boundary annotations (G11) are reapplied. G7's duplicate
scissor-disable/restore sequence is merged into upstream 37e21167; the marked
`_sg_gl_begin_resolve` helper remains for G8 and G7b's explicit swapchain resolve.

### Core integration with the new write/copy API (#713)

ShaderStream uploads all draws in one flush before binding its dedicated
transient buffer pair; later flushes select a separate pair. Persistent Texture
and bitmap-atlas updates use staging plus `sg_copy_buffer_to_image`, parking
and resuming target passes with the existing continuation policy. Lazy window
recordings open and consume their CPU prefix before the copy as well. The
once-per-device-frame image update guard is removed. WebGPU uses GPU upload
buffers with 256-byte row padding so its copies stay ordered between encoded
draws; upstream's CPU staging path uses QueueWriteTexture before submission.
Sokol's signed buffer-size and row/slice representation bounds are checked
before staging allocation. Immutable mip data is a contiguous mip-major blob.

The addon copy of `sokol_imgui.h` is unchanged in this core slice. Its old
update API calls require the approved Q3/Q4 migration in the following slice;
ImGui-dependent targets do not compile until that integration lands.


---

## sokol_glue.h

### 9. RGB10A2 Format Mapping

**Purpose:** Map the new `SAPP_PIXELFORMAT_RGB10A2` to `SG_PIXELFORMAT_RGB10A2` in the bridge between sokol_app and sokol_gfx.

---

## util/sokol_gl_tc.h (forked from upstream util/sokol_gl.h)

**Forked from upstream `sokol_gl.h`** and renamed to `sokol_gl_tc.h` to clearly separate from upstream. No other sokol header depends on sokol_gl, so this is a safe rename.

TrussC-specific public functions use the `sgl_tc_` prefix to distinguish from upstream `sgl_` API.

### 10. Write-transient buffers per flush (#713, Q1; replaces L10 append uploads)

**Purpose:** Preserve multiple flushes in one device frame (Fbo suspend/resume,
layer drawing and draw rewind) with upstream's write-transient API.

**Changes (marked `[TrussC fork]`):**
- Each changed vertex set uses a separate write-transient buffer. The per-context
  pool grows as flushes are needed and reuses its buffers after `sg_commit()`;
  sokol handles in-flight storage renaming. Context reset does not reset the
  device-frame cursor. Release and context destruction free the whole pool.
- Unchanged layer draws share one upload using the existing `upload_*` cache,
  avoiding the historical N-layers times V-vertices allocation blow-up.
- Buffers grow to the CPU vertex capacity when needed; offsets start at zero.
  Capacity is bounded by `INT_MAX / sizeof(_sgl_vertex_t)`; a doubled CPU
  capacity beyond that uses the requested count if it remains representable.
  Earlier flush buffers retain their contents until the device frame finishes.
- Depth-NONE pipelines disable depth writes and use compare ALWAYS to satisfy
  upstream validation. Core target pipelines already normalize the same way.

Regression: `sglLayerUpload`, `shaderStreamOverflow`, and
`textureUploadGuard --gpu-check` (nested Fbo suspend/resume and pixel checks).

### 11. Auto-grow Buffers

**Purpose:** CPU buffers (vertices, uniforms, commands) automatically grow to 2x capacity on overflow. GPU vertex buffer is also recreated when it overflows. This eliminates rendering loss from buffer exhaustion.

**Changes:**
- `_sgl_next_vertex()`: auto-realloc to 2x on overflow
- `_sgl_next_uniform()`: same
- `_sgl_next_command()`: same
- `_sgl_prepare_flush_buffer()`: resize an unused flush-pool slot to the CPU vertex capacity when needed; sokol retains its old backend storage through in-flight work
- Default initial capacities kept small (128 verts, 64 cmds) for low-memory targets

### 12. TrussC-specific Public API (`sgl_tc_` prefix)

These functions do NOT exist in upstream sokol_gl. They are TrussC additions.

| Function | Purpose |
|---|---|
| `sgl_tc_draw_rewind()` | Flush all layers then mark matrix dirty for reuse in same frame (FBO suspend/resume) |
| `sgl_tc_context_draw_rewind(ctx)` | Context-specific version of draw_rewind |
| `sgl_tc_context_reset(ctx)` | Reset command/vertex/uniform counters to zero (fast path between FBO draws on shared context) |
| `sgl_tc_context_release_buffers(ctx)` | Release CPU + GPU buffers to free idle memory (context shell and pipelines preserved) |
| `sgl_tc_context_ensure_buffers(ctx)` | Ensure buffers are allocated (no-op if already allocated, call before drawing after release) |
| `sgl_tc_reset_matrix_stacks()` | Drop every matrix stack of the current context back to depth 0 — called at frame end (`present()`, TrussC #232) and at FBO pass begin (`Fbo::beginInternal()`, TrussC #327) so an unpopped `pushMatrix()` can't carry sgl's stack depth into the next frame or FBO pass |

### 13. Float Vertex Colors (UBYTE4N -> FLOAT4)

**Purpose:** Replace 8-bit packed vertex colors with full float precision. The original UBYTE4N format caused visible artifacts in FBO accumulation techniques (trail/afterimage effects) because colors were quantized to 1/255 precision, preventing smooth fade-to-zero.

**Changes:**
- `_sgl_vertex_t.rgba`: `uint32_t` -> `float[4]`
- `_sgl_context_t.rgba`: `uint32_t` -> `float[4]`
- Vertex attribute format: `SG_VERTEXFORMAT_UBYTE4N` -> `SG_VERTEXFORMAT_FLOAT4`
- Removed `_sgl_pack_rgbab()` and `_sgl_pack_rgbaf()` (no longer needed)
- All `sgl_c*f/c*b/c1i` functions store floats directly
- Vertex size increased from 28 to 40 bytes (~1.4x)

### 14. Pool handling (#317)

**Purpose:** pool limits show up as id 0 that callers already handle, and the
context pool grows like the buffers in 11.

**Changes:**
- `_sgl_make_pipeline()`: all or nothing. `_sgl_init_pipeline()` stops at the
  first `sg_make_pipeline()` that returns an invalid id or a pipeline that is
  not `SG_RESOURCESTATE_VALID`; `_sgl_make_pipeline()` then destroys the sg
  pipelines already made, frees the sgl slot and returns `SG_INVALID_ID`.
- `_sgl_draw()`: skips a draw command whose sg pipeline id is 0 (recorded
  while an invalid or destroyed sgl pipeline was loaded) instead of passing
  it to `sg_apply_pipeline()`.
- `_sgl_grow_context_pool()`: when `_sgl_alloc_context()` finds no free slot,
  the context array and the pool bookkeeping double (up to
  `_SGL_MAX_POOL_SIZE - 1`), `_sgl.cur_ctx` is looked up again, and a warning
  with the old and new size is logged (`SGL_LOGITEM_CONTEXT_POOL_GROWN`,
  message built with `snprintf` through `_sgl_log_msg()`; `<stdio.h>` added).
  Contexts are referenced by id, so moving them is safe.
  Growth allocations are fallible: on allocation failure, temporary arrays
  are freed, the old pool and current context stay intact, and context
  creation returns id 0 so TrussC can warn once without retrying.
- `_SGL_DEFAULT_CONTEXT_POOL_SIZE`: 4 -> 8 (start size; reason next to it).

Regression test: `core/tests/sglPoolLimits` (dummy backend).

### Growable matrix stacks (#331)

Each context owns a heap array per matrix mode, starting with only the base
identity matrix. `sgl_push_matrix()` doubles its mode's capacity as needed,
copies the live matrices through the configured allocator; growth is silent.
Context destruction frees all three arrays. No fixed nesting limit
remains; underflow is unchanged. Allocation/representable-capacity failure
keeps the existing stack and reports out of memory plus `stack_overflow`.
The fixed pipeline stack still uses `stack_overflow`, so that public flag stays.
The shared screen/Fbo warning now describes dropped draws and these remaining
causes; target completion already reports and clears Fbo context errors.

### 15. Explicit custom-shader vertex layouts

**Purpose:** Custom sgl shaders can declare only the attributes they read,
avoiding GL warnings for the unused point-size input in premult/coverage draws.

**Changes:** `_sgl_init_pipeline()` preserves `sg_pipeline_desc.layout` when
the caller supplies a shader and `layout.attrs[0].format` is non-invalid. No
struct or API gains a field. The built-in shader, context defaults, and custom
shaders without an explicit layout keep the standard pos/uv/float color/psize
layout. Explicit layouts must describe the existing sgl vertex buffer; TrussC's
premult and coverage pipelines use attributes 0–2 at stride 40. Their shader
sources omit the unused point-size declaration on every generated backend.

Regression test: `core/tests/sglVertexLayout` (dummy backend, both constructors,
context defaults, all primitive variants, explicit offsets and legacy layouts).
GLCORE/GLES3 warning and pixel probes plus render-regression cover rendering.

---

## sokol_gfx.h

**Pool configuration (separate from the Metal patches above).** Pool sizes are configured at runtime in TrussC's `tcGlobal.cpp`:
- `shader_pool_size = 1024` (default 32) and `pipeline_pool_size = 1024`
  (default 64): fixed sizes, since these pools cannot grow (reasons next to
  the constants)
- `image_pool_size = 10000`
- `view_pool_size = 10000`
- `sampler_pool_size = 10000`

---

## How to Update Sokol

For files with TrussC patches (sokol_gfx.h, sokol_glue.h, util/sokol_gl_tc.h, and
addons/tcxImGui/src/sokol_imgui.h), **use `git merge-file` as a 3-way merge** instead of overwriting and manually
re-applying patches. This avoids slip bugs from manual patch transcription.

`sokol_app_tc.h` is NOT part of this process — it is a permanent fork, not a
patched upstream copy. Pull upstream sokol_app improvements into it by
deliberate cherry-pick (the per-platform impl specs in docs/dev/ map upstream
line ranges to the tc sections).

### Recommended: 3-way merge for patched files

```bash
# Stage three versions in /tmp:
#   BASE   — upstream sokol at the commit recorded above (pre-patch)
#   OURS   — current TrussC copy with all patches
#   THEIRS — new upstream master we want to update to
(cd <sokol-clone>; git show <base-sha>:sokol_glue.h) > /tmp/BASE.h
cp <sokol-clone>/sokol_glue.h /tmp/THEIRS.h
cp core/include/sokol/sokol_glue.h /tmp/OURS.h

# Run merge; conflicts (if any) end up as <<<<<<< / ======= / >>>>>>> blocks
# (git 2.43 rejects --diff-algorithm; drop it there — plain merge-file works)
git merge-file --diff-algorithm=histogram -p /tmp/OURS.h /tmp/BASE.h /tmp/THEIRS.h > /tmp/MERGED.h

# Resolve conflicts in /tmp/MERGED.h, then copy back
cp /tmp/MERGED.h core/include/sokol/sokol_glue.h
```

Repeat for `util/sokol_gl_tc.h` (use upstream `util/sokol_gl.h` as BASE and
THEIRS, since sokol_gl_tc.h is the renamed fork), and for
`addons/tcxImGui/src/sokol_imgui.h` (upstream `util/sokol_imgui.h`).

### Direct overwrite (for files without TrussC patches)

1. **Unmodified headers** (sokol_log.h, etc.) -- overwrite directly

### After updating

4. Update the **Upstream base** commit hash at the top of this file
5. Verify by searching for patch markers: `grep -rn "tettou771\|\[TrussC" core/include/sokol/` — the count should match what's documented per file
6. Test on all platforms (macOS, Windows D3D11, Emscripten Web, Linux, iOS, Android)

## Removed Patches

These were previously in sokol_app.h but have been moved to TrussC platform code:

- **SetForegroundWindow (Win32):** Moved to `trussc::bringWindowToFront()` in `tcPlatform.h` / per-platform implementations. Cross-platform (macOS, Windows, Linux).

## Author

All modifications by tettou771

## OS-initiated exit requests (#266, #239 C3)

- Runtime owns session query/commit and the retained reverse-attachment ending.
  Win32 user windows acknowledge session messages but do not end the process;
  a Runtime-owned hidden top-level HWND in tcPlatform_win.cpp receives broadcasts.
- macOS applicationShouldTerminate forwards logout/restart/shutdown query/commit;
  NSWorkspaceWillPowerOff commits directly. Busy ordinary termination is deferred.
  Accepted ordinary termination runs Runtime shutdown before NSTerminateNow so
  AppKit's remaining closes cannot release targets after sg/sgl shutdown.
- Busy main-stack commits call no App exit and arm no watchdog: Windows
  returns to WM_ENDSESSION; macOS calls _exit(0) before NSTerminateNow.
  Idle commits run synchronous normal ending and process termination under one
  OS-only deadline (default 3 seconds). No session freeze is needed. Backends
  contain no second session state machine or App-notification runner.
  After idle cleanup, Runtime closes the Logger file sink and flushes stdout /
  stderr before terminal process exit. Busy termination does not flush sinks.
- Shared runtime signal handling, Logger markers and MCP drain semantics are
  documented in [EXIT_PROTOCOL.md](../../../docs/EXIT_PROTOCOL.md).

## Mesa V3D pre-resolve submission and activation Notice (#858)

At GL backend setup, `GL_RENDERER` must start with the exact `V3D ` prefix
(including the space). This enables native names such as `V3D 4.2` and
`V3D 7.1.10.2`, while excluding Zink's wrapped Vulkan device name, for example
`zink Vulkan 1.3(V3D 4.2.14.0 (MESA_V3DV))`. Each resolving raster pass submits
with one `glFlush()` before its first resolve, regardless of draws or load
actions; all resolve blits still execute. Passes without resolves add no flush.

The Windows desktop-GL embedded loader registers `glGetString` and `glFlush`
in `_SG_GL_FUNCS` and defines `GL_RENDERER` as `0x1F01`. Its existing generated
pointer declarations and WGL/`opengl32.dll` lookup populate both functions.
The other GL paths use their platform GL/GLES headers. The round-2 checks
cover the Windows branch before/after, Linux GLX/EGL, both Android header
branches and Emscripten; macOS GL received a source/API audit without a native
SDK build. Full commands, scope and numerical reruns are in `REPORT.md`.

Round-2 reruns passed: **244 GPU stack runs / 104,432 assertions**, **16 raw
GLES3/GLCORE runs / 496 checks**, **8 regression self-checks / 128 scenes**,
and **16 header/logging check commands**, with zero failures. All **88** exact
workaround Notices occur in native-V3D processes, once per process; normal
and forced Zink emit none. Both native names remain positive controls.

The pre-resolve `glFlush()` emits the `[TrussC]` log item
`SG_LOGITEM_TRUSSC_GL_V3D_RESOLVE_FLUSH`. TrussC's existing `internal::sokolLog`
bridge handles it as a RenderTarget Notice once per process, guarded by an
atomic in `tcGlobal.cpp` (including backend reinitialization). Other informational
items retain their usual Verbose mapping. The bridge supplies the exact message
also in release builds, where sokol may omit message strings. No query API,
header-inline mutable state, or Mesa version check is added.

## `sapp_dpi_scale()` before `sapp_run()` (#841)

- The X11/EGL (Raspberry Pi), Android, iOS and Emscripten backends returned
  the zero-initialized `dpi_scale` before `sapp_run()`, so a headless app saw
  a DPI scale of 0 (and every font size mapped to one cache key). They now
  report 1.0 until the backend sets the real scale, like the X11/GLX and
  Windows backends. macOS keeps its documented deviation: the main screen's
  scale before `sapp_run()`.

## macOS secondary-window capture (#879)

- Secondary-window `CAMetalLayer`s now set `framebufferOnly = NO`, like the
  main window and iOS. Window capture blits from the drawable texture, which
  Metal does not allow for a framebuffer-only texture (validation aborts with
  "sourceTexture must not be a framebufferOnly texture").

### Target completion, capture prefixes and presentation (#821 PR 5, #864)

All changes carry `[TrussC]` markers; no upstream Sokol API is replaced.

The ten declarations `sgl_tc_context_target_owned`, `sgl_tc_context_complete`,
`sgl_tc_context_dependencies`, `sg_tc_submit_prefix`, `sg_tc_gl_resolve_swapchain`,
`sg_tc_mtl_command_buffer`, `sg_tc_mtl_register_drawable`,
`sg_tc_mtl_authorize_drawable`, `sg_tc_mtl_cancel_drawables`, and
`sapp_tc_gl_make_current` are each a TrussC-internal framework seam, not app API.

- `sgl_tc_context_target_owned()` opts framework-created contexts into explicit
  CPU completion. Every device commit invalidates their upload epoch/cache;
  `sgl_tc_context_complete()` rewinds only its argument. Ordinary sokol-gl
  contexts keep automatic commit rewind. Pool return and teardown complete
  only their own contexts. `sgl_tc_context_dependencies()` visits unconsumed
  pipeline/view/sampler references for typed resource retirement, including
  pipeline-to-shader and view-to-image dependencies in the host.
- Metal registration retains a drawable with a stable owner ID, resource
  generation and frame serial. Parking/end-pass leaves it unready. Only final
  owner completion authorizes that exact frame; stale/duplicate authorization
  fails. Ordinary commit presents/removes ready records once, retaining all
  unready owners. Close/discard cancels the owner's records. Unmanaged Sokol
  drawables retain automatic end-pass/commit behavior.
  Each record retains the acquired drawable/depth/MSAA generation so the saved
  swapchain descriptor remains valid across resize and foreign commits until
  owner presentation/cancellation. End-pass completion retains that immutable
  attachment array through native completion, also after owner cancellation
  with unretained-reference command buffers. Completion threads never access
  the mutable owner/presentation records; registration, authorization,
  cancellation and presentation scheduling stay on the renderer thread.
- `sg_tc_submit_prefix()` requires no open pass and submits without presenting,
  completion listeners, device-frame advancement or uniform/stream rotation.
  Metal commits its renderer buffer and preserves the uniform slot/offset;
  the next buffer acquires its own normal semaphore permit. GL uses `glFlush`;
  D3D11 uses the immediate context's `Flush`. Synchronous native capture first
  parks the global pass and submits this prefix. Metal async staging uses
  `sg_tc_mtl_command_buffer()` to encode its blit in the prefix, then submits
  before rendering can overwrite the source. `sg_tc_mtl_flush()` remains the
  synchronous between-pass submission used by Fbo readback.
  Capture command-buffer acquisition uses a bounded ten-second semaphore wait
  (#884), sharing command-buffer setup with the ordinary renderer throttle.
  The macOS synchronous window wrapper also bounds completion to ten seconds,
  owns late results and leaves the pass parked on failure rather than retrying
  the renderer throttle while returning an error.
- `sg_tc_gl_resolve_swapchain()` supplies an explicit destination to the shared
  end-pass resolve machinery. It shares native `V3D ` detection, the activation
  Notice and one pre-first-MSAA-resolve flush with attachment resolves. Scissor
  is disabled before full-extent blits and restored enabled; READ/DRAW bindings
  and source read-buffer selection are restored before return. No independent
  platform resolve or `glFinish` is added.
- `sapp_tc_gl_make_current()` restores the GLX drawable around nested window
  execution. The host coordinator parks the outgoing pass before selecting
  the incoming window and resumes its attachments with LOAD on the next draw.

Part 1 installs no completed-frame snapshot allocation or per-window shadow
allocation. `windowCaptureSource`/`finishWindowCaptureSource` and
`shadowStorageForWindow` are the respective host integration interfaces.
Presentation authorization follows final owner pass completion today; the Q2
copy will be inserted before that authorization. Existing backend capture
sources remain in use, including their existing presentation-time limitations.
Native Metal presentation/API validation, Windows debug-layer execution and
Pi V3D swapchain pixels remain required device checks.

### Checked WebGPU uniform growth (K9; #863, lane 4B; slimmed in #1097)

- `_sg_wgpu_uniform_reserve()` checks wide size arithmetic, the device buffer
  limit and uint32 dynamic offsets before allocating CPU/GPU replacements.
  Uploads retain the 64 KiB binding extent and device alignment (256 bytes on
  WebGPU). TrussC starts WebGPU at 1 MiB; explicit reservations still apply.
- Growth copies staging without changing any block offset, uploads the old
  buffer's recorded prefix, then releases the backend's reference. WebGPU
  encoders/commands and bind groups retain referenced resources through queue
  submission/completion; the backend never destroys a retired buffer.
  Old shader uniform groups (including idle shaders) are retired on growth;
  replacements are built lazily by generation, so uploads split
  between stages of one draw keep valid offsets into the copied data.
- Synchronous allocation failures leave the previous storage unchanged. One Error suppresses later
  uploads/draws/dispatches until commit; the next frame can retry. One
  `WGPU_UNIFORM_BUFFER_GROWN` warning is emitted per backend initialization.
- Asynchronous GPU out-of-memory or validation failures use WebGPU's own
  error reporting. There are no uniform error scopes, per-generation failure
  callbacks or last-good-buffer fallback (#1097, owner Decision 2026-10-29).
  A non-null error buffer/group can leave a frame dropped or corrupt; the
  backend keeps the current buffer and does not recover it next frame.
- The direct-mapped resource bind-group cache retains its default 1024 slots
  (`Bound (cache)`). Null creation is never cached; failed native
  groups release their pool slot. Disabled-cache failures also skip the draw.
  Normal collisions still evict without a Warning or loss counter.
- `core/tests/webgpuUniformGrowth` owns the Release browser pixel harness.
  It compares tiny/large reservations across raster and compute passes and
  frames; `run.py --build` captures the Chrome console verdict, including any
  uncaptured WebGPU error. Its native mock (run when emdawn's headers are
  found) checks growth, prefix preservation, lazy bind-group replacement,
  resource retention and synchronous allocation/arithmetic failures.

This patch does not change Metal or Vulkan, or TrussC's API. In this sokol fork
it adds two `sg_log_item` entries (`WGPU_UNIFORM_BUFFER_GROWN`,
`WGPU_UNIFORM_UPLOAD_FAILED`). The earlier note that WebGPU is untouched
applies only to the historical Metal-only patch.

## macOS multi-window file-drop registration (#239 D-min)

`_sapp_tc_mac_create_window_ex` registers file URLs on each content view,
including secondary views. The existing view handler sends the drop to its
own window receiver. Drop capacities and rejection behavior are unchanged.


## #239 — Growable drop paths and clipboard text (v0.8.0)

- `sokol_app_tc.h` removes `sapp_desc::max_dropped_files`,
  `max_dropped_file_path_length` and `clipboard_size`; no replacement caps.
- macOS, Windows, X11 and Web retain all drop paths in growable strings.
  Windows queries each UTF-16 path length with `DragQueryFileW` before allocation,
  then queries the UTF-8 conversion size. X11 reads the URI property in chunks
  and grows each decoded path from the actual input, including INCR drop transfers.
- Clipboard text grows with actual content on desktop, Web paste/copy and the
  iOS/Android internal clipboard. Web bridges allocate UTF-8 on the heap instead
  of the fixed WASM stack. Windows queries conversion lengths; X11
  supports full properties and INCR transfers in both directions.
- Allocation failure follows normal C++ allocation failure behavior; no OOM
  recovery machinery or preallocated slots are added.
- Verification: `tools/tests/test_drop_clipboard_unbounded.py` exercises the
  real X11 backend on Xvfb, Windows/Web production-source seams and old-cap
  mutants. Native macOS/Windows, browser and Raspberry Pi checks follow separately.

### Follow-up 1

- The Windows window-procedure boundary reports `bad_alloc` through
  `sapp_desc.logger` at Error level, identifying out-of-memory drop or
  event/clipboard handling failures. Direct clipboard API calls still propagate
  `bad_alloc` to their caller.
- Web paste/drop callbacks catch allocation failures and return their status;
  the JS bridge raises an Error identifying out of memory after C++ unwinds.
  Heap text is still freed in `finally`. No recovery or retry is added.
- Windows clipboard `GlobalAlloc` failure now logs an out-of-memory Error and returns instead of explicitly throwing from the C API.

### Follow-up 2

- The Web paste/drop bridges free their heap text through the exported
  `_sapp_tc_emsc_free` instead of `_free`, so a link that does not export
  `free` (no ASYNCIFY or FETCH, e.g. `core/tests/webDropSize`) no longer aborts.


## X11 clipboard TARGETS reply (#1023)

- The selection owner's reply to a `TARGETS` request now lists `{TARGETS, UTF8_STRING}`
  instead of `UTF8_STRING` alone, as ICCCM expects. `TIMESTAMP` is not advertised
  (it would need the acquisition time to be tracked). One change covers the GLX and
  GLES3/EGL backends because the code is shared.

## Window requested visibility (#1262)

The macOS section gains `sapp_query_desc()` (Win32 and X11 already had it), so launch code can read `_tc_start_hidden` on every desktop backend.

`sokol_app_tc.h` and `util/sokol_window_native.h` carry requested hide state
separately from OS occlusion. Main and secondary constructors skip their native
show operation for hidden creation. macOS selects its existing fallback tick
source immediately for hidden startup, defers fullscreen until shown, and returns
NO from `applicationShouldTerminateAfterLastWindowClosed:`; Runtime still owns
last-window and quit-on-close termination. Explicitly hidden children attach to their parent only while shown, so parent
ordering cannot expose them. The desktop visibility seam uses `orderOut:` / `orderFront:`,
`ShowWindow(SW_HIDE/SW_SHOWNA)`, or `XWithdrawWindow` / `XMapWindow`. X11 reapplies
fullscreen once on the first visible `MapNotify` after explicit show and sets the parent hint before hidden creation can return. Windows fullscreen style changes retain explicit
hidden state. Metadata and native drawable admission include explicit hiding even
when OS hidden signals are disabled. Unsupported platform seams do nothing;
TrussC's public API logs the Notice.

Follow-up 1: macOS launch activation and its synthetic focus event are skipped
when the launch window is requested hidden. Decoration changes preserve hiding;
shared Window controls ignore bringToFront while explicitly hidden. Windows
fullscreen entry and restore also clear WS_VISIBLE while requested hidden. Runtime
rechecks requested visibility after draw callbacks and cancels hidden Metal
drawables before taking the existing skip-present path. Unsupported visibility
Notices use runtime-owned OnceGates.

Follow-ups 2–4: unsupported visibility Notices are emitted on every call. X11
and Windows recheck requested hiding at the native swap boundary, covering all
client callbacks including afterFrame. Metal cancels hidden drawables immediately
before authorization/commit in present(). Runtime keeps its frame-accounting
check after present() and no longer checks visibility after draw callbacks.
macOS preserves the base OS-only suspension reset on trust opt-out; explicit
hides retain their matching recovery notification.

Follow-up 7: X11 hide withdraws the window through the WM. A late MapNotify
withdraws again if requested hidden; otherwise it requests one Runtime redraw
after mapping, so EVENT_DRIVEN content survives asynchronous WM presentation.
The internal redraw event coalesces into the existing counter without app events.

Follow-up 8: a per-window flag set by explicit show limits MapNotify fullscreen
replay to that show. Ordinary WM remaps retain the user's fullscreen choice and
still request a redraw. Withdrawal can discard user-set WM state such as Always
on Top or workspace placement; TrussC restores its requested fullscreen state,
and the public reference documents this limitation.

Follow-up 9: X11 serializes map/withdraw with one per-window operation state.
MapNotify and UnmapNotify complete only their matching operation and then apply
the latest requested visibility. This replaces the late-map re-withdraw special
case. Initial visible creation participates in the same gate. A 500 ms bound,
checked on native loop turns even when no window tick is due, samples server map
state and retries the latest request if a WM acknowledgment is missing. Ordinary
WM minimization does not trigger a map. Explicit-show fullscreen replay and the
visible-map redraw remain unchanged.

Follow-up 10: MapNotify also reconciles a requested-hidden window after the map
operation timed out. This withdraws late WM maps without changing unsolicited
UnmapNotify handling for requested-visible windows. The native probe checks
requested state immediately and native convergence after normal event dispatch.

[TrussC #1273] `sapp_tc_window_map_complete()` reports the X11 native map acknowledgment
(`native_visible`, with no pending visibility operation and no requested hide).
TrussC retains the hidden frame-position snapshot until this completes. Other
platforms share the definition and return their existing requested visibility.

## GLX input deadline reset (#1012)

- The shared X11 run loop clears input-delivery flags and resets the affected
  window's `earliest_next` after event delivery for GLX as well as EGL. Input
  during a skipped-present wait can therefore admit a tick in the same turn.
  The shared due-check already reads that deadline; EGL behavior is unchanged.
- `core/tests/windowTick/x11-pacing-test` compiles the production loop, tick and
  due-check for both backends with deterministic input during the wait, and
  requires a compiled GLX mutant without the deadline reset to fail.

## File-open requests (#1061)

[TrussC #1061] The macOS application delegate forwards `application:openURLs:`
and the legacy `application:openFiles:` to Runtime's process-level filesOpened
delivery. Only file URLs are accepted. Runtime queues launch requests until
the launch App completes setup, and delivers later requests to all listeners.
Paths use the same OS/available-memory and container representation bounds as
file drops; no shell invocation or file-content reading occurs in the bridge.

## iOS process exit (#1304)

[TrussC #1304] After a frame returns with an accepted quit, run the existing
cleanup callback before releasing iOS native state and shared backend state,
matching `applicationWillTerminate`. Then call `exit` with the requested process
code, so programmatic quitting completes instead of leaving the process alive.
