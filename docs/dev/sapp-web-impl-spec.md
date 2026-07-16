# sokol_app.h Emscripten (Web) Backend — Behavioral Specification

Implementation contract for replacing sokol_app.h's Emscripten/Web main-window /
app-lifecycle with `sokol_app_tc.h` (project "P3 Web driver"). All line references
are into `core/include/sokol/sokol_app.h` (14602 lines, this worktree). Focus:
`_SAPP_EMSCRIPTEN` with BOTH graphics backends — `SOKOL_WGPU` (TrussC web default)
and `SOKOL_GLES3` (WebGL2, opt-in).

This is the fourth sibling of `sapp-mac-impl-spec.md` (event-driven `[NSApp run]`),
`sapp-win32-impl-spec.md` (owned PeekMessage loop) and `sapp-x11-impl-spec.md`
(owned XPending loop). **The web backend is unlike all three: there is NO owned
loop.** The browser owns the loop. `_sapp_emsc_run()` sets everything up, hands a
per-frame callback to `emscripten_request_animation_frame_loop()` (or
`emscripten_set_main_loop()`), and **returns immediately**. All rendering and
event dispatch happen later, re-entrant, from browser-driven callbacks. There is
no blocking swap, no timer, no message pump, no `sleep`. The whole backend is a
collection of C callbacks + `EM_JS` JavaScript shims registered against the
canvas / window / document.

**Which #ifdef path is live in TrussC.** `core/CMakeLists.txt` compiles the web
target with **`SOKOL_WGPU` by default** and `SOKOL_GLES3` as an option (see §13).
sokol_app resolves the API at 2330–2335: `__EMSCRIPTEN__` → `_SAPP_EMSCRIPTEN`,
then requires `SOKOL_GLES3 || SOKOL_WGPU`. So on TrussC/web the live default path
is **`SOKOL_WGPU`**; **`SOKOL_GLES3`/WebGL2** is the fallback. This document
documents BOTH because the port must keep both working. Vulkan/Metal/D3D11 forks
inside shared code are dead on web and marked inline.

**Single-window is baked in harder than any native backend.** The web "window" is
one HTML `<canvas>` element, addressed by a CSS selector string
`_sapp.html5_canvas_selector` (default `"#canvas"`, 3324). There is no window
object, no NSWindow/HWND/Xwindow XID, no window list. **A second window is not
representable** in this backend at all (see §12, §14). The multiwindow port must
treat web as strictly single-window (window #0) and stub/no-op the plural-window
APIs.

---

## 0. Global state touched (the shared contract)

Single global `static _sapp_t _sapp;` embeds `_sapp_emsc_t emsc;` (3302–3303,
only under `_SAPP_EMSCRIPTEN`) and, under `SOKOL_WGPU`, `_sapp_wgpu_t wgpu;`
(3292–3294, compiled on ALL platforms that select WGPU, not web-specific).

### `_sapp_emsc_t` (2881–2887) — the ENTIRE web-specific per-app state
```c
typedef struct {
    bool mouse_lock_requested;   // deferred pointer-lock (§8): request must fire from inside a user-gesture event
    uint16_t mouse_buttons;      // last-seen browser button bitmask (bit0=L, bit1=R, bit2=M) — see §4 mods
} _sapp_emsc_t;
```
That is the whole struct. **Everything else the web backend needs lives in
JavaScript-side globals** (`Module.sapp_emsc_target`, `Module.sokol_*`,
`Module.__sapp_custom_cursors`, `specialHTMLTargets`) reachable only from `EM_JS`
blocks, NOT in C. This is the biggest structural surprise vs native: a large part
of the "state" is in JS closures on `Module`, not in `_sapp`.

### `_sapp_wgpu_t` (2720–2733, `SOKOL_WGPU`) — shared with native WGPU
```c
typedef struct {
    WGPUInstance instance;
    WGPUAdapter adapter;
    WGPUDevice device;
    WGPUSurface surface;
    WGPUTextureFormat render_format;      // picked from surface caps (RGBA8Unorm / BGRA8Unorm)
    WGPUTexture msaa_tex;                 // only if sample_count > 1
    WGPUTextureView msaa_view;
    WGPUTexture depth_stencil_tex;        // Depth32FloatStencil8
    WGPUTextureView depth_stencil_view;
    WGPUTextureView swapchain_view;       // per-frame; acquired in sapp_get_swapchain, released in _sapp_wgpu_frame
    bool init_done;                       // GATE: frame callbacks no-op until async device+swapchain ready (§3)
} _sapp_wgpu_t;
```

### `_sapp.gl.framebuffer` (`_sapp_gl_t`, `_SAPP_ANY_GL` only)
The default framebuffer object id captured after
`emscripten_webgl_make_context_current` (§2). On WebGL2 this is **0** (the default
framebuffer). `sapp_get_swapchain()` reports it as `res.gl.framebuffer` (§11).

Cross-platform `_sapp` fields the web path reads/writes: `desc`, `valid`,
`first_frame`, `quit_requested`, `quit_ordered`, `fullscreen`,
`window_width/height`, `framebuffer_width/height`, `dpi_scale`, `sample_count`,
`timing`, `mouse.*` (x/y/dx/dy/locked/shown/pos_valid/current_cursor),
`clipboard.*`, `drop.*`, `event`, `custom_cursor_bound[]`, `window_title`,
`html5_ask_leave_site`, `onscreen_keyboard_shown` (always false on web),
`skip_present` (TrussC patch — but **NOT USED on web WGPU**, see §3/§14), and
`html5_canvas_selector[_SAPP_MAX_TITLE_LENGTH]` (3324, the canvas CSS selector,
filled from `desc.html5.canvas_selector`).

### `_sapp_init_state(desc)` / `_sapp_desc_defaults` (relevant subset, ~3520–3574)
`_sapp_def(val,def)` = "use def if val==0" (2291). Web-relevant defaults:
`sample_count→1` (3527), `swap_interval→1` (3528, **ignored on web** — the browser
paces via rAF), `html5.canvas_selector→"#canvas"` (3546), `clipboard_size→8192`
(3547), `max_dropped_files→1` (3548), `max_dropped_file_path_length→2048` (3549),
`window_title→"sokol"` (3550). The canvas selector string is copied into
`_sapp.html5_canvas_selector` and the desc pointer re-pointed at that stable
buffer (3573–3574). `_SAPP_FALLBACK_DEFAULT_WINDOW_WIDTH/HEIGHT = 640/480`
(2301–2302), used only in the `canvas_resize` path (§9).

---

## 1. Entry / run flow — there is NO loop

### The include + API-select block (2330–2335, 2412–2417, 2448–2453)
- **API select (2330–2335):** `__EMSCRIPTEN__` → `#define _SAPP_EMSCRIPTEN (1)`,
  then `#error` unless `SOKOL_GLES3 || SOKOL_WGPU`.
- **`_SAPP_ANY_GL` (2381–2383):** defined for `SOKOL_GLCORE || SOKOL_GLES3`. On web
  it's set only in the GLES3 build; **NOT set for WGPU**. Gates `_sapp.gl.*` and
  the `res.gl.framebuffer` swapchain field.
- **WGPU header (2412–2417):** `#if defined(SOKOL_WGPU)` → `#include <webgpu/webgpu.h>`,
  and `#if !defined(__EMSCRIPTEN__)` → `#define _SAPP_WGPU_HAS_WAIT (1)`. **On web
  `_SAPP_WGPU_HAS_WAIT` is NOT defined** → the whole WGPU init is async-callback
  driven (§3). This single macro forks nearly all WGPU control flow between
  native (synchronous `wgpuInstanceWaitAny`) and web (chained callbacks).
- **Emscripten includes (2448–2453):** `#if defined(SOKOL_GLES3) #include <GLES3/gl3.h>`
  then unconditionally `#include <emscripten/emscripten.h>` and
  `#include <emscripten/html5.h>`. **Note: no `<GLES3/gl3.h>` for the WGPU build.**

### `_sapp_emsc_run(const sapp_desc* desc)` (8064–8102) — setup then return
1. `_sapp_init_state(desc)`.
2. **`_sapp.fullscreen = false`** (8066) — hard override of user's `desc.fullscreen`
   with comment "can't start in fullscreen on the web!" (browsers only allow
   fullscreen from a user gesture). This is web-specific and must be kept.
3. `document_title = desc->html5.update_document_title ? _sapp.window_title : 0`
   (8067) → `sapp_js_init(_sapp.html5_canvas_selector, document_title)` (8068,
   §1-JSinit) — resolves the canvas element in JS and optionally sets
   `document.title`.
4. **Canvas size determination (8069–8083):**
   - If `desc.html5.canvas_resize` (8070–8072): the app OWNS the canvas size —
     `w/h = _sapp_def(desc.width, 640) / _sapp_def(desc.height, 480)`. No resize
     listener registered.
   - Else (8073–8076): the canvas size is TRACKED —
     `emscripten_get_element_css_size(selector, &w, &h)` reads the CSS size, and
     `emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, ...,
     _sapp_emsc_size_changed)` registers the window-resize handler (§10).
   - `if (desc.high_dpi) dpi_scale = emscripten_get_device_pixel_ratio()` (8077–8079)
     — else `dpi_scale` stays 1.0. **`high_dpi=false` genuinely disables DPR
     scaling on web** (unlike X11 where it's unconditional).
   - `window_width/height = round(w/h)`; `framebuffer_width/height = round(w/h * dpi_scale)`
     (8080–8083). `_sapp_roundf_gzero` = round, clamped to ≥ 0.
   - `emscripten_set_canvas_element_size(selector, framebuffer_width, framebuffer_height)`
     (8084) — sets the canvas **backing-store (drawing-buffer) pixel size** to the
     framebuffer size. CSS size stays whatever the page set.
5. **Renderer bring-up (8085–8089):** `#if SOKOL_GLES3 _sapp_emsc_webgl_init()`
   (§2) `#elif SOKOL_WGPU _sapp_wgpu_init()` (§3). NOTE: on WGPU this only KICKS OFF
   async adapter/device request; the device is not ready yet when run() returns.
6. `_sapp.valid = true` (8090).
7. `_sapp_emsc_register_eventhandlers()` (8091, §4/§5/§8) — registers every DOM
   callback. **This is where the TrussC canvas-keyboard patch lives (§6).**
8. `sapp_set_icon(&desc->icon)` (8092) → `_sapp_emsc_set_icon` sets a favicon (§10).
9. **Start the frame loop (8094–8099):**
   - If `desc.html5.use_emsc_set_main_loop`:
     `emscripten_set_main_loop(_sapp_emsc_frame_main_loop, 0, desc.html5.emsc_set_main_loop_simulate_infinite_loop)`.
   - Else (default): `emscripten_request_animation_frame_loop(_sapp_emsc_frame_animation_loop, 0)`.
10. **Returns.** Comment 8100–8101: "NOT A BUG: do not call `_sapp_discard_state()`
    here" — cleanup happens later inside the frame callback on quit (§12).

### Entry point (8104–8110) and the SOKOL_NO_ENTRY dispatch
- Default: `#if !defined(SOKOL_NO_ENTRY)` → `int main(int argc,char**){ desc = sokol_main(...); _sapp_emsc_run(&desc); return 0; }` (8105–8108).
- **TrussC compiles `SOKOL_NO_ENTRY`** so this `main` is compiled out. The live
  path is `sapp_run(desc)` (13941, `#if defined(SOKOL_NO_ENTRY)`) →
  `_sapp_emsc_run(desc)` (13947–13948). TrussC's `runApp()` calls `sapp_run()`.
  On web `sapp_run()` **returns immediately** (docs 1174–1190) — so no user code
  may run after it; the app lives entirely in callbacks.

### The frame callbacks
- **`_sapp_emsc_frame_animation_loop(double time, void* ud)` (8027–8055)** — the
  rAF callback. `time` is the rAF DOMHighResTimeStamp in **milliseconds**.
  1. `_sapp_timing_update(&_sapp.timing, time / 1000.0)` (8029) — feed the EXTERNAL
     browser clock (converted ms→s) into the EMA timer. **This is the web timing
     source; there is no `emscripten_get_now`/`performance.now` inside the tick
     itself for the rAF path — the browser hands the timestamp.**
  2. `#if SOKOL_WGPU _sapp_wgpu_frame()` (8031–8032, §3) `#else _sapp_frame()`
     (8034). `_sapp_frame` runs `init_cb` on first frame then `frame_cb`.
  3. **Quit handling (8037–8053):** if `quit_requested` → fire
     `SAPP_EVENTTYPE_QUIT_REQUESTED`; if still requested → `quit_ordered=true`.
     If `quit_ordered` → `_sapp_emsc_unregister_eventhandlers()` (§4),
     `#if SOKOL_WGPU _sapp_wgpu_discard()`, `_sapp_call_cleanup()`,
     `_sapp_discard_state()`, `return EM_FALSE` (stops the rAF loop). Else `EM_TRUE`.
- **`_sapp_emsc_frame_main_loop(void)` (8057–8062)** — the `set_main_loop` variant.
  `time = emscripten_performance_now()` (8058, **THIS is the `performance.now()`
  path**), then calls the animation-loop body; if it returns false,
  `emscripten_cancel_main_loop()`. So the two loop modes differ only in the clock
  source (rAF timestamp vs `performance.now()`) and the stop mechanism.

### Frame timing plumbing (2579–2580, 2609–2610, 2626–2629, 2881)
- `_sapp_timestamp_t` on emscripten is `{ int _dummy; }` (2579–2580);
  `_sapp_timestamp_init` is `(void)ts` (2609–2610); **`_sapp_timestamp_now`
  asserts `false` and returns 0.0 (2626–2629)** — the internal monotonic clock is
  deliberately UNUSED on web. All timing comes from the `time` argument passed into
  `_sapp_timing_update` by the frame callbacks. The EMA filter itself
  (`_sapp_timing_*`, dt_min/dt_max/threshold/alpha, seed 1/60) is identical to the
  other backends. `sapp_frame_duration()` → smoothed; `_unfiltered()` → raw dt.

---

## 2. GLES3 / WebGL2 renderer bring-up (`SOKOL_GLES3` only)

### Context creation (`_sapp_emsc_webgl_init`, 7935–7950)
```c
EmscriptenWebGLContextAttributes attrs;
emscripten_webgl_init_context_attributes(&attrs);   // fills defaults
attrs.alpha                = _sapp.desc.alpha;
attrs.depth                = true;
attrs.stencil              = true;
attrs.antialias            = _sapp.sample_count > 1;   // MSAA on iff sample_count>1
attrs.premultipliedAlpha   = _sapp.desc.html5.premultiplied_alpha;
attrs.preserveDrawingBuffer= _sapp.desc.html5.preserve_drawing_buffer;
attrs.enableExtensionsByDefault = true;
attrs.majorVersion         = 2;    // WebGL2 (== GLES3)
EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = emscripten_webgl_create_context(_sapp.html5_canvas_selector, &attrs);
// FIXME: error message?  (no error check on ctx==0)
emscripten_webgl_make_context_current(ctx);
glGetIntegerv(GL_FRAMEBUFFER_BINDING, (GLint*)&_sapp.gl.framebuffer);   // capture default FBO (== 0)
```
- **`attrs.powerPreference` / `attrs.majorVersion` note:** upstream sets NO
  `powerPreference` here (defaults to browser default). `majorVersion=2` is the
  WebGL2/GLES3 selector; there is no `minorVersion` for WebGL. The context handle
  `ctx` is discarded after make-current (**not stored in `_sapp`** — the current
  context is thread-implicit like GLX). No error handling on failure (FIXME).
- **Swapchain semantics:** the "swapchain" is just the default framebuffer id
  (0). `sapp_get_swapchain()` returns `res.gl.framebuffer = _sapp.gl.framebuffer`
  (14478) + width/height/color_format/depth_format/sample_count. **No
  render_view / textures** — WebGL resolves MSAA implicitly via the context's
  `antialias` attribute, so there's no explicit MSAA texture like WGPU/Metal.
- `sample_count` meaning on WebGL: it only toggles `antialias` on/off; the actual
  sample count is browser-chosen. `sapp_sample_count()` still returns the
  requested `_sapp.sample_count` (14052) — a mild lie the port should preserve for
  API consistency (sokol_gfx swapchain expects the requested count).

### Context loss (`_sapp_emsc_webgl_context_cb`, 7918–7933, `SOKOL_GLES3` only)
Maps `EMSCRIPTEN_EVENT_WEBGLCONTEXTLOST → SAPP_EVENTTYPE_SUSPENDED` and
`WEBGLCONTEXTRESTORED → SAPP_EVENTTYPE_RESUMED`, fires the sapp event. Registered
in `register_eventhandlers` (7985–7988) / unregistered (8021–8024), both guarded
by `#if defined(SOKOL_GLES3)`. **Note: the lost/restored handlers do NOT actually
recreate the GL context** — they only surface SUSPENDED/RESUMED to the app. Real
context recovery is not implemented (app is expected to rebuild GPU resources on
RESUMED). WGPU has no analogous webgl-context-loss path (it has device-lost, §3).

---

## 3. WGPU renderer bring-up (`SOKOL_WGPU`, TrussC web DEFAULT)

**Header / API flavor — CRITICAL (2412–2413, 3891–3895):** this vendored sokol
uses the **modern Dawn-style `webgpu.h`** (`WGPUStringView`, `WGPUFuture`,
`wgpuInstanceWaitAny`, `WGPURequestAdapterCallbackInfo`, `WGPUSurfaceConfiguration`,
`wgpuSurfaceGetCurrentTexture`, `wgpuSurfacePresent`), NOT the old Emscripten
`emscripten/html5_webgpu.h` / `wgpuInstanceCreateSurface`-with-
`WGPUSurfaceDescriptorFromCanvasHTMLSelector`. The surface source struct is
`WGPUEmscriptenSurfaceSourceCanvasHTMLSelector` with
`sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector` (3892–3894) — these
symbols come from **emdawnwebgpu** (Emscripten's Dawn port). **The port's web WGPU
build MUST link `--use-port=emdawnwebgpu`** (or equivalent), NOT the legacy
`-sUSE_WEBGPU=1` bindings. This is a hard ABI dependency — the whole `_sapp_wgpu_*`
code will not compile against the old bindings. (Verify against §13 CMake flags.)

### The async device dance (web = `!_SAPP_WGPU_HAS_WAIT`)
On web, adapter→device→swapchain are chained through callbacks because there is no
synchronous wait. Call order and the `init_done` gate:

1. **`_sapp_wgpu_init()` (4213–4235):** create `WGPUInstance` via
   `wgpuCreateInstance(&desc)` (on web, `desc.requiredFeatures` is empty because
   `_SAPP_WGPU_HAS_WAIT` is off — the `TimedWaitAny` feature block 4218–4224 is
   compiled out). PANIC `WGPU_CREATE_INSTANCE_FAILED` on null. Then
   `_sapp_wgpu_create_adapter()`. The `#if _SAPP_WGPU_HAS_WAIT` synchronous
   `_sapp_wgpu_create_device_and_swapchain()` (4231–4233) is **compiled out on web**
   — comment 4229: "on Emscripten, device and swapchain creation are chained in
   the callbacks."
2. **`_sapp_wgpu_create_adapter()` (4199–4210):** `callbackmode = AllowProcessEvents`
   (web, 3846) — NOT `WaitAnyOnly`. `wgpuInstanceRequestAdapter(instance, 0, cb_info)`.
   On web the returned `WGPUFuture` is ignored (no await); the callback fires later.
3. **`_sapp_wgpu_request_adapter_cb` (4180–4197):** stores `_sapp.wgpu.adapter`;
   on web (`#if !_SAPP_WGPU_HAS_WAIT`, 4193–4195) chains
   `_sapp_wgpu_create_device_and_swapchain()`.
4. **`_sapp_wgpu_create_device_and_swapchain()` (4110–4177):** queries adapter
   features (BC/ETC2/ASTC compression, DualSourceBlending, ShaderF16,
   Float32Filterable, Float32Blendable, TextureFormatsTier2 — added iff present),
   always requires `Depth32FloatStencil8` (4114–4116). Sets device-lost +
   uncaptured-error callbacks. `callbackmode = AllowProcessEvents` on web.
   `wgpuAdapterRequestDevice(...)`; web ignores the future.
5. **`_sapp_wgpu_request_device_cb` (4087–4108):** stores `_sapp.wgpu.device`;
   `#if !_SAPP_EMSCRIPTEN` sets a logging callback (4101–4105) — **skipped on web**
   (emdawnwebgpu has no `wgpuDeviceSetLoggingCallback`). Then
   `_sapp_wgpu_create_swapchain(false)` and **`_sapp.wgpu.init_done = true`**
   (4107) — this flips the gate that lets frames render.
6. **`_sapp_wgpu_device_lost_cb` (4044–4055):** logs unless reason is
   `CallbackCancelled` (fired on instance release). `_sapp_wgpu_device_logging_cb`
   (4058–4074) and `_sapp_wgpu_uncaptured_error_cb` (4077–4085) exist; the logging
   one is `#if !_SAPP_EMSCRIPTEN` (4057), so not used on web.

### Swapchain creation (`_sapp_wgpu_create_swapchain`, 3880–3982)
- **Surface (first call only, 3888–3926):** builds a `WGPUSurfaceDescriptor` whose
  `nextInChain` is the emscripten canvas-selector struct (3891–3895):
  `html_canvas_desc.selector = _sapp_wgpu_stringview(_sapp.html5_canvas_selector)`.
  So the WGPU surface is bound to the SAME canvas selector as everything else.
  `wgpuInstanceCreateSurface` → PANIC `WGPU_SWAPCHAIN_CREATE_SURFACE_FAILED` on null.
  `wgpuSurfaceGetCapabilities(surface, adapter, &surf_caps)` → PANIC on non-Success.
  `render_format = _sapp_wgpu_pick_render_format(...)` (3864–3878): picks the first
  of `RGBA8Unorm`/`BGRA8Unorm` (non-SRGB only). This drives `sapp_color_format()`
  (14019–14028: RGBA8Unorm→RGBA8, BGRA8Unorm→BGRA8).
- **Surface config (3928–3943):** `WGPUSurfaceConfiguration` with device,
  `render_format`, `usage = RenderAttachment`, size = framebuffer w/h,
  `alphaMode = Opaque` **but on emscripten (3936–3940) if
  `desc.html5.premultiplied_alpha` → `alphaMode = Premultiplied`**,
  `presentMode = Fifo` (vsync). `wgpuSurfaceConfigure(...)`.
- **Depth-stencil texture (3945–3961):** always created, `Depth32FloatStencil8`,
  size = framebuffer w/h, sample_count = `_sapp.sample_count`. PANIC on failure.
- **MSAA texture (3963–3980):** only if `sample_count > 1`; `render_format`,
  sample_count. PANIC on failure.

### Per-frame (`_sapp_wgpu_frame`, 4253–4270)
```c
wgpuInstanceProcessEvents(_sapp.wgpu.instance);   // pump async callbacks (adapter/device ready) — ESSENTIAL on web
if (_sapp.wgpu.init_done) {                         // GATE: no rendering until device+swapchain ready
    _sapp_frame();                                  // init_cb (first frame) + frame_cb
    if (_sapp.wgpu.swapchain_view) {                // released the view acquired in sapp_get_swapchain
        wgpuTextureViewRelease(_sapp.wgpu.swapchain_view);
        _sapp.wgpu.swapchain_view = 0;
    }
    #if !defined(_SAPP_EMSCRIPTEN)                   // <-- present is a NATIVE-ONLY call
    if (!_sapp.skip_present) { wgpuSurfacePresent(_sapp.wgpu.surface); }
    else { _sapp.skip_present = false; }
    #endif
}
```
- **`wgpuInstanceProcessEvents` every frame is load-bearing on web** — it's how the
  async adapter/device-request callbacks actually get delivered; without it
  `init_done` never flips and nothing ever renders.
- **`init_done` gate:** for the first N rAF ticks (until the async device arrives)
  `_sapp_wgpu_frame` does nothing but pump events. The app's `init_cb`/`frame_cb`
  do not run until the device is ready. **The port must replicate this gate** —
  callers of `_sapp_frame` on web-WGPU must wait for the device.
- **No explicit present on web (4261 `#if !_SAPP_EMSCRIPTEN`)** — the browser
  auto-presents the configured surface at rAF boundaries. Consequently
  **`skip_present` (the TrussC D3D11-flicker patch) is a no-op on web** — the whole
  present block is compiled out. TrussC's event-driven-render "skip present" trick
  cannot suppress a web frame; don't rely on it there (§14).

### Swapchain acquire / resize / discard
- **`_sapp_wgpu_swapchain_next()` (4009–4035):** called from `sapp_get_swapchain()`
  (14442–14444). `wgpuSurfaceGetCurrentTexture`; on `SuccessOptimal/Suboptimal`
  create the view; on `Timeout/Outdated/Lost` release + **discard & recreate the
  swapchain** (4024–4025) then... (falls through — note: does not re-acquire in the
  same call, so `swapchain_view` may be left 0; there's a FIXME at 14445 that null
  view "should skip the frame" but currently asserts). On `Error` → PANIC.
- **`_sapp_wgpu_swapchain_size_changed()` (4037–4042):** discard(true) +
  create(true) — keeps the surface, recreates depth/MSAA + reconfigures. Called
  from `_sapp_emsc_size_changed` (7548–7551, `#if SOKOL_WGPU`) on every canvas
  resize.
- **`_sapp_wgpu_discard_swapchain(bool from_resize)` (3984–4006):** releases
  msaa/depth views+textures; releases the surface only when `!from_resize` (4001).
- **`_sapp_wgpu_discard()` (4237–4251):** swapchain + device + adapter + instance
  release. Called from the quit path (8047–8048).

### WGPU environment / swapchain getters mapping (14404–14455)
- `sapp_get_environment().wgpu.device = _sapp.wgpu.device` (14405).
- `sapp_get_swapchain()` (14442–14454): asserts `swapchain_view==0`, calls
  `_sapp_wgpu_swapchain_next()`, then (sample_count>1) `render_view = msaa_view`,
  `resolve_view = swapchain_view`; else `render_view = swapchain_view`;
  `depth_stencil_view = depth_stencil_view`. These map straight to
  `sglue_swapchain()` (sokol_glue.h 193–195). **NOTE: the doc-comment public
  getters `sapp_wgpu_get_device/render_view/resolve_view/depth_stencil_view`
  (declared in the header comment at 361–364) are NOT implemented in this version**
  — the live contract is `sapp_get_environment()`/`sapp_get_swapchain()` consumed
  by `sglue_environment()`/`sglue_swapchain()`. Don't resurrect the old getters;
  keep the environment/swapchain path.

---

## 4. Event registration and dispatch model

### Registration (`_sapp_emsc_register_eventhandlers`, 7953–7989)
All handlers are registered with `useCapture = true` (the `true` third arg) and
`0` userData. Targets:

| DOM event | emscripten setter | target | callback |
|---|---|---|---|
| mousedown/up/move/enter/leave | `emscripten_set_mouse*_callback` | **canvas selector** | `_sapp_emsc_mouse_cb` |
| wheel | `emscripten_set_wheel_callback` | **canvas selector** | `_sapp_emsc_wheel_cb` |
| keydown/keyup/keypress | `emscripten_set_key*_callback` | **canvas selector** ⟵ TrussC PATCH (§6) | `_sapp_emsc_key_cb` |
| touchstart/move/end/cancel | `emscripten_set_touch*_callback` | **canvas selector** | `_sapp_emsc_touch_cb` |
| pointerlockchange/error | `emscripten_set_pointerlock*_callback` | `EMSCRIPTEN_EVENT_TARGET_DOCUMENT` | `_sapp_emsc_pointerlock*_cb` |
| focus/blur | `emscripten_set_focus/blur_callback` | `EMSCRIPTEN_EVENT_TARGET_WINDOW` | `_sapp_emsc_focus_cb`/`_blur_cb` |
| fullscreenchange | `emscripten_set_fullscreenchange_callback` | **canvas selector** | `_sapp_emsc_fullscreenchange_cb` |
| beforeunload | `sapp_js_add_beforeunload_listener` (EM_JS) | `window` | JS closure |
| paste (if clipboard) | `sapp_js_add_clipboard_listener` (EM_JS) | `window` | JS → `_sapp_emsc_onpaste` |
| dragenter/leave/over/drop (if drop) | `sapp_js_add_dragndrop_listeners` (EM_JS) | canvas | JS → begin/drop/end |
| webglcontextlost/restored (GLES3 only) | `emscripten_set_webglcontext*_callback` | canvas | `_sapp_emsc_webgl_context_cb` |

**Upstream comment 7954–7956** (now stale after the patch): "HTML canvas doesn't
receive input focus, this is why key event handlers are added to the window object
(this could be worked around by adding a 'tab index' to the canvas)". The TrussC
patch (§6) does exactly that workaround. Note the resize handler is registered
separately in `_sapp_emsc_run` (8075), not here.

### Unregistration (`_sapp_emsc_unregister_eventhandlers`, 7991–8025)
Mirror image — all set to callback `0`. **Asymmetry to preserve (8011–8013):** the
resize callback is only unregistered here `if (!_sapp.desc.html5.canvas_resize)`
(because it was only registered when tracking). Keyboard callbacks are also
unregistered from the canvas selector (patched, comment 7998).

### The `_sapp_events_enabled()` gate + consume/bubble model
Every callback body wraps event delivery in `if (_sapp_events_enabled())`
(event_cb set AND init_called) — no events before the first frame, identical to
native. Callbacks return an `EM_BOOL` **"consume" value**: returning true tells
the browser this handler consumed the event (preventDefault). The `bubble_*`
desc flags invert this:
- `consume_event = !_sapp.desc.html5.bubble_mouse_events` (7561) — default bubble
  off ⇒ consume on. Then `consume_event |= _sapp_call_event(&_sapp.event)` (7622):
  the app's `sapp_consume_event()` also forces consume. Same shape for wheel
  (`bubble_wheel_events`, 7635), touch (`bubble_touch_events`, 7855), keys (§5).
- **`sapp_consume_event()` (14237–14239)** just sets `_sapp.event_consumed`; on web
  the return of `_sapp_call_event` reflects that and becomes the DOM
  preventDefault. So consuming a web event = preventDefault on the DOM event.

---

## 5. Per-event-type semantics

### Mouse (`_sapp_emsc_mouse_cb`, 7559–7630)
- Stores `_sapp.emsc.mouse_buttons = emsc_event->buttons` (browser bitmask) every
  call. **Position:** if `mouse.locked` → `dx/dy = movementX/movementY` (raw
  pointer-lock deltas, 7563–7565); else `x/y = targetX/targetY * dpi_scale`, and
  `dx/dy` computed vs previous when `pos_valid` (7566–7576). Note **targetX/Y are
  CSS pixels, scaled by dpi_scale to framebuffer pixels** — the app sees
  framebuffer-space coords.
- Type map (7581–7604): MOUSEDOWN→MOUSE_DOWN (button event), MOUSEUP→MOUSE_UP
  (button event), MOUSEMOVE→MOUSE_MOVE, MOUSEENTER→MOUSE_ENTER (clear dx/dy),
  MOUSELEAVE→MOUSE_LEAVE (clear dx/dy). Guarded by
  `emsc_event->button in [0, SAPP_MAX_MOUSEBUTTONS)` (7577).
- **Button map (7613–7618):** 0→LEFT, 1→MIDDLE, 2→RIGHT (browser order differs from
  sokol's L/M/R — note this is NOT a straight cast for 1/2). Non-button events set
  `mouse_button = SAPP_MOUSEBUTTON_INVALID`.
- Modifiers (`_sapp_emsc_mouse_event_mods`, 7477–7485): ctrl/shift/alt/meta →
  CTRL/SHIFT/ALT/SUPER, plus held-button mods from `mouse_buttons` via
  `_sapp_emsc_mouse_button_mods` (7469–7475: bit0→LMB, **bit1→RMB, bit2→MMB** —
  "not a bug" comments; browser button bitmask order R/M swapped vs sokol enum).
- **Pointer-lock activation (7625–7627):** `_sapp_emsc_update_mouse_lock_state()`
  is called only on button events (mouse-lock can only be requested from a user
  gesture; see §8).

### Wheel (`_sapp_emsc_wheel_cb`, 7632–7654)
- MOUSE_SCROLL. **Delta scaling by `deltaMode` (7641–7647):** `DOM_DELTA_PIXEL →
  -0.01`, `DOM_DELTA_LINE → -1.33`, `DOM_DELTA_PAGE → -10.0` (guessed),
  default `-0.1`. `scroll_x/y = scale * deltaX/deltaY`. **Sign is negated** (natural
  scroll direction). This exact table (github issue #339) is load-bearing —
  lift verbatim. Also updates mouse-lock state (7652).

### Keyboard (`_sapp_emsc_key_cb`, 7783–7851) + keymap (7656–7775)
- Type map: KEYDOWN→KEY_DOWN, KEYUP→KEY_UP, KEYPRESS→CHAR (7788–7801).
- `key_repeat = emsc_event->repeat` (7805). Modifiers via
  `_sapp_emsc_key_event_mods` (7487–7495).
- **CHAR path (7807–7810):** `char_code = emsc_event->charCode`; consume unless
  `bubble_char_events`. (Comment: charCode unsupported on Android Chrome.)
- **KEY path (7811–7840):**
  - **Keycode table = HTML5 `code` strings** (7812–7815): if `emsc_event->code[0]`
    non-empty (desktop browsers send physical `code` like "KeyA", "Digit1",
    "ArrowLeft") → `_sapp_emsc_translate_key(code)`. Else (mobile browsers send
    only localized `key`) → `_sapp_emsc_translate_key(key)` — works only for a small
    localization-agnostic subset (Enter/arrows/etc.), alpha-numerics become
    INVALID. **This is a `str→sapp_keycode` linear-search table
    `_sapp_emsc_keymap[]` (7656–7763)** keyed on `code` strings ("Backspace",
    "Tab", "Enter", "ShiftLeft"…"KeyZ", "Digit0"…"Numpad0"…"F1"…, punctuation).
    ~115 entries. **Lift the entire table verbatim** — it's the authoritative
    web keycode map. Note the "Quote" → `SAPP_KEYCODE_GRAVE_ACCENT` "FIXME: ???"
    quirk (7761) is a known upstream bug; keep as-is for parity.
  - **macOS Super-key keyup hack (7824–7834):** on KEY_DOWN, if a non-Super key is
    pressed while SUPER modifier is held, set `send_keyup_followup = true` — because
    macOS browsers don't deliver keyUp while Cmd is held, so keys would "stick". A
    synthetic KEY_UP is fired right after the KEY_DOWN (7843–7846). Load-bearing;
    lift.
  - **Forward-special-keys / char-key logic (7836–7840):** `_sapp_emsc_is_char_key`
    (7777–7781) = `key_code < SAPP_KEYCODE_WORLD_1`. **Only NON-char keys are
    consumed** (`consume_event |= !bubble_key_events` at 7838–7840). Char keys are
    deliberately left to bubble so the browser can generate the follow-up
    `keypress`/CHAR event. This is the "forward special keys" rule: prevent-default
    F-keys/Tab/Backspace/arrows/etc. (non-char) to stop browser default actions,
    but let character keys through. **This interacts with the TrussC canvas patch
    (§6):** with keys on the canvas + tabindex, preventDefault on the canvas stops
    the page from scrolling on space/arrows while the canvas is focused.
- `_sapp_emsc_update_mouse_lock_state()` at 7849 (keys are user gestures too).

### Touch (`_sapp_emsc_touch_cb`, 7853–7894)
- **Real multi-touch, NOT mouse emulation.** TOUCHSTART→TOUCHES_BEGAN,
  MOVE→TOUCHES_MOVED, END→TOUCHES_ENDED, CANCEL→TOUCHES_CANCELLED (7858–7873).
  `num_touches = min(emsc numTouches, SAPP_MAX_TOUCHPOINTS)` (7878–7881). Per point:
  `identifier`, `pos_x/y = targetX/Y * dpi_scale`, `changed = isChanged`
  (7882–7889). Consume unless `bubble_touch_events`. No synthetic mouse events are
  generated from touch — the app gets native touch events.

### Focus / blur (7896–7916)
`_sapp_emsc_focus_cb` → FOCUSED, `_sapp_emsc_blur_cb` → UNFOCUSED. Registered on
`EMSCRIPTEN_EVENT_TARGET_WINDOW` (not canvas). Always return true.

### Fullscreen change (`_sapp_emsc_fullscreenchange_cb`, 7378–7384)
Syncs `_sapp.fullscreen = emsc_event->isFullscreen` — needed to catch the user
pressing Esc to exit fullscreen. No sapp event is fired.

### Resize (`_sapp_emsc_size_changed`, 7507–7557) — see §10.

---

## 6. THE TRUSSC CANVAS-KEYBOARD PATCH (native feature of the port)

**TRUSSC_MODIFICATIONS.md patch #2 "Keyboard Events on Canvas (Emscripten)".** The
patched lines are in `_sapp_emsc_register_eventhandlers` (7963–7968) and
`_sapp_emsc_unregister_eventhandlers` (7998–8001):

```c
// Modified by tettou771 for TrussC: register keyboard events on canvas instead of window
// This allows other page elements (like Monaco editor) to handle keyboard events
// Canvas must have tabindex attribute to receive focus
emscripten_set_keydown_callback(_sapp.html5_canvas_selector, 0, true, _sapp_emsc_key_cb);
emscripten_set_keyup_callback(_sapp.html5_canvas_selector, 0, true, _sapp_emsc_key_cb);
emscripten_set_keypress_callback(_sapp.html5_canvas_selector, 0, true, _sapp_emsc_key_cb);
```

**What differs from upstream:** upstream sokol registers keydown/keyup/keypress on
`EMSCRIPTEN_EVENT_TARGET_WINDOW` (so the app grabs ALL page keystrokes globally).
TrussC registers them on `_sapp.html5_canvas_selector` instead, so keyboard events
only reach the sokol app when the **canvas element itself has focus**. This lets
other DOM elements on the same page (the Monaco code editor in TrussSketch, form
inputs, etc.) receive keyboard input independently — critical for the web IDE
coexistence.

**Requirement this imposes:** an HTML canvas element cannot receive keyboard focus
unless it has a `tabindex` attribute. **The shell/host page MUST set `tabindex`
(e.g. `tabindex="0"` or `-1`) on the canvas**, and something must give the canvas
focus (click-to-focus works by default once tabindex is present; the page may also
call `canvas.focus()`). Without tabindex, keyboard events never fire → the app
appears to ignore the keyboard. The stale upstream comment at 7954–7956 documents
exactly this workaround.

**Port directive:** this is a NATIVE feature of `sokol_app_tc.h`, not an optional
patch. **P3 must register keyboard handlers on the canvas selector by default**
(matching this behavior), and the TrussC web shell.html must keep `tabindex` on the
canvas (verify against §13). Do NOT revert to window-target keyboard.

**Other emscripten-relevant patches in TRUSSC_MODIFICATIONS.md:** patch #1
"Skip Present" adds `skip_present` to the WGPU present path — but that path is
`#if !_SAPP_EMSCRIPTEN` (4261), so **skip_present is a no-op on web** (§3/§14).
Patch #3 (RGB10A2) is Metal/D3D11 only; comment explicitly says "WebGL unchanged"
— web color format is RGBA8/BGRA8 (§11). No other patches touch the emscripten
path.

---

## 7. Clipboard, drag&drop, fetch (EM_JS-heavy)

### Clipboard
- **Set (`_sapp_emsc_set_clipboard_string` → `sapp_js_write_clipboard`, 7079–7099):**
  creates a hidden off-screen `<textarea>`, sets its value, `select()`, then
  **`document.execCommand('copy')`** (the legacy sync clipboard API — works only
  inside a user-gesture event; the public note at 14241 warns
  "sapp_set_clipboard_string() must be called from within event handler!"). Then
  `sapp_set_clipboard_string()` (14242–14259) also copies into `_sapp.clipboard.buffer`.
- **Get (`sapp_get_clipboard_string`, 14261–14277):** on emscripten just returns
  `_sapp.clipboard.buffer` (14268) — the buffer is filled asynchronously by the
  paste listener, NOT by a synchronous read (browsers forbid sync clipboard read).
- **Paste listener (`sapp_js_add_clipboard_listener`, 7064–7073):** registers a
  `window` 'paste' handler that reads `event.clipboardData.getData('text')`,
  marshals it to a C string via `withStackSave`/`stringToUTF8OnStack`, and calls
  **`_sapp_emsc_onpaste`** (7069, C fn at 6955–6963) which copies into
  `clipboard.buffer` and fires `SAPP_EVENTTYPE_CLIPBOARD_PASTED`. So on web, paste
  is event-driven; the app reads the buffer in its CLIPBOARD_PASTED handler.
  Only registered if `_sapp.clipboard.enabled` (7979–7981).

### Drag & drop
- **Listeners (`sapp_js_add_dragndrop_listeners`, 7101–7141):** on the canvas
  (`Module.sapp_emsc_target`), registers dragenter/dragleave/dragover (all just
  stopPropagation+preventDefault) and **drop**. The drop handler: grabs
  `event.dataTransfer.files`, stashes them in `Module.sokol_dropped_files`, calls
  `_sapp_emsc_begin_drop(files.length)` (C, 6970–6982), then for each file marshals
  `files[i].name` and calls `_sapp_emsc_drop(i, cstr)` (C, 6984–7000, copies the
  **filename only, not a path** into the drop buffer), then computes a mods bitmask
  (shift=1/ctrl=2/alt=4/meta=8) and calls `_sapp_emsc_end_drop(clientX, clientY,
  mods)` (C, 7002–7025). `end_drop` sets `mouse.x/y = x/y * dpi_scale` and fires
  **SAPP_EVENTTYPE_FILES_DROPPED** with the decoded modifiers. Only if
  `_sapp.drop.enabled`.
- **Fetch dropped file (`sapp_html5_get_dropped_file_size` / `_fetch_dropped_file`,
  14338–14385 + EM_JS 7143–7172):** `sapp_js_dropped_file_size(index)` returns
  `Module.sokol_dropped_files[index].size`. `sapp_js_fetch_dropped_file` uses a
  JS `FileReader.readAsArrayBuffer`; on load, if content ≤ buf_size, `HEAPU8.set`
  into the wasm buffer and invoke `_sapp_emsc_invoke_fetch_cb` (C, 7027–7038, builds
  a `sapp_html5_fetch_response` and calls the user callback) with success; else
  BUFFER_TOO_SMALL (error 1); on reader error → OTHER (error 2). **This async file
  read is web-only and has no native analog** — the port must keep the full
  `sapp_html5_fetch_dropped_file` / response / error-enum surface.

### beforeunload (`html5_ask_leave_site`)
`sapp_js_add_beforeunload_listener` (7050–7058) registers a `window` beforeunload
handler that, iff `_sapp_html5_get_ask_leave_site()` (C, 6966–6968 reads
`_sapp.html5_ask_leave_site`) is nonzero, calls `preventDefault()` +
`returnValue=' '` to show the browser "leave site?" prompt.
`sapp_html5_ask_leave_site(bool)` (14592–14594) sets the flag at runtime;
`desc.html5.ask_leave_site` seeds it.

### `EM_JS_DEPS` (6945–6947)
`EM_JS_DEPS(sokol_app, "$withStackSave,$stringToUTF8OnStack,$findCanvasEventTarget")`
— declares the JS-library dependencies the EM_JS blocks need so emscripten links
them. **Load-bearing** — the port's web TU must emit the same DEPS or the paste /
drop / init shims fail to link. `findCanvasEventTarget` (used in `sapp_js_init`) is
an emscripten library function that resolves a CSS selector to a DOM element.

---

## 8. Pointer lock, cursor, fullscreen (JS shims)

### Pointer lock (deferred-on-gesture rule)
- `sapp_lock_mouse(true)` → `_sapp_emsc_lock_mouse(true)` (7234–7243) **does NOT
  call requestPointerLock immediately** — it only sets
  `_sapp.emsc.mouse_lock_requested = true`. The actual
  `sapp_js_request_pointerlock()` (7220–7226, calls
  `Module.sapp_emsc_target.requestPointerLock()`) is invoked later by
  `_sapp_emsc_update_mouse_lock_state()` (7248–7253) **from inside a mouse-button,
  wheel or key callback** (7625–7627, 7652, 7849). This is because browsers only
  grant pointer-lock from within a user-gesture event handler. **This deferred
  latch is load-bearing and web-specific** — the port must keep the
  `mouse_lock_requested` flag + gesture-time flush.
- Unlock (`_sapp_emsc_lock_mouse(false)`): clears the flag and calls
  `sapp_js_exit_pointerlock()` (7228–7232, `document.exitPointerLock()`).
- State callbacks: `_sapp_emsc_pointerlockchange_cb` (7204–7209) sets
  `_sapp.mouse.locked = isActive`; `_sapp_emsc_pointerlockerror_cb` (7211–7218)
  clears locked + `mouse_lock_requested`. Registered on
  `EMSCRIPTEN_EVENT_TARGET_DOCUMENT`.

### Cursor (CSS `style.cursor`)
- `_sapp_emsc_update_cursor` (7281–7285) → `sapp_js_set_cursor(cursor_type, shown,
  use_custom)` (7256–7279): sets `Module.sapp_emsc_target.style.cursor`. Mapping
  (7263–7276): shown==0 → "none"; custom → the object-URL css_property; else the
  standard CSS cursor names (DEFAULT→"auto", ARROW→"default", IBEAM→"text",
  CROSSHAIR→"crosshair", POINTING_HAND→"pointer", RESIZE_EW→"ew-resize",
  RESIZE_NS→"ns-resize", RESIZE_NWSE→"nwse-resize", RESIZE_NESW→"nesw-resize",
  RESIZE_ALL→"all-scroll", NOT_ALLOWED→"not-allowed"). Lift verbatim.
- **Custom cursors (`sapp_js_make_custom_mouse_cursor`, 7289–7358):** encodes the
  RGBA pixels into a **BMP (BITMAPV… header, BI_BITFIELDS, bottom-up rows)** in JS,
  wraps in a Blob, `URL.createObjectURL`, and stores
  `{ css_property: "url('...') hx hy, auto", blob_url }` in
  `Module.__sapp_custom_cursors[slot]`. `sapp_js_destroy_custom_mouse_cursor`
  (7361–7367) revokes the object URL. This whole BMP-in-JS encoder is web-specific
  and load-bearing for `sapp_bind_mouse_cursor_image` on web; lift verbatim
  (wrapped in `#pragma GCC diagnostic ... -Wdollar-in-identifier-extension`).

### Fullscreen (JS request/exit + rejection reconcile)
- `sapp_toggle_fullscreen` → `_sapp_emsc_toggle_fullscreen` (7430–7435): flip
  `_sapp.fullscreen` preliminarily, then `sapp_js_toggle_fullscreen()`
  (7386–7428) which requests/exits fullscreen on the canvas with vendor-prefix
  fallbacks (requestFullscreen/webkit/moz). On promise rejection it calls back
  `_sapp_emsc_set_fullscreen_flag(0/1)` (C, 7042–7044) to **undo** the preliminary
  flip. `_sapp_emsc_fullscreenchange_cb` (§5) also re-syncs on Esc-exit.
  **Must be triggered from a user gesture** (browser rule) — cannot force
  fullscreen at startup (hence §1 step 2).

---

## 9. Canvas size, DPI, resize tracking

### Two size modes (set at run(), §1 step 4)
- **`canvas_resize = true`:** app owns size (`desc.width/height`, defaulting
  640×480). No window-resize listener. The canvas backing store is set to the
  framebuffer size and never auto-tracked.
- **`canvas_resize = false` (default/tracking):** the canvas CSS size is read via
  `emscripten_get_element_css_size`, and a `window` resize listener
  (`_sapp_emsc_size_changed`) keeps it in sync. This is the usual "canvas stretched
  by CSS, follow the layout" mode.

### `_sapp_emsc_size_changed` (7507–7557)
1. `emscripten_get_element_css_size(selector, &w, &h)` (7511) — CSS (logical) px.
2. **Fullscreen-toggle zero guard (7532–7541):** if `w<1.0` use
   `ui_event->windowInnerWidth` (fallback for the flaky HTML5 fullscreen API),
   else `window_width = round(w)`; same for height. The long comment (7512–7531)
   recommends "soft fullscreen" (CSS-stretched canvas) over the HTML5 fullscreen
   API — relevant design guidance.
3. `if (desc.high_dpi) dpi_scale = emscripten_get_device_pixel_ratio()` (7542–7544).
4. `framebuffer_width/height = round(w/h * dpi_scale)` (7545–7546).
5. `emscripten_set_canvas_element_size(selector, framebuffer_width, framebuffer_height)`
   (7547) — resize the drawing buffer.
6. `#if SOKOL_WGPU _sapp_wgpu_swapchain_size_changed()` (7548–7551) — recreate
   surfaces. **On GLES3 nothing is recreated** (the WebGL default framebuffer
   follows the canvas backing size automatically).
7. Fire `SAPP_EVENTTYPE_RESIZED` if events enabled (7552–7555).

**`dpi_scale` = `emscripten_get_device_pixel_ratio()` (window.devicePixelRatio),
gated by `desc.high_dpi`.** `sapp_high_dpi()` (14056–14058) = `high_dpi &&
dpi_scale >= 1.5`. All mouse/touch/drop coords are in CSS px multiplied by
dpi_scale → framebuffer space.

---

## 10. Title, icon, misc

- **Title:** `document.title` is set once in `sapp_js_init` (7183–7186) iff
  `update_document_title` (§1). **`sapp_set_window_title` (14279–14289) has NO
  emscripten branch** — runtime title changes do NOT update `document.title` on web
  (only the initial one does). Gap to note; the port could add a
  `document.title=` JS shim if desired, but upstream doesn't.
- **Icon = favicon (`_sapp_emsc_set_icon`, 7460–7467):** picks the best ~16×16
  image via `_sapp_image_bestmatch`, then `sapp_js_set_favicon(w,h,pixels)`
  (7445–7458) draws the RGBA into an offscreen canvas, `toDataURL()`, and injects a
  `<link id="sokol-app-favicon" rel="shortcut icon">` into `<head>`.
  `sapp_js_clear_favicon` (7438–7443) removes it. Called from `sapp_set_icon`
  (14314–14315).
- **`sapp_js_init` (7183–7202):** sets document.title; resolves the canvas —
  honors `Module['canvas']` (if the host set a canvas object, registers it in
  `specialHTMLTargets[selector]`), then `Module.sapp_emsc_target =
  findCanvasEventTarget(selector)`. Warns if the target can't be found or lacks
  `requestPointerLock`. **`Module.sapp_emsc_target` is THE canvas handle every
  other EM_JS block reads** — the port must set it up identically.
- **`sapp_show_keyboard` (14086–14094):** on web falls into the `#else _SOKOL_UNUSED`
  branch — **no-op**; `onscreen_keyboard_shown` stays false. `sapp_keyboard_shown()`
  → false.

---

## 11. Color/depth format, environment, swapchain (both backends)

- `sapp_color_format()` (14018–14045): **WGPU** → maps `_sapp.wgpu.render_format`
  (RGBA8Unorm→RGBA8, BGRA8Unorm→BGRA8, 14020–14028). **GLES3** → falls to the
  `#else` branch → **`SAPP_PIXELFORMAT_RGBA8`** (14044). The RGB10A2 patch
  (14040–14042) is Metal/D3D11 only — **web is always 8-bit**.
- `sapp_depth_format()` → `SAPP_PIXELFORMAT_DEPTH_STENCIL` (14048–14050) both.
- `sapp_sample_count()` → `_sapp.sample_count` (14052).
- `sapp_get_environment()` (14387–14415): `defaults.{color,depth}_format`,
  `sample_count`; WGPU adds `res.wgpu.device` (14405). GLES3 adds nothing (context
  is thread-implicit).
- `sapp_get_swapchain()` (14417–14485): WGPU per §3; **GLES3 →
  `res.gl.framebuffer = _sapp.gl.framebuffer` (0)** (14477–14479). Plus width/
  height/formats/sample_count. Consumed by `sglue_environment()`/`sglue_swapchain()`
  (sokol_glue.h 159–206) — these are the ONLY consumers; keep the field contract
  intact.

---

## 12. Quit / cleanup semantics on web (what "quit" even means in a tab)

- `sapp_request_quit()` (14225–14227) sets `quit_requested`; `sapp_cancel_quit()`
  clears it; `sapp_quit()` (14233–14235) sets `quit_ordered` directly.
- The quit is processed inside the rAF frame callback (8037–8053): QUIT_REQUESTED
  event → if not cancelled, `quit_ordered` → unregister all handlers → WGPU discard
  → `cleanup_cb` → `_sapp_discard_state()` → **return `EM_FALSE`** which tells
  `emscripten_request_animation_frame_loop` to STOP calling back. For the
  `set_main_loop` variant, the wrapper calls `emscripten_cancel_main_loop()`.
- **"Quit" on web = stop the rAF loop and tear down sokol state; the browser tab
  stays open, the wasm module stays loaded.** There is no process exit, no window
  close. The canvas remains in the DOM (blank). This is a fundamentally different
  lifecycle from native and the port must not assume `main()` returns or the
  process dies. (`_sapp_discard_state` is called HERE, never in `run()` — comment
  8100.)

---

## 13. TrussC-side wiring the port must keep working (verified against the repo)

**THE MIGRATION IN ONE LINE:** `core/platform/web/sokol_impl.cpp` currently
`#include "sokol_app.h"` (stock). The desktop TUs (`platform/win/sokol_impl.cpp`,
`platform/mac/sokol_impl.mm`, `platform/linux/sokol_impl.cpp`) already
`#include "sokol_app_tc.h"`. **P3 = give `sokol_app_tc.h` an emscripten backend and
switch the web TU's include to it.** `sokol_app_tc.h` today has NO emscripten code
at all (only a macOS line) — the web section is entirely new.

- **`core/platform/web/sokol_impl.cpp` (25 lines):** `#define SOKOL_IMPL`,
  `#define SOKOL_NO_ENTRY`, GCC diag push (ignore unused var/func/missing-field-init),
  then includes IN THIS ORDER: `sokol_log.h`, **`sokol_app.h`** ← swap to
  `sokol_app_tc.h`, `sokol_gfx.h`, `sokol_glue.h`, `util/sokol_gl_tc.h`,
  `util/sokol_memtrack.h`. **No `SOKOL_WGPU`/`SOKOL_GLES3` define here** — those come
  from CMake via `target_compile_definitions(TrussC PUBLIC ...)`.
- **None of `core/platform/web/*.cpp` call any `sapp_*` symbol.** The only
  emscripten surface outside the impl TU is `tcPlatform_web.cpp` using
  `emscripten_get_device_pixel_ratio()` and
  `emscripten_set_canvas_element_size("canvas", …)` (hardcodes the id `"canvas"`,
  NOT `#canvas`). So the port's public `sapp_*` API surface is all that TrussC
  consumes, plus the shell + CMake wiring.
- **`core/CMakeLists.txt` 47–55, 188–194:** web backend `TC_WEB_BACKEND` defaults
  to `"WGPU"` (line 52). WGPU branch → `target_compile_definitions(TrussC PUBLIC
  SOKOL_WGPU)` + `target_compile_options(TrussC PUBLIC --use-port=emdawnwebgpu)`;
  else → `SOKOL_GLES3`. So **WGPU is the default and pulls in the emdawnwebgpu
  port** (§3 hazard confirmed).
- **`core/cmake/trussc_app.cmake` 975–1013 (emscripten link flags):**
  `-sALLOW_MEMORY_GROWTH=1`, `-sALLOW_TABLE_GROWTH=1`, `-sFETCH=1`, **`-sASYNCIFY=1`**,
  `--shell-file=${TC_ROOT}/core/platform/web/shell.html`; WGPU branch adds
  `--use-port=emdawnwebgpu` (link), GLES3 branch adds `-sMIN_WEBGL_VERSION=2
  -sMAX_WEBGL_VERSION=2`; `--preload-file bin/data@/data` if present. **NOT present:**
  `-sUSE_WEBGL2`, `-sUSE_WEBGPU`, `-sEXPORTED_FUNCTIONS`, `-sEXPORTED_RUNTIME_METHODS`,
  `--emrun`. **`-sASYNCIFY=1` is on** — the port must stay Asyncify-safe (no
  mid-frame resource creation that reenters the loop; cf. known web mid-frame
  resource crashes). `-sFETCH=1` is enabled (used by the dropped-file FileReader and
  other fetches).
- **`core/platform/web/shell.html` (the only shell):** canvas is
  `<canvas id="canvas" oncontextmenu="event.preventDefault()" tabindex="-1">`
  (line 62). `#canvas` matches sokol's default selector. **`tabindex="-1"` is
  present** (programmatically focusable) and the shell JS calls
  `canvasElement.focus()` on `mousedown` and on window `load`, plus binds
  `Module.canvas = canvasElement` and a `webglcontextlost` alert. This is exactly
  the focus mechanism the §6 keyboard-on-canvas patch needs — **keep it.**
- **Entry:** `TrussC.h:15` `#define SOKOL_NO_ENTRY`. `runApp<AppClass>()`
  (TrussC.h 2631–2638) → `sapp_run(&desc)` — the SAME path web and desktop, no
  `#ifdef __EMSCRIPTEN__`. Examples use a plain `int main(){ … TC_RUN_APP(...) }`
  (no `sokol_main`, no web special-case). On web `sapp_run` returns immediately.
- **App descriptor (`buildAppDescriptor`, TrussC.h 2582–2612):** sets
  `window_title`, `high_dpi = settings.highDpi`, `sample_count`, `fullscreen`
  (overridden false on web, §1), `swap_interval` (ignored on web), the 4 callbacks,
  logger, `enable_dragndrop=true` + `max_dropped_files=16` +
  `max_dropped_file_path_length=2048`, `enable_clipboard=true` + `clipboard_size`.
  **It sets NO `desc.html5.*` fields whatsoever** — canvas_selector, canvas_resize,
  premultiplied_alpha, preserve_drawing_buffer, bubble_*, update_document_title,
  use_emsc_set_main_loop all stay at sokol defaults (canvas_selector → `"#canvas"`,
  everything else false/0). **Port consequence:** the web default is
  `canvas_resize=false` (tracking mode, §9), all `bubble_*` false (events consumed
  by default), `update_document_title=false` (document.title only from the shell's
  `{{{ TITLE }}}`), rAF loop (not set_main_loop). Keep those defaults working.

**Used and must be preserved on web:** the whole `_sapp_emsc_run` setup + rAF
callback loop; WGPU async device/swapchain (init_done gate, ProcessEvents per
frame, emdawnwebgpu); GLES3 context path; all mouse/wheel/key/CHAR/touch/focus/
fullscreen events; **keyboard-on-canvas patch (§6, default) + shell tabindex/focus**;
clipboard paste/copy, drag&drop (16 files) + fetch, favicon, custom cursors, resize
tracking + RESIZED, beforeunload; the `sapp_get_environment()`/`sapp_get_swapchain()`
contract consumed by `sglue_*`.

**Present but skippable / gaps on web:** `skip_present` (no-op, §3); runtime
`document.title` change (not implemented, §10); on-screen keyboard (no-op);
`sapp_wgpu_get_*` legacy getters (not implemented — use environment/swapchain);
WebGL context true recovery (only SUSPENDED/RESUMED surfaced).

---

## 14. PORT CHECKLIST (sokol_app_tc.h emscripten section)

### (a) Lift verbatim (line ranges in sokol_app.h)
1. **`_sapp_emsc_keymap[]` keycode table** — 7656–7763 (the `code`-string →
   `sapp_keycode` map, ~115 entries). Copy exactly incl. the "Quote"→GRAVE_ACCENT
   FIXME quirk.
2. **All `EM_JS` JS shims** — 7050–7458: beforeunload (7050–7062), clipboard
   add/remove/write (7064–7099), dragndrop listeners + dropped-file size/fetch +
   remove (7101–7181), `sapp_js_init` (7183–7202), pointerlock request/exit
   (7220–7232), `sapp_js_set_cursor` (7256–7279), make/destroy custom cursor
   (7289–7367, keep the `#pragma GCC diagnostic` guards), toggle_fullscreen
   (7386–7428), favicon clear/set (7438–7458). Keep the `EM_JS_DEPS` line
   (6946) and the `extern "C"` KEEPALIVE C bridges (6955–7044).
3. **Wheel delta-mode scaling table** — 7641–7647 (issue #339). Verbatim.
4. **Cursor CSS-name switch** — 7263–7276.
5. **Super-key keyup-followup hack** — 7824–7834/7843–7846.
6. **The whole WGPU async chain** — 3827–4271 (shared with native, but web takes
   the `!_SAPP_WGPU_HAS_WAIT` / `_SAPP_EMSCRIPTEN` branches at 3843/3846, 3891–3895,
   3936–3940, 4057, 4101, 4193–4195, 4229, 4261). Lift as-is; the macros already
   fork correctly.

### (b) Adapt to the tc window model (single window = window #0)
1. **`sapp_create_window()` and plural-window APIs → return invalid + log** on web.
   A second HTML canvas as a "window" is not modeled; the whole backend is bound to
   one `Module.sapp_emsc_target`. Document as an explicit gap.
2. **`sapp_window_gl_framebuffer()` / new `sapp_window_*` getters** → for window #0,
   return the real values (`_sapp.gl.framebuffer` on GLES3; the WGPU
   environment/swapchain on WGPU). For any other window index → invalid/0 + log.
3. **`html5_canvas_selector` is the "window handle."** Map window #0's identity to
   the canvas selector string; there is no HWND/XID/NSWindow equivalent to expose.
   `sapp_x11_get_window`-style native-handle getters have no web analog.
4. **DPI is single, window/document-wide** (`emscripten_get_device_pixel_ratio`,
   gated by `high_dpi`). No per-window DPI. Per-monitor spanning not representable.
5. **Timing feeds from the external browser clock** (rAF timestamp ÷1000, or
   `performance.now()`); the internal `_sapp_timestamp_now` asserts false. Do not
   wire a monotonic clock on web.
6. **No owned loop:** `_sapp_emsc_run` must return after registering the rAF
   callback. Cleanup/quit lives in the callback (return `EM_FALSE`), not after run.

### (c) Known hazards
1. **emdawnwebgpu vs legacy `-sUSE_WEBGPU` (BIGGEST):** this sokol uses the modern
   Dawn `webgpu.h` (§3). The web WGPU build MUST use the emdawnwebgpu port
   (`--use-port=emdawnwebgpu`, matching the include `<webgpu/webgpu.h>` at 2413 and
   the `WGPUEmscriptenSurfaceSourceCanvasHTMLSelector` type). The legacy Emscripten
   WebGPU bindings will NOT compile this code. Confirm the CMake flag (§13).
2. **`skip_present` is compiled out on web** (4261) — do not build event-driven
   render suppression on it for web; the browser auto-presents at rAF.
3. **`init_done` gate + `wgpuInstanceProcessEvents` per frame** — forget either and
   web WGPU never renders (device request callback never delivered / frame runs
   before device ready). The port's web-WGPU frame must pump events and gate.
4. **GLES3 include-order gotcha (parity with P2's `GL_GLEXT_PROTOTYPES`):** the
   emscripten include block (2448–2453) pulls `<GLES3/gl3.h>` **only for
   SOKOL_GLES3**, before `<emscripten/html5.h>`. In the SOKOL_IMPL TU, `<GLES3/gl3.h>`
   must be visible before sokol_gfx.h's GLES3 loader in the same translation unit.
   For WGPU there is NO GL header pulled — do not accidentally include GLES3 headers
   in the WGPU build. `<webgpu/webgpu.h>` (2413) must precede any use of WGPU types;
   the emdawnwebgpu port header has its own ordering constraints with sokol_gfx.h's
   WGPU backend (include the port's webgpu.h before sokol_gfx.h IMPL).
5. **Keyboard-on-canvas requires `tabindex`** (§6) — a purely HTML requirement that
   is invisible to the C code. If the shell drops tabindex, keyboard silently dies.
   Ship tabindex in the TrussC web shell and treat canvas-target keyboard as the
   default.
6. **`Module.*` JS state (sapp_emsc_target, sokol_*, __sapp_custom_cursors) is the
   real backend state** — the port's EM_JS blocks and the C side must agree on these
   names, and `sapp_js_init` must run first to populate `Module.sapp_emsc_target`.
7. **`_sapp.fullscreen` forced false at start** (8066) and fullscreen/pointer-lock/
   clipboard-copy all require a user gesture — the port cannot drive these outside
   an event callback.
8. **`-sASYNCIFY=1` is enabled (§13).** The rAF/main-loop callback may be unwound by
   Asyncify at any suspension point. Do NOT create GPU resources mid-frame in a way
   that reenters the loop, and keep the WGPU async chain (adapter/device callbacks)
   compatible with Asyncify — this is the same failure class as the known web
   mid-frame resource crashes. The port is an include-swap
   (`sokol_impl.cpp: sokol_app.h → sokol_app_tc.h`), so the surrounding build flags
   (Asyncify, emdawnwebgpu, FETCH, memory growth) are FIXED — the new backend must
   work under them unchanged.

---

## Appendix: function → line index (emscripten-relevant)

| symbol | line |
|---|---|
| API select / `#error` | 2330–2335 |
| WGPU header + `_SAPP_WGPU_HAS_WAIT` (off on web) | 2412–2417 |
| emscripten includes (GLES3 gl3.h, emscripten.h, html5.h) | 2448–2453 |
| `_sapp_timestamp_*` (dummy / assert-false on web) | 2579, 2609, 2626 |
| `_sapp_wgpu_t` struct | 2720–2733 |
| `_sapp_emsc_t` struct | 2881–2887 |
| `_sapp.emsc` field | 3302–3303 |
| `_sapp_wgpu_stringview` / `_callbackmode` / `_await` | 3830 / 3842 / 3850 |
| `_sapp_wgpu_pick_render_format` | 3864 |
| `_sapp_wgpu_create_swapchain` (surface source emsc @3891, alphaMode @3936) | 3880 |
| `_sapp_wgpu_discard_swapchain` / `_swapchain_next` / `_size_changed` | 3984 / 4009 / 4037 |
| `_sapp_wgpu_device_lost_cb` / `_logging_cb` (@4057 !emsc) / `_uncaptured_error_cb` | 4044 / 4058 / 4077 |
| `_sapp_wgpu_request_device_cb` (@4101 !emsc logging) | 4087 |
| `_sapp_wgpu_create_device_and_swapchain` | 4110 |
| `_sapp_wgpu_request_adapter_cb` / `_create_adapter` | 4180 / 4199 |
| `_sapp_wgpu_init` / `_discard` / `_frame` (@4261 present !emsc) | 4213 / 4237 / 4253 |
| `_sapp_emsc_onpaste` | 6955 |
| `_sapp_html5_get_ask_leave_site` | 6966 |
| `_sapp_emsc_begin_drop` / `_drop` / `_end_drop` | 6970 / 6984 / 7002 |
| `_sapp_emsc_invoke_fetch_cb` | 7027 |
| `_sapp_emsc_set_fullscreen_flag` | 7042 |
| EM_JS beforeunload add/remove | 7050 / 7060 |
| EM_JS clipboard listener add/remove/write | 7064 / 7075 / 7079 |
| `_sapp_emsc_set_clipboard_string` | 7097 |
| EM_JS dragndrop listeners / dropped_file_size / fetch_dropped_file / remove | 7101 / 7143 / 7153 / 7174 |
| EM_JS `sapp_js_init` | 7183 |
| pointerlock change/error cb | 7204 / 7211 |
| EM_JS request/exit pointerlock | 7220 / 7228 |
| `_sapp_emsc_lock_mouse` / `_update_mouse_lock_state` | 7234 / 7248 |
| EM_JS `sapp_js_set_cursor` / `_update_cursor` | 7256 / 7281 |
| EM_JS make/destroy custom cursor + C wrappers | 7289 / 7361 / 7369 / 7374 |
| `_sapp_emsc_fullscreenchange_cb` | 7379 |
| EM_JS `sapp_js_toggle_fullscreen` / `_sapp_emsc_toggle_fullscreen` | 7386 / 7430 |
| EM_JS favicon clear/set / `_sapp_emsc_set_icon` | 7438 / 7445 / 7460 |
| mouse/key/touch event mods helpers | 7469 / 7477 / 7487 / 7497 |
| `_sapp_emsc_size_changed` | 7507 |
| `_sapp_emsc_mouse_cb` | 7559 |
| `_sapp_emsc_wheel_cb` | 7632 |
| `_sapp_emsc_keymap[]` / `_translate_key` / `_is_char_key` | 7656 / 7765 / 7777 |
| `_sapp_emsc_key_cb` | 7783 |
| `_sapp_emsc_touch_cb` | 7853 |
| `_sapp_emsc_focus_cb` / `_blur_cb` | 7896 / 7907 |
| `_sapp_emsc_webgl_context_cb` / `_webgl_init` (GLES3) | 7919 / 7935 |
| `_sapp_emsc_register_eventhandlers` (kbd-on-canvas patch @7963) | 7953 |
| `_sapp_emsc_unregister_eventhandlers` (@7998) | 7991 |
| `_sapp_emsc_frame_animation_loop` / `_frame_main_loop` | 8027 / 8057 |
| `_sapp_emsc_run` | 8064 |
| `main` (SOKOL_NO_ENTRY out) | 8105 |
| `sapp_run` (NO_ENTRY dispatch → `_sapp_emsc_run`) | 13941 / 13947 |
| `sapp_color_format` (WGPU/GLES3) | 14018 |
| `sapp_toggle_fullscreen` / `_update_cursor` / `_lock_mouse` (emsc branches) | 14104 / 14116 / 14141 |
| `sapp_set/get_clipboard_string` (emsc) | 14242 / 14261 |
| `sapp_set_icon` (emsc) | 14291 |
| `sapp_html5_get_dropped_file_size` / `_fetch_dropped_file` | 14338 / 14351 |
| `sapp_get_environment` / `sapp_get_swapchain` (wgpu @14442 / gl @14477) | 14387 / 14417 |
| `sapp_html5_ask_leave_site` / `sapp_skip_present` | 14592 / 14597 |
| glue `sglue_environment` / `sglue_swapchain` | glue.h 159 / 178 |
