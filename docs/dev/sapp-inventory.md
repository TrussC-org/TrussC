# sokol_app (sapp_) usage inventory

Purpose: complete inventory of everything TrussC consumes from `sokol_app.h`,
to prepare a drop-in replacement header (`sokol_app_tc.h`). Scope: `core/`,
`addons/`, a quick sweep of `examples/`/`apps/`/`tools/`, plus the existing
TrussC patches already applied to `sokol_app.h` (these must carry over).

Method: `grep -rohE 'sapp_[A-Za-z0-9_]*' <dir> --exclude-dir=sokol | sort | uniq -c`
per file, read call sites for context. Counts are per-identifier occurrence
(not unique call sites — a line using the same identifier twice counts twice).

Headline numbers: **core/ (excluding `sokol/`) uses 27 distinct `sapp_*`
identifiers** across 10 files, overwhelmingly concentrated in `TrussC.h`
(52 of ~75 occurrences). Addons add 3 more identifiers (all via
`tcxImGui/src/sokol_imgui.h`, which is the single largest consumer outside
core). Only 1 identifier (`sapp_request_quit`) leaks into example/app user
code, and only in the trivial "Esc to quit" idiom (9 sites). Nothing in
`tools/` (trusscli) touches sapp at all.

---

## Table 1: API usage

### Lifecycle / frame

| sapp API | call sites | files | what it provides | per-window / app-global | platforms |
|---|---|---|---|---|---|
| `sapp_run` | 3 | TrussC.h (`runApp`), tcHotReloadHost.h (`runHotReloadApp`) x2 | Starts the sokol_app event loop from a built `sapp_desc` | app-global (one loop) | all desktop (not called on Android — see notes) |
| `sapp_desc` (type) | 8 | TrussC.h, tcHotReloadHost.h | Descriptor struct: width/height/title/high_dpi/sample_count/fullscreen/swap_interval, `init_cb`/`frame_cb`/`cleanup_cb`/`event_cb`, `logger.func`, `enable_dragndrop`+`max_dropped_files`+`max_dropped_file_path_length`, `enable_clipboard`+`clipboard_size` | app-global | all |
| `sapp_quit` | 2 | TrussC.h (`exitApp`), tcHotReloadHost.h (host init failure) | Immediate, non-cancellable exit | app-global | all |
| `sapp_request_quit` | 2 (core) + 9 (examples) | TrussC.h (`requestExitApp`), tcStandardTools.h; examples/\*/tcApp.cpp (9 files: coordinateConversionExample, 3DPrimitivesExample, vectorMathExample, loopModeExample, hitTestExample, nodeExample, graphicsExample, roundedRectExample, colorExample) | Cancellable exit; triggers `SAPP_EVENTTYPE_QUIT_REQUESTED` | app-global | all except Android (patched out — see Table 2 item "Android BACK key") |
| `sapp_cancel_quit` | 1 | TrussC.h (`_event_cb`, `SAPP_EVENTTYPE_QUIT_REQUESTED` handler) | Cancels a pending quit if `exitRequested` listener sets `args.cancel` | app-global | all |
| `sapp_frame_duration` | 3 | TrussC.h (x2, update-loop dt + first-frame estimate), tcxImGui.h/sokol_imgui.h (`simgui_desc.delta_time`) | Smoothed per-frame delta time from sokol's internal timer | app-global | all |
| `sapp_frame_count` | 5 | tcTexture.h x2 (`loadData` once-per-frame guard), tcFont.h x2 (dpi + `flushPendingDestroys` once-per-frame guard), tcGlobal.cpp (`getDrawCount()`) | Monotonic frame counter | app-global | all |
| `sapp_skip_present` | 1 | TrussC.h (`_frame_cb`, after `endSwapchainPass`) | Skips the next present/swap call (event-driven low-power redraw without visible flicker) | app-global | all (TrussC-added function; see Table 2) |
| `sapp_event` (type) | 3 | TrussC.h (`_event_cb` signature x1), tcCoreEvents.h (`rawEvent` listener signature x2) | Raw input event struct forwarded to `events().rawEvent` for addons (tcxImGui) | app-global | all |
| `sapp_event_type` enum consts (`SAPP_EVENTTYPE_*`) | ~14 distinct + counts | TrussC.h `_event_cb` switch | Dispatch table: KEY_DOWN/UP, MOUSE_DOWN/UP/MOVE/SCROLL, TOUCHES_BEGAN/MOVED/ENDED/CANCELLED, RESIZED, FILES_DROPPED, CLIPBOARD_PASTED, QUIT_REQUESTED | app-global (delivered per active window in multi-window future) | all (touch: Android/iOS/web only) |

### Window geometry / DPI

| sapp API | call sites | files | what it provides | per-window / app-global | platforms |
|---|---|---|---|---|---|
| `sapp_width` | 11 | TrussC.h (getWidth/getViewport x4, resize handler, aspect ratio, screenshot helpers), tcVideoRecorder.h x2, tcHotReloadHost.h, tcxLut/tcLut.h, tcxImGui.h/sokol_imgui.h, tcPlatform_linux.cpp, tcPlatform_android.cpp, tcVideoPlayer_linux.cpp | Framebuffer width in pixels | **app-global only for the main window** — `getWidth()` branches on `ctx.isMain` and uses `ctx.fbWidth` for secondary windows (see Notes) | all |
| `sapp_height` | 9 | same set minus one | Framebuffer height in pixels | same caveat as above | all |
| `sapp_dpi_scale` | 11 | TrussC.h (getDisplayScaleFactor x1 + 6 more sites), tcFont.h (glyph physical-size scaling), tcHotReloadHost.h, tcxLut/tcLut.h, tcxImGui.h/sokol_imgui.h, tcPlatform_android.cpp (`getDisplayScaleFactor`), tcPlatform_win.cpp (indirectly via setWindowSize), tcVideoPlayer_linux.cpp | Display scale factor (Retina/HiDPI); on Android patched to use real `AConfiguration` density instead of fb/win ratio (Table 2) | same `isMain` caveat | all |
| `sapp_sample_count` | 2 | tcMeshPointPipeline.h, tcMeshPbrPipeline.h | MSAA sample count for the default swapchain, used to pick/build the right pipeline when not inside an FBO pass | app-global | all |

### Events / input

| sapp API | call sites | files | what it provides | per-window / app-global | platforms |
|---|---|---|---|---|---|
| `sapp_event` fields (`type`, `modifiers`, `key_code`, `key_repeat`, `mouse_x/y`, `mouse_button`, `scroll_x/y`, `num_touches`, `touches[]`) | many (read, not counted separately) | TrussC.h `_event_cb` | Source data for TrussC's `KeyEventArgs`/`MouseEventArgs`/`ScrollEventArgs`/`TouchEventArgs` and legacy `App::handle*` callbacks | app-global input → written into `WindowContext` (mouseX/Y, keysPressed) | all |
| `SAPP_MODIFIER_SHIFT/CTRL/ALT/SUPER` | 4 | TrussC.h `_event_cb` | Modifier bitmask decode | app-global | all |
| `SAPP_KEYCODE_*` (30 distinct constants) | 35 | TrussC.h: `constexpr int KEY_*` alias table ~L1827-1866 (SPACE/ESCAPE/ENTER/TAB/BACKSPACE/DELETE, arrows, F1-F12) + `isShiftPressed`/`isControlPressed`/`isAltPressed`/`isSuperPressed` modifier-check helpers ~L1567-1572 (LEFT/RIGHT_SHIFT/CONTROL/ALT/SUPER) | Named key constants re-exported as TrussC's own `KEY_*` constants, so user code never needs to spell `SAPP_KEYCODE_*` | app-global | all |
| `sapp_get_num_dropped_files` | 1 | TrussC.h (`SAPP_EVENTTYPE_FILES_DROPPED` handler) | Count of files in a drag-and-drop event | app-global | desktop (macOS/Windows/Linux) + web |
| `sapp_get_dropped_file_path` | 1 | TrussC.h (same handler, loop) | Path of the i-th dropped file | app-global | desktop + web |
| `sapp_consume_event` | 4 | addons/tcxImGui/src/sokol_imgui.h (`simgui_handle_event`, key down/up when ImGui wants keyboard) | Marks the current event as consumed so sokol_app / TrussC's own dispatch doesn't double-handle it | app-global | all |
| `sapp_keyboard_shown` / `sapp_show_keyboard` | 1 each | addons/tcxImGui/src/sokol_imgui.h (`simgui_new_frame`, toggles on-screen keyboard based on `io->WantTextInput`) | On-screen (mobile) keyboard visibility control | app-global | Android/iOS (no-op elsewhere) |
| `sapp_ios_set_supported_orientations` | 1 | tcPlatform_ios.mm | Runtime orientation mask control | per-app (iOS has one screen) | iOS (TrussC-patched API — Table 2) |

### Mouse cursor

| sapp API | call sites | files | what it provides | per-window / app-global | platforms |
|---|---|---|---|---|---|
| `sapp_show_mouse` | 2 | TrussC.h (`showCursor`/`hideCursor`) | Show/hide system cursor | app-global | all |
| `sapp_set_mouse_cursor` / `sapp_get_mouse_cursor` | 1 / 1 (core) + 2 (sokol_imgui.h ImGui-driven cursor sync) | TrussC.h (`setCursor`/`getCursor`), sokol_imgui.h `simgui_new_frame` (maps `ImGuiMouseCursor` → `sapp_mouse_cursor` each frame unless `disable_set_mouse_cursor`) | Set/read current cursor shape (system cursor enum or custom slot) | app-global | all |
| `sapp_mouse_cursor` (type) + `SAPP_MOUSECURSOR_*` (11 system + 16 `CUSTOM_0..15` slots) | 40 + many enum refs | TrussC.h `enum class Cursor` (1:1 mirror of the sokol enum) | Cursor shape enum, including 16 custom-image slots | app-global | all |
| `sapp_bind_mouse_cursor_image` / `sapp_unbind_mouse_cursor_image` | 1 / 1 | TrussC.h (`bindCursorImage`/`unbindCursorImage`) | Upload/release a custom RGBA cursor image + hotspot into a `CUSTOM_n` slot | app-global | all (custom cursor images not exercised much beyond this wrapper — no example uses it) |
| `sapp_image_desc` (type) | 1 | TrussC.h (`bindCursorImage`) | Struct for `{width, height, pixels, cursor_hotspot_x/y}` passed to bind | app-global | all |

### Clipboard

| sapp API | call sites | files | what it provides | per-window / app-global | platforms |
|---|---|---|---|---|---|
| `sapp_set_clipboard_string` | 1 (core) + 1 (sokol_imgui.h) | TrussC.h (`setClipboardString`), sokol_imgui.h (ImGui `SetClipboardTextFn`) | Write OS clipboard | app-global | all |
| `sapp_get_clipboard_string` | 1 (core) + 1 (sokol_imgui.h) | TrussC.h (`getClipboardString`), sokol_imgui.h (ImGui `GetClipboardTextFn`) | Read OS clipboard | app-global | all |
| `SAPP_EVENTTYPE_CLIPBOARD_PASTED` | 1 | TrussC.h `_event_cb` | Notifies when web clipboard content becomes available (web needs the paste event; desktop can read clipboard synchronously) | app-global | primarily web |
| `desc.enable_clipboard` / `desc.clipboard_size` | (part of `sapp_desc` count above) | TrussC.h `buildAppDescriptor`, tcHotReloadHost.h | Enables clipboard subsystem + buffer size (mirrored into `WindowContext::clipboardSize` for overflow warnings) | app-global | all |

### Swapchain / native handles

| sapp API | call sites | files | what it provides | per-window / app-global | platforms |
|---|---|---|---|---|---|
| `sapp_get_swapchain` | 1 (core) + 1 (win) + 1 (mac) | tcGlobal.cpp (fallback when `ctx.acquireSwapchain` is null), tcPlatform_win.cpp (`captureWindow` — reads `sc.d3d11.render_view`), tcPlatform_mac.mm (`captureWindowAsync` fallback — reads `.metal.current_drawable`) | Current-frame `sg_swapchain` handle (backend-specific texture/view/drawable) | **app-global / main-window only** — secondary windows supply their own via `ctx.acquireSwapchain` hook, bypassing sapp entirely | all (fields vary: d3d11/.metal/.gl/.wgpu) |
| `sglue_swapchain()` (sokol_glue, wraps `sapp_get_environment()` + `sapp_get_swapchain()`) | 1 | tcGlobal.cpp (`beginSwapchainPass`, main-window default) | Bridges sapp's swapchain/environment into an `sg_pass.swapchain` | app-global (main window) | all |
| `sapp_macos_get_window` | 3 | tcPlatform_mac.mm (window-level ops: always-on-top, position, etc.) | Native `NSWindow*` for the sokol-owned main window | per-window (main only) | macOS |
| `sapp_win32_get_hwnd` | 6 | tcPlatform_win.cpp | Native `HWND` for the sokol-owned main window | per-window (main only) | Windows |
| `sapp_x11_get_window` | 2 | tcPlatform_linux.cpp | Native `Window` (X11) for the sokol-owned main window | per-window (main only) | Linux |
| `sapp_ios_get_window` | 1 | tcFileDialog_ios.mm | Native `UIWindow*` (for presenting a document picker) | per-window | iOS |
| `sapp_android_get_native_activity` | 6 | tcPlatform_android.cpp (x4), tcSerial_android.cpp (x2), tcxCurl_android.cpp (x2 — wait, counted separately below) | `ANativeActivity*` — JNI bridge for file dialogs, serial permissions, network state | app-global (Android is single-activity) | Android |

### Window control / fullscreen

| sapp API | call sites | files | what it provides | per-window / app-global | platforms |
|---|---|---|---|---|---|
| `sapp_set_window_title` | 1 | TrussC.h (`setWindowTitle`) | Set OS window title | main window only | all (no-op/limited on web) |
| `sapp_is_fullscreen` | 2 | TrussC.h (`setFullscreen`, `isFullscreen`) | Query fullscreen state | main window only | all |
| `sapp_toggle_fullscreen` | 2 | TrussC.h (`setFullscreen`, `toggleFullscreen`) | Toggle fullscreen (sokol has no "set" — only toggle) | main window only | all |

Note: `setWindowSize`/`setWindowSizeLogical` do **not** go through sapp at
all — sokol_app has no runtime desktop resize API, so TrussC calls native
platform code directly (`SetWindowPos`/`NSWindow setFrame`/`XResizeWindow`)
using the native handle obtained via `sapp_*_get_*window` above. `sapp_dpi_scale()`
is used only to convert between pixel-perfect and logical size requests.

### Misc / android-serial / curl (indirect, via native activity handle)

| sapp API | call sites | files | what it provides | per-window / app-global | platforms |
|---|---|---|---|---|---|
| `sapp_android_get_native_activity` (addon) | 2 | addons/tcxCurl/src/tcxCurl_android.cpp | Same JNI bridge, used to query Android network connectivity for libcurl | app-global | Android |

---

## Table 2: TrussC's existing sokol_app.h patches

(Source: `core/include/sokol/TRUSSC_MODIFICATIONS.md`, cross-checked against
`grep -n "tettou771\|\[TrussC" core/include/sokol/sokol_app.h`.)

| patch | location (anchor) | why it exists | must carry over? |
|---|---|---|---|
| **Skip Present** (adds `sapp_skip_present()`) | Declaration ~L2258; `skip_present` field in `_sapp_t` ~L3276; checked in `_sapp_wgpu_frame` ~L4262, `_sapp_vk_frame` ~L5022, `_sapp_d3d11_present` ~L8751, `_sapp_wgl_swap_buffers` ~L9059, Android EGL swap ~L10530, Linux GLX swap ~L12566, Linux EGL swap ~L13829, public impl `sapp_skip_present()` ~L14596 | Fixes D3D11 flickering in event-driven (non-continuous) rendering by letting TrussC skip the next present call after a frame that shouldn't visibly swap | **Yes — used by `TrussC.h::_frame_cb`** (1 call site in core); needed on all backends it patches, but D3D11 is the motivating case |
| **Emscripten: keyboard events on canvas, not window** | Register ~L7963–7972 (`emscripten_set_keydown/keyup/keypress_callback` target changed from `EMSCRIPTEN_EVENT_TARGET_WINDOW` to `_sapp.html5_canvas_selector`); unregister ~L7998 | Lets other page elements (e.g. Monaco editor embedded alongside a TrussSketch canvas) receive keyboard input independently; canvas needs a `tabindex` to be focusable | Yes — required for TrussSketch web embedding use case |
| **10-bit color output (RGB10A2)** | Enum `SAPP_PIXELFORMAT_RGB10A2` ~L1865; macOS Metal layer + MSAA tex ~L5095/L5217; iOS Metal layer + MSAA tex ~L6407/L6488; D3D11 swapchain format + resize ~L8592/L8717/L8745; `sapp_color_format()` report ~L14041; plus 1-line mapping in `sokol_glue.h` (`SAPP_PIXELFORMAT_RGB10A2 → SG_PIXELFORMAT_RGB10A2`) | Reduces color banding on 10-bit displays at no bandwidth cost (same 32-bit/px as BGRA8); Metal + D3D11 only, WebGL unchanged | Yes — visual quality feature, no known regressions |
| **iOS custom view controller + runtime orientation control** | `_sapp_ios_view_ctrl` @interface/@impl ~L6917–6918 area; `sapp_ios_set_supported_orientations()` decl ~L14508 region | Upstream `UIViewController` doesn't support changing supported orientations at runtime; TrussC apps need this (e.g. rotate-to-landscape features) | Yes — `tcPlatform_ios.mm` calls `sapp_ios_set_supported_orientations()` directly (1 call site) |
| **iOS immersive mode** | Non-static `_sapp_ios_immersive_mode` global + `prefersStatusBarHidden`/`prefersHomeIndicatorAutoHidden` overrides on `_sapp_ios_view_ctrl` | Hide status bar / home indicator for fullscreen iOS experiences; `tcPlatform_ios.mm` reads/writes the extern global directly (not a function call) | Yes — used by `tcPlatform_ios.mm` (`getImmersiveMode`/set) |
| **iOS: use view bounds instead of `UIScreen.mainScreen.bounds`** | `_sapp_ios_update_dimensions()` ~L6658 | Avoids a timing bug where screen bounds don't yet match actual view layout; falls back to screen bounds if view isn't laid out yet | Yes — correctness fix for iOS sizing |
| **iOS: read back actual Metal drawable dimensions** | `_sapp_ios_mtl_update_framebuffer_dimensions()` ~L6512 | After setting drawable size, re-reads `layer.drawableSize` so `sapp_width()`/`sapp_height()` always matches what Metal will actually render, preventing scissor-rect-exceeds-render-pass errors | Yes — correctness fix |
| **Android: BACK key no longer triggers shutdown** | `_sapp_android_key_event()` ~L10594 | Upstream calls `_sapp_android_shutdown()` on BACK, destroying the EGL surface + firing `cleanup_cb`; under screen-pinning (lock-task) `ANativeActivity_finish()` is blocked by the OS, so the process survives but EGL is gone → frozen app. Event is consumed without shutdown instead. Documented long-term fix: implement `SAPP_EVENTTYPE_QUIT_REQUESTED` for Android (already implemented for macOS/Windows/Linux) | Yes — kiosk/lock-task deployments depend on this |
| **Android: real display density for `sapp_dpi_scale()`** | `_sapp_android_update_dimensions()` ~L10482 (`#include <android/configuration.h>` + `AConfiguration_getDensity()` / 160.0f) | Upstream computes dpi_scale from framebuffer/window pixel ratio, which is unreliable; using actual density makes Android's `sapp_dpi_scale()` consistent with macOS/Windows semantics | Yes — `tcFont.h` and others rely on `sapp_dpi_scale()` being meaningful cross-platform |
| **CGBitmapInfo enum-cast (silence C++20 warning)** | `_sapp_macos_set_dock_tile()` CGImage creation ~L5833 | C++20 flags `-Wdeprecated-enum-enum-conversion` on the original `CGImageAlphaInfo | CGImageByteOrderInfo` OR; explicit cast to the destination `CGBitmapInfo` (`uint32_t`) type, no behavior change | Yes, trivially (or simply avoid the warning in the rewritten code) |
| **(Removed) SetForegroundWindow (Win32)** | N/A — historical | Was in sokol_app.h; moved to `trussc::bringWindowToFront()` in `tcPlatform.h`/platform files, made cross-platform | N/A — already lives outside sokol_app; nothing to port from sokol_app.h itself |

Also documented but **not sokol_app.h** (carries over to the `sokol_gl_tc.h`
fork, out of scope for this inventory but noted since it's in the same
"sokol modifications" family): `sg_append_buffer` multi-draw-per-frame,
auto-grow CPU/GPU buffers, `sgl_tc_*` public API additions, float vertex
colors (UBYTE4N→FLOAT4). These live in `util/sokol_gl_tc.h`, not
`sokol_app.h`, and are unaffected by a sokol_app replacement.

---

## Table 3: Unused sokol_app features

| feature | evidence it's unused | port now / defer / never |
|---|---|---|
| Mouse lock / pointer lock (`sapp_lock_mouse`, `sapp_mouse_locked`) | Zero hits anywhere in core/addons/examples (`grep sapp_lock_mouse\|sapp_mouse_locked` → empty) | Defer — no current feature needs it, but cheap to keep if the replacement supports it structurally |
| Window icon API (`sapp_set_icon`, `sapp_icon_desc`) | Zero hits outside `sokol_app.h` itself; TrussC's app icons are handled by CMake/platform resource embedding (`.ico`/`Info.plist`/`resources/`), not sapp | Never (for the sapp path) — keep using native resource-embedded icons |
| Vulkan backend (`sapp_vk_*`, `SOKOL_VULKAN`) | No `SOKOL_VULKAN` define anywhere in `core/CMakeLists.txt`; backends selected are Metal (mac/iOS), D3D11 (Windows), GLCORE (Linux desktop/Raspbian via GLES3), GLES3 (Android, Raspbian, web default), optional WGPU (web only) | Never (unless a future Vulkan backend is added — not currently planned) |
| WGPU backend (`sapp_wgpu_*`) | Only reachable via `TC_GRAPHICS_BACKEND STREQUAL "WGPU"` opt-in on Emscripten (`core/CMakeLists.txt` L188-193); default web build uses GLES3 | Defer — keep as an optional path if the replacement wants to support it, but it's not exercised by default CI/build |
| GL desktop path on macOS/iOS (`sapp_gl_*`, `sapp_macos_gl_*`) | macOS/iOS always build `SOKOL_METAL` (CMakeLists.txt), never `SOKOL_GLCORE`/`SOKOL_GLES3` on Apple platforms | Never for Apple; GLCORE path IS used on Linux desktop/Raspbian, GLES3 on Android — so the GL backend overall is NOT dead, just dead specifically on Apple |
| Custom mouse cursor images beyond the wrapper (`sapp_bind/unbind_mouse_cursor_image`) | `TrussC.h` exposes `bindCursorImage`/`unbindCursorImage` wrappers (1 call site each) but no example/app in the repo calls them | Port now (thin, already wired) but low priority to verify beyond compiling — no exerciser exists |
| Gamepad input | sokol_app has no gamepad API at all (not a TrussC omission — upstream doesn't expose this) | N/A — not a sokol_app feature to begin with |
| `sapp_html5_*` fetch / drag&drop / ask-leave-site internals | Only the public surface (`enable_dragndrop`, `SAPP_EVENTTYPE_FILES_DROPPED`, `SAPP_EVENTTYPE_CLIPBOARD_PASTED`) is touched by TrussC; the `sapp_html5_fetch_*`/`sapp_js_*` symbols found in the earlier broad grep are sokol_app's own internal implementation, not called by TrussC code | N/A (internal to sokol_app, not a TrussC dependency) |
| iOS/Android/Web-specific internals (`sapp_ios_mtl_*`, `sapp_android_egl_*`, `sapp_emsc_*`, `sapp_x11_*`, `sapp_win32_*`, `sapp_wgl_*`, `sapp_glx_*`, `sapp_egl_*`, `sapp_dxgi_*`, `sapp_d3d11_*` private helpers) | These are sokol_app's own backend implementation details (all inside `core/include/sokol/sokol_app.h`, prefixed `_sapp_` in the source even though the broad grep strips the leading underscore) — TrussC never calls them directly except through the small public surface documented in Table 1 | N/A — these must exist in the replacement's own backend implementations, but they are not a "TrussC consumes X" dependency; they're implementation, not API surface |
| Fullscreen: TrussC never calls a "set fullscreen(bool)" sapp function directly | sokol_app only exposes `sapp_toggle_fullscreen()` (no direct setter); TrussC's `setFullscreen(bool)` wrapper compares against `sapp_is_fullscreen()` first, then toggles | Port now — trivial, already wrapped this way |
| Runtime desktop window resize | No `sapp_set_window_size` — doesn't exist upstream. TrussC's `setWindowSizeLogical` bypasses sapp entirely via native handles (`sapp_macos_get_window`, `sapp_win32_get_hwnd`, `sapp_x11_get_window`) | Port now — but note the native-handle escape hatch (`sapp_*_get_*window`) is exactly how TrussC already works around this sokol_app gap; the replacement should keep exposing an equivalent native handle getter |

---

## Notes

- **Entry point / `TC_RUN_APP` — there is no macro; it's a template function.**
  `runApp<AppClass>(settings)` (in `TrussC.h`, ~L2622–2644) builds a `sapp_desc`
  via `buildAppDescriptor<AppClass>()` and calls `sapp_run(&desc)`. All the
  `App` lifecycle glue (`internal::appSetupFunc`, `appUpdateFunc`,
  `appDrawFunc`, `appCleanupFunc`, plus the per-input-type forwarding lambdas)
  is wired inside `buildAppDescriptor` before the descriptor is returned.
  `main.cpp` in every example just calls `return trussc::runApp<tcApp>(settings);`.
  **On Android**, `sapp_run` is never called — `runApp<AppClass>()` just stores
  the built descriptor into `internal::g_androidDesc`, and
  `core/platform/android/sokol_impl.cpp`'s `sokol_main(argc, argv)` calls the
  app's `main()` (which populates that global via `runApp`) then returns the
  stored descriptor, because `ANativeActivity_onCreate` (provided by
  sokol_app.h without `SOKOL_NO_ENTRY` on Android) is the real entry point and
  calls `sokol_main()` to get it.

- **There are two independent places that build a `sapp_desc` and call `sapp_run`**:
  `TrussC.h::buildAppDescriptor`/`runApp` (normal path) and
  `tcHotReloadHost.h::runHotReloadApp` (hot-reload path, `internal::` namespace).
  They duplicate almost the entire descriptor-building logic (width/height/
  pixel-perfect conversion, high_dpi, sample_count, fullscreen, swap_interval,
  the four core callbacks, drag&drop + clipboard enable/size). A sokol_app
  replacement should ideally let both paths share one builder function instead
  of hand-duplicating the field list twice (currently they're just two copies
  kept in sync by hand).

- **`sokol_glue.h` is the sapp↔sgfx bridge.** `sglue_swapchain()` (used once,
  in `tcGlobal.cpp::beginSwapchainPass`) internally calls
  `sapp_get_environment()` + `sapp_get_swapchain()` and converts
  `sapp_pixel_format` → `sg_pixel_format` (including the TrussC-added
  `SAPP_PIXELFORMAT_RGB10A2` → `SG_PIXELFORMAT_RGB10A2` mapping, the file's
  only patch). This is the *only* place `sapp_get_environment` is reached from
  TrussC code (transitively) — TrussC itself never calls it directly.

- **Multi-window architecture already treats sapp as "main-window-only."**
  `core/include/tc/app/tcWindowContext.h`'s `WindowContext` has an `isMain`
  flag: when true, `getWidth()`/`getHeight()`/`getDisplayScaleFactor()`
  (TrussC.h ~L317-328) read `sapp_width()`/`sapp_height()`/`sapp_dpi_scale()`
  directly; when false (a Phase-1 secondary native window), they read
  `ctx.fbWidth`/`ctx.fbHeight`/`ctx.dpiScale`, which the native window layer
  keeps up to date itself, and the frame's `sg_swapchain` comes from
  `ctx.acquireSwapchain(ctx.acquireSwapchainUser)` instead of
  `sapp_get_swapchain()`. In other words: **the multi-window seam already
  exists and sapp's job is scoped to exactly one (the main) window** — a
  sokol_app replacement only needs to serve that single main-window role;
  secondary windows are handled entirely outside sapp already (see
  `core/platform/mac/tcWindowMac.mm`, which has zero `sapp_` references).

- **Emscripten/web build differs only in backend selection, not in the sapp
  API surface consumed.** `core/CMakeLists.txt` picks `SOKOL_GLES3` by default
  or `SOKOL_WGPU` if `TC_GRAPHICS_BACKEND=WGPU`; the emscripten keyboard-on-canvas
  patch (Table 2) is the only web-specific sokol_app.h modification. No
  TrussC/addon code calls `sapp_emsc_*`/`sapp_js_*`/`sapp_html5_*` directly —
  those are all sokol_app's own internal Emscripten implementation.

- **`tcxImGui/src/sokol_imgui.h`'s sapp dependency is fully self-contained
  and small.** It needs exactly: `sapp_event`/`sapp_keycode` (types, for
  `simgui_handle_event`/`simgui_map_keycode`), `sapp_consume_event` (x4, key
  down/up when ImGui wants keyboard focus), `sapp_width`/`sapp_height`/
  `sapp_frame_duration`/`sapp_dpi_scale` (feeding `simgui_new_frame`'s
  `simgui_frame_desc_t`), `sapp_set_clipboard_string`/`sapp_get_clipboard_string`
  (ImGui clipboard callbacks), `sapp_keyboard_shown`/`sapp_show_keyboard`
  (mobile on-screen keyboard sync with `io->WantTextInput`), and
  `sapp_get_mouse_cursor`/`sapp_set_mouse_cursor` (ImGui-driven cursor shape
  sync, gated by `disable_set_mouse_cursor`). `tcxImGui.h` (the TrussC-side
  wrapper) itself only touches `sapp_event` (raw-event listener signature) and
  `sapp_width`/`sapp_height`/`sapp_frame_duration`/`sapp_dpi_scale` (building
  the per-frame `simgui_frame_desc_t`) — i.e. it needs the exact same small
  surface as sokol_imgui.h itself, nothing extra.

- **User-facing code essentially never touches sapp directly.** Of ~30
  example projects, only 9 call `sapp_request_quit()` (the "Esc quits" idiom),
  and none call anything else. This means the replacement header's public API
  compatibility surface for *user* code is trivially small — the real
  compatibility burden is entirely inside `TrussC.h`/`tcHotReloadHost.h`
  (core lifecycle + input dispatch) and `tcxImGui`'s vendored `sokol_imgui.h`.

- **`tools/` (trusscli) has zero sapp references** — it's a project-generator/
  build tool, not a sokol_app consumer at all.
