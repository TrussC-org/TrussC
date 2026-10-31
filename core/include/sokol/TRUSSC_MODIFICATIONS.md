# TrussC Modifications to Sokol Headers

This file documents all modifications made to sokol headers for TrussC.
When updating from upstream, these changes need to be re-applied.

Search for `tettou771` or `Modified by` or `[TrussC` to find all modified sections.

**Upstream base:** https://github.com/floooh/sokol commit `082152c` (2026-05-21)

**Shader compiler:** sokol-tools-bin commit
`11d0cf678105d614d675e6d9bd2aaf3eeff12f8c` (2026-08-29T14:15:01Z), the
owner-selected master HEAD used by CI. The host hashes are recorded in
`core/cmake/trussc_shaders.cmake`; update the commit and all five hashes together
and check the generated shaders against these vendored headers. This compiler
preserves the `metal_sim` and `wgsl` outputs required by TrussC.

---

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

**Note:** `sokol_imgui.h` has been moved to `addons/tcxImGui/src/sokol_imgui.h`. It carries TrussC patches (the `simgui_tc_*` multi-context API, marked `[TrussC]`) and one upstream cherry-pick, documented in [`addons/tcxImGui/src/imgui/TRUSSC_MODIFICATIONS.md`](../../../addons/tcxImGui/src/imgui/TRUSSC_MODIFICATIONS.md) together with the Dear ImGui patches. When updating sokol, update that copy as well (see How to Update Sokol below).

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
to `MultisampleResolve` (MSAA color) and `DontCare` (depth), so the loaded
content would be undefined.

**Changes (both in `_sg_mtl_begin_pass`, swapchain branch, marked `[TrussC]`):**
- MSAA color: when `action->depth.store_action == SG_STOREACTION_STORE` (the
  "this pass may be suspended/resumed" hint set by
  `trussc::ensureSwapchainPass()` / `resumeSwapchainPass()`), use
  `MTLStoreActionStoreAndMultisampleResolve` instead of `MultisampleResolve`.
- Depth: honor `action->depth.store_action` via `_sg_mtl_store_action()`
  instead of hardcoded `DontCare` (default `DONTCARE` keeps upstream behavior).

Passes without the hint (including every pass begun by plain sokol users and
TrussC's final per-frame pass) behave exactly as upstream.

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

WebGPU/Vulkan backends are untouched (their uniform buffers are baked into
bind groups / descriptor sets; growing them is much more invasive).

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

---

## sokol_glue.h

### 9. RGB10A2 Format Mapping

**Purpose:** Map the new `SAPP_PIXELFORMAT_RGB10A2` to `SG_PIXELFORMAT_RGB10A2` in the bridge between sokol_app and sokol_gfx.

---

## util/sokol_gl_tc.h (forked from upstream util/sokol_gl.h)

**Forked from upstream `sokol_gl.h`** and renamed to `sokol_gl_tc.h` to clearly separate from upstream. No other sokol header depends on sokol_gl, so this is a safe rename.

TrussC-specific public functions use the `sgl_tc_` prefix to distinguish from upstream `sgl_` API.

### 10. sg_append_buffer (multi-draw per frame)

**Purpose:** Replace `sg_update_buffer` with `sg_append_buffer` so that `_sgl_draw` can be called multiple times per frame. This is required for the FBO suspend/resume pattern -- drawing to an FBO mid-frame without losing the main pass commands.

**Changes:**
- `_sgl_draw()`: uses `sg_append_buffer` instead of `sg_update_buffer`
- Vertex buffer offsets tracked per draw call via `base_vertex`
- GPU buffer recreated on overflow if released
- **Single upload per frame (shared across layer draws):** `_sgl_draw()` appends
  the full vertex set to the GPU buffer only on the FIRST call of a frame and
  reuses the returned base offset for the remaining layer draws (cache keyed on
  `frame_id` / vertex count / `vbuf.id`, fields `upload_*`; invalidated by
  `sgl_tc_context_reset`). Without this, flushing N layers (e.g. one per deferred
  PBR/shader draw, see `flushDeferredShaderDraws`) re-appended V vertices N times,
  so `append_pos` reached N×V and `_sgl_grow_gpu_buffer` doubled the GPU buffer
  until allocation failed (Metal `id:52` / `METAL_CREATE_BUFFER_FAILED`), killing
  all sgl rendering. Regression test: `core/tests/sglLayerUpload` (dummy backend).

### 11. Auto-grow Buffers

**Purpose:** CPU buffers (vertices, uniforms, commands) automatically grow to 2x capacity on overflow. GPU vertex buffer is also recreated when it overflows. This eliminates rendering loss from buffer exhaustion.

**Changes:**
- `_sgl_next_vertex()`: auto-realloc to 2x on overflow
- `_sgl_next_uniform()`: same
- `_sgl_next_command()`: same
- `_sgl_grow_gpu_buffer()`: recreate the GPU buffer with larger capacity in-frame and retain the old buffer until retirement
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
