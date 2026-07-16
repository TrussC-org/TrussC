# sokol_app.h macOS (Metal) Backend — Behavioral Specification

Implementation contract for replacing sokol_app.h's macOS main-window / app-lifecycle
with `sokol_app_tc.h`. All line references are into
`core/include/sokol/sokol_app.h` (14602 lines, this worktree). Focus: `_SAPP_MACOS`
+ `SOKOL_METAL`. GL/WGPU paths noted only where they diverge; iOS/Win/Linux/web ignored.

The replacement already covers secondary windows (NSWindow+CAMetalLayer, CADisplayLink,
event mapping w/ 111-key table, occlusion gating, depth/MSAA textures). This document
specifies the **main window + app lifecycle** that is still missing.

---

## 0. Global state touched (the shared contract)

The single global is `static _sapp_t _sapp;` (definition around line 3298 embeds
`_sapp_macos_t macos;`). The macOS struct is at **2790–2823**:

```c
typedef struct {
    uint32_t flags_changed_store;   // last NSEventModifierFlags seen by flagsChanged
    uint8_t mouse_buttons;          // bitmask of held buttons (enter/leave suppression during drag)
    NSWindow* window;
    NSTrackingArea* tracking_area;
    id keyup_monitor;               // local event monitor (Cmd+key-up workaround)
    _sapp_macos_app_delegate* app_dlg;
    _sapp_macos_window_delegate* win_dlg;
    _sapp_macos_view* view;
    NSCursor* standard_cursors[_SAPP_MOUSECURSOR_NUM];
    NSCursor* custom_cursors[_SAPP_MOUSECURSOR_NUM];
    struct {                        // SOKOL_METAL only
        id<MTLDevice> device;
        CAMetalLayer* layer;
        CADisplayLink* display_link;
        NSTimer* fallback_timer;
        id<MTLTexture> depth_tex;
        id<MTLTexture> msaa_tex;
        struct { CFTimeInterval timestamp; CFTimeInterval frame_duration_sec; } timing;
    } mtl;
} _sapp_macos_t;
```

Cross-platform `_sapp` fields the mac path reads/writes: `desc`, `valid`,
`first_frame`, `init_called`, `cleanup_called`, `quit_requested`, `quit_ordered`,
`fullscreen`, `window_width/height`, `framebuffer_width/height`, `dpi_scale`,
`sample_count`, `swap_interval`, `frame_count`, `timing`, `mouse.*`, `clipboard.*`,
`drop.*`, `event`, `keycodes[]`, `window_title`, `custom_cursor_bound[]`,
`event_consumed`, `skip_present`, `onscreen_keyboard_shown`.

`_sapp_init_state()` (**3554**) is called first by `_sapp_macos_run` and initializes
all of the above from `desc` (defaults applied by `_sapp_desc_defaults` at **3524**:
sample_count→1, swap_interval→1, clipboard_size→8192, max_dropped_files→1,
max_dropped_file_path_length→2048, window_title→"sokol"). Notably it sets
`first_frame=true`, `dpi_scale=1.0`, `mouse.shown=true`, `fullscreen=desc.fullscreen`,
and copies width/height verbatim (**may be 0** — the mac backend must resolve 0).

---

## 1. NSApplication bootstrap

### Entry (`_sapp_macos_run`, 5519–5542; `main`, 5545–5551; `sapp_run`, 13941)

Order is exact and load-bearing:

1. `_sapp_init_state(desc)` — zero+init global state.
2. `_sapp_macos_init_keytable()` — fill `_sapp.keycodes[]` (111 entries, 5359–5471).
3. `[NSApplication sharedApplication]` — creates NSApp (does **not** set activation policy yet).
4. `sapp_set_icon(&_sapp.desc.icon)` — set dock icon "as early as possible" (comment at 5524). This calls `_sapp_macos_set_icon` (5816) which writes `NSApp.dockTile.contentView`. **Main-window path does NOT require icon support** — it is a dock-tile decoration only, safe to stub/skip. `sapp_set_icon` (14291) no-ops when `num_images==0` / default not requested.
5. `_sapp.macos.app_dlg = [[_sapp_macos_app_delegate alloc] init]; NSApp.delegate = app_dlg;`
6. Install **Cmd-key-up workaround** (5530–5537): a block-based `addLocalMonitorForEventsMatchingMask:NSEventMaskKeyUp`. When a keyUp arrives while `NSEventModifierFlagCommand` is held, it force-forwards the event to `[NSApp keyWindow]` (Cocoa otherwise swallows key-ups under Cmd). The monitor id is stored in `_sapp.macos.keyup_monitor`. Removed in `_sapp_macos_discard_state` via `[NSEvent removeMonitor:]` (which also releases it).
7. `[NSApp run]` — **never returns**. All cleanup lives in `applicationWillTerminate`.

`SOKOL_NO_ENTRY`: when defined, the `int main(...)` at 5546 is compiled out; the user
supplies their own entry that calls `sapp_run(desc)` → `_sapp_macos_run`. Otherwise
sokol's `main` calls `sokol_main(argc,argv)` to obtain `desc` then `_sapp_macos_run`.

### NSApplicationDelegate methods (`@implementation _sapp_macos_app_delegate`, 5872–5945)

- **`applicationDidFinishLaunching:` (5873–5932)** — the real app setup, runs inside `[NSApp run]`. See §2.
- **`applicationShouldTerminateAfterLastWindowClosed:` (5934–5937)** — returns `YES`. Closing the (sole) window terminates the app.
- **`applicationWillTerminate:` (5939–5944)** — the cleanup site. Runs, in order: `_sapp_call_cleanup()` (user cleanup_cb, once), `_sapp_macos_discard_state()` (release ObjC objects incl. Metal device/layer/textures, stop display link/timer, remove tracking area + keyup monitor), `_sapp_discard_state()` (free clipboard/drop/icon buffers, unbind custom cursors, zero `_sapp`).

No `applicationShouldTerminate:` is implemented — termination is driven by window close
(see §3). **sokol does NOT create a default menu bar** (no `NSMenu`/`mainMenu` setup
anywhere in the mac path). Activation policy is `NSApplicationActivationPolicyRegular`
set inside `applicationDidFinishLaunching` (5876), explicitly kept **before window
creation** (comment cites floooh/sokol#1500).

---

## 2. Main window creation (`applicationDidFinishLaunching:`, 5873–5932)

Exact sequence:

1. `NSApp.activationPolicy = NSApplicationActivationPolicyRegular;` (must precede window creation).
2. `_sapp_macos_init_cursors()` (5502) — populate `standard_cursors[]` (see §6).
3. If `window_width==0 || window_height==0` → `_sapp_macos_init_default_dimensions()` (5608): sets `dpi_scale` (backingScaleFactor if `high_dpi` else 1.0), computes default = **4/5 of `NSScreen.mainScreen.frame`** for each 0 dimension, and sets `framebuffer_width/height = default * dpi_scale`.
4. **Style mask is fixed** (5881–5885): `Titled | Closable | Miniaturizable | Resizable`. There is **no borderless / no fullscreen-derived style** — `desc.fullscreen` does not change the style mask; fullscreen is entered via `toggleFullScreen:` after creation (step 11).
5. `window_rect = NSMakeRect(0,0,window_width,window_height)` (content rect, points).
6. `_sapp.macos.window = [[_sapp_macos_window alloc] initWithContentRect:window_rect styleMask:style backing:NSBackingStoreBuffered defer:NO]`. The custom `_sapp_macos_window initWithContentRect:` (6044) also calls `registerForDraggedTypes:@[NSPasteboardTypeFileURL]` (drag&drop is always registered on the main window regardless of `enable_dragndrop`; the *handler* gates on `_sapp.drop.enabled` indirectly, see §7).
7. `window.releasedWhenClosed = NO` — **required** so the window object survives `performClose`/close until `applicationWillTerminate` cleanup (comment at 5892).
8. `window.title = NSString(window_title)`, `window.acceptsMouseMovedEvents = YES`, `window.restorable = YES`.
9. `win_dlg = [[_sapp_macos_window_delegate alloc] init]; window.delegate = win_dlg;`
10. Renderer init: **`_sapp_macos_mtl_init()` (5211)** for Metal (creates device, CAMetalLayer, view, starts display link, inits timing — see §10). Then `window.contentView = _sapp.macos.view; [window makeFirstResponder:view]; [window center];`
11. `_sapp.valid = true;` (set **before** fullscreen toggle — comment 5911 notes GL renders a frame during toggle). If `_sapp.fullscreen`: `[window toggleFullScreen:self]`.
12. `[NSApp activateIgnoringOtherApps:YES];` then `[window makeKeyAndOrderFront:nil];`
13. `_sapp_macos_update_dimensions()` (5632) — sample real bounds, resize swapchain, but **suppresses** the RESIZED event because `first_frame` is still true (see §11).
14. `[NSEvent setMouseCoalescingEnabled:NO]` — full-resolution mouse-move events.
15. **Focus workaround** (5919–5931, cites sokol#982): posts a synthetic `NSEventTypeAppKitDefined` / `NSEventSubtypeApplicationActivated` event to `NSApp` at-start, so the window is focused even if the init callback blocks for a long time.

### High-DPI / dpi_scale sourcing

- `desc.high_dpi` gates everything. `dpi_scale` is sampled from `backingScaleFactor` of the *screen* (`NSScreen.mainScreen.backingScaleFactor` at startup in `_sapp_macos_init_default_dimensions`; `[_sapp.macos.window screen].backingScaleFactor` on every `_sapp_macos_update_dimensions`, 5634). If `!high_dpi`, `dpi_scale = 1.0` always.
- `framebuffer = round(view.bounds.size * dpi_scale)`; `window_width/height = round(view.bounds.size)` in points.
- `view.layer.contentsScale = dpi_scale` is re-applied every update (5639) because `windowWillStartLiveResize` sets a non-scaling `layerContentsPlacement`.
- `sapp_high_dpi()` (14056) returns `desc.high_dpi && dpi_scale >= 1.5` (NOT just `high_dpi`).

Initial size logic: `desc.width/height==0` → 4/5 of main screen. Centering via
`[window center]`. Title from `window_title`. `collectionBehavior` is **not** set
(default) — standard AppKit fullscreen behavior applies via `toggleFullScreen:`.

---

## 3. Quit flow semantics (critical)

State flags: `quit_requested`, `quit_ordered` (both bool in `_sapp`).

### Public API
- `sapp_request_quit()` (14225): `quit_requested = true;` (nothing else).
- `sapp_cancel_quit()` (14229): `quit_requested = false;`.
- `sapp_quit()` (14233): `quit_ordered = true;` (hard, unconditional — no user veto).

### How a quit reaches the OS

There is **no NSApp terminate call** in the mac path. Termination is driven entirely
through **window close**:

1. **`_sapp_macos_frame()` tail (5867–5869):** after every frame, `if (quit_requested || quit_ordered) [_sapp.macos.window performClose:nil];`. So a `sapp_request_quit()` or `sapp_quit()` made from user code takes effect on the *next frame boundary* by asking the window to close.
2. **`windowShouldClose:` (5948–5966)** — the decision point, invoked by `performClose:`, the red close button, or Cmd+Q routed to the window:
   ```
   if (!quit_ordered) {                      // sapp_quit() not already called
       quit_requested = true;
       fire SAPP_EVENTTYPE_QUIT_REQUESTED;    // user can call sapp_cancel_quit() here
       if (quit_requested) quit_ordered = true;   // user did NOT cancel → commit
   }
   return quit_ordered ? YES : NO;           // YES closes the window
   ```
   - **Red close button / Cmd+Q path:** `quit_ordered` starts false → QUIT_REQUESTED is delivered synchronously; if the event handler calls `sapp_cancel_quit()`, `quit_requested` becomes false, `quit_ordered` stays false, returns **NO** (window stays open). Otherwise returns **YES**.
   - **`sapp_quit()` path:** `quit_ordered` already true → skips the whole block, no QUIT_REQUESTED fired, returns **YES** immediately (unvetoable).
3. Window actually closes → (sole window) `applicationShouldTerminateAfterLastWindowClosed:`=YES → app terminates → **`applicationWillTerminate:`** runs cleanup: `_sapp_call_cleanup()` (frame/user cleanup_cb) **then** `_sapp_macos_discard_state()` (which releases the Metal device etc.). **Ordering guarantee:** user `cleanup_cb` runs *before* any GPU/Metal teardown. (TrussC does its own `sg_shutdown` inside cleanup_cb; sokol only releases the *view/layer/device* afterward — sokol_gfx shutdown is the app's responsibility.)

QUIT_REQUESTED event population: plain `_sapp_init_event(SAPP_EVENTTYPE_QUIT_REQUESTED)`
via `_sapp_macos_app_event` (5600), only if `_sapp_events_enabled()` (event_cb set AND
`init_called`).

---

## 4. Frame timing

Two layers: the platform-agnostic filtered timer (`_sapp.timing`, a `_sapp_timing_t`)
and a Metal-specific CADisplayLink-timestamp timer (`_sapp.macos.mtl.timing`).

### Platform-agnostic filter (`_sapp_timing_*`, 2643–2711)
Config (`_sapp_timing_init`, 2655): `dt_min=1µs`, `dt_max=100ms`, `dt_threshold=4ms`,
`alpha=0.025`, initial `dt=ema=smooth_dt=1/60`.

`_sapp_timing_update(t, external_now)` (2694): `now = external_now==0 ? timestamp_now()
: external_now`. On the **first** call `t->last==0` so no delta is produced (just stores
`last=now`). Subsequent calls compute `dt = now - last` → `_sapp_timing_delta`.

`_sapp_timing_delta` (2677): clamp raw `dt` to [dt_min,dt_max] → store as unfiltered
`t->dt`. Then `error = |dt - smooth_dt|`; if `error > dt_threshold` **reset** the filter
(`ema = smooth_dt = dt`), else EMA: `ema += alpha*(dt - ema); smooth_dt = clamp(ema)`.

`_sapp_timing_get` (2709) returns `smooth_dt`. `sapp_frame_duration_unfiltered()`
(13998) returns raw `t->dt`.

### Metal CADisplayLink timing (`_sapp_macos_mtl_timing_*`, 5126–5154)
Because `CADisplayLink.timestamp` is very stable, Metal uses it *instead of* the measured
filter when the display link is active:
- `_timing_init` (5126): `timestamp=0`, `frame_duration_sec = 1.0/_sapp_macos_max_fps()` (`max_fps = [NSScreen.mainScreen maximumFramesPerSecond]`, 5056).
- `_timing_update` (5131), called each frame from `_sapp_macos_frame`: only when display link active. `cur = display_link.timestamp`; **first frame is skipped** (`timing.timestamp>0` guard) — `frame_duration_sec` keeps its refresh-rate seed; otherwise `dt = cur - prev`, run through `_sapp_timing_clamp` (min/max clamp only, NOT the EMA) → `frame_duration_sec`. Store `timestamp=cur`.
- `_timing_frame_duration` (5147): if display link active return `frame_duration_sec`; else fall back to `_sapp_timing_get(&_sapp.timing)` (the EMA) — this is the **occluded / fallback-timer** case.

`sapp_frame_duration()` (13988) on macOS+Metal → `_sapp_macos_mtl_timing_frame_duration()`.

### Frame count
`_sapp.frame_count` incremented in `_sapp_frame()` (3655), *after* `_sapp_call_frame()`.
On the very first `_sapp_frame` call `first_frame` is cleared and `init_cb` is called
before the first `frame_cb`. `sapp_frame_count()` (13984) returns the raw counter.
`_sapp_init_event` stamps `event.frame_count = _sapp.frame_count` (3617), so events fired
during frame N carry count N (count is incremented only after the frame_cb returns).
First frame value: 0 (frame_cb runs with count still 0, becomes 1 after).

Per-frame timing driver (`_sapp_macos_frame`, 5849): `_sapp_timing_update(&_sapp.timing, 0.0)`
runs **every** frame (feeds the fallback path), then `_sapp_macos_mtl_timing_update()`,
then (inside `@autoreleasepool`) `_sapp_frame()`.

---

## 5. Clipboard

Enabled only if `desc.enable_clipboard`. `_sapp_init_state` allocates
`clipboard.buffer` of `clipboard_size` bytes (default 8192) and sets `clipboard.enabled`,
`clipboard.buf_size` (3576–3580).

- **`sapp_set_clipboard_string(str)` (14242):** no-op if `!clipboard.enabled`. Calls `_sapp_macos_set_clipboard_string` (5664): inside `@autoreleasepool`, `[[NSPasteboard generalPasteboard] declareTypes:@[NSPasteboardTypeString] owner:nil]` then `setString:@(str) forType:NSPasteboardTypeString`. Then also copies `str` into `_sapp.clipboard.buffer`.
- **`sapp_get_clipboard_string()` (14261):** returns `""` if disabled; else `_sapp_macos_get_clipboard_string` (5672): zeroes `buffer[0]`, checks pasteboard `types` contains `NSPasteboardTypeString`, reads `stringForType:` UTF8 into `buffer` (clamped to `buf_size`). Returns the internal buffer pointer.
- **CLIPBOARD_PASTED on mac:** fired from `keyDown:` (6292–6296): if `clipboard.enabled && mods == SAPP_MODIFIER_SUPER && key_code == SAPP_KEYCODE_V`, a `SAPP_EVENTTYPE_CLIPBOARD_PASTED` event is emitted (after the KEY_DOWN and CHAR events). **Exact-match** on modifiers: `mods == SAPP_MODIFIER_SUPER` (Cmd alone; Cmd+Shift+V would not match). Unlike Win/Linux, mac does *not* pre-populate the clipboard buffer before the event — the app is expected to call `sapp_get_clipboard_string()` in its handler.

---

## 6. Mouse cursor

### Show/hide
- `sapp_show_mouse(show)` (14131): **does not stack** (comment 14130). Only acts when `mouse.shown != show`, routing through `_sapp_update_cursor(current_cursor, show)` (14116) which calls `_sapp_macos_update_cursor` then stores `mouse.current_cursor`/`mouse.shown`.
- `_sapp_macos_show_mouse(visible)` (5715) — used by lock path only; uses `CGDisplayShowCursor`/`CGDisplayHideCursor(kCGDirectMainDisplay)`.
- `_sapp_macos_update_cursor(cursor, shown)` (5750): if `shown != mouse.shown` it stacks `[NSCursor unhide]`/`[NSCursor hide]` (note: this *does* stack, so it is gated on an actual change). Then selects the NSCursor: custom if `custom_cursor_bound[cursor]`, else `standard_cursors[cursor]`, else `[NSCursor arrowCursor]`; `[ns_cursor set]`.

### Standard cursor table (`_sapp_macos_init_cursors`, 5502–5517)
`ARROW→arrowCursor`, `IBEAM→IBeamCursor`, `CROSSHAIR→crosshairCursor`,
`POINTING_HAND→pointingHandCursor`, and the resize cursors use **undocumented private
selectors** declared in a category (5495–5500):
`RESIZE_EW→_windowResizeEastWestCursor` (fallback `resizeLeftRightCursor`),
`RESIZE_NS→_windowResizeNorthSouthCursor` (fb `resizeUpDownCursor`),
`RESIZE_NWSE→_windowResizeNorthWestSouthEastCursor` (fb `closedHandCursor`),
`RESIZE_NESW→_windowResizeNorthEastSouthWestCursor` (fb `closedHandCursor`),
`RESIZE_ALL→closedHandCursor`, `NOT_ALLOWED→operationNotAllowedCursor`.
Each uses `[NSCursor respondsToSelector:...] ? private : fallback`.

### Custom cursors
- `sapp_bind_mouse_cursor_image(cursor, desc)` (14170): asserts hotspot `< dim-1` and `pixels.size == w*h*4`. Unbinds any existing, then `_sapp_macos_make_custom_mouse_cursor` (5774): builds an `NSBitmapImageRep` (RGBA8, `NSBitmapFormatAlphaNonpremultiplied`, `NSCalibratedRGBColorSpace`), `memcpy`s `desc->pixels`, wraps in `NSImage`, creates `[[NSCursor alloc] initWithImage:hotSpot:]` into `custom_cursors[cursor]`. Sets `custom_cursor_bound[cursor]=res`. If the bound cursor is current, re-applies immediately. Returns the passed-in cursor.
- `sapp_unbind_mouse_cursor_image` (14203): if bound, clears the flag, re-applies default if current (before destroy), then `_sapp_macos_destroy_custom_mouse_cursor` releases the NSCursor. There are `_SAPP_MOUSECURSOR_NUM` slots (the "CUSTOM_0..15" model is per-enum-slot; on mac every standard cursor enum can be overridden by a bound custom image).

### Cursor re-application (tracking area interplay)
`updateTrackingArea` (6134) builds an `NSTrackingArea` with options
`MouseEnteredAndExited | ActiveInKeyWindow | EnabledDuringMouseDrag | CursorUpdate |
InVisibleRect | AssumeInside`. Because `CursorUpdate` is set, AppKit calls
**`cursorUpdate:` (6351)** which re-applies `_sapp_macos_update_cursor(current_cursor,
shown)` whenever the pointer (re)enters — this is what keeps a custom/hidden cursor
sticky across window regions. `mouseEntered:` (6157) also re-syncs mouse position.

---

## 7. Drag & drop

- Registration: **always** on the main window (`registerForDraggedTypes:@[NSPasteboardTypeFileURL]` in `_sapp_macos_window initWithContentRect:` at 6050, guarded by `__MAC_OS_X_VERSION_MAX_ALLOWED >= 101300`). Not gated on `enable_dragndrop`; the buffer/handler is what gates.
- `desc.enable_dragndrop` → `_sapp_init_state` allocates `drop.buffer` of `max_files * max_path_length` bytes and sets `drop.enabled/max_files/max_path_length/buf_size` (3581–3587). Defaults: 1 file, 2048 bytes/path.
- NSDraggingDestination methods on `_sapp_macos_window` (6056–6094): `draggingEntered:`→`NSDragOperationCopy`, `draggingUpdated:`→`NSDragOperationCopy`, `performDragOperation:` does the extraction.
- `performDragOperation:` (6064): if pasteboard has `NSPasteboardTypeFileURL`: `_sapp_clear_drop_buffer()`; `num_files = min(pasteboardItems.count, drop.max_files)`; for each item build `[NSURL fileURLWithPath:[item stringForType:NSPasteboardTypeFileURL]]` and `_sapp_strcpy(fileUrl.standardizedURL.path.UTF8String, _sapp_dropped_file_path_ptr(i), max_path_length)`. If any path overflows → `_SAPP_ERROR(DROPPED_FILE_PATH_TOO_LONG)`, abort, clear buffer, `num_files=0`. On success, if `_sapp_events_enabled()`: update mouse pos from `draggingLocation`, fire `SAPP_EVENTTYPE_FILES_DROPPED` with `modifiers = _sapp_macos_mods(nil)`. Returns YES if handled.
- Retrieval: `sapp_get_num_dropped_files()` (14319) returns `drop.num_files` (0 if disabled); `sapp_get_dropped_file_path(i)` (14326) returns `_sapp_dropped_file_path_ptr(i)`.
- Path extraction uses **NSURL / NSPasteboardTypeFileURL**, not the legacy NSFilenamesPboardType.

---

## 8. Fullscreen

- `sapp_is_fullscreen()` (14100): returns `_sapp.fullscreen` (a tracked bool, NOT a styleMask query).
- `sapp_toggle_fullscreen()` (14104) → `_sapp_macos_toggle_fullscreen` (5655): flips `_sapp.fullscreen` **and** calls `[window toggleFullScreen:nil]`. Note the flag is toggled optimistically here **and** authoritatively corrected by the notifications below — a double source of truth.
- `windowDidEnterFullScreen:` (6032) sets `fullscreen=true`; `windowDidExitFullScreen:` (6037) sets `fullscreen=false`. These are the reliable authority (native OS fullscreen, mission-control gestures, etc.).
- Startup `desc.fullscreen=true`: after window creation and `valid=true`, `applicationDidFinishLaunching` calls `[window toggleFullScreen:self]` (5910–5913). The window is created windowed first, then toggled.

---

## 9. sapp_get_environment / sapp_get_swapchain (Metal)

### `sapp_get_environment()` (14387) — asserts `_sapp.valid`
- `defaults.color_format = sapp_color_format()` → **`SAPP_PIXELFORMAT_RGB10A2`** on Metal (TrussC 10-bit patch, 14040–14042; upstream would be BGRA8).
- `defaults.depth_format = sapp_depth_format()` → always `SAPP_PIXELFORMAT_DEPTH_STENCIL` (14048).
- `defaults.sample_count = sapp_sample_count()` → `_sapp.sample_count`.
- `metal.device = (__bridge const void*) _sapp.macos.mtl.device` (14395).

### `sapp_get_swapchain()` (14417) — asserts `_sapp.valid`
Metal branch (14421–14424):
- `metal.current_drawable = (__bridge) _sapp_macos_mtl_swapchain_next()` — **`[layer nextDrawable]` is called HERE, lazily, at swapchain-query time** (5116; asserts non-nil). It is NOT acquired at frame start. TrussC calls `sglue_swapchain()` inside `sg_begin_pass`, so the drawable is acquired at begin-pass time each frame. Calling `sapp_get_swapchain()` twice per frame would acquire two drawables — must be called exactly once per frame.
- `metal.depth_stencil_texture = _sapp.macos.mtl.depth_tex`
- `metal.msaa_color_texture = _sapp.macos.mtl.msaa_tex` (nil when sample_count==1).
- Common tail: `width = sapp_width()` (framebuffer_width, min 1), `height = sapp_height()`, `color_format = RGB10A2`, `depth_format = DEPTH_STENCIL`, `sample_count`.

### glue contract (`sokol_glue.h`)
`sglue_environment()` (159) reads only: `env.defaults.color_format/depth_format/sample_count`
and `env.metal.device` (maps sapp_pixel_format→sg_pixel_format via `_sglue_to_sgpixelformat`
which handles `RGB10A2→SG_PIXELFORMAT_RGB10A2`, 149). `sglue_swapchain()` (178) reads:
`sc.width/height/sample_count/color_format/depth_format` and
`sc.metal.current_drawable / depth_stencil_texture / msaa_color_texture`. **These are the
only sapp fields the shim must fill for the Metal render path.** All d3d11/wgpu/vulkan/gl
fields are memset-0 and ignored on mac.

---

## 10. Metal view/layer details (main window)

### Layer/view creation (`_sapp_macos_mtl_init`, 5211–5227)
- `device = MTLCreateSystemDefaultDevice()`.
- `layer = [CAMetalLayer layer]`; `layer.device = device`; `layer.magnificationFilter = kCAFilterNearest`; `layer.opaque = true`; **`layer.pixelFormat = MTLPixelFormatRGB10A2Unorm`** (TrussC 10-bit patch, 5217); **`layer.framebufferOnly = false`** (TrussC patch enabling `captureWindow()` GPU reads, issue #56, 5218). `maximumDrawableCount` left at default 3 (commented note about 2, 5219). `colorspace` not set (FIXME).
- `view = [[_sapp_macos_view alloc] init]; [view updateTrackingAreas]; view.wantsLayer = YES; view.layer = layer;` (a **CAMetalLayer-backed NSView**, not MTKView — TRUSSC_MODIFICATIONS note confirms upstream migrated off MTKView to CAMetalLayer).
- `_sapp_macos_mtl_start_display_link()` (5156), `_sapp_macos_mtl_timing_init()`.

### Display link + swap interval
`_sapp_macos_mtl_start_display_link` (5156): if link exists, unpause and return. Else
`[view displayLinkWithTarget:view selector:@selector(displayLinkFired:)]`; sets
`preferredFrameRateRange = {p,p,p}` where `p = max_fps / swap_interval`; adds to current
run loop in `NSRunLoopCommonModes`. `displaySyncEnabled`/`maximumDrawableCount` are not
explicitly set; vsync is implied by the CAMetalLayer + display-link cadence and
`swap_interval` scales the preferred fps. `_sapp_macos_mtl_display_link_active()` =
link non-nil and not paused.

### Occlusion / fallback timer (already handled for secondary windows, documented for parity)
`_transition_to_occluded` (5197): if active → stop display link (pause) + start a repeating
`NSTimer` at `_SAPP_MACOS_MTL_OBSCURED_FRAME_DURATION_IN_SECONDS = 0.0166667` (~60Hz)
firing `fallbackTimerFired:` (5119). `_transition_to_visible` (5204): stop fallback timer,
(re)start display link. Triggered by `windowDidChangeOcclusionState:` (6012,
`occlusionState & NSWindowOcclusionStateVisible`), `windowDidMiniaturize:`/`Deminiaturize:`.

### Swapchain textures (`_sapp_macos_mtl_create_texture`, 5061; `_swapchain_create`, 5089)
- Depth: `MTLPixelFormatDepth32Float_Stencil8`, sample_count, panic on nil.
- MSAA color (only if `sample_count > 1`): **`MTLPixelFormatRGB10A2Unorm`** (TrussC patch, 5095).
- Descriptor: `textureType = MTLTextureType2DMultisample` if sample_count>1 else `2D`; `usage = MTLTextureUsageRenderTarget`; `resourceOptions = MTLResourceStorageModePrivate`; mip=1, arrayLength=1, depth=1.
- `_swapchain_resize` (5111) = destroy + create. Called from `_sapp_macos_mtl_update_framebuffer_dimensions` (5237) only when the drawable size actually changed; also sets `layer.drawableSize = {framebuffer_width, framebuffer_height}`.

### RGB10A2 patch locations (must replicate all four)
1. `layer.pixelFormat = MTLPixelFormatRGB10A2Unorm` (5217).
2. MSAA texture format `MTLPixelFormatRGB10A2Unorm` (5095).
3. `sapp_color_format()` returns `SAPP_PIXELFORMAT_RGB10A2` on Metal/D3D11 (14042).
4. glue `_sglue_to_sgpixelformat` maps `RGB10A2→SG_PIXELFORMAT_RGB10A2` (149).
Plus `layer.framebufferOnly = false` (5218) for captureWindow reads.

---

## 11. Window/app events delivered on mac (beyond raw input)

All go through `_sapp_macos_app_event(type)` (5600) → gated by `_sapp_events_enabled()`.
`_sapp_init_event` (3614) populates every event with: `frame_count`,
`mouse_button=INVALID`, `window_width/height` (points), `framebuffer_width/height`
(pixels), `mouse_x/y/dx/dy`. Per-type specifics:

- **RESIZED** — fired from `_sapp_macos_update_dimensions` (5632→5651) **only if** dimensions changed **and** `!first_frame` (so the startup update at step 13 is silent). Triggered by `windowDidResize:` (5986) and `windowDidChangeScreen:` (5991). Event carries the new `window_width/height` (points) and `framebuffer_width/height` (pixels) — both already updated before the event fires.
- **ICONIFIED** — `windowDidMiniaturize:` (5996); also transitions Metal to occluded first.
- **RESTORED** — `windowDidDeminiaturize:` (6004); transitions Metal to visible first.
- **FOCUSED** — `windowDidBecomeKey:` (6022).
- **UNFOCUSED** — `windowDidResignKey:` (6027).
- **QUIT_REQUESTED** — `windowShouldClose:` (see §3).
- **SUSPENDED / RESUMED** — **not used on macOS** (event table lines 156–157 mark them iOS/Android/web only; the mac window delegate never fires them). `windowDidChangeOcclusionState:` only drives Metal render gating, it does NOT emit an app event.
- **DISPLAY_CHANGED** — no such event type on mac; `windowDidChangeScreen:` re-runs `update_dimensions` (may emit RESIZED if dpi/size changed) but there is no dedicated display-changed event.
- FILES_DROPPED / CLIPBOARD_PASTED — see §7 / §5 (fired from window/view, not the delegate).

`_sapp_events_enabled()` (3629) requires `(event_cb || event_userdata_cb) && init_called`
— so **no events fire before the first frame's init_cb** runs. This is why the RESIZED
`!first_frame` guard matters (events would be dropped anyway pre-init, but the guard also
covers the case where init already ran).

---

## 12. Misc

- **`sapp_set_window_title(str)` (14279):** copies into `_sapp.window_title`, then `_sapp_macos_update_window_title` (5689): `[window setTitle:NSString(window_title)]`.
- **`sapp_high_dpi()` (14056):** `desc.high_dpi && dpi_scale >= 1.5` (not a plain flag).
- **`sapp_isvalid()` (13972):** returns `_sapp.valid` (set true at 5909, inside `applicationDidFinishLaunching` after renderer init, before fullscreen toggle).
- **`sapp_show_keyboard(show)` (14086):** **mac no-op** (only iOS/Android implemented). `sapp_keyboard_shown()` returns `_sapp.onscreen_keyboard_shown` (always false on mac).
- **`sapp_skip_present()` (14597):** sets `_sapp.skip_present=true`, but on the **Metal path it is IGNORED** — only the Vulkan (`_sapp_vk_frame`, 5023) and D3D11/WGPU frame functions honor it. `_sapp_macos_frame` (5849) has no skip_present check; the Metal drawable is presented by sokol_gfx's commit. So on mac+Metal, `sapp_skip_present()` has no effect (event-driven present suppression is a D3D11/Vulkan feature).
- **Dock icon / `sapp_set_icon`:** dock-tile only (`_sapp_macos_set_icon`, 5816 writes `NSApp.dockTile.contentView`). **Main-window rendering path does not require it** — safe to omit in the shim.
- **AppKit-notification-driven event_cb sites** (outside the NSView): `windowShouldClose:`, `windowDidResize:`, `windowDidChangeScreen:`, `windowDidMiniaturize:`, `windowDidDeminiaturize:`, `windowDidBecomeKey:`, `windowDidResignKey:` (all in the window delegate), and `performDragOperation:` (in the NSWindow subclass). The shim must route these to `event_cb` just like the view routes input.
- **GL/WGPU divergences** (not needed for Metal shim, noted for completeness): GL uses `NSOpenGLView` + a 1ms `NSTimer`→`setNeedsDisplay`→`drawRect:`→`_sapp_macos_frame`, and `prepareOpenGL` sets swap interval 1; WGPU mirrors the Metal CADisplayLink setup. Both share the same window/delegate/event code.

---

## Appendix: function → line index (Metal-relevant)

| function | line |
|---|---|
| `_sapp_macos_max_fps` | 5056 |
| `_sapp_macos_mtl_create_texture` | 5061 |
| `_sapp_macos_mtl_swapchain_create/destroy/resize/next` | 5089/5102/5111/5116 |
| `_sapp_macos_mtl_timing_init/update/frame_duration` | 5126/5131/5147 |
| `_sapp_macos_mtl_start/stop_display_link` | 5156/5173 |
| `_sapp_macos_mtl_start/stop_fallback_timer` | 5179/5190 |
| `_sapp_macos_mtl_transition_to_occluded/visible` | 5197/5204 |
| `_sapp_macos_mtl_init/discard_state` | 5211/5229 |
| `_sapp_macos_mtl_update_framebuffer_dimensions` | 5237 |
| `_sapp_macos_init_keytable` | 5359 |
| `_sapp_macos_discard_state` | 5473 |
| `_sapp_macos_init_cursors` | 5502 |
| `_sapp_macos_run` / `main` | 5519 / 5546 |
| `_sapp_macos_mods` | 5553 |
| `_sapp_macos_mouse/key/app_event` | 5581/5590/5600 |
| `_sapp_macos_init_default_dimensions` | 5608 |
| `_sapp_macos_update_dimensions` | 5632 |
| `_sapp_macos_toggle_fullscreen` | 5655 |
| `_sapp_macos_set/get_clipboard_string` | 5664/5672 |
| `_sapp_macos_update_window_title` | 5689 |
| `_sapp_macos_mouse_update_from_nspoint/nsevent` | 5693/5711 |
| `_sapp_macos_show/lock_mouse` | 5715/5724 |
| `_sapp_macos_update_cursor` | 5750 |
| `_sapp_macos_make/destroy_custom_mouse_cursor` | 5774/5810 |
| `_sapp_macos_set_icon` | 5816 |
| `_sapp_macos_frame` | 5849 |
| `app_delegate` (`applicationDidFinishLaunching:` etc.) | 5872 |
| `window_delegate` | 5947 |
| `_sapp_macos_window` (drag&drop) | 6043 |
| `_sapp_macos_view` (input, timers) | 6097 |
