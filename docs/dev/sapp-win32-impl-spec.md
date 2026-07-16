# sokol_app.h Win32 (D3D11) Backend — Behavioral Specification

Implementation contract for replacing sokol_app.h's Win32 main-window / app-lifecycle
with `sokol_app_tc.h`. All line references are into
`core/include/sokol/sokol_app.h` (14602 lines, this worktree). Focus: `_SAPP_WIN32`
+ `SOKOL_D3D11`. TrussC builds Windows with D3D11 only; GL/WGL, WGPU, Vulkan paths on
Win32 are noted only where a shared code path forks on them. iOS/macOS/Linux/web ignored.

This is the sibling of `sapp-mac-impl-spec.md`. Where the mac backend is event-driven
(`[NSApp run]` + CADisplayLink), the Win32 backend is an **explicit owned message+render
loop** (`_sapp_win32_run`) — this is the single biggest structural difference and shapes
everything below.

**Per-window vs process-global (read this first — the replacement makes windows plural):**
Almost everything the Win32 backend touches is currently **single-window global state**
baked into `static _sapp_t _sapp`. The truly *process-global* pieces (must stay global /
be registered once regardless of window count) are:
- **Window class** `L"SOKOLAPP"` — `RegisterClassW` / `UnregisterClassW` (§2). One class,
  many windows; register once, unregister when the last window dies.
- **DPI awareness** — a *process* setting (`SetProcessDpiAwarenessContext`, §1). Set once
  at startup, never per-window.
- **Console** attach/alloc + codepage (§1) — process-global.
- **Cursor clip / ShowCursor counter / raw-input registration** (mouse lock, §6) — global
  OS state; only one window can own the locked cursor.
- **Standard cursors** (`LoadImageW(..., LR_SHARED)`) — shareable HCURSORs, effectively global.
- **Keytable** `_sapp.keycodes[]` (§4) — global, fill once.

Everything else is **per-window** and must move into a per-window struct when windows go
plural: `hwnd`, `hmonitor`, `dc`, `big_icon`/`small_icon`, `custom_cursors[]`, `surrogate`
(WM_CHAR surrogate accumulator), `stored_window_rect`, `iconified`, `in_create_window`,
per-window `dpi` scales, `mouse.tracked`/`capture_mask`, and the entire D3D11 device+
swapchain+RTV/DSV set (`_sapp_d3d11_t`). Note the D3D11 **device+context** could be
shared across windows but each window needs its own **swapchain + RTV/DSV/MSAA**.

---

## 0. Global state touched (the shared contract)

Single global `static _sapp_t _sapp;` (definition ~3298) embeds `_sapp_win32_t win32;`
(3305) and, under D3D11, `_sapp_d3d11_t d3d11;` (3307).

### `_sapp_d3d11_t` (2889–2903, `SOKOL_D3D11 && _SAPP_WIN32`)
```c
typedef struct {
    ID3D11Device* device;
    ID3D11DeviceContext* device_context;
    ID3D11Texture2D* rt;                 // swapchain backbuffer texture
    ID3D11RenderTargetView* rtv;         // view onto backbuffer (== resolve target when MSAA)
    ID3D11Texture2D* msaa_rt;            // MSAA color texture (only if sample_count>1)
    ID3D11RenderTargetView* msaa_rtv;    // view onto MSAA color (render target when MSAA)
    ID3D11Texture2D* ds;                 // depth-stencil texture
    ID3D11DepthStencilView* dsv;
    DXGI_SWAP_CHAIN_DESC swap_chain_desc;// kept around; BufferCount reused on resize
    IDXGISwapChain* swap_chain;
    IDXGIDevice1* dxgi_device;           // held only to set max frame latency / disable Alt-Enter
} _sapp_d3d11_t;
```

### `_sapp_win32_dpi_t` (2922–2927)
```c
typedef struct {
    bool aware;            // did we successfully set a DPI-aware mode
    float content_scale;   // framebuffer px per window pt (== window_scale if high_dpi else 1.0)
    float window_scale;    // OS DPI / 96.0 (physical px per logical pt)
    float mouse_scale;     // multiplier applied to raw WM_MOUSEMOVE coords → sapp coords
} _sapp_win32_dpi_t;
```

### `_sapp_win32_t` (2929–2961)
```c
typedef struct {
    HWND hwnd;
    HMONITOR hmonitor;
    HDC dc;                       // GetDC(hwnd); CS_OWNDC so persistent
    HICON big_icon, small_icon;
    HCURSOR standard_cursors[_SAPP_MOUSECURSOR_NUM];  // LR_SHARED (global-ish)
    HCURSOR custom_cursors[_SAPP_MOUSECURSOR_NUM];
    UINT orig_codepage;           // to restore console CP on exit
    WCHAR surrogate;              // WM_CHAR high-surrogate accumulator
    RECT stored_window_rect;      // saved windowed pos/size for fullscreen→windowed restore
    bool is_win10_or_greater;     // gates FLIP_DISCARD + DXGI_PRESENT_DO_NOT_WAIT
    bool in_create_window;        // wndproc no-ops all messages while true (see §3)
    bool iconified;
    _sapp_win32_dpi_t dpi;
    struct {                      // mouse-lock / capture / raw-input bookkeeping
        struct { LONG pos_x, pos_y; bool pos_valid; } lock;
        struct { LONG pos_x, pos_y; bool pos_valid; } raw_input;
        bool requested_lock;
        bool tracked;             // TrackMouseEvent enter/leave state
        uint8_t capture_mask;     // SetCapture refcount by button bit
    } mouse;
    struct { size_t size; void* ptr; } raw_input_data;  // grow-on-demand WM_INPUT buffer
} _sapp_win32_t;
```

Cross-platform `_sapp` fields the win32 path reads/writes are the same set as mac
(`desc`, `valid`, `first_frame`, `quit_requested`, `quit_ordered`, `fullscreen`,
`window_width/height`, `framebuffer_width/height`, `dpi_scale`, `sample_count`,
`swap_interval`, `timing`, `mouse.*`, `clipboard.*`, `drop.*`, `event`, `keycodes[]`,
`skip_present`, `custom_cursor_bound[]`) **plus** `window_title_wide[_SAPP_MAX_TITLE_LENGTH]`
(3326) — a UTF-16 title buffer that is win32-specific but declared cross-platform in the
main struct.

`_sapp_init_state(desc)` runs first inside `_sapp_win32_run` (same as mac): applies
`_sapp_desc_defaults` (sample_count→1, swap_interval→1, clipboard_size→8192,
max_dropped_files→1, path_length→2048, window_title→"sokol"), sets `first_frame=true`,
`dpi_scale=1.0`, `mouse.shown=true`, `fullscreen=desc.fullscreen`, copies width/height
verbatim (**may be 0** — the win32 backend resolves 0 via `CW_USEDEFAULT`, §2).

---

## 1. Entry / run loop (`_sapp_win32_run`, 10154–10227)

### Entry points (10282–10304)
- Default entry is **`WinMain`** (10291). It calls `_sapp_win32_command_line_to_utf8_argv`
  (`CommandLineToArgvW` + `WideCharToMultiByte` to UTF-8, 10229) then `sokol_main(argc,argv)`
  → `_sapp_win32_run(&desc)` → frees the argv.
- `SOKOL_WIN32_FORCE_MAIN` compiles a plain `int main()` instead (10284).
- `SOKOL_NO_ENTRY` compiles out both; user calls `sapp_run(desc)` (13941) which dispatches
  to `_sapp_win32_run` (13950).

### Init order (10155–10176) — load-bearing, exact
1. `_sapp_init_state(desc)`.
2. `_sapp_win32_init_console()` (§ below).
3. `_sapp.win32.is_win10_or_greater = _sapp_win32_is_win10_or_greater()` (10157). Detection
   is a hack: `GetProcAddress(kernel32, "GetSystemCpuSetInformation") != NULL` (10145).
4. `_sapp_win32_init_keytable()` — fill `_sapp.keycodes[]` (§4).
5. `_sapp_win32_utf8_to_wide(window_title → window_title_wide)` (10159) — pre-convert title
   before window creation.
6. `_sapp_win32_init_dpi()` (§DPI below).
7. `_sapp_win32_init_cursors()` — load all standard cursors (§6).
8. `_sapp_win32_create_window()` (§2) — registers class, creates HWND, gets DC/monitor,
   sizes, optionally goes fullscreen, `ShowWindow(SW_SHOW)`, `DragAcceptFiles`.
9. `sapp_set_icon(&desc->icon)` (10163) — sets window icon (§ icon). Unlike mac's dock-tile
   decoration, on Win32 this is a **real titlebar/taskbar icon** via `WM_SETICON`, but still
   optional for rendering.
10. `_sapp_d3d11_create_device_and_swapchain()` then `_sapp_d3d11_create_default_render_target()`
    (§6-D3D11).
11. `_sapp.valid = true;` (10176).

There is **no `applicationDidFinishLaunching` equivalent** — the run function does all setup
inline, then enters the loop.

### DPI awareness (`_sapp_win32_init_dpi`, 9901–9984) — process-global
Dynamically loads `user32.dll` (`SetProcessDPIAware`, `SetProcessDpiAwarenessContext`) and
`shcore.dll` (`SetProcessDpiAwareness`, `GetDpiForMonitor`). Cascade (D3D11 path always
takes `init_dpi_awareness=true`; the `!high_dpi → PROCESS_DPI_UNAWARE` special-case at
9932–9940 is **`#if !defined(SOKOL_D3D11)` only**, i.e. GL/Vulkan — irrelevant to TrussC):
1. If `SetProcessDpiAwareness` present: set `dpi.aware=true`, **try
   `SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2)`** (the magic value
   `(DPI_AWARENESS_CONTEXT)-4`); if that fails fall back to `SetProcessDpiAwareness(PROCESS_SYSTEM_DPI_AWARE)`.
2. Else if `SetProcessDPIAware` present (Win7): `dpi.aware=true`, call it.
3. Compute `window_scale`: if `dpi.aware && GetDpiForMonitor` present, query the monitor at
   point `{1,1}` (`MDT_EFFECTIVE_DPI`) → `window_scale = dpix / 96.0`; else `window_scale=1.0`.
4. Derive `content_scale`/`mouse_scale` from `desc.high_dpi`:
   - `high_dpi`: `content_scale = window_scale`, `mouse_scale = 1.0`.
   - `!high_dpi`: `content_scale = 1.0`, `mouse_scale = 1.0 / window_scale` (mouse coords are
     divided back down so app sees logical pixels).
5. `_sapp.dpi_scale = content_scale`.

### WM_DPICHANGED (`_sapp_win32_dpi_changed`, 9486–9515)
Only delivered when PER_MONITOR_AWARE_V2 is active. No-op if `!dpi.aware`. Dynamically loads
`GetDpiForWindow`, recomputes `window_scale = GetDpiForWindow(hwnd)/96.0`, re-derives
content/mouse scale exactly as init, updates `_sapp.dpi_scale`, then **honors the OS-proposed
rect**: `SetWindowPos(hWnd, proposed_win_rect, SWP_NOZORDER|SWP_NOACTIVATE)`. Framebuffer
resize itself is picked up by the loop's `_sapp_win32_update_dimensions()` next tick, not here.

### Console (`_sapp_win32_init_console` 9871 / `_restore_console` 9895) — process-global
Gated on `desc.win32.console_create` / `console_attach` / `console_utf8`:
- `console_attach` → `AttachConsole(ATTACH_PARENT_PROCESS)`; if that fails and
  `console_create` → `AllocConsole()`. On success `freopen_s` redirects stdout+stderr to `"CON"`.
- `console_utf8` → save `orig_codepage = GetConsoleOutputCP()`, `SetConsoleOutputCP(CP_UTF8)`.
- Restore on exit sets the codepage back.

### The main loop (10178–10208)
```c
bool done = false;
while (!(done || _sapp.quit_ordered)) {
    _sapp_timing_update(&_sapp.timing, 0.0);              // measure dt via QPC-based timer
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {   // drain ALL pending messages
        if (WM_QUIT == msg.message) { done = true; continue; }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    _sapp_win32_frame(false);                             // render + present ONE frame
    if (_sapp_win32_update_dimensions()) {                // resize detected post-drain
        _sapp_d3d11_resize_default_render_target();       // ResizeBuffers + recreate RTV/DSV
        _sapp_win32_app_event(SAPP_EVENTTYPE_RESIZED);
    }
    if (_sapp.quit_requested) {                           // user asked to quit this frame
        PostMessage(_sapp.win32.hwnd, WM_CLOSE, 0, 0);    // route through the WM_CLOSE dance
    }
    _sapp_win32_update_mouse_lock();                      // reconcile requested vs actual lock
}
```
Key structural facts, contrasted with the assumptions in the task:
- **PeekMessage-drain-then-render**, NOT `GetMessage` (blocking). It is a busy render loop:
  drain the whole queue with `PM_REMOVE`, then render exactly one frame, then check resize,
  quit, and mouse-lock. `frame_cb` runs inside `_sapp_win32_frame(false)` **once per outer
  loop iteration**, not per message.
- **Resize is deliberately handled in the loop, NOT in `WM_SIZE`.** `WM_SIZE` only tracks
  iconify/restore (§3). The comment (10192, and the commented-out block in `WM_TIMER`
  9761–9772) explains: calling `ResizeBuffers` every `WM_SIZE` "explodes memory usage," so
  resize is coalesced to once per loop iteration via `_sapp_win32_update_dimensions()`.
- **Loop exit** happens two ways, both converging on `WM_QUIT`:
  - `_sapp.quit_ordered` becomes true (loop condition) — set by the WM_CLOSE dance.
  - `WM_QUIT` dequeued → `done=true`. `WM_QUIT` is produced by `PostQuitMessage(0)` which is
    called from `WM_CLOSE` once quit is committed (§3).
  So: user close / `sapp_quit()` → `WM_CLOSE` → (maybe QUIT_REQUESTED) → `PostQuitMessage` →
  `WM_QUIT` dequeued → loop ends.

### Cleanup order (10209–10226) — mirrors mac's guarantee
1. `_sapp_call_cleanup()` (user `cleanup_cb`, once) — **before** any GPU teardown.
2. `_sapp_d3d11_destroy_default_render_target()` then `_sapp_d3d11_destroy_device_and_swapchain()`.
3. `_sapp_win32_destroy_window()` (`DestroyWindow` + `UnregisterClassW`).
4. `_sapp_win32_destroy_icons()`, `_sapp_win32_restore_console()`,
   `_sapp_win32_free_raw_input_data()`, `_sapp_discard_state()`.

### Frame pacing (`_sapp_win32_frame`, 9548–9568)
Renders (`_sapp_frame()`), then on D3D11 `_sapp_d3d11_present(do_not_wait)` where
`do_not_wait = from_winproc` (true only from the WM_TIMER modal path, §3). When NOT called
from winproc and the window is iconic (`IsIconic`), it `Sleep(16 * swap_interval)` to avoid a
spin loop while minimized. There is **no vsync-blocking present abstraction beyond DXGI
`Present(sync_interval)`** — the loop otherwise runs as fast as Present allows.

### Frame timing source
`_sapp.timing` is the platform-agnostic filtered timer (identical config to mac:
dt_min=1µs, dt_max=100ms, threshold=4ms, alpha=0.025, seed 1/60). It is fed by
`_sapp_timing_update(&_sapp.timing, 0.0)` at the top of every loop iteration (10180) **and**
inside `WM_TIMER` (9759). `external_now==0` means it samples `timestamp_now()` internally
(QPC-based `_sapp_timing`), i.e. **there is no CADisplayLink equivalent** — Win32 always uses
the measured EMA filter. `sapp_frame_duration()` → `_sapp_timing_get`,
`sapp_frame_duration_unfiltered()` → raw `t->dt`.

---

## 2. Window creation (`_sapp_win32_create_window`, 9797–9853)

### Window class (RegisterClassW, 9798–9805) — process-global, once
```c
WNDCLASSW wndclassw = {0};
wndclassw.style       = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;   // CS_OWNDC → persistent HDC
wndclassw.lpfnWndProc = _sapp_win32_wndproc;
wndclassw.hInstance   = GetModuleHandleW(NULL);
wndclassw.hCursor     = LoadCursor(NULL, IDC_ARROW);
wndclassw.hIcon       = LoadIcon(NULL, IDI_WINLOGO);          // default; real icon set later
wndclassw.lpszClassName = L"SOKOLAPP";
RegisterClassW(&wndclassw);
```

### Style + initial size (9811–9835)
- `win_ex_style = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE`.
- `win_style = WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
  WS_MAXIMIZEBOX | WS_SIZEBOX` — **always resizable**; there is no non-resizable option and
  fullscreen is not baked into the initial style (a windowed window is always created first,
  hidden, then toggled to fullscreen — see below).
- Requested client size is DPI-scaled: `rect.right = window_width * dpi.window_scale`,
  `rect.bottom = window_height * dpi.window_scale`, then `AdjustWindowRectEx(&rect, win_style,
  FALSE, win_ex_style)` converts client→window rect.
- **`desc.width/height == 0` handling:** `use_default_width/height = (0 == window_width/height)`.
  If set, `CreateWindowExW` is passed `CW_USEDEFAULT` for that dimension so the OS picks it
  (NOT mac's 4/5-of-screen rule). Caveat noted in-source (9831): if width is `CW_USEDEFAULT`,
  the height arg is ignored by Win32.

### CreateWindowExW (9821–9836)
- `in_create_window = true` around the call so the wndproc **no-ops every message during
  creation** (9571 guard) — creation-time messages (WM_CREATE etc.) are swallowed.
- Position: `X = CW_USEDEFAULT`, `Y = SW_HIDE` — note (9829) this is a quirk: the window is
  created hidden. Class name `L"SOKOLAPP"`, title `window_title_wide`, `hInstance =
  GetModuleHandle(NULL)`, no parent, no menu.
- After: `dc = GetDC(hwnd)` (persistent via CS_OWNDC), `hmonitor = MonitorFromWindow(...,
  MONITOR_DEFAULTTONULL)`.

### Show sequence (9846–9852)
1. `_sapp_win32_update_dimensions()` — sample real client rect into window/framebuffer size.
2. If `_sapp.fullscreen`: `_sapp_win32_set_fullscreen(true, SWP_HIDEWINDOW)` then
   `_sapp_win32_update_dimensions()` again.
3. `ShowWindow(hwnd, SW_SHOW)`.
4. `DragAcceptFiles(hwnd, 1)` — drag&drop is **always** registered on the window (like mac),
   independent of `desc.enable_dragndrop`; the *handler* (`_sapp_win32_files_dropped`) gates
   on `drop.enabled`.

### `_sapp_win32_update_dimensions` (9080–9111) — the size authority
`GetClientRect` → `window_width/height = (client px) / dpi.window_scale` (logical pts). If
both are 0 → **minimized**, return false (pretend no change, matches other backends,
sokol#1465). Else `framebuffer_width/height = round(window_width * dpi.content_scale)`;
returns **true only if the framebuffer size actually changed** (this is what drives
`ResizeBuffers` in the loop). On `GetClientRect` failure clamps everything to 1.

---

## 3. WndProc (`_sapp_win32_wndproc`, 9570–9795)

All handling is gated by `if (!_sapp.win32.in_create_window)` (9571); unhandled messages fall
through to `DefWindowProcW` (9794). Complete list of handled messages:

- **WM_CLOSE (9573–9589) — the quit dance** (mirror of mac's `windowShouldClose:`):
  ```c
  if (!quit_ordered) {                 // sapp_quit() not already forced
      quit_requested = true;
      _sapp_win32_app_event(QUIT_REQUESTED);   // user may sapp_cancel_quit() here
      if (quit_requested) quit_ordered = true; // not cancelled → commit
  }
  if (quit_ordered) PostQuitMessage(0);
  return 0;                            // never let DefWindowProc DestroyWindow us directly
  ```
  Returns 0 (does not forward to DefWindowProc, so the window is NOT auto-destroyed here;
  teardown happens after the loop exits). If cancelled, `PostQuitMessage` is skipped and the
  app keeps running.
- **WM_SYSCOMMAND (9590–9603):** `switch (wParam & 0xFFF0)`:
  - `SC_SCREENSAVE` / `SC_MONITORPOWER` → `return 0` **only if `_sapp.fullscreen`** (suppress
    screensaver/monitor-blank in fullscreen; in windowed mode falls through to default).
  - `SC_KEYMENU` → `return 0` (swallow the Alt-menu activation so Alt doesn't freeze rendering).
- **WM_ERASEBKGND (9604):** `return 1` (claim erased; prevents flicker, no GDI clear).
- **WM_SIZE (9606–9618):** tracks only iconify state. `iconified = (wParam == SIZE_MINIMIZED)`;
  on change updates `win32.iconified` and fires `ICONIFIED` / `RESTORED`. **Does NOT fire
  RESIZED and does NOT resize the swapchain** — that is the loop's job (§1). No RESTORED/
  MAXIMIZED distinction beyond minimize.
- **WM_SETFOCUS (9619) → FOCUSED**, **WM_KILLFOCUS (9622) → UNFOCUSED**.
- **WM_SETCURSOR (9625–9630):** if `LOWORD(lParam) == HTCLIENT`, call
  `_sapp_win32_update_cursor(current_cursor, shown, skip_area_test=true)` and `return TRUE`
  (§6); otherwise fall through (non-client area keeps system cursor).
- **WM_DPICHANGED (9631–9638):** `_sapp_win32_dpi_changed` (§1).
- **Mouse buttons (9639–9668):** each of L/R/M down/up runs `_sapp_win32_mouse_update(lParam)`
  then `_sapp_win32_mouse_event(MOUSE_DOWN/UP, button)` then capture/release:
  - **down** → `_sapp_win32_capture_mouse(1<<button)`: `SetCapture(hwnd)` on first held button,
    OR the button bit into `capture_mask` (9226).
  - **up** → `_sapp_win32_release_mouse(1<<button)`: clear the bit; `ReleaseCapture()` when
    `capture_mask` hits 0 (9233). This is a **refcounted capture across buttons** so dragging
    with multiple buttons doesn't drop capture early.
- **WM_MOUSEMOVE (9669–9685):** only when `!mouse.locked`. `_sapp_win32_mouse_update`; if not
  yet `mouse.tracked`, set tracked, arm `TrackMouseEvent(TME_LEAVE)`, zero dx/dy, fire
  **MOUSE_ENTER**; always fire **MOUSE_MOVE**.
- **WM_INPUT (9686–9725):** raw-input path, **only during mouse-lock**. Polls size, grows
  `raw_input_data` buffer, reads `RAWINPUT`; handles both `MOUSE_MOVE_ABSOLUTE` (delta from
  stored prev, guarded by `raw_input.pos_valid`) and the common relative case
  (`dx/dy = lLastX/lLastY`); fires MOUSE_MOVE. (Mouse-lock is not a TrussC main-path concern
  but the buffer/state is per-window.)
- **WM_MOUSELEAVE (9727–9734):** only when `!locked`; zero dx/dy, clear `tracked`, fire
  **MOUSE_LEAVE**.
- **WM_MOUSEWHEEL (9735):** `_sapp_win32_scroll_event(0, GET_WHEEL_DELTA_WPARAM(wParam)/WHEEL_DELTA)`.
- **WM_MOUSEHWHEEL (9738):** `_sapp_win32_scroll_event(-GET_WHEEL_DELTA_WPARAM/WHEEL_DELTA, 0)`
  (note the sign flip on horizontal). `WHEEL_DELTA` normalization → scroll in "notches".
- **WM_CHAR (9741):** `_sapp_win32_char_event((uint32_t)wParam, repeat = !!(lParam & 0x40000000))`.
  Char event (9467): filters `c < 32` (control chars dropped). **UTF-16 surrogate handling:**
  high surrogate (0xD800–0xDBFF) is stored in `win32.surrogate` and NOT emitted; a following
  low surrogate (0xDC01–0xDFFF) is combined `surrogate<<10 | (c-0xDC00) + 0x10000` into a full
  codepoint, then emitted. Fires CHAR with `char_code` and repeat flag.
- **WM_KEYDOWN / WM_SYSKEYDOWN (9744–9747):** `_sapp_win32_key_event(KEY_DOWN,
  scancode = HIWORD(lParam) & 0x1FF, repeat = !!(lParam & 0x40000000))`. The `& 0x1FF` keeps
  the 9-bit scancode **including the extended bit (0x100)** — that is how the keytable
  distinguishes e.g. left vs right Ctrl/Alt and keypad Enter (§4/§5).
- **WM_KEYUP / WM_SYSKEYUP (9748–9750):** same but KEY_UP, repeat always false.
  - Note: **SYSKEY messages are handled identically to normal keys** — Alt-combos and F10 do
    generate KEY_DOWN/UP events (unlike being swallowed). Alt-menu freeze is prevented via the
    `SC_KEYMENU` suppression in WM_SYSCOMMAND, not by dropping syskeys.
- **WM_ENTERSIZEMOVE (9752) / WM_EXITSIZEMOVE (9755) / WM_TIMER (9758) — the modal-loop render
  trick:** entering a resize/move modal loop, Win32 stops returning to the app loop. To keep
  rendering, `WM_ENTERSIZEMOVE` does `SetTimer(hwnd, 1, USER_TIMER_MINIMUM, NULL)`;
  `WM_TIMER` then runs `_sapp_timing_update` + `_sapp_win32_frame(true)` (present with
  `DXGI_PRESENT_DO_NOT_WAIT`, §6) each tick; `WM_EXITSIZEMOVE` `KillTimer`s. The commented-out
  block (9761–9772) shows resize is intentionally NOT done here either (memory blowup).
- **WM_NCLBUTTONDOWN (9774–9785):** workaround for the half-second stall when grabbing the
  title bar — if hit-test is `HTCAPTION`, synthesize a `WM_MOUSEMOVE` post to keep things moving.
- **WM_DROPFILES (9786):** `_sapp_win32_files_dropped((HDROP)wParam)` (§ drag&drop).

**Not handled (fall through to DefWindowProc):** WM_PAINT (rendering is loop-driven, not
paint-driven — the class is CS_OWNDC and WM_ERASEBKGND is claimed, so no paint cycle is
needed), WM_MOVE, WM_ACTIVATE, WM_DESTROY, WM_QUIT (dequeued in the loop, not in wndproc).

### Modifiers helper (`_sapp_win32_mods`, 9387–9412)
Sampled fresh for **every** mouse/key/scroll event via `GetKeyState` on the high bit `(1<<15)`:
`VK_SHIFT→SHIFT`, `VK_CONTROL→CTRL`, `VK_MENU→ALT`, `(VK_LWIN|VK_RWIN)→SUPER`. Mouse-button
modifiers use `GetAsyncKeyState(VK_LBUTTON/RBUTTON/MBUTTON)` with `SM_SWAPBUTTON` respected
(L/R swapped for left-handed mice). **Left/right modifier distinction is NOT in the modifier
mask** (only the combined SHIFT/CTRL/ALT/SUPER bits); L/R is distinguished only in the
`key_code` via the scancode+extended-bit keytable (§4/§5).

### Event helpers
`_sapp_win32_app_event(type)` (8342) = gated `_sapp_init_event` + `_sapp_call_event` for
windowless events (RESIZED/FOCUSED/etc.). `_sapp_win32_mouse_event` / `_scroll_event` /
`_key_event` / `_char_event` each set `event.modifiers = _sapp_win32_mods()` and populate
type-specific fields. All gated on `_sapp_events_enabled()` (event_cb set AND `init_called`),
so no events before the first frame's init — identical semantics to mac.

---

## 4. Keycode table (`_sapp_win32_init_keytable`, 8349–8470)

- **Indexing: by Win32 scancode** (the `HIWORD(lParam) & 0x1FF` 9-bit value with the extended
  bit), NOT by VK code. e.g. `_sapp.keycodes[0x01E] = SAPP_KEYCODE_A`,
  `_sapp.keycodes[0x11D] = RIGHT_CONTROL` (0x100 extended bit distinguishes it from
  `0x01D = LEFT_CONTROL`).
- Array is `_sapp.keycodes[SAPP_MAX_KEYCODES]` (cross-platform; guard `vk < SAPP_MAX_KEYCODES`
  in `_sapp_win32_key_event`, 9449). The extended entries (0x100+) fit because the table is
  sized to hold 9-bit scancodes.
- Comment says "same as GLFW". Entries span 0x001–0x076 plus extended 0x11C–0x15D. **Lift
  verbatim** — the indexing scheme is: `keycodes[scancode] = SAPP_KEYCODE_*`.
- Quirks baked into the table: `0x045` and `0x146` both → `PAUSE`; `0x036` and `0x136` both →
  `RIGHT_SHIFT`; `0x137 → PRINT_SCREEN` (the extended-code form). PrintScreen's well-known
  down-event quirk is handled by the table entry existing for the extended scancode.

---

## 5. Modifiers / left-right distinction

Covered in §3. Summary: modifier **mask** is combined-only (no L/R), sampled via `GetKeyState`
high bit. Left/right **key identity** flows through `key_code` only, resolved by the
scancode+extended-bit keytable (e.g. left Ctrl scancode 0x01D vs right Ctrl 0x11D; keypad
Enter 0x11C vs main Enter 0x01C). There is no separate "which side" flag on the event.

---

## 6. D3D11 / DXGI setup

### Device + swapchain (`_sapp_d3d11_create_device_and_swapchain`, 8588–8679)
`DXGI_SWAP_CHAIN_DESC` (stored in `_sapp.d3d11.swap_chain_desc`, reused on resize):
- `BufferDesc.Width/Height = framebuffer_width/height`.
- **`BufferDesc.Format = DXGI_FORMAT_R10G10B10A2_UNORM`** — **TrussC 10-bit patch #1** (8592;
  upstream is BGRA8/RGBA8).
- `RefreshRate = 60/1`, `OutputWindow = hwnd`, `Windowed = true`.
- **Win10+**: `BufferCount = 2`, `SwapEffect = FLIP_DISCARD` (via `_SAPP_DXGI_SWAP_EFFECT_FLIP_DISCARD
  = 4`, defined at 2512 because older SDKs lack the enum). **Pre-Win10**: `BufferCount = 1`,
  `SwapEffect = DISCARD`. (`is_win10_or_greater` gates this.)
- `SampleDesc.Count = 1, Quality = 0` — **the swapchain itself is always single-sampled**;
  MSAA is done via a separate offscreen render target because DXGI flip-model can't MSAA the
  backbuffer directly (this is the sokol MSAA-resolve model, see render target below).
- `BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT`.

Create flags: `D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_BGRA_SUPPORT`, plus
`D3D11_CREATE_DEVICE_DEBUG` under `SOKOL_DEBUG`. `D3D11CreateDeviceAndSwapChain` with
`D3D_DRIVER_TYPE_HARDWARE`, feature levels `{11_1, 11_0}`. On debug-flag failure it logs
`WIN32_D3D11_CREATE_DEVICE_AND_SWAPCHAIN_WITH_DEBUG_FAILED` and **retries without the DEBUG
flag** (8639–8653).

Post-create (8657–8678): `QueryInterface(IDXGIDevice1)` → `SetMaximumFrameLatency(1)` (minimize
latency), then walk `GetAdapter → GetParent(IDXGIFactory)` →
`MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_PRINT_SCREEN)` — **disables
DXGI's built-in Alt-Enter fullscreen and PrintScreen capture** (sokol drives fullscreen itself).

### Default render target (`_sapp_d3d11_create_default_render_target`, 8688–8731)
1. `rt = swap_chain->GetBuffer(0)`, `rtv = CreateRenderTargetView(rt)` — view onto the
   single-sampled backbuffer.
2. Common `D3D11_TEXTURE2D_DESC`: `Width/Height = framebuffer`, `MipLevels=1, ArraySize=1`,
   `Usage=DEFAULT`, `BindFlags=RENDER_TARGET`, `SampleDesc.Count = sample_count`,
   `Quality = (sample_count>1 ? D3D11_STANDARD_MULTISAMPLE_PATTERN : 0)`.
3. **If `sample_count > 1`:** `tex_desc.Format = DXGI_FORMAT_R10G10B10A2_UNORM` — **TrussC
   10-bit patch #2** (8717) — create `msaa_rt` + `msaa_rtv` (the actual render target; resolved
   into `rt`/`rtv` at present by sokol_gfx).
4. Depth-stencil: `Format = DXGI_FORMAT_D24_UNORM_S8_UINT`, `BindFlags = DEPTH_STENCIL`, create
   `ds` + `dsv`. (Depth is D24S8, NOT patched to 10-bit — only color is.)

### Resize (`_sapp_d3d11_resize_default_render_target`, 8742–8748)
Guarded on `swap_chain` non-null: **destroy the render target first**
(`destroy_default_render_target` releases rt/rtv/msaa/ds/dsv), then
`swap_chain->ResizeBuffers(BufferCount, framebuffer_width, framebuffer_height,
DXGI_FORMAT_R10G10B10A2_UNORM, 0)` — **TrussC 10-bit patch #3** (8745) — then recreate the
render target. Ordering matters: all views onto the backbuffer must be released before
`ResizeBuffers` or it fails. Called from the loop after `_sapp_win32_update_dimensions()`
returns true (10195–10197), NOT from WM_SIZE.

### Present + skip_present (`_sapp_d3d11_present`, 8750–8766)
```c
if (_sapp.skip_present) { _sapp.skip_present = false; return; }   // TrussC patch, HONORED on D3D11
UINT flags = 0;
if (is_win10_or_greater && do_not_wait) flags = DXGI_PRESENT_DO_NOT_WAIT;  // modal-loop responsiveness
swap_chain->Present((UINT)_sapp.swap_interval, flags);            // sync interval = swap_interval
```
- **`skip_present` IS honored on Windows/D3D11** (unlike mac/Metal where it's ignored). It is a
  one-shot: consumed (reset to false) each time. This is the TrussC event-driven present
  suppression that fixes D3D11 flickering (patch #4, and the whole reason skip_present exists).
- `do_not_wait` is only true when presenting from `WM_TIMER` during a resize/move modal loop
  (a NVIDIA/Win10 responsiveness workaround, 8758–8762).
- Sync interval comes straight from `_sapp.swap_interval` (1 = vsync on).

### Win32 D3D11 vtable shims (8477–8586)
C++ vs C dispatch macros (`_sapp_d3d11_Release`, `_sapp_win32_refiid`) and thin inline wrappers
(`_sapp_dxgi_GetBuffer`, `_sapp_d3d11_CreateRenderTargetView/CreateTexture2D/
CreateDepthStencilView`, `_sapp_dxgi_ResizeBuffers/Present/GetFrameStatistics/
SetMaximumFrameLatency/GetAdapter/GetParent/MakeWindowAssociation`). TrussC builds as C++ so the
`->Method()` form is used. `_SAPP_SAFE_RELEASE` releases+nulls.

---

## 7. sapp_get_environment / sapp_get_swapchain (D3D11)

### `sapp_get_environment()` (14387–14415) — asserts `_sapp.valid`
- `defaults.color_format = sapp_color_format()` → **`SAPP_PIXELFORMAT_RGB10A2`** on D3D11
  (14042, `#elif defined(SOKOL_METAL) || defined(SOKOL_D3D11)` — **TrussC 10-bit patch**).
- `defaults.depth_format = SAPP_PIXELFORMAT_DEPTH_STENCIL` (14048), `sample_count`.
- `d3d11.device = _sapp.d3d11.device`, `d3d11.device_context = _sapp.d3d11.device_context`
  (14401–14402). (`sapp_d3d11_environment` = {device, device_context}, 1894.)

### `sapp_get_swapchain()` (14417–14485) — asserts `_sapp.valid`
D3D11 branch (14431–14441), asserts `rtv`:
- **`sample_count > 1`:** `render_view = msaa_rtv`, `resolve_view = rtv` (render into MSAA,
  resolve into backbuffer).
- **`sample_count == 1`:** `render_view = rtv`, `resolve_view` left null.
- `depth_stencil_view = dsv`.
- Common tail: `width = sapp_width()`, `height = sapp_height()`,
  `color_format = RGB10A2`, `depth_format = DEPTH_STENCIL`, `sample_count`.

**Crucial contrast with Metal:** the D3D11 views are **created once at swapchain-create /
resize time and reused every frame** — `sapp_get_swapchain()` on D3D11 does NOT acquire or
rotate anything per frame (no `nextDrawable` equivalent). The FLIP_DISCARD swapchain rotates
its backbuffer internally; sokol keeps rendering into the same RTV. So the "must call exactly
once per frame" rule (documented at 1925) is a Metal/WGPU/Vulkan constraint, harmless on D3D11.

### glue contract (`sokol_glue.h`)
`sglue_environment()` reads `env.defaults.{color,depth}_format/sample_count` plus
`env.d3d11.device` (167) and `env.d3d11.device_context` (168). `sglue_swapchain()` reads
`sc.width/height/sample_count/color_format/depth_format` plus `sc.d3d11.render_view` (190),
`sc.d3d11.resolve_view` (191), `sc.d3d11.depth_stencil_view` (192). **These are the only sapp
fields the shim must fill for the D3D11 render path** — metal/wgpu/vulkan/gl fields are
memset-0. Color format maps RGB10A2 → SG_PIXELFORMAT_RGB10A2 (same helper as mac).

The declared getters `sapp_d3d11_get_device/device_context/render_view/resolve_view/
depth_stencil_view` (docs 334–338) are **NOT implemented in this fork** — only
`sapp_d3d11_get_swap_chain` (14522) exists. The glue reads the environment/swapchain structs
directly, so the missing getters are not load-bearing.

---

## 8. Quit flow (Win32)

Public API is cross-platform and identical to mac: `sapp_request_quit()` sets `quit_requested`;
`sapp_cancel_quit()` clears it; `sapp_quit()` sets `quit_ordered` (unvetoable). The Win32-specific
plumbing:
1. From user code, `quit_requested` is noticed at the **bottom of the loop** (10203):
   `PostMessage(hwnd, WM_CLOSE, 0, 0)` — routes even a programmatic quit through the same
   `WM_CLOSE` dance so QUIT_REQUESTED still fires (for `sapp_request_quit`) or is skipped (for
   `sapp_quit`, since `quit_ordered` is already true).
2. `WM_CLOSE` (§3) either fires QUIT_REQUESTED (user can cancel) or commits, then
   `PostQuitMessage(0)`.
3. `WM_QUIT` is dequeued in the loop → `done = true` → loop exits → cleanup (§1) runs
   `cleanup_cb` **before** D3D11 teardown (same ordering guarantee as mac).

Note: `sapp_quit()` also makes the loop condition `!(done || quit_ordered)` false directly, so
even without the WM_CLOSE round-trip the loop would exit; the PostMessage path just ensures the
event semantics are consistent.

---

## 9. Clipboard (process-global OS clipboard, per-window HWND owner)

Enabled only if `desc.enable_clipboard` (allocates `clipboard.buffer`). Uses `hwnd` as the
clipboard owner.
- **`sapp_set_clipboard_string` → `_sapp_win32_set_clipboard_string` (9986–10029):**
  `OpenClipboard(hwnd)`; `GlobalAlloc(GMEM_MOVEABLE, buf_size*sizeof(wchar_t))`; `GlobalLock`;
  `_sapp_win32_utf8_to_wide`; `GlobalUnlock`; `EmptyClipboard()`;
  `SetClipboardData(CF_UNICODETEXT, object)` (**clipboard takes ownership of the global on
  success**); `CloseClipboard`. Full goto-based error cleanup.
- **`sapp_get_clipboard_string` → `_sapp_win32_get_clipboard_string` (10031–10056):**
  `OpenClipboard(hwnd)`; `GetClipboardData(CF_UNICODETEXT)`; `GlobalLock`;
  `_sapp_win32_wide_to_utf8` into `clipboard.buffer` (`CLIPBOARD_STRING_TOO_BIG` error if it
  overflows); returns the internal buffer. On any failure silently returns the current buffer.
- **CLIPBOARD_PASTED trigger:** unlike mac (which detects Cmd+V in `keyDown:`), Win32 detects
  it inside `_sapp_win32_key_event` (9455–9463): on `KEY_DOWN` with `modifiers == SAPP_MODIFIER_CTRL`
  (exact match) and `key_code == SAPP_KEYCODE_V`, fire `CLIPBOARD_PASTED` (reusing the already-
  populated event). There is **no WM_PASTE handling** — it's key-detection based, same idea as mac
  but with Ctrl instead of Super, and Win32 does not pre-populate the buffer (app calls
  `sapp_get_clipboard_string()` in its handler).

---

## 10. Mouse cursor

### Standard cursors (`_sapp_win32_init_cursor` 9151 / `_init_cursors` 9182) — global-ish (LR_SHARED)
Per-enum `LoadImageW(NULL, MAKEINTRESOURCEW(OCR_*), IMAGE_CURSOR, 0,0, LR_DEFAULTSIZE|LR_SHARED)`.
OCR ids inlined (OEMRESOURCE may not be defined): ARROW→32512, IBEAM→32513, CROSSHAIR→32515,
POINTING_HAND→32649, RESIZE_EW→32644, RESIZE_NS→32645, RESIZE_NWSE→32642, RESIZE_NESW→32643,
RESIZE_ALL→32646, NOT_ALLOWED→32648. Fallback to `LoadCursorW(IDC_ARROW=32512)` if load fails.

### Show/hide + apply (`_sapp_win32_update_cursor`, 9203–9224)
Called with `(cursor, shown, skip_area_test)`. Unless `skip_area_test`, it first checks
`_sapp_win32_cursor_in_content_area()` (GetCursorPos + WindowFromPoint==hwnd + PtInRect on the
client rect, 9188) and bails if the pointer isn't over the client area. Then: if `shown`, pick
`custom_cursors[cursor]` (when `custom_cursor_bound[cursor]`) else `standard_cursors[cursor]`;
if `!shown`, `cursor_handle = NULL`. `SetCursor(cursor_handle)` — **NULL hides the cursor**.
Driven from **WM_SETCURSOR** (skip_area_test=true, HTCLIENT only) and from the public
`sapp_show_mouse`/cursor-type setters (skip_area_test=false, 14120). Note: hide is via
`SetCursor(NULL)` per WM_SETCURSOR, NOT the `ShowCursor` counter — the `ShowCursor(FALSE/TRUE)`
counter is used **only** by the mouse-lock path (9281/9330), avoiding the counter-stacking bug.

### Custom cursors (`_sapp_win32_make_custom_mouse_cursor` 10259 / `_destroy` 10267)
`make` = `_sapp_win32_create_icon_from_image(desc, is_cursor=true)` (shared with icon creation,
10063) → an HCURSOR via `CreateIconIndirect` with the hotspot. `destroy` = `DestroyIcon`
(warns `WIN32_DESTROYICON_FOR_CURSOR_FAILED` on failure but doesn't hard-fail). Public
`sapp_bind/unbind_mouse_cursor_image` route here (14188/14218).

---

## 11. Fullscreen (`_sapp_win32_set_fullscreen`, 9113–9145)

`sapp_is_fullscreen()` returns `_sapp.fullscreen`. `sapp_toggle_fullscreen()` →
`_sapp_win32_toggle_fullscreen()` → `_sapp_win32_set_fullscreen(!fullscreen, SWP_SHOWWINDOW)`.
The set function does a **style-swap + reposition** (no OS "fullscreen mode"):
- Monitor rect via `MonitorFromWindow(MONITOR_DEFAULTTONEAREST)` + `GetMonitorInfo`.
- **→ fullscreen:** save current `GetWindowRect` into `stored_window_rect`; style becomes
  `WS_POPUP | WS_SYSMENU | WS_VISIBLE`; target rect = full monitor rect, adjusted via
  `AdjustWindowRectEx`.
- **→ windowed:** style restored to `WS_CLIPSIBLINGS|WS_CLIPCHILDREN|WS_CAPTION|WS_SYSMENU|
  WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_SIZEBOX`; rect = `stored_window_rect`.
- Apply: `SetWindowLongPtr(GWL_STYLE)` then `SetWindowPos(HWND_TOP, ..., swp_flags |
  SWP_FRAMECHANGED)`.
- `_sapp.fullscreen` is set at the top (9126) — single source of truth (no separate
  notification correction like mac's `windowDidEnter/ExitFullScreen`). At startup, fullscreen
  is entered from `create_window` with `SWP_HIDEWINDOW` (window built windowed+hidden first,
  9848). This is **borderless-fullscreen-window**, not exclusive DXGI fullscreen (which is
  explicitly disabled via `DXGI_MWA_NO_ALT_ENTER`).

---

## 12. Misc

- **`sapp_set_window_title` → `_sapp_win32_update_window_title` (10058–10061):** convert
  `window_title` → `window_title_wide` (`_sapp_win32_utf8_to_wide`), `SetWindowTextW`.
- **UTF-8 ↔ UTF-16 helpers:** `_sapp_win32_utf8_to_wide` (8328, `MultiByteToWideChar(CP_UTF8)`,
  returns false if it won't fit) and `_sapp_win32_wide_to_utf8` (9067,
  `WideCharToMultiByte(CP_UTF8)`). Used for title, clipboard, dropped-file paths, argv. WM_CHAR
  surrogate accumulation is separate (§3, `win32.surrogate`).
- **`sapp_win32_get_hwnd` (14531):** returns `_sapp.win32.hwnd` (asserts valid).
- **`sapp_high_dpi` (14056):** `desc.high_dpi && dpi_scale >= 1.5` (same as mac).
- **`sapp_show_keyboard`:** Win32 no-op (`onscreen_keyboard_shown` stays false).
- **Icon (`_sapp_win32_set_icon`, 10115–10138):** best-match big+small images →
  `_sapp_win32_create_icon_from_image` (BITMAPV5 top-down BGRA DIB + mono mask →
  `CreateIconIndirect`) → `WM_SETICON ICON_BIG/ICON_SMALL`. Destroys prior icons. Optional for
  rendering; a real titlebar/taskbar icon (unlike mac's dock-tile-only). Destroyed at exit.
- **`iconified` tracking:** `win32.iconified` set in WM_SIZE (§3); `IsIconic(hwnd)` used in
  `_sapp_win32_frame` to throttle while minimized.
- **TrussC patches in the win32/D3D11 path** (all cross-reference TRUSSC_MODIFICATIONS.md):
  1. Swapchain `BufferDesc.Format = R10G10B10A2_UNORM` (8592).
  2. MSAA texture `Format = R10G10B10A2_UNORM` (8717).
  3. `ResizeBuffers(... R10G10B10A2_UNORM ...)` (8745).
  4. `sapp_color_format()` returns `SAPP_PIXELFORMAT_RGB10A2` on D3D11 (14042).
  5. **`skip_present` honored in `_sapp_d3d11_present`** (8752, the big one — event-driven
     present suppression to fix D3D11 flicker; one-shot consume). No such patch exists/works on
     Metal.
  `SAPP_PIXELFORMAT_RGB10A2` enum itself is a TrussC addition (1865).

---

## Contradictions / surprises vs the task's stated assumptions

See the summary at the top of the final report.

---

## Appendix: function → line index (D3D11-relevant)

| function | line |
|---|---|
| `_sapp_win32_utf8_to_wide` | 8328 |
| `_sapp_win32_app_event` | 8342 |
| `_sapp_win32_init_keytable` | 8349 |
| d3d11 vtable shims | 8477–8586 |
| `_sapp_d3d11_create_device_and_swapchain` | 8588 |
| `_sapp_d3d11_destroy_device_and_swapchain` | 8681 |
| `_sapp_d3d11_create_default_render_target` | 8688 |
| `_sapp_d3d11_destroy_default_render_target` | 8733 |
| `_sapp_d3d11_resize_default_render_target` | 8742 |
| `_sapp_d3d11_present` | 8750 |
| `_sapp_win32_wide_to_utf8` | 9067 |
| `_sapp_win32_update_dimensions` | 9080 |
| `_sapp_win32_set_fullscreen` / `_toggle_fullscreen` | 9113 / 9147 |
| `_sapp_win32_init_cursor` / `_init_cursors` | 9151 / 9182 |
| `_sapp_win32_cursor_in_content_area` | 9188 |
| `_sapp_win32_update_cursor` | 9203 |
| `_sapp_win32_capture_mouse` / `_release_mouse` | 9226 / 9233 |
| `_sapp_win32_lock_mouse` / `_do_lock` / `_do_unlock` / `_update_mouse_lock` | 9246 / 9276 / 9326 / 9353 |
| `_sapp_win32_mods` | 9387 |
| `_sapp_win32_mouse_update` | 9414 |
| `_sapp_win32_mouse_event` / `_scroll_event` / `_key_event` / `_char_event` | 9429 / 9438 / 9448 / 9467 |
| `_sapp_win32_dpi_changed` | 9486 |
| `_sapp_win32_files_dropped` | 9517 |
| `_sapp_win32_frame` | 9548 |
| `_sapp_win32_wndproc` | 9570 |
| `_sapp_win32_create_window` / `_destroy_window` | 9797 / 9855 |
| `_sapp_win32_init_console` / `_restore_console` | 9871 / 9895 |
| `_sapp_win32_init_dpi` | 9901 |
| `_sapp_win32_set/get_clipboard_string` | 9986 / 10031 |
| `_sapp_win32_update_window_title` | 10058 |
| `_sapp_win32_create_icon_from_image` / `_set_icon` | 10063 / 10115 |
| `_sapp_win32_is_win10_or_greater` | 10145 |
| `_sapp_win32_run` | 10154 |
| `_sapp_win32_command_line_to_utf8_argv` | 10229 |
| `_sapp_win32_make/destroy_custom_mouse_cursor` | 10259 / 10267 |
| `main` / `WinMain` | 10284 / 10291 |
| `sapp_color_format` | 14018 |
| `sapp_get_environment` (d3d11 fields) | 14400 |
| `sapp_get_swapchain` (d3d11 fields) | 14431 |
| `sapp_d3d11_get_swap_chain` | 14522 |
| `sapp_win32_get_hwnd` | 14531 |
