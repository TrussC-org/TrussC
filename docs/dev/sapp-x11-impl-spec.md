# sokol_app.h X11 (GLX / OpenGL Core) Backend — Behavioral Specification

Implementation contract for replacing sokol_app.h's X11/Linux main-window /
app-lifecycle with `sokol_app_tc.h` (project "P2 Linux driver"). All line
references are into `core/include/sokol/sokol_app.h` (14602 lines, this
worktree). Focus: `_SAPP_LINUX` + `_SAPP_GLX` (desktop OpenGL via GLX).

**Which #ifdef path is live in TrussC.** `core/CMakeLists.txt:277` compiles the
Linux target with `SOKOL_GLCORE` (and links `X11`, `Xi`, `Xcursor`, `OpenGL`).
sokol_app resolves that at 2361–2366: `SOKOL_GLCORE && !SOKOL_FORCE_EGL` →
`#define _SAPP_GLX (1)`. So on TrussC/Linux the live paths are **`_SAPP_GLX`**
for the renderer and **`_SAPP_X11`** for windowing. The `_SAPP_EGL` path
(2363/2369, selected by `SOKOL_GLES3` or `SOKOL_FORCE_EGL`) is **dead on
desktop Linux** — it is the live path only on Android/Raspbian (`SOKOL_GLES3`,
different CMake branches). `SOKOL_VULKAN`/`SOKOL_WGPU` on Linux are also dead.
This document documents GLX and ignores EGL/Vulkan/WGPU except where a shared
X11 code path forks on them (marked inline).

This is the third sibling of `sapp-mac-impl-spec.md` (event-driven `[NSApp run]`)
and `sapp-win32-impl-spec.md` (owned PeekMessage loop). Like Win32, the X11
backend is an **explicit owned event+render loop** (`_sapp_linux_run`). Unlike
both, its frame pacing is **entirely delegated to blocking `glXSwapBuffers`
with the swap interval** — there is no timer, no CADisplayLink, no `Sleep`, and
crucially **no RandR usage anywhere** (see §11).

**Per-window vs process-global (read this first — the replacement makes windows plural):**
Almost everything is single-window global state baked into `static _sapp_t _sapp`.
The truly *process-global* pieces (must stay global / open once regardless of
window count) are:
- **`Display* display`** — the single X11 server connection (`XOpenDisplay(NULL)`,
  §1). One connection multiplexes all windows; open once, close last. **All X11
  calls take this `Display*`**, so it is the spine every per-window struct hangs off.
- **`int screen` / `Window root`** — default screen + its root window (§1). Screen-wide.
- **Interned atoms** (`UTF8_STRING`, `CLIPBOARD`, `TARGETS`, `WM_PROTOCOLS`,
  `WM_DELETE_WINDOW`, `WM_STATE`, `NET_WM_*`, all XDND atoms) — atom identity is
  per-`Display`, so intern once, share across windows (§4).
- **`_sapp.glx` function pointers + extension flags** — the whole dlopen'd libGL
  vtable and the `EXT/MESA_swap_control`, `ARB_create_context*` booleans (§3).
  Loaded once. **The `GLXContext ctx` inside it is also effectively global** —
  the multiwindow plan uses ONE shared context (see §3).
- **`_sapp.x11.xi`** (XInput2 opcode/version) — extension is per-`Display`, query once (§5).
- **`standard_cursors[]` / `hidden_cursor`** — `Cursor` XIDs; shareable across
  windows on the same `Display` (§9).
- **`_sapp.keycodes[256]`** — the X11-keycode→sapp-keycode table (§6). Global, fill once.
- **`dpi`** — currently one display-wide DPI value (§1, §4-DPI).

Everything else is **per-window** and must move into a per-window struct when
windows go plural: **`Window window`** (the X11 window XID), **`Colormap colormap`**,
**`GLXWindow glx.window`** (the GLX drawable — one per X window), the sizes
(`window_width/height`, `framebuffer_width/height`), `window_state`,
`mouse_buttons`, `custom_cursors[]`, `key_repeat[256]`, and the `xdnd` in-flight
drag state. Note the **DPI is currently a single `_sapp.x11.dpi`** shared by all —
per-monitor DPI is not implemented (§4-DPI, §11).

---

## 0. Global state touched (the shared contract)

Single global `static _sapp_t _sapp;` (definition ~3264) embeds `_sapp_x11_t x11;`
(3314) and, under GLX, `_sapp_glx_t glx;` (3316).

### `_sapp_x11_t` (3150–3179)
```c
typedef struct {
    uint8_t mouse_buttons;      // bitmask of held buttons (enter/leave suppression during drag)
    Display* display;           // THE server connection (process-global)
    int screen;                 // default screen index
    Window root;                // root window of screen
    Colormap colormap;          // per-window (created against the chosen visual)
    Window window;              // per-window (the X11 window XID)
    Cursor hidden_cursor;       // blank cursor for hide / mouse-lock
    Cursor standard_cursors[_SAPP_MOUSECURSOR_NUM];
    Cursor custom_cursors[_SAPP_MOUSECURSOR_NUM];
    int window_state;           // NormalState / IconicState / WithdrawnState (from WM_STATE)
    float dpi;                  // display-wide DPI (Xft.dpi or 96.0 fallback)
    unsigned char error_code;   // set by the temporary X error handler
    Atom UTF8_STRING, CLIPBOARD, TARGETS, WM_PROTOCOLS, WM_DELETE_WINDOW,
         WM_STATE, NET_WM_NAME, NET_WM_ICON_NAME, NET_WM_ICON,
         NET_WM_STATE, NET_WM_STATE_FULLSCREEN;
    _sapp_xi_t xi;              // XInput2 (raw mouse in lock mode)
    _sapp_xdnd_t xdnd;          // XDND drag&drop protocol state
    bool key_repeat[256];       // per-X11-keycode repeat tracking (see §6)
} _sapp_x11_t;
```
Note: **there is NO XIM/XIC (X Input Method / Input Context) anywhere in this
struct or the whole backend.** See §6 — this is a major surprise vs the task
brief, which assumed XIM/XIC handling.

### `_sapp_xi_t` (3125–3132) — XInput2 raw motion (mouse-lock only)
```c
typedef struct { bool available; int major_opcode, event_base, error_base, major, minor; } _sapp_xi_t;
```

### `_sapp_xdnd_t` (3134–3148) — XDND protocol state
```c
typedef struct {
    int version; Window source; Atom format;
    Atom XdndAware, XdndEnter, XdndPosition, XdndStatus, XdndActionCopy,
         XdndDrop, XdndFinished, XdndSelection, XdndTypeList, text_uri_list;
} _sapp_xdnd_t;
```

### `_sapp_glx_t` (3183–3222, `_SAPP_GLX` only)
```c
typedef struct {
    void* libgl;                // dlopen("libGL.so.1")
    int major, minor;           // GLX version
    int event_base, error_base;
    GLXContext ctx;             // THE GL context (shared in multiwindow plan)
    GLXWindow window;           // per-window GLX drawable
    /* GLX 1.3 fn ptrs */ GetFBConfigs, GetFBConfigAttrib, GetClientString,
        QueryExtension, QueryVersion, DestroyContext, MakeCurrent, SwapBuffers,
        QueryExtensionsString, GetVisualFromFBConfig, CreateWindow, DestroyWindow;
    /* GLX 1.4 / ext */ GetProcAddress, GetProcAddressARB, SwapIntervalEXT,
        SwapIntervalMESA, CreateContextAttribsARB;
    void (*GetIntegerv)(uint32_t, int32_t*);
    bool EXT_swap_control, MESA_swap_control, ARB_multisample,
         ARB_create_context, ARB_create_context_profile;
} _sapp_glx_t;
```

Cross-platform `_sapp` fields the X11 path reads/writes are the same set as the
other backends (`desc`, `valid`, `first_frame`, `quit_requested`, `quit_ordered`,
`fullscreen`, `window_width/height`, `framebuffer_width/height`, `dpi_scale`,
`sample_count`, `swap_interval`, `timing`, `mouse.*`, `clipboard.*`, `drop.*`,
`event`, `keycodes[]`, `custom_cursor_bound[]`, `skip_present`) **plus**
`window_title` (a plain `char[]`, UTF-8 — no wide/UTF-16 companion like Win32,
since X11 is UTF-8 native via `UTF8_STRING`). Also `_sapp.gl.framebuffer`
(`_sapp_gl_t`, 3234–3238) = the default framebuffer object id captured after
`glXMakeCurrent` (§3, §7).

`_sapp_init_state(desc)` runs first inside `_sapp_linux_run` (same as the others):
applies `_sapp_desc_defaults` (sample_count→1, swap_interval→1, clipboard_size→8192,
max_dropped_files→1, path_length→2048, window_title→"sokol", **and on GLCORE the
GL version defaults to 4.3 core** — 3529–3536: major=4, minor=3 on non-Apple),
sets `first_frame=true`, `dpi_scale=1.0`, `mouse.shown=true`,
`fullscreen=desc.fullscreen`, copies width/height verbatim (**may be 0** — the
X11 backend resolves 0 to 4/5 of the display, §4).

---

## 1. Entry / run loop (`_sapp_linux_run`, 13836–13922)

### Entry points (13924–13957)
- Default entry is a plain **`int main(int argc, char* argv[])`** (13925): calls
  `sokol_main(argc,argv)` → `_sapp_linux_run(&desc)`. **argv is passed straight
  to `sokol_main` as UTF-8** — no conversion dance (contrast Win32's
  `CommandLineToArgvW`).
- **TrussC compiles `SOKOL_NO_ENTRY`** (`core/include/TrussC.h:15`, and
  `core/platform/linux/sokol_impl.cpp`), so sokol's `main` is compiled out. The
  live path is `sapp_run(desc)` (13941) → `_sapp_linux_run(desc)` (13952), called
  from TrussC's own `runApp()` (`TrussC.h:2636`).

### Init order (13845–13887) — load-bearing, exact
1. `_sapp_init_state(desc)`; then `_sapp.x11.window_state = NormalState`.
2. **`XInitThreads()`** — must be called before any other Xlib call to make the
   connection thread-safe (TrussC runs off-thread work; keep this).
3. `XrmInitialize()` — init the resource-manager subsystem (for Xft.dpi query).
4. `_sapp.x11.display = XOpenDisplay(NULL)` — PANIC `LINUX_X11_OPEN_DISPLAY_FAILED`
   on null (no DISPLAY / no X server).
5. `screen = DefaultScreen(display)`, `root = DefaultRootWindow(display)`.
6. `_sapp_x11_query_system_dpi()` (§4-DPI) → then
   **`_sapp.dpi_scale = _sapp.x11.dpi / 96.0f`** (13858). Comment (13857): "on
   Linux system-window-size to frame-buffer-size mapping is always 1:1" — X11 has
   no separate backing-scale; DPI scaling is applied to the framebuffer directly.
7. `_sapp_x11_init_extensions()` — intern all atoms + query XInput2 (§4/§5).
8. `_sapp_x11_create_standard_cursors()` — build all `Cursor`s incl. hidden (§9).
9. **`XkbSetDetectableAutoRepeat(display, true, NULL)`** — enable detectable
   auto-repeat so key-repeat does NOT generate spurious KeyRelease/KeyPress pairs
   (§6). Process/connection-global.
10. `_sapp_x11_init_keytable()` — fill `_sapp.keycodes[]` via XKB names (§6).
11. **GLX bring-up** (13863–13870, `_SAPP_GLX` block):
    - `_sapp_glx_init()` — dlopen libGL, resolve fn ptrs, query extensions (§3).
    - `_sapp_glx_choose_visual(&visual, &depth)` — pick fbconfig → `XVisualInfo` (§3).
    - `_sapp_x11_create_window(visual, depth)` — create the X window on that visual (§4).
    - `_sapp_glx_create_context()` — create context + `GLXWindow` drawable, make current (§3).
    - `_sapp_glx_swapinterval(_sapp.swap_interval)` — set vsync on the drawable (§3).
12. `sapp_set_icon(&desc->icon)` (13880) → `_sapp_x11_set_icon` sets `_NET_WM_ICON` (§10).
13. `_sapp.valid = true;` (13881).
14. `_sapp_x11_show_window()` — `XMapWindow` + wait for `VisibilityNotify` + raise (§4).
15. If `_sapp.fullscreen`: `_sapp_x11_set_fullscreen(true)` — **must be after map** (§4).
16. `XFlush(display)` — flush the whole setup batch to the server, then enter the loop.

There is no `applicationDidFinishLaunching` equivalent; the run function does all
setup inline (like Win32), then loops.

### The main loop (13888–13907)
```c
XFlush(_sapp.x11.display);
while (!_sapp.quit_ordered) {
    _sapp_timing_update(&_sapp.timing, 0.0);          // measure dt (external_now=0 → internal clock)
    int count = XPending(_sapp.x11.display);          // # events already in the queue
    while (count--) {
        XEvent event;
        XNextEvent(_sapp.x11.display, &event);        // dequeue (won't block: count bounded)
        _sapp_x11_process_event(&event);              // dispatch (§5)
    }
    _sapp_linux_frame();                              // render + swap (BLOCKS on vsync, §1-frame)
    XFlush(_sapp.x11.display);                         // push any X requests the frame made
    if (_sapp.quit_requested && !_sapp.quit_ordered) {// quit dance (§8)
        _sapp_x11_app_event(SAPP_EVENTTYPE_QUIT_REQUESTED);
        if (_sapp.quit_requested) _sapp.quit_ordered = true;
    }
}
```
Key structural facts, contrasted with the other backends and the task brief:
- **`XPending`-snapshot-then-drain, NOT blocking `XNextEvent` at the top.**
  `XPending` returns the count of already-queued events (flushing output first);
  the inner loop drains *exactly that many* so it can't block waiting for input.
  `frame_cb` runs inside `_sapp_linux_frame()` **once per outer iteration**, not
  per event. This is the same shape as Win32's PeekMessage drain.
- **The loop has NO explicit sleep/timer.** It runs as fast as
  `_sapp_linux_frame` returns, and that function **blocks inside
  `glXSwapBuffers` for vsync** when `swap_interval >= 1`. That blocking swap IS
  the frame pacing (see §1-frame and §11). With `swap_interval == 0` the loop
  becomes a busy spin (uncapped fps).
- **Loop exit is single-flag:** the condition is only `!_sapp.quit_ordered`.
  There is no `WM_QUIT`-style sentinel event. `quit_ordered` is set either by the
  quit dance in the loop body (from a WM_DELETE_WINDOW ClientMessage or a
  programmatic `sapp_request_quit`) or directly by `sapp_quit()` (§8).

### Cleanup order (13908–13921) — mirrors the other backends' guarantee
1. `_sapp_call_cleanup()` (user `cleanup_cb`, once) — **before** any GL teardown.
2. `_sapp_glx_destroy_context()` — `glXDestroyWindow` + `glXDestroyContext` (§3).
3. `_sapp_x11_destroy_window()` — `XUnmapWindow` + `XDestroyWindow` + `XFreeColormap` (§4).
4. `_sapp_x11_destroy_standard_cursors()` — free all `Cursor`s (§9).
5. `XCloseDisplay(display)`.
6. `_sapp_discard_state()` — free clipboard/drop/icon buffers, zero `_sapp`.

### Frame function (`_sapp_linux_frame`, 13818–13834)
```c
_sapp_x11_update_dimensions_from_window_size();       // sample window size EVERY frame (§4)
/* GLX branch: */
_sapp_frame();                                        // runs init_cb (first frame) then frame_cb
_sapp_glx_swap_buffers();                             // BLOCKS on vsync; honors skip_present (§3)
```
- **Resize is polled here every frame**, not event-driven:
  `_sapp_x11_update_dimensions_from_window_size()` does `XGetWindowAttributes` and
  recomputes framebuffer size, firing RESIZED if it changed (§4). ConfigureNotify
  is *not* separately handled (§5) — this poll is the size authority. Contrast
  Win32 (loop-tail `update_dimensions`) and mac (delegate notification).
- The `_SAPP_EGL` fork here (13828–13832) calls `eglSwapBuffers` with the same
  `skip_present` guard — **dead on desktop, live on Android/Raspbian.**

### Frame timing source
`_sapp.timing` is the platform-agnostic filtered EMA timer (identical config to
the other backends: dt_min=1µs, dt_max=100ms, threshold=4ms, alpha=0.025, seed
1/60), fed by `_sapp_timing_update(&_sapp.timing, 0.0)` at the top of every loop
iteration (13889). `external_now==0` → it samples the internal monotonic clock.
**There is no display-refresh timestamp source** (no CADisplayLink, no RandR
mode query) — X11/GLX always uses the measured EMA filter. `sapp_frame_duration()`
→ `_sapp_timing_get` (smoothed); `sapp_frame_duration_unfiltered()` → raw `t->dt`.

---

## 2. GLX renderer bring-up (`_sapp_glx_*`, 12319–12579)

### libGL load + entry points (`_sapp_glx_init`, 12359–12426)
- dlopen cascade `"libGL.so.1"` then `"libGL.so"` (RTLD_LAZY|RTLD_GLOBAL); PANIC
  `LINUX_GLX_LOAD_LIBGL_FAILED` if neither loads.
- `dlsym` all GLX 1.3 entry points (list in the struct above); PANIC
  `LINUX_GLX_LOAD_ENTRY_POINTS_FAILED` if any core one is missing.
- `QueryExtension` / `QueryVersion` → PANIC on failure; **require GLX ≥ 1.3**
  (`LINUX_GLX_VERSION_TOO_LOW`).
- Query the extension string and set booleans + resolve ext fn ptrs:
  - `GLX_EXT_swap_control` → `SwapIntervalEXT` (per-drawable vsync).
  - `GLX_MESA_swap_control` → `SwapIntervalMESA` (context-wide vsync).
  - `GLX_ARB_multisample` → allow reading `GLX_SAMPLES` on fbconfigs.
  - `GLX_ARB_create_context` → `CreateContextAttribsARB`.
  - `GLX_ARB_create_context_profile` → flag.
- `getprocaddr` order: `glXGetProcAddress` → `glXGetProcAddressARB` → `dlsym`
  (12348–12357).

### fbconfig selection (`_sapp_glx_choosefbconfig`, 12434–12505)
`glXGetFBConfigs` for the screen → filter to RGBA + window-drawable configs
(with a Chromium/VirtualBox `GLX_VENDOR == "Chromium"` hack, 12442–12448, that
distrusts the window bit) → score each against a **desired config: 8/8/8/8 RGBA,
24 depth, 8 stencil, doublebuffer, `samples = sample_count>1 ? sample_count : 0`**
using the shared `_sapp_gl_choose_fbconfig` scorer. PANIC
`LINUX_GLX_NO_SUITABLE_GLXFBCONFIG` if none. `_sapp_glx_choose_visual`
(12507–12519) turns the chosen fbconfig into an `XVisualInfo` (visual + depth)
via `glXGetVisualFromFBConfig` — those are handed to `XCreateWindow` so the X
window matches the GL pixel format.

### Context + drawable (`_sapp_glx_create_context`, 12526–12552)
- Re-chooses the fbconfig (must match the window's visual).
- **Requires `ARB_create_context && ARB_create_context_profile`** (else PANIC
  `LINUX_GLX_REQUIRED_EXTENSIONS_MISSING`) — legacy `glXCreateContext` is never used.
- `CreateContextAttribsARB` with attribs:
  `MAJOR_VERSION = desc.gl.major_version` (4), `MINOR_VERSION = minor` (3),
  `PROFILE_MASK = CORE_PROFILE_BIT`, `FLAGS = FORWARD_COMPATIBLE_BIT`.
  Wrapped in `_sapp_x11_grab_error_handler()` / `_release_error_handler()` so a
  GLX BadMatch surfaces as a controlled PANIC (`LINUX_GLX_CREATE_CONTEXT_FAILED`),
  not an Xlib abort.
- **`GLXWindow window = glXCreateWindow(display, fbconfig, x11.window, NULL)`** —
  the GLX drawable wrapping the X window. **This is the per-window object in the
  multiwindow plan** (one `GLXWindow` per X window). PANIC `LINUX_GLX_CREATE_WINDOW_FAILED`.
- `_sapp_glx_make_current()` (12521–12524): `glXMakeCurrent(display, glx.window,
  glx.ctx)` then `glGetIntegerv(GL_FRAMEBUFFER_BINDING, &_sapp.gl.framebuffer)` —
  captures the default FBO id (0) that `sapp_get_swapchain` later reports (§7).

### Swap + swap interval
- `_sapp_glx_swap_buffers` (12565–12569): **honors `skip_present`**
  (TrussC patch, 12566–12567 — one-shot consume, returns without swapping), else
  `glXSwapBuffers(display, glx.window)`. **This call blocks until vsync** when the
  swap interval is ≥1 — the loop's only pacing mechanism.
- `_sapp_glx_swapinterval` (12571–12577): prefer `SwapIntervalEXT(display,
  glx.window, interval)` (**per-drawable** — so each window in the multiwindow
  plan can set its own), else fall back to `SwapIntervalMESA(interval)`
  (**context-wide** — cannot be per-window). If neither ext exists, vsync is
  whatever the driver defaults to.

### What a SECOND window needs (multiwindow-glfw pattern)
Per the design doc P2 entry (X11+GLX shared context): keep **one** `GLXContext`
(shared), and per window create its own `GLXWindow` drawable + `Colormap` +
`Window`. Before rendering window N: `glXMakeCurrent(display, glxWindowN, ctx)`,
render, `glXSwapBuffers(display, glxWindowN)`. Swap interval is per-drawable via
`SwapIntervalEXT` (set once per drawable). **Caveat for pacing:** with N vsync'd
drawables you block on N swaps per loop iteration → the frame rate can degrade to
refresh/N if the swaps don't overlap. See §11.

---

## 3. Window creation (`_sapp_x11_create_window`, 12939–13002)

- **Visual:** on GLX, the visual+depth from `_sapp_glx_choose_visual`; if null
  (Vulkan/WGPU dead paths) falls back to `DefaultVisual`/`DefaultDepth`.
- **Colormap:** `XCreateColormap(display, root, visual, AllocNone)` — **per-window**
  (must match the visual). Stored in `x11.colormap`, freed in destroy.
- **Attributes / event mask** (`XSetWindowAttributes`, mask
  `CWBorderPixel|CWColormap|CWEventMask`):
  ```
  event_mask = StructureNotifyMask | KeyPressMask | KeyReleaseMask |
               PointerMotionMask   | ButtonPressMask | ButtonReleaseMask |
               ExposureMask        | FocusChangeMask | VisibilityChangeMask |
               EnterWindowMask     | LeaveWindowMask | PropertyChangeMask;
  ```
  (This is the complete set of events the window opts into. Note `ExposureMask`
  and `VisibilityChangeMask` are selected but Expose is **not** handled in
  `process_event` — see §5.)
- **Initial size:** `x11_window_width = round(window_width * dpi/96)`. If
  `window_width == 0` → **4/5 of `DisplayWidth`** (12961–12966; same 4/5 rule as
  mac, unlike Win32's `CW_USEDEFAULT`). Position is `0,0` (WM will place it).
- `XCreateWindow(...)` wrapped in grab/release error handler; PANIC
  `LINUX_X11_CREATE_WINDOW_FAILED` on null.
- **WM protocols:** `XSetWMProtocols(WM_DELETE_WINDOW)` — the close-button
  protocol. **Note: only `WM_DELETE_WINDOW` is registered; `_NET_WM_PING` is NOT**
  (grep confirms zero `NET_WM_PING` in the file) — contrary to the task brief.
- **Size hints:** `XAllocSizeHints` with only `flags = PWinGravity;
  win_gravity = CenterGravity`. Comment (12988): "PPosition and PSize are obsolete
  and ignored." **No `PMinSize`/`PMaxSize`/`PResizeInc` are set** — there is no
  min/max-size or non-resizable option (contrary to the task brief's `PMinSize`).
- **XDND advertise:** if `drop.enabled`, set `XdndAware = _SAPP_X11_XDND_VERSION (5)`
  on the window (12996–12999). TrussC always sets `desc.enable_dragndrop = true`
  (`TrussC.h`), so this is always on.
- **Decorations (`_MOTIF_WM_HINTS`):** **NOT set by sokol at all.** sokol has no
  decoration flag in `sapp_desc`. TrussC applies it *itself* after the window
  exists — `core/platform/linux/tcPlatform_linux.cpp:83 setWindowDecorated()` sets
  `_MOTIF_WM_HINTS` via `sapp_x11_get_window()`/its own `XOpenDisplay`. **The
  replacement must keep exposing `sapp_x11_get_window()` and `sapp_x11_get_display()`**
  or TrussC's decoration/bring-to-front code breaks (§12).
- Tail: `_sapp_x11_update_window_title()` (§10) + `_sapp_x11_update_dimensions_from_window_size()`.

`_sapp_x11_show_window` (13023–13031): `XMapWindow` → `_sapp_x11_wait_for_event(
VisibilityNotify, 0.1s)` (poll-with-timeout on the connection fd, 12599–12610) →
`XRaiseWindow` → `XFlush`. `_sapp_x11_destroy_window` (13004–13015): unmap +
destroy window + free colormap + flush.

---

## 4. DPI, dimensions, fullscreen

### DPI (`_sapp_x11_query_system_dpi`, 12283–12317)
- Reads **`Xft.dpi`** from the X resource-manager string: `XResourceManagerString`
  → `XrmGetStringDatabase` → `XrmGetResource(db, "Xft.dpi", "Xft.Dpi", ...)`,
  `atof` the value. This matches Qt/GTK behaviour.
- **Fallback: 96.0** (with `_SAPP_WARN(LINUX_X11_QUERY_SYSTEM_DPI_FAILED)`) if the
  resource is absent. The commented-out alternative (12289) shows the physical
  `DisplayWidthMM` formula sokol deliberately does *not* use.
- **Single display-wide value** (`_sapp.x11.dpi`); no per-monitor query, no RandR.
  `high_dpi` in `desc` does not gate the scale on X11 the way it does on mac —
  `dpi_scale` is unconditionally `dpi/96` (see §11 for the surprise).

### Dimensions (`_sapp_x11_update_dimensions`, 12619–12641)
- **`window_scale = _sapp.x11.dpi / 96.0` — NOT `dpi_scale`** (explicit comment
  12620): `window_width = round(x11_px / window_scale)` (logical points),
  `framebuffer_width = round(window_width * dpi_scale)` (device pixels). Because
  `dpi_scale == window_scale` on X11 (both `dpi/96`), framebuffer ≈ raw X11 px.
- Fires RESIZED only if the framebuffer size changed **and** `!first_frame`
  (12636 — so the startup poll is silent, same guard as the other backends).
- `_sapp_x11_update_dimensions_from_window_size` (12643–12647): `XGetWindowAttributes`
  → feed width/height. Called **every frame** from `_sapp_linux_frame` (§1) and
  after fullscreen toggle. This is the resize authority.

### Fullscreen (`_sapp_x11_set_fullscreen`, 12649–12667)
- **EWMH `_NET_WM_STATE` client message** to the root window (via
  `_sapp_x11_send_event`, 12581–12597, `SubstructureNotify|Redirect` mask), adding
  (`_NET_WM_STATE_ADD=1`) or removing (`REMOVE=0`) `_NET_WM_STATE_FULLSCREEN`.
  **Must be called after `XMapWindow`** (comment 12650) — hence the ordering in §1.
- `sapp_toggle_fullscreen` → `_sapp_x11_toggle_fullscreen` (12758–12762): flip
  `_sapp.fullscreen`, send the state message, re-poll dimensions. `_sapp.fullscreen`
  is the single source of truth (no OS notification correction like mac; the WM
  may reject the request and there is no reconcile).

---

## 5. Event handling (`_sapp_x11_process_event`, 13637–13685)

Dispatch is a `switch(event->type)`. **Complete list of handled X event types:**

| X event | handler | behaviour |
|---|---|---|
| `GenericEvent` | `_on_genericevent` 13329 | XInput2 `XI_RawMotion` — **only in mouse-lock**; reads raw dx/dy from valuators, fires MOUSE_MOVE. Uses `XGetEventData`/`XFreeEventData` cookie dance. |
| `FocusIn` | `_on_focusin` 13353 | FOCUSED (ignores `NotifyGrab`/`NotifyUngrab` modes, like GLFW). |
| `FocusOut` | `_on_focusout` 13360 | if mouse-locked → unlock; then UNFOCUSED (same grab/ungrab filter). |
| `KeyPress` | `_on_keypress` 13371 | KEY_DOWN + CHAR (§6). |
| `KeyRelease` | `_on_keyrelease` 13390 | KEY_UP (§6). |
| `ButtonPress` | `_on_buttonpress` 13402 | MOUSE_DOWN for buttons 1–3; **buttons 4/5 = vertical wheel, 6/7 = horizontal wheel** → MOUSE_SCROLL (§5-mouse). |
| `ButtonRelease` | `_on_buttonrelease` 13422 | MOUSE_UP for buttons 1–3 (wheel buttons ignored on release). |
| `EnterNotify` | `_on_enternotify` 13434 | MOUSE_ENTER — **suppressed while any button held** (`mouse_buttons != 0`). |
| `LeaveNotify` | `_on_leavenotify` 13442 | MOUSE_LEAVE — same button-held suppression. |
| `MotionNotify` | `_on_motionnotify` 13449 | MOUSE_MOVE — **only when `!mouse.locked`** (locked motion comes via GenericEvent). |
| `PropertyNotify` | `_on_propertynotify` 13456 | watches `WM_STATE` → fires ICONIFIED (IconicState) / RESTORED (NormalState) on change. This is how iconify is detected — **there is no dedicated map/unmap event handling.** |
| `SelectionNotify` | `_on_selectionnotify` 13472 | XDND drop data arrival (§8) + clipboard-paste reply (§7). |
| `SelectionRequest` | `_on_selectionrequest` 13596 | another app reads our clipboard (§7). |
| `ClientMessage` | `_on_clientmessage` 13510 | `WM_DELETE_WINDOW` → `quit_requested=true`; XDND Enter/Position/Drop (§8). |
| `DestroyNotify` | (13678) | **explicitly a no-op** ("not a bug" comment). |

**Events selected but NOT handled:** `Expose` (ExposureMask is set but there is no
`case Expose:` — rendering is loop-driven, not paint-driven, exactly like Win32
ignoring WM_PAINT), `ConfigureNotify` (resize is polled per-frame, §4, not
event-driven), `VisibilityNotify` (only consumed synchronously by
`_sapp_x11_wait_for_event` during map, not in the dispatch), `MapNotify`/`UnmapNotify`.

### Mouse coordinates + scaling (`_sapp_x11_mouse_update`, 13136–13151)
- Coordinates come straight from `event->xbutton.x/y` / `xmotion.x/y` /
  `xcrossing.x/y` — **raw X11 window pixels, NO dpi scaling applied** (the app
  sees pixel coords; framebuffer==window in px on X11 so this is consistent).
  `dx/dy` computed from the previous position when `pos_valid`; `clear_dxdy` zeroes
  them on enter/leave to avoid a jump. Skipped entirely when `mouse.locked` (raw
  motion path owns dx/dy then).
- Wheel: buttons 4/5 → `scroll_y = +1/-1`, buttons 6/7 → `scroll_x = +1/-1`
  (13413–13418). Values are unit notches, not pixel deltas.

### Modifier translation (`_sapp_x11_mods`, 13101–13125)
Maps the X11 state mask: `ShiftMask→SHIFT`, `ControlMask→CTRL`, `Mod1Mask→ALT`,
`Mod4Mask→SUPER`, `Button1/2/3Mask→LMB/MMB/RMB`. **Left/right is NOT distinguished
in the modifier mask** (only combined SHIFT/CTRL/ALT/SUPER) — L/R identity flows
through `key_code` only (§6). Because X11 does not set the modifier bit on the
*same* event that presses/releases the modifier key, the code **emulates** it:
key-down ORs in `_sapp_x11_key_modifier_bit(key)` (13378), key-up ANDs it out
(13397); button-down/up do the same with `_sapp_x11_button_modifier_bit`.

Event helpers `_mouse_event`/`_scroll_event`/`_key_event`/`_char_event`
(13153–13199) all gate on `_sapp_events_enabled()` (event_cb set AND init_called),
so no events fire before the first frame's init — identical to the other backends.

---

## 6. Keyboard

### Keycode table (`_sapp_x11_init_keytable`, 12104–12281) — XKB physical-key names
- Indexing: **by X11 keycode** (range [8,255]); `_sapp.keycodes[256]`.
- Approach: **`XkbGetMap` + `XkbGetNames`**, then match each keycode's **XKB
  physical key name** (`desc->names->keys[scancode].name`, 4-char strings like
  `"AC01"`, `"TLDE"`, `"SPCE"`, `"RALT"`) against a hardcoded name→sapp_keycode
  table (12116–12238), with a key-alias fallback. This is **layout-independent**
  (physical location, not the produced symbol). `RALT`/`LVL3`/`MDSW` all map to
  `RIGHT_ALT`.
- **Fallback for still-INVALID keycodes:** `XGetKeyboardMapping` + the
  layout-dependent `_sapp_x11_translate_keysyms` (11939+), which switches on the
  keysym (used only where the XKB name lookup failed; noted as a US-layout-biased
  fallback). So the primary mechanism is **XKB key names**, NOT
  `XkbKeycodeToKeysym`.
- `_sapp_x11_translate_key(scancode)` (13201–13207): bounds-check + table lookup.

### Key repeat (`_sapp_x11_keypress_repeat`, 13242–13255) — software-tracked
- **`XkbSetDetectableAutoRepeat(true)` is set at startup** (§1 step 9) so held
  keys emit repeated `KeyPress` without paired `KeyRelease` — NOT the classic
  event-peek trick. Repeat detection is then a **per-keycode bool array**:
  `key_repeat[keycode]` returns its prior value and sets it true on each KeyPress;
  KeyRelease clears it. So the FIRST KeyPress reports `repeat=false`, subsequent
  ones `repeat=true` until release.

### CHAR events (`_on_keypress`, 13371–13388) — **XLookupString, NOT XIM/XIC**
- **This is the single biggest surprise vs the task brief.** There is **no
  XOpenIM / XCreateIC / Xutf8LookupString / XmbLookupString** anywhere. (The only
  `XI*` symbols in the file are `XIMaskLen`/`XIMaskIsSet` — XInput2 raw-motion
  masks, unrelated to input methods.)
- CHAR is produced by: `XLookupString(&event->xkey, NULL, 0, &keysym, NULL)` to
  get the raw `KeySym`, then **`_sapp_x11_keysym_to_unicode(keysym)`**
  (13209+) — a Latin-1 fast path plus a **binary search over a static
  keysym→Unicode table** (`_sapp_x11_keysymtab`, defined ~11050) plus the
  directly-encoded 24-bit UCS keysym range. If it yields `chr > 0`, fire CHAR.
- **Consequences:** no compose-key sequences, no dead-key composition, no
  CJK/IME input, no locale-aware multibyte input. Only characters expressible as a
  single keysym→codepoint mapping work. **If TrussC needs IME/dead-key/CJK text
  input, the port must ADD XIM/XIC (or better, xkbcommon-compose) — it cannot be
  lifted from sokol because sokol never implemented it.** (This matches
  `tcxIME` being a separate addon; core text input is intentionally minimal.)

### CLIPBOARD_PASTED
Detected inside `_sapp_x11_key_event` (13179–13188): on KEY_DOWN with
`modifiers == SAPP_MODIFIER_CTRL` (exact match) and `key_code == V`, fire
CLIPBOARD_PASTED reusing the event. Same idea as Win32 (Ctrl+V), not mac (Cmd+V).
The app must call `sapp_get_clipboard_string()` in its handler (buffer not pre-filled).

---

## 7. Clipboard (X selection ownership)

Uses the **`CLIPBOARD`** selection (not `PRIMARY`), owned by our `x11.window`.
Enabled only if `desc.enable_clipboard` (TrussC always sets it true).

- **Set (`_sapp_x11_set_clipboard_string`, 12823–12832):** copies into
  `clipboard.buffer` implicitly via the caller, then `XSetSelectionOwner(CLIPBOARD,
  window)` and verifies ownership (`LINUX_X11_FAILED_TO_BECOME_OWNER_OF_CLIPBOARD`
  on failure). Errors `CLIPBOARD_STRING_TOO_BIG` if the string exceeds `buf_size`.
- **Get (`_sapp_x11_get_clipboard_string`, 12834–12886):** if we already own the
  selection, return the internal buffer directly (avoid a round-trip). Else
  `XConvertSelection(CLIPBOARD, UTF8_STRING, ...)` into a scratch property, block
  on `_sapp_x11_wait_for_event(SelectionNotify, 0.1s)`, then `XGetWindowProperty`
  the data. **INCR (incremental) transfers are explicitly REJECTED** (12878,
  `actualType == incremental` → `CLIPBOARD_STRING_TOO_BIG`, return NULL) — large
  pastes beyond `buf_size` fail rather than chunk. Only `UTF8_STRING` is requested.
- **Serving other apps (`_on_selectionrequest`, 13596–13635):** responds to
  `SelectionRequest` on `CLIPBOARD` only. For `target == UTF8_STRING` → hand over
  `clipboard.buffer`; for `target == TARGETS` → reply with the single-element list
  `{UTF8_STRING}`; anything else → `property = None` (refuse). No INCR serving.
- **Receiving our own paste reply (`_on_selectionnotify`, 13472+):** the same
  handler is shared with XDND — it only acts on the XDND selection there; clipboard
  gets are synchronous via `wait_for_event`.

---

## 8. Drag & drop (XDND protocol, `_on_clientmessage` + `_on_selectionnotify`)

Implements **XDND version 5** (`_SAPP_X11_XDND_VERSION`). Only active when
`drop.enabled` (advertised via `XdndAware` on the window, §3; atoms interned in
`init_extensions` only when drop enabled, §4).

- **`XdndEnter` (13519–13543):** record `source` window and negotiated `version`
  (rejects versions > 5). Scan the offered type list (inline 3 types, or the
  `XdndTypeList` property when the list bit is set) for **`text/uri-list`**; store
  it as the accepted `format`.
- **`XdndPosition` (13571–13593):** reply with `XdndStatus`, accepting (`data.l[1]=1`,
  `XdndActionCopy`) iff we found a usable format. Mouse position during drag is
  **NOT tracked** (FIXME comment 13573 — parity with other backends).
- **`XdndDrop` (13544–13570):** if we have a format, `XConvertSelection(XdndSelection,
  text/uri-list)` (with the v≥1 timestamp) to request the data; else (v≥2) reply
  `XdndFinished` with rejected.
- **`SelectionNotify` for `XdndSelection` (13472–13508):** the drop data arrives;
  `_sapp_x11_parse_dropped_files_list` (13257–13327) parses the (percent-encoded,
  `\r\n`-separated, `file://`-prefixed) URI list into `drop.buffer`
  (`num_files`/`max_path_length` bounded; `DROPPED_FILE_PATH_TOO_LONG` /
  `LINUX_X11_DROPPED_FILE_URI_WRONG_SCHEME` errors). On success fires
  **FILES_DROPPED** (with `modifiers` left 0 — FIXME 13484: XSelection has no state
  and `XQueryKeymap` returns zeros). Then sends `XdndFinished` to the source (v≥2).

---

## 9. Mouse cursor (Xcursor lib + font-cursor fallback)

### Standard cursors (`_sapp_x11_create_standard_cursors`, 12698–12713)
Per enum, try the themed cursor first via **`XcursorLibraryLoadImage(name, theme,
size)`** (theme from `XcursorGetTheme`, size from `XcursorGetDefaultSize`) →
`XcursorImageLoadCursor`; on failure fall back to **`XCreateFontCursor(fallback)`**
(the classic X font cursors, e.g. `XC_left_ptr`, `XC_xterm`, `XC_crosshair`,
`XC_hand2`, `XC_sb_h_double_arrow`, `XC_fleur`). Names are the CSS/freedesktop
cursor names (`"default"`, `"text"`, `"pointer"`, `"ew-resize"`, `"not-allowed"`,
…). Some (NWSE/NESW/NOT_ALLOWED) have **no font fallback (fallback 0)** so they
only appear if the theme provides them.

### Hidden cursor (`_sapp_x11_create_hidden_cursor`, 12669–12681)
A 16×16 fully-transparent `XcursorImage` loaded as a `Cursor` — used both for
`sapp_show_mouse(false)` and for mouse-lock. (No blank pixmap trick; Xcursor is a
hard dependency, linked in CMake.)

### Apply (`_sapp_x11_update_cursor`, 12764–12780)
If shown: `XDefineCursor(window, custom_cursors[c] | standard_cursors[c])` (custom
wins when `custom_cursor_bound[c]`), or `XUndefineCursor` if the standard slot is
empty. If not shown: `XDefineCursor(window, hidden_cursor)`. Then `XFlush`.

### Custom cursors (`_sapp_x11_make_custom_mouse_cursor`, 12729–12748)
Builds an `XcursorImage` at the requested size with the hotspot, **copies RGBA→BGRA**
(Xcursor is BGRA/ARGB32, 12739–12743), loads via `XcursorImageLoadCursor`. Destroy
via `XFreeCursor` (12750–12756). Public `sapp_bind/unbind_mouse_cursor_image` route here.

### Mouse lock (`_sapp_x11_lock_mouse`, 12782–12821)
`XGrabPointer` confined to the window with the hidden cursor + (if XInput2 present)
`XISelectEvents(XI_RawMotion)` on the root for raw deltas; unlock re-warps the
pointer back and ungrabs. Not a TrussC main-path concern but state is per-window.

---

## 10. Title, icon, misc window props

- **Title (`_sapp_x11_update_window_title`, 12888–12904):** sets it three ways for
  WM compatibility — `Xutf8SetWMProperties` (legacy `WM_NAME`/`WM_ICON_NAME`),
  plus `_NET_WM_NAME` and `_NET_WM_ICON_NAME` as `UTF8_STRING` properties (the
  EWMH way). UTF-8 throughout; no wide-string conversion. `sapp_set_window_title`
  copies into `_sapp.window_title` then calls this.
- **Icon (`_sapp_x11_set_icon`, 12906–12937):** packs all images into a single
  `_NET_WM_ICON` `CARDINAL` array — each image is `width, height, then w*h`
  pixels in **ARGB `long`** order (`A<<24 | R<<16 | G<<8 | B`), `XChangeProperty`
  replace. Optional for rendering; a real taskbar/titlebar icon.
- **`sapp_x11_get_window()` (14566) / `sapp_x11_get_display()` (14574):** return
  the `Window` XID / `Display*`. **TrussC depends on both** in
  `tcPlatform_linux.cpp` (decoration, bring-to-front) — the replacement MUST keep
  them (§12).
- **`sapp_high_dpi`, `sapp_show_keyboard`:** `show_keyboard` is a no-op on X11
  (`onscreen_keyboard_shown` stays false).

---

## 11. sapp_get_environment / sapp_get_swapchain (GL) + contradictions vs the design doc

### GL environment / swapchain (14387–14486)
- `sapp_color_format()` (14018–14046): on the `#else` (GL) branch returns
  **`SAPP_PIXELFORMAT_RGBA8`** (14044). **The 10-bit `RGB10A2` patch is
  Metal/D3D11 ONLY** (14040–14042) — the Linux GL path is plain 8-bit RGBA8. So
  none of the win32/mac 10-bit patch machinery applies here.
- `sapp_depth_format()` → `DEPTH_STENCIL` (14048).
- `sapp_get_swapchain()` fills, for `_SAPP_ANY_GL` (14477–14478),
  **`res.gl.framebuffer = _sapp.gl.framebuffer`** — the default FBO id (0)
  captured in `_sapp_glx_make_current`. That's the *entire* GL swapchain contract:
  the glue's `sglue_swapchain()` reads `sc.gl.framebuffer` + width/height/formats/
  sample_count. No render_view/drawable/textures like D3D11/Metal.
- `sapp_get_environment()` sets `defaults.{color,depth}_format/sample_count`. There
  is no device handle to pass for GL (the context is current on the thread).

### Contradictions / surprises vs the design doc's P2 assumptions

The design doc P2 entry says: *"X11 + GLX shared context (multiwindow-glfw
pattern), timer-paced ticks"*, and the table row reads *"Linux | timer paced to
RandR refresh rate (clock_nanosleep) | best-effort; GLX has no per-window vsync
callback."* Against the actual upstream code:

1. **No RandR, no `clock_nanosleep`, no timer.** Upstream sokol paces frames
   purely by **blocking inside `glXSwapBuffers` with the swap interval** (§1-frame,
   §2). It never queries the display's refresh rate (zero RandR/XRR calls in the
   file) and never sleeps. If the P2 port wants explicit timer pacing to a known
   refresh rate, it must **add** RandR (`XRRGetScreenResources`/`XRRGetCrtcInfo`)
   or a `clock_nanosleep` loop itself — there is nothing to lift. The simplest
   faithful port keeps the blocking-swap model.
2. **Per-window vsync DOES exist** via `GLX_EXT_swap_control`
   (`glXSwapIntervalEXT(display, drawable, interval)` is per-drawable, §2) — so
   the doc's "GLX has no per-window vsync callback" is only half-true: there's no
   *callback*, but there IS per-drawable interval control. The real
   multi-window pacing hazard is the opposite: **blocking on N vsync'd swaps per
   iteration can serialize to refresh/N fps.** The port likely wants to vsync only
   the primary drawable (interval 1) and set interval 0 on the others, or swap all
   then rely on one blocking swap.
3. **Shared context is correct** and already how upstream is shaped (one
   `glx.ctx`, per-window `glx.window` `GLXWindow`). The per-window tick plan works
   as long as each window does `glXMakeCurrent(display, itsDrawable, sharedCtx)`
   before rendering — the current code only ever makes-current once because there's
   one window.
4. **XIM/XIC does not exist** (§6) — the task/plan assumption of "XIM/XIC input
   method handling to lift" is false. Text input is `XLookupString` +
   keysym→Unicode table only. Dead keys / compose / IME are absent and must be
   added if required (via `tcxIME` or xkbcommon-compose), not ported.
5. **`_NET_WM_PING`, `_MOTIF_WM_HINTS`, `PMinSize`/size constraints are absent**
   from sokol (§3). Decoration control is TrussC's own code atop
   `sapp_x11_get_window()`. Keep those getters.
6. **DPI is display-wide and unconditional** (`dpi_scale = Xft.dpi/96` regardless
   of `desc.high_dpi`, §4-DPI) — there's no per-monitor DPI and no `high_dpi=false`
   opt-out on X11, unlike mac/win32. A multiwindow port spanning monitors of
   different DPI has no upstream story here.
7. **Resize is per-frame polled, not event-driven** (no `ConfigureNotify`
   handler, §5) — a per-window tick that renders each window will naturally re-poll
   that window's size each tick, which fits, but the port must poll *per window*,
   not once globally.

---

## 12. What TrussC actually uses (don't port what nothing calls)

TrussC on Linux is `SOKOL_GLCORE` + `SOKOL_NO_ENTRY`, driving sapp via
`sapp_run()` → `_sapp_linux_run()`. `buildAppDescriptor` (`TrussC.h:2504`) always
sets `enable_dragndrop=true`, `enable_clipboard=true`, `high_dpi` from settings,
`sample_count` (default 4), `swap_interval` (default 1), `fullscreen`, title, icon.

**Used and must be preserved:**
- The whole GLX bring-up + the owned XPending/frame/swap loop.
- `skip_present` in `_sapp_glx_swap_buffers` (TrussC event-driven-render patch).
- Clipboard (CLIPBOARD/UTF8_STRING), drag&drop (XDND v5, `file://` list),
  fullscreen (`_NET_WM_STATE`), standard+custom+hidden cursors, `_NET_WM_ICON`,
  UTF-8 title, per-frame resize poll + RESIZED, iconify/restore via WM_STATE,
  focus, all mouse/key/scroll/char events.
- **Public getters `sapp_x11_get_window()` and `sapp_x11_get_display()`** —
  `core/platform/linux/tcPlatform_linux.cpp` calls `sapp_x11_get_window()` for
  `setWindowDecorated` (`_MOTIF_WM_HINTS`) and `bringWindowToFront` (`XRaiseWindow`
  via its own `XOpenDisplay`). Dropping these breaks TrussC's window management.
- `sapp_width()/sapp_height()` (used by `tcPlatform_linux` screenshot path).

**Present in sokol but UNUSED by TrussC / skippable in the port:**
- **The entire `_SAPP_EGL` fork** (13687–13816, and the swap fork at 13828) —
  dead on desktop (only `SOKOL_GLES3` = Android/Raspbian). Don't port for desktop.
- **Vulkan / WGPU Linux forks** — dead (`SOKOL_GLCORE` only).
- **Mouse lock / XInput2 raw motion** (`_on_genericevent`, `_lock_mouse`, `xi`) —
  TrussC's main path doesn't lock the pointer; low priority, but small.
- **`XkbSetDetectableAutoRepeat` is required** (not skippable) — without it the
  software repeat tracking mis-detects (KeyRelease/KeyPress pairs).
- **Legacy `Xutf8SetWMProperties`** in title-setting — harmless, could keep just
  the EWMH `_NET_WM_NAME` path, but cheap to keep for old-WM compatibility.
- Icon (`_NET_WM_ICON`) is used but non-load-bearing for rendering.

---

## Appendix: function → line index (GLX/X11-relevant)

| function | line |
|---|---|
| `_sapp_x11_error_handler` / `_grab` / `_release` | 11884 / 11890 / 11895 |
| `_sapp_x11_init_extensions` (atoms + XInput2) | 11900 |
| `_sapp_x11_translate_keysyms` (fallback) | 11939 |
| `_sapp_x11_keysymtab` (keysym→Unicode table) | 11050 |
| `_sapp_x11_init_keytable` (XKB names) | 12104 |
| `_sapp_x11_query_system_dpi` (Xft.dpi) | 12283 |
| `_sapp_glx_has_ext` / `_extsupported` / `_getprocaddr` | 12321 / 12340 / 12348 |
| `_sapp_glx_init` | 12359 |
| `_sapp_glx_attrib` / `_choosefbconfig` / `_choose_visual` | 12428 / 12434 / 12507 |
| `_sapp_glx_make_current` | 12521 |
| `_sapp_glx_create_context` / `_destroy_context` | 12526 / 12554 |
| `_sapp_glx_swap_buffers` (skip_present patch) | 12565 |
| `_sapp_glx_swapinterval` | 12571 |
| `_sapp_x11_send_event` / `_wait_for_event` | 12581 / 12599 |
| `_sapp_x11_app_event` | 12612 |
| `_sapp_x11_update_dimensions` / `_from_window_size` | 12619 / 12643 |
| `_sapp_x11_set_fullscreen` / `_toggle_fullscreen` | 12649 / 12758 |
| `_sapp_x11_create_hidden_cursor` | 12669 |
| `_sapp_x11_create_standard_cursor(s)` | 12683 / 12698 |
| `_sapp_x11_destroy_standard_cursors` | 12715 |
| `_sapp_x11_make/destroy_custom_mouse_cursor` | 12729 / 12750 |
| `_sapp_x11_update_cursor` | 12764 |
| `_sapp_x11_lock_mouse` | 12782 |
| `_sapp_x11_set/get_clipboard_string` | 12823 / 12834 |
| `_sapp_x11_update_window_title` | 12888 |
| `_sapp_x11_set_icon` (_NET_WM_ICON) | 12906 |
| `_sapp_x11_create_window` / `_destroy_window` | 12939 / 13004 |
| `_sapp_x11_window_visible` / `_show_window` / `_hide_window` | 13017 / 13023 / 13033 |
| `_sapp_x11_get_window_property` / `_get_window_state` | 13038 / 13057 |
| `_sapp_x11_key_modifier_bit` / `_button_modifier_bit` / `_mods` | 13073 / 13092 / 13101 |
| `_sapp_x11_translate_button` / `_mouse_update` | 13127 / 13136 |
| `_sapp_x11_mouse/scroll/key/char_event` | 13153 / 13162 / 13172 / 13191 |
| `_sapp_x11_translate_key` / `_keysym_to_unicode` | 13201 / 13209 |
| `_sapp_x11_keypress_repeat` / `_keyrelease_repeat` | 13242 / 13251 |
| `_sapp_x11_parse_dropped_files_list` | 13257 |
| `_on_genericevent` (XI raw motion) | 13329 |
| `_on_focusin` / `_on_focusout` | 13353 / 13360 |
| `_on_keypress` / `_on_keyrelease` | 13371 / 13390 |
| `_on_buttonpress` / `_on_buttonrelease` | 13402 / 13422 |
| `_on_enternotify` / `_on_leavenotify` / `_on_motionnotify` | 13434 / 13442 / 13449 |
| `_on_propertynotify` (WM_STATE iconify) | 13456 |
| `_on_selectionnotify` (XDND drop / clipboard) | 13472 |
| `_on_clientmessage` (WM_DELETE / XDND) | 13510 |
| `_on_selectionrequest` (serve clipboard) | 13596 |
| `_sapp_x11_process_event` (dispatch) | 13637 |
| `_sapp_linux_frame` | 13818 |
| `_sapp_linux_run` | 13836 |
| `main` | 13925 |
| `sapp_run` (SOKOL_NO_ENTRY dispatch) | 13941 |
| `sapp_color_format` (GL → RGBA8) | 14018 |
| `sapp_get_environment` / `sapp_get_swapchain` (gl.framebuffer) | 14387 / 14417 |
| `sapp_x11_get_window` / `sapp_x11_get_display` | 14566 / 14574 |
