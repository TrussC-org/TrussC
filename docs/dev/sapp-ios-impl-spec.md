# sokol_app.h iOS (Metal) Backend — Behavioral Specification

Implementation contract for giving `sokol_app_tc.h` an iOS main-window /
app-lifecycle backend (project "sokol_app graduation", iOS leg). All line
references are into `core/include/sokol/sokol_app.h` (14602 lines, this worktree).
Focus: `_SAPP_IOS` + **`SOKOL_METAL`** — the only configuration TrussC ships on
iOS (see §11). The `SOKOL_GLES3`/EAGL path that also lives in the iOS backend is
**DEAD for TrussC** and is marked not-ported wherever it appears.

This is the fifth sibling of `sapp-mac-impl-spec.md` (event-driven `[NSApp run]`),
`sapp-win32-impl-spec.md` (owned PeekMessage loop), `sapp-x11-impl-spec.md` (owned
XPending loop) and `sapp-web-impl-spec.md` (no owned loop, browser rAF). **iOS is
closest to macOS** — same Apple/Metal machinery (CAMetalLayer, CADisplayLink,
RGB10A2 10-bit patch, MSAA/depth textures, mach timestamp clock, `_SAPP_OBJC_RELEASE`
teardown). It differs from mac in four structural ways:

1. **UIKit, not AppKit.** `UIApplicationMain` + a `UIWindowSceneDelegate` own the
   loop (never returns), instead of `[NSApplication sharedApplication] … [NSApp run]`.
2. **Single window, like web.** There is exactly one `UIWindow` bound to the
   connecting `UIWindowScene`. A second window is not modeled — `sapp_create_window()`
   and the plural-window APIs must return an invalid handle + log, exactly as the web
   port does.
3. **Touch, not mouse.** No mouse / cursor / pointer-lock / clipboard-from-OS /
   drag&drop / window-title / favicon surface at all (§10). The whole input model is
   multi-touch + an on-screen keyboard driven through a hidden `UITextField`.
4. **App lifecycle events.** `SAPP_EVENTTYPE_SUSPENDED` / `RESUMED` are fired on
   scene resign/activate (mac never fires these).

**Port target.** `sokol_app_tc.h` currently has a macOS impl section guarded by
`#if defined(__APPLE__) && TARGET_OS_OSX` (sokol_app_tc.h:199) followed by
`#elif defined(_WIN32)` (1723). The iOS backend slots in as a new
`#elif defined(__APPLE__) && TARGET_OS_IPHONE` branch between them. Like the macOS
branch it MUST be compiled as Objective-C++ (`.mm`) — enforce with the same
`#error "… must be compiled as Objective-C"` guard (cf. sokol_app_tc.h:201–203).

---

## 0. Global state touched (the shared contract)

The single global is `static _sapp_t _sapp;` (~3298) which embeds `_sapp_ios_t ios;`
at **3301** (under `_SAPP_IOS`). The iOS struct is at **2850–2877**:

```c
typedef struct {
    UIWindow* window;
    _sapp_ios_view* view;               // UIView (Metal) subclass; hosts touch + display link
    UITextField* textfield;             // hidden field driving the on-screen keyboard (§7)
    _sapp_textfield_dlg* textfield_dlg;
    NSUInteger supported_orientations;  // UIInterfaceOrientationMask (default: All) — TrussC patch (§8)
    UIViewController* view_ctrl;         // actually a _sapp_ios_view_ctrl instance — TrussC patch (§8)
    struct {                             // SOKOL_METAL only
        id<MTLDevice> device;
        CAMetalLayer* layer;
        CADisplayLink* display_link;
        id<MTLTexture> depth_tex;
        id<MTLTexture> msaa_tex;         // nil unless sample_count > 1
        struct { CFTimeInterval timestamp; CFTimeInterval frame_duration_sec; } timing;
    } mtl;
    // #else EAGLContext* eagl_ctx;      // GLES3 path — DEAD for TrussC
    bool suspended;                      // scene resign/activate latch (§3)
} _sapp_ios_t;
```

The declared type of `view_ctrl` in the struct is `UIViewController*` (2857) but it
is **always constructed as `_sapp_ios_view_ctrl`** (a TrussC UIViewController subclass,
2837–2839, §8). `supported_orientations` and the subclass are both TrussC additions;
upstream sokol stores no orientation mask and uses a plain `UIViewController`.

Cross-platform `_sapp` fields the iOS path reads/writes: `desc`, `valid`,
`first_frame`, `init_called`, `cleanup_called`, `window_width/height`,
`framebuffer_width/height`, `dpi_scale`, `sample_count`, `swap_interval`, `timing`,
`frame_count`, `event`, `onscreen_keyboard_shown`. Notably **NOT** touched on iOS:
`fullscreen`, `mouse.*`, `clipboard.*`, `drop.*`, `keycodes[]`, `custom_cursor_bound[]`,
`window_title`, `quit_requested`/`quit_ordered`, `skip_present` (all inert here, §10).

`_sapp_init_state(desc)` runs first inside `_sapp_ios_run` (§1) and applies
`_sapp_desc_defaults` (sample_count→1, swap_interval→1, window_title→"sokol", etc.),
sets `first_frame=true`, `dpi_scale=1.0`. `desc.width/height` are ignored on iOS —
the window is always screen-sized (§2/§5).

### iOS-specific desc: `sapp_ios_desc` (2031–2033)
```c
typedef struct sapp_ios_desc { bool keyboard_resizes_canvas; } sapp_ios_desc;
```
Embedded as `desc.ios` (2068). **TrussC's `buildAppDescriptor` never sets it**, so
`keyboard_resizes_canvas` stays `false` — the on-screen keyboard does NOT shrink the
render view by default (§7).

### `_SAPP_OBJC_RELEASE` / ARC
All ObjC teardown uses `_SAPP_OBJC_RELEASE(obj)` (the shared Apple release macro,
Apple decls region 2554–2630) — same as mac. `_SAPP_CLEAR_ARC_STRUCT` (2554–2563) is
ARC-compatible. iOS builds are typically ARC (`.mm`); "safe to `[release]` a nil
object" is relied on in `_sapp_ios_discard_state` (6576).

---

## 1. Entry / run flow — UIApplicationMain owns the loop

### Platform detection (2314–2329)
`__APPLE__` → include `<TargetConditionals.h>`; then
`#if defined(TARGET_OS_IPHONE) && !TARGET_OS_IPHONE` → `_SAPP_MACOS`, **else**
`#define _SAPP_IOS (1)` (2322). API-select `#error` unless `SOKOL_METAL || SOKOL_GLES3`
(2323–2324). `TARGET_OS_TV` → `_SAPP_TVOS` (2326–2327) — TrussC never builds tvOS,
but the tvOS code (press events, `multipleTouchEnabled` guards) is compiled in and
must survive the lift; mark it dead-but-kept.

### Apple / UIKit includes (2435–2447)
`#elif defined(_SAPP_IOS)` → `#import <UIKit/UIKit.h>`; under `SOKOL_METAL`:
`<Metal/Metal.h>`, `<QuartzCore/CAMetalLayer.h>`, `<QuartzCore/CADisplayLink.h>`
(2437–2440). The `_SAPP_ANY_GL` alternative pulls `<GLKit/GLKit.h>` + `<OpenGLES/ES3/gl.h>`
(2441–2444) — **DEAD for TrussC**. Plus `<AvailabilityMacros.h>`, `<mach/mach_time.h>`
(2446–2447, shared with mac).

### `_sapp_ios_run(const sapp_desc* desc)` (6588–6593)
```c
_sapp_init_state(desc);
static int argc = 1;
static char* argv[] = { (char*)"sokol_app" };
UIApplicationMain(argc, argv, nil, NSStringFromClass([_sapp_scene_delegate class]));
```
- `UIApplicationMain` **never returns** (owns the run loop, like `[NSApp run]`). The
  4th arg (principal class) is `nil`; the 5th (delegate class) names
  `_sapp_scene_delegate`, which conforms to **both** `UIApplicationDelegate` and
  `UIWindowSceneDelegate` (2829). So the app delegate *is* the scene delegate class.
- **No `sg_setup`, no window, no Metal device is created here** — all of that happens
  later, inside `scene:willConnectToSession:` when UIKit connects the scene (§2). This
  is the crucial timing difference from mac (where `applicationDidFinishLaunching:`
  does everything under `[NSApp run]`). On iOS, `_sapp_ios_run` returns control to
  UIKit and the real setup is scene-driven.

### Entry point / SOKOL_NO_ENTRY (6595–6602)
```c
#if !defined(SOKOL_NO_ENTRY)
int main(int argc, char* argv[]) { sapp_desc desc = sokol_main(argc, argv); _sapp_ios_run(&desc); return 0; }
#endif
```
**TrussC defines `SOKOL_NO_ENTRY`** (TrussC.h:15 and platform/ios/sokol_impl.mm:6), so
this `main` is compiled out. The live entry chain is:
**user `int main()` → `runApp<App>()` (TrussC.h:2634–2637) → `sapp_run(&desc)`
(13941, `#if SOKOL_NO_ENTRY`) → `_sapp_ios_run(desc)` (13946) → `UIApplicationMain`.**
So on iOS the user's own `main()` blocks forever inside `UIApplicationMain` — unlike
web where `sapp_run` returns immediately. The port must keep this NO_ENTRY dispatch.

### Scene adoption WITHOUT an Info.plist scene manifest (LOAD-BEARING, 6727–6735)
```objc
- (UISceneConfiguration*) application:(UIApplication*)application
    configurationForConnectingSceneSession:(UISceneSession*)connectingSceneSession
    options:(UISceneConnectionOptions*)options {
    UISceneConfiguration* config = [[UISceneConfiguration alloc]
        initWithName:@"SokolSceneConfiguration" sessionRole:connectingSceneSession.role];
    config.delegateClass = [_sapp_scene_delegate class];
    return config;
}
```
`_sapp_scene_delegate` returns a `UISceneConfiguration` **programmatically** from the
app-delegate method. This is what makes iOS adopt the `UIWindowScene` lifecycle
**even though `core/resources/Info-iOS.plist.in` declares NO
`UIApplicationSceneManifest`** (verified: the plist has `LSRequiresIPhoneOS`,
`UISupportedInterfaceOrientations`, usage strings — but no scene manifest). If a
future refactor adds a manifest, it must name this delegate class or the mechanism
double-registers. **Keep the programmatic config path.**

`application:didFinishLaunchingWithOptions:` (6759–6761) just returns `YES`.

---

## 2. Window + Metal setup (`scene:willConnectToSession:`, 6737–6757)

This is the iOS analogue of mac's `applicationDidFinishLaunching:`. Exact sequence:

1. `windowScene = (UIWindowScene*)scene; screen_rect = windowScene.screen.bounds;`
2. `_sapp.ios.window = [[UIWindow alloc] initWithWindowScene:windowScene];`
3. `window_width/height = round(screen_rect.size)` (points) — **always screen-sized**;
   `desc.width/height` play no role.
4. **DPI (6743–6747):** `if (desc.high_dpi) dpi_scale = windowScene.screen.nativeScale;
   else 1.0`. (`nativeScale`, not `scale` — the true backing ratio.)
5. `framebuffer_width/height = round(window_width/height * dpi_scale)` (6748–6749).
6. **Renderer bring-up:** `#if SOKOL_METAL _sapp_ios_mtl_init(windowScene)` (6751) —
   **`#else _sapp_ios_gles3_init` is DEAD** (6753).
7. `[_sapp.ios.window makeKeyAndVisible];` (6755).
8. `_sapp.valid = true;` (6756).

### `_sapp_ios_mtl_init(UIWindowScene*)` (6474–6501)
- `mtl.device = MTLCreateSystemDefaultDevice()` (6475).
- `view = [[_sapp_ios_view alloc] initWithFrame:windowScene.screen.bounds]`;
  `userInteractionEnabled = YES`; `multipleTouchEnabled = YES` (guarded `!_SAPP_TVOS`,
  6479–6481).
- `supported_orientations = UIInterfaceOrientationMaskAll` (6482, and again 6493 —
  redundant but harmless).
- **CAMetalLayer (6484–6491):** `layer = [CAMetalLayer layer]`; `layer.device = device`;
  `layer.opaque = true`; **`layer.framebufferOnly = false`** (6487, TrussC patch, issue
  #56 — enables `captureWindow()` GPU readback); **`layer.pixelFormat =
  MTLPixelFormatRGB10A2Unorm`** (6488, TrussC 10-bit patch); `layer.frame =
  view.layer.frame`; then `[view.layer addSublayer:layer]`. NOTE: the Metal layer is a
  **sublayer of the view's backing layer**, not the view's own layer (differs from mac,
  where the view IS layer-backed by the CAMetalLayer).
- **View controller (6494–6497):** `view_ctrl = [[_sapp_ios_view_ctrl alloc] init]`
  (TrussC subclass, §8); `modalPresentationStyle = UIModalPresentationFullScreen`;
  `view_ctrl.view = view`; `window.rootViewController = view_ctrl`.
- `_sapp_ios_mtl_start_display_link()` (6499); `_sapp_ios_mtl_timing_init()` (6500).

### Metal swapchain textures (`_sapp_ios_mtl_create/swapchain_*`, 6373–6432)
- `_sapp_ios_mtl_create_texture` (6373–6399): `MTLTextureType2DMultisample` if
  `sample_count>1` else `2D`; `usage = RenderTarget`; `resourceOptions =
  StorageModePrivate`; mip=1, arrayLength=1, depth=1. Labels only under `SOKOL_DEBUG`.
- `_swapchain_create` (6401–6412): depth = `MTLPixelFormatDepth32Float_Stencil8`
  (panic `METAL_CREATE_SWAPCHAIN_DEPTH_TEXTURE_FAILED`); MSAA (only if `sample_count>1`)
  = **`MTLPixelFormatRGB10A2Unorm`** (6407, TrussC patch; panic
  `..._MSAA_TEXTURE_FAILED`).
- `_swapchain_destroy` (6414–6421) releases both; `_swapchain_resize` (6423–6426) =
  destroy+create.
- `_swapchain_next` (6428–6432): `[layer nextDrawable]`, **asserts non-nil**, returns
  the drawable. Called lazily at swapchain-query time (§9), not at frame start.

### Display link + swap interval (`_sapp_ios_mtl_start/stop_display_link`, 6456–6472)
`display_link = [CADisplayLink displayLinkWithTarget:_sapp.ios.view
selector:@selector(displayLinkFired:)]`. `preferred_fps = max_fps / swap_interval`
(6460, `max_fps = windowScene.screen.maximumFramesPerSecond`, 6367–6369);
`preferredFrameRateRange = {p,p,p}` (6461–6462); added to the current run loop in
`NSRunLoopCommonModes` (6463). Stop invalidates + nils it (the run loop held the only
strong ref, 6466–6472). This is the frame driver — `displayLinkFired:` (6878) calls
`_sapp_ios_frame()` every vsync.

---

## 3. App lifecycle / quit semantics (SUSPENDED · RESUMED · terminate)

Unlike mac (window-close-driven quit, `quit_requested`/`quit_ordered`), **iOS has no
user-driven quit path.** There is no red button, no Cmd+Q, no `sapp_request_quit`
route to the OS. `quit_requested`/`quit_ordered` and `sapp_request_quit()` etc. are
inert on iOS (the frame loop never inspects them). Lifecycle is entirely
scene/UIKit-driven:

### Suspend / resume (`_sapp_scene_delegate`, 6763–6785)
- **`sceneWillResignActive:` (6763–6773):** if not already suspended → `suspended=true`;
  **pause the display link** (`display_link.paused = YES`, Metal only); fire
  `SAPP_EVENTTYPE_SUSPENDED`.
- **`sceneDidBecomeActive:` (6775–6785):** if suspended → `suspended=false`; **unpause
  the display link**; fire `SAPP_EVENTTYPE_RESUMED`.

Both go through `_sapp_ios_app_event(type)` (6604–6609) → gated by
`_sapp_events_enabled()` (event_cb set AND `init_called`). **The display-link pause is
load-bearing:** it stops the frame callback (and therefore GPU work) while
backgrounded, which iOS requires — rendering to a Metal drawable while suspended is a
watchdog kill. The port MUST pause/resume the CADisplayLink on these transitions.

> **⚠ TrussC wiring gap (finding):** TrussC.h's `_event_cb` switch (2248–2489) has NO
> `case SAPP_EVENTTYPE_SUSPENDED:` / `RESUMED:`. sokol fires them but TrussC drops them
> — they are **not surfaced to app code today**. The port doesn't need to change this,
> but the events must keep firing (the display-link pause is internal to the backend
> and independent of dispatch). If TrussC later wants app-visible suspend/resume, the
> plumbing already exists at the sokol layer.

### Terminate (`applicationWillTerminate:`, 6791–6796)
```objc
_sapp_call_cleanup();      // user cleanup_cb, once (3450)
_sapp_ios_discard_state(); // release ObjC objects incl. Metal device/layer/textures (6575)
_sapp_discard_state();     // free cross-platform buffers, zero _sapp
```
Same ordering guarantee as mac: **user `cleanup_cb` runs before any Metal teardown**
(TrussC does its own `sg_shutdown` in cleanup_cb). Comment 6787–6790 warns this method
**"will rarely ever be called — iOS apps are usually killed via SIGKILL"**, so the port
must not rely on clean teardown; treat it as best-effort. `_sapp_ios_discard_state`
(6575–6586) releases `textfield_dlg`, `textfield`, then `_sapp_ios_mtl_discard_state`
(6503–6509: stop display link, destroy swapchain, release layer/view_ctrl/device),
then `view`, then `window`.

---

## 4. Frame timing

Same two-layer model as mac: the platform-agnostic EMA filter (`_sapp.timing`) plus a
CADisplayLink-timestamp timer (`_sapp.ios.mtl.timing`). **iOS uses the real mach clock**
(unlike web, whose `_sapp_timestamp_now` asserts false) — the Apple timestamp path at
2606–2608 / 2621–2625 (`mach_absolute_time`) is live.

### `_sapp_ios_frame(void)` (6678–6690) — the per-vsync driver
```c
_sapp_timing_update(&_sapp.timing, 0.0);   // external_now=0 → uses mach clock (feeds fallback)
#if SOKOL_METAL
_sapp_ios_mtl_timing_update();             // CADisplayLink-timestamp timer
#endif
#if _SAPP_ANY_GL glGetIntegerv(GL_FRAMEBUFFER_BINDING, …) #endif   // DEAD (GLES3 only)
@autoreleasepool {
    _sapp_ios_update_dimensions();          // §5 — every frame
    _sapp_frame();                          // init_cb (first frame) then frame_cb
}
```
Called from `_sapp_ios_view displayLinkFired:` (6878–6881) on Metal. The
`@autoreleasepool` per frame matters (touch/Metal churn ObjC temporaries).

### CADisplayLink timing (`_sapp_ios_mtl_timing_*`, 6434–6454)
- `_timing_init` (6434–6437): `timestamp=0`, `frame_duration_sec = 1.0/max_fps`.
- `_timing_update` (6439–6449): `cur = display_link.timestamp`; **first frame skipped**
  (`timing.timestamp>0` guard); else `dt = cur - prev` run through
  `_sapp_timing_clamp(&_sapp.timing, dt)` (min/max clamp only, NOT the EMA, 2666) →
  `frame_duration_sec`. Store `timestamp=cur`.
- `_timing_frame_duration` (6451–6454): returns `frame_duration_sec` (asserts >0).

`sapp_frame_duration()` (13988) → `#elif _SAPP_IOS && SOKOL_METAL` →
`_sapp_ios_mtl_timing_frame_duration()` (13991–13992).
`sapp_frame_duration_unfiltered()` → raw `_sapp.timing.dt`. `sapp_frame_count()`
(13984) is the shared counter, incremented in `_sapp_frame()` after `frame_cb`.

**Note vs mac:** iOS has NO occlusion/fallback-timer machinery (no `NSTimer`
fallback, no `_transition_to_occluded`). When backgrounded the display link is simply
paused (§3); there is no obscured-but-rendering state, so `frame_duration_sec` is
always the display-link value while running.

---

## 5. Dimensions, DPI, rotation (`_sapp_ios_update_dimensions`, 6657–6676)

Runs **every frame** from `_sapp_ios_frame`. This is how rotation is picked up (no
dedicated rotate event — the view bounds change and RESIZED fires).

```c
CGRect view_rect = _sapp.ios.view.bounds;                 // TrussC patch (6661)
if (view_rect.size.width < 1 || height < 1)               // not laid out yet →
    view_rect = _sapp.ios.window.windowScene.screen.bounds;   // fall back to screen
_sapp.window_width  = round(view_rect.size.width);
_sapp.window_height = round(view_rect.size.height);
bool dim_changed = _sapp_ios_mtl_update_framebuffer_dimensions(view_rect);   // Metal
if (dim_changed && !_sapp.first_frame) _sapp_ios_app_event(SAPP_EVENTTYPE_RESIZED);
```

- **TrussC patch (6658–6665):** uses `view.bounds` instead of
  `UIScreen.mainScreen.bounds` (consistent with mac's `[view bounds]`), avoiding a
  timing bug where screen bounds don't yet match the view's actual laid-out size;
  falls back to screen bounds only before first layout.
- RESIZED is suppressed on `first_frame` (same as mac's startup-silent update), and
  anyway `_sapp_events_enabled()` gates it (no events pre-init).

### `_sapp_ios_mtl_update_framebuffer_dimensions(CGRect)` (6511–6532) — TrussC patch #7
```c
_sapp.framebuffer_width  = round(screen_rect.size.width  * dpi_scale);
_sapp.framebuffer_height = round(screen_rect.size.height * dpi_scale);
CGSize cur = layer.drawableSize;
bool dim_changed = (framebuffer_width != round(cur.width)) || (framebuffer_height != round(cur.height));
if (dim_changed) {
    layer.drawableSize = { framebuffer_width, framebuffer_height };
    layer.frame        = screen_rect;
    _sapp_ios_mtl_swapchain_resize(framebuffer_width, framebuffer_height);
}
// ALWAYS read back the ACTUAL drawable size (Metal may clamp/round):
CGSize actual = layer.drawableSize;
_sapp.framebuffer_width  = round(actual.width);
_sapp.framebuffer_height = round(actual.height);
return dim_changed;
```
The readback (6526–6530) is the load-bearing TrussC fix: `sapp_width()`/`sapp_height()`
must equal what Metal will actually render, or a scissor/viewport can exceed the render
pass bounds and Metal validation aborts. **Replicate the set-then-readback exactly.**
`_sapp_roundf_gzero` (3423) = round, clamped ≥0.

---

## 6. Touch input (`_sapp_ios_touch_event`, 6636–6655)

Real multi-touch (no mouse emulation). Wired from `_sapp_ios_view` (6903–6914):
`touchesBegan/Moved/Ended/Cancelled:withEvent:` → `_sapp_ios_touch_event(TYPE, touches,
event)`.

```c
_sapp_init_event(type);
// enumerate ALL active touches (event.allTouches), not just the changed set:
for (UITouch* t in event.allTouches) {
    if (num_touches+1 < SAPP_MAX_TOUCHPOINTS) {
        CGPoint p = [t locationInView:_sapp.ios.view];
        sapp_touchpoint* cp = &event.touches[num_touches++];
        cp->identifier = (uintptr_t)t;              // pointer identity as stable id
        cp->pos_x = p.x * dpi_scale;                // framebuffer-space
        cp->pos_y = p.y * dpi_scale;
        cp->changed = [touches containsObject:t];   // was THIS the changed touch?
    }
}
if (num_touches > 0) _sapp_call_event(&event);
```
- Event type map: BEGAN→`TOUCHES_BEGAN`, MOVED→`TOUCHES_MOVED`, ENDED→`TOUCHES_ENDED`,
  CANCELLED→`TOUCHES_CANCELLED`.
- **All active touches are emitted every event**, with `changed` distinguishing which
  points this call is about — the app reconstructs deltas from `identifier`. Positions
  are CSS→wait, iOS points × `dpi_scale` = framebuffer pixels.
- TrussC consumes these in `_event_cb` (TrussC.h:2373–2440): maps to `TouchEventArgs`,
  tracks began/moved/ended, `cancelled` flag from CANCELLED. **Keep the `changed` flag
  and `identifier` semantics intact** — TrussC's touch tracking relies on them.

### tvOS press events (6611–6634, 6892–6902) — DEAD-but-kept
`_sapp_tvos_press_event` maps Apple-TV remote `UIPress` arrow/select/menu/playpause to
`SAPP_KEYCODE_*` and fires KEY_DOWN/KEY_UP. The `_sapp_ios_view` wires
`pressesBegan/Ended/Cancelled:`. TrussC never targets tvOS, but the code compiles on
iOS (harmless) — lift as-is, mark not-exercised.

---

## 7. On-screen keyboard (hidden UITextField, 6692–6725, 6799–6874)

There is no hardware keyboard model. Text entry is driven by a hidden `UITextField`
made first-responder to raise the software keyboard.

### `_sapp_ios_show_keyboard(bool)` (6692–6725)
Lazily (first call) creates `textfield` (`CGRectMake(10,10,100,50)`, `hidden=YES`,
`text=@"x"`, no autocap/autocorrect/spellcheck) and `textfield_dlg`, adds the field as
a subview of `view_ctrl.view`, and registers three `NSNotificationCenter` observers
(`UIKeyboardDidShow`, `WillHide`, `DidChangeFrame`; guarded `!_SAPP_TVOS`, 6707–6717).
Then `shown ? [textfield becomeFirstResponder] : [textfield resignFirstResponder]`.

`sapp_show_keyboard(bool)` (14086–14088) → `_sapp_ios_show_keyboard`.
`sapp_keyboard_shown()` (14096+) returns `_sapp.onscreen_keyboard_shown`.

### `_sapp_textfield_dlg` (6799–6874)
- `keyboardWasShown:` (6800–6811): sets `onscreen_keyboard_shown=true`; **iff
  `desc.ios.keyboard_resizes_canvas`** shrinks `view.frame` by the keyboard height.
  Since TrussC leaves that flag false, the view is NOT resized — the keyboard overlays.
- `keyboardWillBeHidden:` (6813–6818): `onscreen_keyboard_shown=false`; restore frame
  if resizing.
- `keyboardDidChangeFrame:` (6819–6830): re-apply shrink on rotation while open.
- **`textField:shouldChangeCharactersInRange:replacementString:` (6831–6873)** — the
  actual text→event bridge. For each char: if `c >= 32` (and not a surrogate) fire
  `SAPP_EVENTTYPE_CHAR` with `char_code=c`; if `c <= 32` map 10→ENTER, 32→SPACE and
  fire KEY_DOWN+KEY_UP. Empty replacement string = **backspace** → KEY_DOWN+KEY_UP with
  `SAPP_KEYCODE_BACKSPACE`. Returns `NO` (the field never actually accumulates text —
  it stays `@"x"`; it's purely an event source). This yields CHAR + a *tiny* keycode
  subset (Enter/Space/Backspace only) — there is NO full keycode table on iOS.

### TrussC consumer
`addons/tcxImGui/src/sokol_imgui.h` (2661–2665) toggles the keyboard from ImGui:
`if (io->WantTextInput && !sapp_keyboard_shown()) sapp_show_keyboard(true);` and the
inverse. **Keep `sapp_show_keyboard` / `sapp_keyboard_shown` working** — this is the
one real consumer of the iOS keyboard on TrussC.

---

## 8. Orientation + immersive mode (TrussC patches — `_sapp_ios_view_ctrl`, 6917–6930)

Upstream sokol uses a plain `UIViewController` with no runtime orientation control.
TrussC replaces it with a subclass and adds two runtime hooks. **These are the highest-
risk items to preserve** because `tcPlatform_ios.mm` couples to them directly.

```objc
bool _sapp_ios_immersive_mode = false;   // 6919 — NON-STATIC extern global (TrussC patch)
@implementation _sapp_ios_view_ctrl
- (UIInterfaceOrientationMask)supportedInterfaceOrientations { return _sapp.ios.supported_orientations; }
- (BOOL)prefersStatusBarHidden          { return _sapp_ios_immersive_mode; }
- (BOOL)prefersHomeIndicatorAutoHidden  { return _sapp_ios_immersive_mode; }
@end
```

### (1) Runtime orientation — `sapp_ios_set_supported_orientations` (decl 2235; impl 14508–14520)
```c
_sapp.ios.supported_orientations = (NSUInteger)mask;
#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 160000
    if (@available(iOS 16.0, *)) [_sapp.ios.view_ctrl setNeedsUpdateOfSupportedInterfaceOrientations];
#endif
```
The subclass's `supportedInterfaceOrientations` returns the stored mask, so changing it
+ the iOS16 nudge re-queries the OS. **Consumer:** `tcPlatform_ios.mm:453–455`
`setOrientation(Orientation) → sapp_ios_set_supported_orientations((uint32_t)mask)`.

### (2) Immersive mode — non-static extern global `_sapp_ios_immersive_mode`
The view controller's `prefersStatusBarHidden` / `prefersHomeIndicatorAutoHidden`
return this global. **`tcPlatform_ios.mm` reads/writes the extern directly** (NOT
through a function): 421 `extern bool _sapp_ios_immersive_mode;`, then
`setImmersiveMode(bool)` (423–433) sets it and calls
`setNeedsStatusBarAppearanceUpdate` + `setNeedsUpdateOfHomeIndicatorAutoHidden` on the
root VC; `getImmersiveMode()` (434) returns it. **The port MUST expose
`_sapp_ios_immersive_mode` as a non-static symbol with C++ linkage-visible name** (the
extern in tcPlatform_ios.mm must resolve). This is a fragile cross-TU coupling: if the
port renames/statics it, iOS immersive mode silently breaks at link time.

`sapp_ios_set_supported_orientations` and `_sapp_ios_immersive_mode` are TrussC
additions (TRUSSC_MODIFICATIONS.md patches #4, #5) — not in upstream. Preserve the
exact symbol names.

---

## 9. sapp_get_environment / sapp_get_swapchain (Metal)

Same shape as mac; the iOS branch just reads `_sapp.ios.*` instead of `_sapp.macos.*`.

### `sapp_get_environment()` (14387–14415) — asserts `_sapp.valid`
- `defaults.color_format = sapp_color_format()` → **`SAPP_PIXELFORMAT_RGB10A2`** on
  Metal (14040–14042, TrussC 10-bit patch, shared with mac/D3D11).
- `defaults.depth_format = sapp_depth_format()` → `SAPP_PIXELFORMAT_DEPTH_STENCIL`
  (14048).
- `defaults.sample_count = sapp_sample_count()` → `_sapp.sample_count`.
- `#if SOKOL_METAL … #else res.metal.device = _sapp.ios.mtl.device` (14397).

### `sapp_get_swapchain()` (14417–14430) — asserts `_sapp.valid`
iOS Metal branch (14425–14428):
- `metal.current_drawable = (__bridge) _sapp_ios_mtl_swapchain_next()` — **`[layer
  nextDrawable]` is called HERE, lazily**, exactly once per frame (TrussC calls
  `sglue_swapchain()` inside `sg_begin_pass`). Calling twice per frame acquires two
  drawables — must be called exactly once.
- `metal.depth_stencil_texture = _sapp.ios.mtl.depth_tex`
- `metal.msaa_color_texture = _sapp.ios.mtl.msaa_tex` (nil when sample_count==1)
- Common tail: `width/height` (framebuffer, min 1), `color_format=RGB10A2`,
  `depth_format=DEPTH_STENCIL`, `sample_count`.

These feed `sglue_environment()`/`sglue_swapchain()` (sokol_glue.h) exactly as on mac;
`_sglue_to_sgpixelformat` maps `RGB10A2 → SG_PIXELFORMAT_RGB10A2`. All d3d11/wgpu/gl
fields are memset-0 and ignored.

### `sapp_ios_get_window()` (14498–14506) — TrussC-consumed
```c
#if defined(_SAPP_IOS) return (__bridge const void*) _sapp.ios.window;  // asserts non-null
#else return 0; #endif
```
Returns the `UIWindow*`. **Consumer:** `tcFileDialog_ios.mm:23`
`UIWindow* window = (__bridge UIWindow*)sapp_ios_get_window();` → `window.rootViewController`
to present the `UIDocumentPickerViewController`. **Keep this getter and its non-null
guarantee** or the iOS document picker (open/save dialogs) loses its presentation
anchor.

---

## 10. Features NOT applicable on iOS (explicitly not-ported)

Upstream's iOS backend simply has no code for these; the public API functions fall to
no-op/zero branches. The port should stub them the same way (and, per the single-window
adaptation, log on the plural-window ones):

| Feature | iOS reality |
|---|---|
| **Mouse / cursor / pointer-lock** | No pointer model. `sapp_show_mouse`, `sapp_lock_mouse`, `sapp_set_mouse_cursor`, `sapp_bind_mouse_cursor_image` have no iOS branch → inert. |
| **Clipboard** | No `_SAPP_IOS` branch in `sapp_set/get_clipboard_string` (14247/14265 are `_SAPP_MACOS`). `sapp_get_clipboard_string()` returns `""`. No `CLIPBOARD_PASTED`. |
| **Drag & drop (OS)** | No iOS drop path. `enable_dragndrop` is inert; TrussC's file access on iOS is the document picker (§9), not sokol drop. |
| **Window title** | `sapp_set_window_title` (14282) is `_SAPP_MACOS`-only → no-op. No title bar. |
| **Icon / favicon** | `sapp_set_icon` (14308) `_SAPP_MACOS`-only. iOS icon comes from the app bundle (`CFBundleIconName`, set by trussc_app.cmake). |
| **Fullscreen** | iOS is inherently fullscreen. `sapp_toggle_fullscreen` (14105 `_SAPP_MACOS`) no-op; `_sapp.fullscreen` unused. Immersive mode (§8) is the closest analogue. |
| **Quit** | No `sapp_request_quit`→OS route (§3). App exit is OS/user-driven (home/SIGKILL). |
| **`skip_present`** | Metal presents via sokol_gfx commit; no present-suppression path on iOS (same as mac Metal). Inert. |
| **Secondary windows** | Single `UIWindow` bound to the scene. `sapp_create_window()` / plural-window getters → invalid handle + log (like web). Map window #0's identity to `_sapp.ios.window`. |
| **GLES3 / EAGL** | Present in-source (6535–6573, 6561, 6566) but TrussC builds `SOKOL_METAL` only (§11). **DEAD — do not port.** |

---

## 11. TrussC-side wiring (verified against the repo)

**THE MIGRATION IN ONE LINE:** `core/platform/ios/sokol_impl.mm` currently
`#include "sokol_app.h"` (with `SOKOL_IMPL`). The desktop TUs already include
`sokol_app_tc.h`. The iOS leg = give `sokol_app_tc.h` a `TARGET_OS_IPHONE` backend and
switch this one include.

- **`core/platform/ios/sokol_impl.mm` (25 lines):** `#define SOKOL_IMPL`,
  `#define SOKOL_NO_ENTRY`, GCC diag push (unused var/func, missing-field-init), then
  includes in order: `sokol_log.h`, **`sokol_app.h`** ← swap to `sokol_app_tc.h`,
  `sokol_gfx.h`, `sokol_glue.h`, `util/sokol_gl_tc.h`, `util/sokol_memtrack.h`. No
  `SOKOL_METAL` define here — it comes from CMake.
- **`core/CMakeLists.txt`:** iOS detected at 61–65 (`CMAKE_SYSTEM_NAME STREQUAL "iOS"` →
  `TC_PLATFORM_IOS`, backend METAL); platform sources globbed `platform/ios/*.mm`
  (117–118); **`SOKOL_METAL` defined PUBLIC at 198** (`elseif(TC_PLATFORM_IOS)`). No
  `SOKOL_GLES3` on iOS anywhere → the EAGL path is unreachable. Confirms §0/§10.
- **`core/platform/ios/tcPlatform_ios.mm`** — every sokol touchpoint:
  - `setOrientation` → `sapp_ios_set_supported_orientations` (454) — §8(1).
  - `extern bool _sapp_ios_immersive_mode;` (421) + `setImmersiveMode`/`getImmersiveMode`
    (423–434) — §8(2). **The port must keep this non-static extern.**
  - `captureWindow` (136–226): reads the drawable this frame rendered into via
    `internal::currentWindowContext().lastSwapchainDrawable`, falling back to
    `sapp_get_swapchain().metal.current_drawable` (141–142); blits to a shared-storage
    staging texture (because `framebufferOnly=false` but reading the drawable directly
    still asserts under validation); unpacks `MTLPixelFormatRGB10A2Unorm` → RGBA8
    (204–214). Depends on the RGB10A2 layer patch (§2) and framebufferOnly=false (issue
    #56).
- **`core/platform/ios/tcFileDialog_ios.mm`:** `sapp_ios_get_window()` (23) → root VC to
  present the document picker — §9. The ONLY consumer of the window getter.
- **`TrussC.h`:** `#define SOKOL_NO_ENTRY` (15); `_event_cb` switch handles
  `TOUCHES_BEGAN/MOVED/ENDED/CANCELLED` (2373–2440) and `RESIZED` (2444), but **no
  SUSPENDED/RESUMED case** (§3 finding); `runApp` → `sapp_run` on the non-Android path
  (2634–2637). `buildAppDescriptor` sets no `desc.ios.*` (keyboard_resizes_canvas stays
  false, §7).
- **`addons/tcxImGui/src/sokol_imgui.h`:** `sapp_show_keyboard`/`sapp_keyboard_shown`
  (2661–2665) — §7 consumer.
- **`core/resources/Info-iOS.plist.in`:** `LSRequiresIPhoneOS`,
  `UISupportedInterfaceOrientations`(~ipad), usage strings. **No
  `UIApplicationSceneManifest`** — scene lifecycle is adopted programmatically (§1). No
  `UILaunchStoryboardName` value. `trussc_app.cmake` wires this plist at 1023–1029 and
  sets `TARGETED_DEVICE_FAMILY "1,2"`, automatic code signing.

**Used and must be preserved on iOS:** `_sapp_ios_run` → `UIApplicationMain` +
`_sapp_scene_delegate` (programmatic scene config); Metal init/swapchain/display-link;
per-frame `update_dimensions` with the view-bounds + drawable-readback patches; touch
events (identifier + changed); on-screen keyboard via UITextField; RGB10A2 layer + MSAA
+ framebufferOnly=false; suspend/resume display-link pause; `sapp_ios_get_window`;
`sapp_ios_set_supported_orientations`; the non-static `_sapp_ios_immersive_mode` global;
`sapp_get_environment`/`sapp_get_swapchain` Metal contract.

**Not applicable / gaps on iOS:** §10 table (mouse/clipboard/drop/title/icon/fullscreen/
quit/skip_present/secondary-windows/GLES3); SUSPENDED/RESUMED fire but TrussC drops them
(§3); tvOS press code compiled but unexercised (§6).

---

## 12. PORT CHECKLIST (`sokol_app_tc.h` `TARGET_OS_IPHONE` section)

### (a) Lift verbatim (line ranges in sokol_app.h)
1. **Metal texture/swapchain helpers** — 6373–6432 (`_sapp_ios_mtl_create_texture`,
   `_swapchain_create/destroy/resize/next`). Keep the **RGB10A2 MSAA format** (6407) and
   depth `Depth32Float_Stencil8`.
2. **Metal init + layer setup** — 6474–6501. Keep **all TrussC patches**:
   `framebufferOnly=false` (6487), `pixelFormat=RGB10A2Unorm` (6488), the
   `_sapp_ios_view_ctrl` subclass (6494), and the "layer as sublayer of view.layer"
   arrangement (6491).
3. **Display link + timing** — 6434–6472 (start/stop link, timing init/update/duration).
4. **`update_framebuffer_dimensions`** — 6511–6532 (TrussC set-then-readback patch #7).
5. **`update_dimensions`** — 6657–6676 (view-bounds patch #6 + fallback + RESIZED guard).
6. **`_sapp_ios_frame`** — 6678–6690 (timing + `@autoreleasepool` + `_sapp_frame`).
7. **Touch event** — 6636–6655 (allTouches enumeration, identifier, `changed`).
8. **On-screen keyboard** — 6692–6725 + `_sapp_textfield_dlg` 6799–6874 (CHAR/KEY bridge,
   keyboard notifications).
9. **Scene delegate** — 6727–6797 (programmatic `UISceneConfiguration`, willConnect setup,
   sceneWillResignActive/DidBecomeActive suspend/resume+display-link pause, willTerminate
   cleanup order).
10. **View + view-controller** — 6876–6930 (`_sapp_ios_view` displayLinkFired/touches;
    `_sapp_ios_view_ctrl` orientation/immersive overrides; keep
    `bool _sapp_ios_immersive_mode = false;` NON-STATIC at 6919).
11. **@interface decls** — 2827–2879 (scene delegate, textfield delegate,
    `_sapp_ios_view_ctrl`, `_sapp_ios_view`, `_sapp_ios_t` struct).
12. **Public API iOS branches** — `sapp_run` (13945–13946), `sapp_frame_duration`
    (13991–13992), `sapp_show_keyboard` (14087–14088), env/swapchain iOS branches
    (14397, 14425–14428), `sapp_ios_get_window` (14498–14506),
    `sapp_ios_set_supported_orientations` (14508–14520).

### (b) Adapt to the tc window model (single window = window #0)
1. **`sapp_create_window()` and plural-window APIs → invalid handle + log** (like web).
   The scene owns exactly one `UIWindow`; a second is not representable.
2. **New `sapp_window_*` getters** → for window #0 return real values (the iOS
   environment/swapchain, `_sapp.ios.window`); any other index → 0 + log.
3. **`_sapp.ios.window` is the "window handle."** No HWND/XID analogue beyond
   `sapp_ios_get_window()`'s `UIWindow*`.
4. **DPI is single, screen-wide** (`windowScene.screen.nativeScale`, gated by
   `high_dpi`). No per-window DPI.
5. **Compile as ObjC++** (`#error` guard) — reuse the macOS branch's guard.
6. **Owned loop lives in UIKit:** `_sapp_ios_run` calls `UIApplicationMain` (blocks); the
   frame callback is `displayLinkFired:`. No PeekMessage/XPending/rAF equivalent to write.

### (c) Known hazards
1. **Suspend MUST pause the display link** (§3) — rendering to a Metal drawable while
   backgrounded triggers the iOS watchdog. Don't drop the `display_link.paused` toggle.
2. **`_sapp_ios_immersive_mode` symbol name is an ABI dependency** — `tcPlatform_ios.mm`
   has `extern bool _sapp_ios_immersive_mode;`. Keep it non-static, same name (§8).
3. **Programmatic scene config is load-bearing** (§1) — the TrussC Info.plist has no
   scene manifest; the delegate's `configurationForConnectingSceneSession:` is the only
   thing making scene lifecycle work.
4. **Drawable-size readback** (§5, 6526–6530) — skip it and scissor-exceeds-render-pass
   validation aborts on some devices.
5. **`applicationWillTerminate:` rarely runs** (SIGKILL) — do not rely on clean teardown.
6. **`nextDrawable` exactly once per frame** (§9) — twice acquires two drawables.
7. **GLES3/EAGL path is DEAD** — do not port 6535–6573 / 6561 / 6566 / the `_SAPP_ANY_GL`
   frame-buffer-binding read (6683–6685). iOS is Metal-only in TrussC (§11).

### (d) Device-verification checklist (iOS is NOT in CI — verify on the user's iPhone)
There is no CI coverage for iOS; the only gate is a real device. Before calling the port
done, confirm on-device:
- [ ] App launches, renders (RGB10A2 layer, correct colors, no banding), holds framerate.
- [ ] **Touch**: multi-touch works; began/moved/ended/cancelled reach app; `identifier`
      stable across a drag; coordinates correct at Retina scale.
- [ ] **Rotation**: rotate device → RESIZED fires, framebuffer + drawable resize, no
      scissor/validation abort; both portrait and landscape.
- [ ] **Orientation lock**: `setOrientation(...)` (→ `sapp_ios_set_supported_orientations`)
      actually constrains rotation (test iOS 16+ path).
- [ ] **On-screen keyboard**: ImGui text field (tcxImGui) raises/dismisses the keyboard;
      CHAR + Enter/Space/Backspace events arrive.
- [ ] **Suspend/resume**: background then foreground → display link pauses (no GPU work /
      no watchdog kill) and resumes; SUSPENDED/RESUMED fire at the sokol layer.
- [ ] **Immersive mode**: `setImmersiveMode(true)` hides the status bar + home indicator;
      `false` restores them.
- [ ] **Document picker**: `tcFileDialog` open/save presents (proves `sapp_ios_get_window`
      → root VC still anchors the picker).
- [ ] **Screenshot**: `captureWindow()` returns correct pixels (RGB10A2 unpack path).

---

## Appendix: function → line index (iOS Metal-relevant)

| symbol | line |
|---|---|
| iOS platform detect (`_SAPP_IOS` @2322, API `#error` @2323) | 2314–2329 |
| iOS UIKit/Metal includes (GLES3 GLKit path @2441 DEAD) | 2435–2447 |
| Apple ARC / `_SAPP_OBJC_RELEASE` / timestamp (mach) | 2554–2630 |
| `sapp_ios_desc` (`keyboard_resizes_canvas`) | 2031–2033 |
| `sapp_ios_get_window` / `sapp_ios_set_supported_orientations` decls | 2233 / 2235 |
| iOS @interface decls (`_sapp_scene_delegate`, textfield dlg, view_ctrl, view) | 2827–2848 |
| `_sapp_ios_t` struct | 2850–2877 |
| `_sapp_ios_t ios;` embed | 3301 |
| `_sapp_ios_max_fps` | 6367 |
| `_sapp_ios_mtl_create_texture` | 6373 |
| `_sapp_ios_mtl_swapchain_create` (MSAA RGB10A2 @6407) / `destroy` / `resize` / `next` | 6401 / 6414 / 6423 / 6428 |
| `_sapp_ios_mtl_timing_init` / `update` / `frame_duration` | 6434 / 6439 / 6451 |
| `_sapp_ios_mtl_start_display_link` / `stop` | 6456 / 6466 |
| `_sapp_ios_mtl_init` (framebufferOnly=false @6487, RGB10A2 @6488, view_ctrl @6494) | 6474 |
| `_sapp_ios_mtl_discard_state` | 6503 |
| `_sapp_ios_mtl_update_framebuffer_dimensions` (patch #7 readback @6526) | 6511 |
| `_sapp_ios_gles3_init` / `discard` / `update_fb_dims` — **DEAD** | 6536 / 6561 / 6566 |
| `_sapp_ios_discard_state` | 6575 |
| `_sapp_ios_run` (UIApplicationMain) | 6588 |
| `main` (SOKOL_NO_ENTRY out) | 6595 |
| `_sapp_ios_app_event` | 6604 |
| `_sapp_tvos_press_event` — DEAD-but-kept | 6611 |
| `_sapp_ios_touch_event` | 6636 |
| `_sapp_ios_update_dimensions` (patch #6 view.bounds @6661) | 6657 |
| `_sapp_ios_frame` | 6678 |
| `_sapp_ios_show_keyboard` | 6692 |
| `@implementation _sapp_scene_delegate` (config @6728, willConnect @6737, resign @6763, active @6775, terminate @6791) | 6727 |
| `@implementation _sapp_textfield_dlg` (char/key bridge @6831) | 6799 |
| `@implementation _sapp_ios_view` (displayLinkFired @6878, touches @6903) | 6876 |
| `_sapp_ios_immersive_mode` global + `_sapp_ios_view_ctrl` impl | 6919 / 6920 |
| `#endif /* TARGET_OS_IPHONE */` | 6932 |
| `sapp_run` → `_sapp_ios_run` | 13945–13946 |
| `sapp_frame_duration` (iOS Metal branch) | 13991–13992 |
| `sapp_show_keyboard` / `sapp_keyboard_shown` (iOS) | 14086 / 14096 |
| `sapp_color_format` RGB10A2 (Metal) / `sapp_depth_format` | 14040 / 14048 |
| `sapp_get_environment` (iOS metal.device @14397) | 14387 |
| `sapp_get_swapchain` (iOS metal branch @14425–14428) | 14417 |
| `sapp_ios_get_window` | 14498 |
| `sapp_ios_set_supported_orientations` (iOS16 nudge @14512) | 14508 |
| helpers: `_sapp_timing_clamp` / `_sapp_roundf_gzero` / `_sapp_call_cleanup` | 2666 / 3423 / 3450 |
