# TrussC Modifications to Sokol Headers

This file documents all modifications made to sokol headers for TrussC.
When updating from upstream, these changes need to be re-applied.

Search for `tettou771` or `Modified by` or `[TrussC` to find all modified sections.

**Upstream base:** https://github.com/floooh/sokol commit `082152c` (2026-05-21)

---

## Directory Structure

```
sokol/
├── sokol_app_tc.h       # TrussC-owned fork: full sapp_* implementation on
│                        #   every platform + multi-window API (sokol_app.h
│                        #   no longer exists in this tree — see below)
├── sokol_gfx.h          # Modified (2 patches: swapchain store-action hint +
│                        #   uniform-buffer auto-grow, both Metal)
├── sokol_glue.h         # Modified (1 patch)
├── sokol_log.h          # Untouched
├── TRUSSC_MODIFICATIONS.md
└── util/
    └── sokol_gl_tc.h    # Forked from upstream util/sokol_gl.h + TrussC modifications
```

**Note:** `sokol_audio.h` is intentionally not included. TrussC's audio backend is
miniaudio (see `tc/sound/tcAudio_impl.cpp` for the rationale).

Matches upstream directory layout: core headers at root, utility headers in `util/`.

**Note:** `sokol_imgui.h` has been moved to `addons/tcxImGui/src/sokol_imgui.h`. When updating sokol, update that copy as well (see How to Update Sokol below).

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
  reset `cur_ub_offset` to 0, rebind via `_sg_mtl_bind_uniform_buffers()`, and
  log a warning with old/new sizes.
- `_sg_mtl_apply_uniforms()`: overflow check + grow call before the memcpy.
- `_sg_mtl_begin_pass()`: at first pass of a frame, if this rotate slot's
  buffer is smaller than `ub_size` (grown while the slot was in flight),
  replace it — safe because the inflight-frame semaphore was just acquired.
- `#include <stdio.h>` next to `<string.h>` (snprintf for the log message).

WebGPU/Vulkan backends are untouched (their uniform buffers are baked into
bind groups / descriptor sets; growing them is much more invasive).

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
- `_sgl_grow_gpu_buffer()`: destroy and recreate GPU buffer with larger capacity
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

### 13. Float Vertex Colors (UBYTE4N -> FLOAT4)

**Purpose:** Replace 8-bit packed vertex colors with full float precision. The original UBYTE4N format caused visible artifacts in FBO accumulation techniques (trail/afterimage effects) because colors were quantized to 1/255 precision, preventing smooth fade-to-zero.

**Changes:**
- `_sgl_vertex_t.rgba`: `uint32_t` -> `float[4]`
- `_sgl_context_t.rgba`: `uint32_t` -> `float[4]`
- Vertex attribute format: `SG_VERTEXFORMAT_UBYTE4N` -> `SG_VERTEXFORMAT_FLOAT4`
- Removed `_sgl_pack_rgbab()` and `_sgl_pack_rgbaf()` (no longer needed)
- All `sgl_c*f/c*b/c1i` functions store floats directly
- Vertex size increased from 28 to 40 bytes (~1.4x)

---

## sokol_gfx.h

**Untouched.** Pool sizes are configured at runtime in TrussC's `tcGlobal.cpp`:
- `pipeline_pool_size = 256` (default 64)
- `image_pool_size = 10000`
- `view_pool_size = 10000`
- `sampler_pool_size = 10000`

---

## How to Update Sokol

For files with TrussC patches (sokol_glue.h, util/sokol_gl_tc.h),
**use `git merge-file` as a 3-way merge** instead of overwriting and manually
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
git merge-file --diff-algorithm=histogram -p /tmp/OURS.h /tmp/BASE.h /tmp/THEIRS.h > /tmp/MERGED.h

# Resolve conflicts in /tmp/MERGED.h, then copy back
cp /tmp/MERGED.h core/include/sokol/sokol_glue.h
```

Repeat for `util/sokol_gl_tc.h` (use upstream `util/sokol_gl.h` as BASE and
THEIRS, since sokol_gl_tc.h is the renamed fork).

### Direct overwrite (for files without TrussC patches)

1. **sokol_gfx.h** -- overwrite directly (no modifications)
2. **addons/tcxImGui/src/sokol_imgui.h** -- overwrite directly from upstream `util/sokol_imgui.h`
3. **Other headers** (sokol_log.h, etc.) -- overwrite directly

### After updating

4. Update the **Upstream base** commit hash at the top of this file
5. Verify by searching for patch markers: `grep -rn "tettou771\|\[TrussC" core/include/sokol/` — the count should match what's documented per file
6. Test on all platforms (macOS, Windows D3D11, Emscripten Web, Linux, iOS, Android)

## Removed Patches

These were previously in sokol_app.h but have been moved to TrussC platform code:

- **SetForegroundWindow (Win32):** Moved to `trussc::bringWindowToFront()` in `tcPlatform.h` / per-platform implementations. Cross-platform (macOS, Windows, Linux).

## Author

All modifications by tettou771
