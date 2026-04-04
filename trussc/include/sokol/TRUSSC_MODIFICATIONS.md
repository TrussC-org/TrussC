# TrussC Modifications to Sokol Headers

This file documents all modifications made to sokol headers for TrussC.
When updating from upstream, these changes need to be re-applied.

Search for `tettou771` or `Modified by` or `[TrussC` to find all modified sections.

**Upstream base:** https://github.com/floooh/sokol commit `672dea0` (2026-04-03)

---

## Directory Structure

```
sokol/
├── sokol_app.h          # Modified (8 patches)
├── sokol_gfx.h          # Untouched (direct copy from upstream)
├── sokol_glue.h         # Modified (1 patch)
├── sokol_log.h          # Untouched
├── sokol_audio.h        # Untouched
├── TRUSSC_MODIFICATIONS.md
└── util/
    └── sokol_gl_tc.h    # Forked from upstream util/sokol_gl.h + TrussC modifications
```

Matches upstream directory layout: core headers at root, utility headers in `util/`.

**Note:** `sokol_imgui.h` has been moved to `addons/tcxImGui/src/sokol_imgui.h`. When updating sokol, update that copy as well (see How to Update Sokol below).

---

## sokol_app.h

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

### 8. Android DPI Scale

**Purpose:** Use actual display density from `AConfiguration` instead of framebuffer/window ratio for `sapp_dpi_scale()`. Android baseline is 160dpi (mdpi), so `dpi_scale = density / 160`. This makes behavior consistent with macOS/Windows.

**Changes:**
- Added `#include <android/configuration.h>`
- In Android `_sapp_android_update_dimensions()`: query `AConfiguration_getDensity()` and compute scale from actual density, falling back to fb/win ratio if unavailable

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

1. Download new headers from https://github.com/floooh/sokol
2. **sokol_gfx.h** -- overwrite directly (no modifications)
3. **sokol_app.h** -- overwrite, then re-apply patches #1--#8 (search `tettou771` or `[TrussC`)
4. **sokol_glue.h** -- overwrite, then re-apply patch #9
5. **util/sokol_gl_tc.h** -- copy upstream `util/sokol_gl.h`, rename, then re-apply patches #10--#13 (search `[TrussC`)
6. **addons/tcxImGui/src/sokol_imgui.h** -- overwrite directly from upstream `util/sokol_imgui.h` (moved from trussc core to tcxImGui addon)
7. **Other headers** (sokol_log.h, etc.) -- overwrite directly
8. Update the **Upstream base** commit hash at the top of this file
9. Test on all platforms (macOS, Windows D3D11, Emscripten Web)

## Removed Patches

These were previously in sokol_app.h but have been moved to TrussC platform code:

- **SetForegroundWindow (Win32):** Moved to `trussc::bringWindowToFront()` in `tcPlatform.h` / per-platform implementations. Cross-platform (macOS, Windows, Linux).

## Author

All modifications by tettou771
