# sokol_app.h Android (GLES3 / NativeActivity) Backend — Behavioral Specification

Implementation contract for giving `sokol_app_tc.h` an Android main-window /
app-lifecycle backend (project "sokol_app graduation", **Android leg — P5, the last
driver port before `sokol_app.h` is deleted**). All line references are into
`core/include/sokol/sokol_app.h` (14602 lines, this worktree). Focus: `_SAPP_ANDROID`
+ **`SOKOL_GLES3`** — the only configuration TrussC ships on Android (no Metal/Vulkan;
see §11). There is no alternate 3D-API path in the Android backend (unlike iOS's dead
EAGL branch), so nothing here is "dead-but-kept" — the whole `_SAPP_ANDROID` body ports.

This is the **sixth** sibling of `sapp-mac-impl-spec.md` (event-driven `[NSApp run]`),
`sapp-win32-impl-spec.md` (owned PeekMessage loop), `sapp-x11-impl-spec.md` (owned
XPending loop), `sapp-web-impl-spec.md` (no owned loop, browser rAF) and
`sapp-ios-impl-spec.md` (`UIApplicationMain` owns the loop). **Read the iOS spec first**
— it is the closest structural relative (single window, touch-only, GLES-family,
lifecycle SUSPENDED/RESUMED, posix/monotonic-family clock, port-time it preceded this
one). Android differs from iOS in FIVE structural ways, each load-bearing:

1. **Entry point is inverted (`ANativeActivity_onCreate`, NOT `main`).** Android is the
   ONE platform where `SOKOL_NO_ENTRY` is **forbidden** (compile error at 2352–2354).
   sokol_app.h *exports* `ANativeActivity_onCreate()` (10967), which calls
   `sokol_main(0, NULL)` to obtain the desc. `sapp_run()` is not supported. TrussC's
   `main()`/`runApp` is reached *through* `sokol_main`, not the other way around (§1).
2. **A dedicated app thread, not the UI thread.** The entire backend (EGL, frame loop,
   `init_cb`/`frame_cb`/`cleanup_cb`, input) runs on a **pthread spawned in
   `onCreate`**, talking to Android's UI thread over a **message pipe + `ALooper` +
   mutex/cond** (§2). TrussC's "main thread == app thread" assumption holds only because
   TrussC code runs on *that* thread — but it is NOT Android's Java/UI thread. This is
   the single biggest hazard in the port.
3. **EGL surface has an independent lifecycle.** The window/surface is created,
   destroyed and recreated while the app object lives (background/foreground, screen
   off). The full state is an **APP_CMD state machine** driven by NativeActivity
   callbacks (§3). `init_cb` fires on the first frame *after* a surface exists;
   `frame_cb` pauses whenever there is no surface or no focus.
4. **Touch + BACK key only.** No mouse/cursor/pointer-lock/clipboard-from-OS/drag&drop/
   window-title/icon/fullscreen-toggle surface (§10). Input = multi-touch via
   `AInputQueue` + a single hardware key (BACK) that TrussC deliberately swallows.
5. **Orientation & immersive mode are done in TrussC via JNI, NOT via sokol.** Unlike
   iOS (which pokes sokol's `_sapp_ios_immersive_mode` extern + a UIViewController
   subclass), Android's `tcPlatform_android.cpp` reaches the `ANativeActivity` directly
   (`ANativeActivity_setWindowFlags`, JNI `setRequestedOrientation`). So there is **no
   sokol-side orientation/immersive patch to preserve** on Android (§8/§10).

**Port target.** `sokol_app_tc.h` (11876 lines) is organised as **N independent,
self-contained per-platform copies** of the renamed (`_sapp` → `_sapp_tc`,
`_sapp_*` → `_sapp_tc_*`) sokol_app implementation. The top-level chain is:

```
#if   defined(__APPLE__)                                              // 198  (mac @202, iOS @1726 — DONE)
#elif defined(_WIN32)                                                 // 3670
#elif defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)  // 5650
#elif defined(__EMSCRIPTEN__)                                         // 8917
#else /* other platforms */                                          // 11818 (stub)
#endif                                                                // 11850
```

The Android backend slots in as a **new `#elif defined(__ANDROID__)` branch** anywhere
before the `#else` stub (the linux guard already excludes `__ANDROID__`, so ordering is
not a correctness issue; place it next to linux for readability). It is a **plain C++
translation unit** — **no Objective-C++ `#error` guard** (contrast mac/iOS, which
enforce `.mm`). Each existing branch already carries the full public-API block with
`_SAPP_ANDROID` branches compiled-but-dead inside it (verify: `_sapp_tc.android.display`
at sokol_app_tc.h:3080/11245, `_sapp_tc.android.context` at 3091/11256,
`_sapp_tc_android_show_keyboard` at 3103/11268, `sapp_android_get_native_activity` at
3593–3597). The web/iOS ports lifted those blocks verbatim and the Android port reuses
them the same way — the port's job is to add the **Android-specific** struct + helpers +
`ANativeActivity_onCreate` and wire the already-present public branches to real
`_sapp_tc_android_*` functions.

---

## 0. Global state touched (the shared contract)

The single global is `static _sapp_t _sapp;` (~3298), which embeds `_sapp_android_t
android;` at **3311–3312** (under `_SAPP_ANDROID`). In the port this becomes
`static _sapp_tc_t _sapp_tc;` with `_sapp_tc.android`.

### The Android state structs (3027–3074)
```c
typedef enum {                    // 3028–3037 — messages UI-thread → app-thread
    _SOKOL_ANDROID_MSG_CREATE, _SOKOL_ANDROID_MSG_RESUME, _SOKOL_ANDROID_MSG_PAUSE,
    _SOKOL_ANDROID_MSG_FOCUS,  _SOKOL_ANDROID_MSG_NO_FOCUS,
    _SOKOL_ANDROID_MSG_SET_NATIVE_WINDOW, _SOKOL_ANDROID_MSG_SET_INPUT_QUEUE,
    _SOKOL_ANDROID_MSG_DESTROY,
} _sapp_android_msg_t;

typedef struct {                  // 3039–3045 — the cross-thread pipe + lock
    pthread_t thread; pthread_mutex_t mutex; pthread_cond_t cond;
    int read_from_main_fd; int write_from_main_fd;   // "main" = Android UI thread here
} _sapp_android_pt_t;

typedef struct { ANativeWindow* window; AInputQueue* input; } _sapp_android_resources_t; // 3047–3050

typedef struct {                  // 3052–3072
    ANativeActivity* activity;
    _sapp_android_pt_t pt;
    _sapp_android_resources_t pending;   // set by UI thread, consumed by app thread
    _sapp_android_resources_t current;   // owned by app thread
    ALooper* looper;
    bool is_thread_started, is_thread_stopping, is_thread_stopped;
    bool has_created, has_resumed, has_focus;         // the "should we render?" latches
    EGLConfig config; EGLDisplay display; EGLContext context; EGLSurface surface;
    #if __ANDROID_API__ >= 29
    AChoreographer* choreographer;       // TrussC vsync-paced frame loop (§4)
    bool frame_callback_in_flight;
    #endif
} _sapp_android_t;
```
Note the `pending`/`current` double-buffering of window+input: the UI thread writes
`pending` and posts a MSG; the app thread swaps `current = pending` under the mutex
(§3). `has_resumed && has_focus && surface!=EGL_NO_SURFACE` is the render gate
(`_sapp_android_should_update`, 10720).

### Cross-platform `_sapp` fields the Android path reads/writes
`desc`, `valid`, `first_frame`, `init_called`, `cleanup_called`,
`window_width/height`, `framebuffer_width/height`, `dpi_scale`, `timing`, `frame_count`,
`event`, `gl.framebuffer` (10422), `skip_present` (10531), `onscreen_keyboard_shown`
(read-only via `sapp_keyboard_shown`; **never written on Android** — see §8 gap).
**NOT touched:** `fullscreen`, `mouse.*`, `clipboard.*`, `drop.*`, `keycodes[]`,
`custom_cursor_bound[]`, `window_title`, `quit_requested`/`quit_ordered` (all inert, §10).

### desc defaults — GLES **3.1** context requested (3537–3543)
`_sapp_desc_defaults` under `SOKOL_GLES3`: `gl.major_version = 3`, and
`#if defined(_SAPP_ANDROID) || defined(_SAPP_LINUX) → minor_version = 1` (else 0). So
`_sapp_android_init_egl` (10372–10375) requests an **ES 3.1** context via
`EGL_CONTEXT_MAJOR/MINOR_VERSION = 3/1`. Manifest declares
`glEsVersion="0x00030000"` (ES3 minimum) — §11.

### No ObjC / no ARC macros
Plain C++/NDK. Teardown is explicit EGL/pthread cleanup + `exit(0)` (§3); there is no
`_SAPP_OBJC_RELEASE`/`_SAPP_CLEAR_ARC_STRUCT` machinery in this backend.

---

## 1. Entry / run flow — `ANativeActivity_onCreate` is the entry, `sokol_main` pulls the desc

### Platform detection (2346–2354)
`#elif defined(__ANDROID__)` → `#define _SAPP_ANDROID (1)`;
`#if !defined(SOKOL_GLES3) #error(... must be SOKOL_GLES3)` (2349–2351);
**`#if defined(SOKOL_NO_ENTRY) #error("SOKOL_NO_ENTRY is not supported on Android")`
(2352–2354)** — the defining constraint of this platform. The port's `__ANDROID__`
branch must keep this guard; it is the ONLY tc.h branch where `SOKOL_NO_ENTRY` is absent.

### Includes (2520–2531)
`<pthread.h>`, `<unistd.h>`, `<time.h>`, `<android/native_activity.h>`,
`<android/configuration.h>` (**`// [TrussC] AConfiguration for display density`**, 2525),
`<android/looper.h>`, `#if __ANDROID_API__>=29 <android/choreographer.h>` (2527–2529),
`<EGL/egl.h>`, `<GLES3/gl3.h>`.

### The exported entry: `ANativeActivity_onCreate` (10967–11036)
Android's `NativeActivity` framework (manifest `android.app.lib_name`, §11) `dlopen`s
the app's `.so` and calls the exported `ANativeActivity_onCreate`. Sequence:
```c
JNIEXPORT void ANativeActivity_onCreate(ANativeActivity* activity, void* saved, size_t) {
    _sapp_clear(&_sapp, sizeof(_sapp));           // 10977
    _sapp.android.activity = activity;            // 10978 — set BEFORE sokol_main (issue #708)
    sapp_desc desc = sokol_main(0, NULL);         // 10979 — user desc (calls TrussC main(), §below)
    _sapp_init_state(&desc);                       // 10980 — clears _sapp again!
    _sapp.android.activity = activity;            // 10981 — so re-set activity AFTER init_state
    pipe(pipe_fd); … read/write_from_main_fd       // 10983–10989 — the UI→app pipe
    pthread_mutex_init / cond_init                 // 10991–10992
    pthread_create(&…thread, DETACHED, _sapp_android_loop, 0);  // 10994–10998 — spawn app thread
    // wait until app thread signals is_thread_started         // 11001–11005
    _sapp_android_msg(_SOKOL_ANDROID_MSG_CREATE); wait has_created  // 11008–11013 (EGL init)
    activity->callbacks->onStart/onResume/onPause/onStop/onDestroy/
        onNativeWindowCreated/onNativeWindowDestroyed/
        onInputQueueCreated/onInputQueueDestroyed/onLowMemory = …  // 11016–11031
    /* NOT A BUG: do NOT call sapp_discard_state() */             // 11035
}
```
`onCreate` runs **on the Android UI thread** and **returns** (it does NOT own a loop —
contrast iOS `UIApplicationMain`, which blocks). The loop lives on the spawned app
thread (§2). The double `activity=` (10978, 10981) is required because `_sapp_init_state`
zeroes `_sapp`; keep both. `onSaveInstanceState`/`onWindowFocusChanged`/`onConfigChanged`
handlers also exist (10849, 10856, 10923) but the last two window callbacks are commented
out in the registration block (11024–11025, 11029–11030 — resize/redraw/contentRect/
configChanged are NOT wired; rotation is handled per-frame instead, §5).

### The TrussC entry chain (verified, the crux of the port)
Android is the inverse of every other platform. `sokol_main` is **declared** by
sokol_app.h but **defined by TrussC**, in `core/platform/android/sokol_impl.cpp`:
```c
extern int main();
namespace trussc { namespace internal { extern sapp_desc g_androidDesc; } }
sapp_desc sokol_main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    main();                                   // runs the user's main() → runApp() stores desc
    return trussc::internal::g_androidDesc;
}
```
And `TrussC.h` (2642–2653) specialises `runApp` for `__ANDROID__`:
```c
#ifdef __ANDROID__
    inline sapp_desc g_androidDesc = {};      // 2644 (namespace internal)
    template<class AppClass> int runApp(const WindowSettings& s = {}) {
        internal::g_androidDesc = buildAppDescriptor<AppClass>(s);   // 2649 — store, DON'T run
        return 0;                              // returns to sokol_main; NO sapp_run() call
    }
#else … sapp_run(&desc) …                      // 2656–2658 (every other platform)
#endif
```
So the full boot is:
**NativeActivity `dlopen` → `ANativeActivity_onCreate` (sokol_app_tc.h) → `sokol_main`
(TrussC `sokol_impl.cpp`) → user `int main()` → `runApp<App>()` (stores `g_androidDesc`,
returns) → back in `sokol_main`, returns `g_androidDesc` → `onCreate` inits state, spawns
app thread, returns to UIKit-equivalent.** `TrussC.h:15` still `#define SOKOL_NO_ENTRY`,
but on Android that macro is **not passed to the sokol impl TU** — `sokol_impl.cpp` does
not define it (it can't; 2352), and `sokol_main`+`ANativeActivity_onCreate` provide the
entry. **The port must keep `ANativeActivity_onCreate` exported from sokol_app_tc.h's
`__ANDROID__` branch** (see §11 migration) — this is the one platform where the tc.h
branch supplies the process entry point rather than a `sapp_run`.

---

## 2. Threading model — the app runs on a spawned pthread, NOT the UI thread

**This is the section that contradicts "everything is on the main thread".** There are
two threads:

- **Android UI thread** ("main" in sokol's fd names): runs `ANativeActivity_onCreate`
  and every `activity->callbacks->on*` (onResume/onPause/onWindowFocusChanged/
  onNativeWindowCreated/…). These handlers do almost nothing except **post a message**
  down the pipe and, for window/input/destroy, **block on a condvar** until the app
  thread acknowledges (`_sapp_android_msg_set_native_window` 10877, `_set_input_queue`
  10900, `on_destroy` 10947 all `pthread_cond_wait` until `current==pending` /
  `is_thread_stopped`).
- **App / loop thread** (`_sapp_android_loop`, 10753): the sokol "main thread". Owns the
  `ALooper`, the EGL context/surface, runs `_sapp_frame` (`init_cb`/`frame_cb`/user
  code), processes input, and handles the APP_CMD messages in `_sapp_android_main_cb`
  (10629). **All TrussC user callbacks run here.**

### The message pipe + ALooper (10753–10829, 10832–10836)
`_sapp_android_msg(msg)` (10832) = `write(write_from_main_fd, &msg, sizeof msg)` from the
UI thread. On the app thread, `ALooper_addFd(read_from_main_fd, …, _sapp_android_main_cb)`
(10758–10763) drives `_sapp_android_main_cb` (10629) which `read()`s the msg and switches
under `pthread_mutex_lock(&pt.mutex)` (10642), then `pthread_cond_broadcast` to release
the (possibly blocked) UI thread (10715). Input arrives on a *second* fd: the
`AInputQueue` is `AInputQueue_attachLooper(…, _sapp_android_input_cb)` (10695–10700) so
touch/key dispatch also lands on the app thread (10607).

### Which callback on which thread
| Callback | Thread |
|---|---|
| `ANativeActivity_onCreate`, all `on*` NativeActivity callbacks | **UI thread** |
| `sokol_main` → TrussC `main()`/`runApp` | **UI thread** (inside onCreate, before app thread does anything TrussC-visible) |
| `_sapp_android_loop`, `_sapp_android_frame`, `_sapp_frame` (init/frame/cleanup cb), touch/key events, APP_CMD handling | **App thread** |

### Hazards for TrussC
- **`sokol_main` (hence TrussC `main()`/`runApp`/`buildAppDescriptor`) runs on the UI
  thread**, but every other TrussC callback runs on the app thread. Anything with
  thread-affinity captured at `runApp` time (thread-local, `runOnMainThread` liveness
  token — see MEMORY thread-safety notes) is captured on the *UI* thread, then the app
  runs on a *different* thread. Verify TrussC's main-thread identity is established from
  `setup()`/first frame (app thread), not from `runApp`.
- **JNI on the native activity**: `tcPlatform_android.cpp` calls JNI
  (`setRequestedOrientation`, §8) via `activity->clazz`. JNI calls must be on a
  JNI-attached thread. `sapp_android_get_native_activity()` is callable from any thread
  (no `_sapp.valid` assert, 14582–14584), but JNI usage from the app thread must
  `AttachCurrentThread` — confirm `tcPlatform_android.cpp` does its own attach (it
  uses `activity->env` / `vm->AttachCurrentThread`; the port doesn't change this but the
  spec flags it as a live cross-thread concern).
- The mutex/cond/pipe are **process-global lifetime**; `on_destroy` tears them down
  after the app thread confirms stop, then `exit(0)` (§3).

---

## 3. EGL surface lifecycle + APP_CMD state machine

The window surface is not permanent. `_sapp_android_main_cb` (10629–10718) is the state
machine; every case runs on the app thread under the pt mutex.

### The messages
- **`MSG_CREATE` (10644–10653):** `_sapp_android_init_egl()` (display+context, NO
  surface yet, 10322–10386), `_sapp.valid = true`, `has_created = true`. Sent once from
  `onCreate` (11009). Context lives for the whole process; surface comes later.
- **`MSG_RESUME` (10654–10658):** `has_resumed = true`; fire
  `SAPP_EVENTTYPE_RESUMED` via `_sapp_android_app_event` (10437, gated by
  `_sapp_events_enabled`).
- **`MSG_PAUSE` (10659–10663):** `has_resumed = false`; fire `SAPP_EVENTTYPE_SUSPENDED`.
- **`MSG_FOCUS` / `MSG_NO_FOCUS` (10664–10671):** toggle `has_focus`. No event fired.
- **`MSG_SET_NATIVE_WINDOW` (10672–10687):** the **surface** transition. If
  `current.window != pending.window`: destroy the old EGL surface
  (`_sapp_android_cleanup_egl_surface`, 10426) if any, then if the new window is non-NULL
  `_sapp_android_init_egl_surface(pending.window)` (10404 — `eglCreateWindowSurface` +
  `eglMakeCurrent` + read `gl.framebuffer`) and `_sapp_android_update_dimensions(…, true)`
  (§5). On surface-init failure → `_sapp_android_shutdown()`. Then `current.window =
  pending.window`.
- **`MSG_SET_INPUT_QUEUE` (10688–10704):** detach the old `AInputQueue` from the looper,
  attach the new one with `_sapp_android_input_cb` (§6).
- **`MSG_DESTROY` (10705–10710):** `_sapp_android_cleanup()` (calls `cleanup_cb` if a
  surface still exists, then destroys the EGL context, 10505–10514), `_sapp.valid=false`,
  `is_thread_stopping=true` (breaks the loop).

### NativeActivity callbacks → messages (10838–10932)
`onStart`→(log only), `onResume`→RESUME, `onPause`→PAUSE, `onStop`→(log only),
`onWindowFocusChanged`→FOCUS/NO_FOCUS, `onNativeWindowCreated`→SET_NATIVE_WINDOW(window),
`onNativeWindowDestroyed`→SET_NATIVE_WINDOW(NULL), `onInputQueueCreated/Destroyed`→
SET_INPUT_QUEUE(queue/NULL), `onLowMemory`→(log only), `onConfigurationChanged`→(log
only; NOT registered), `onSaveInstanceState`→returns NULL/0.

### The render gate + when init_cb fires
`_sapp_android_should_update()` (10720–10724) = `has_resumed && has_focus && surface !=
EGL_NO_SURFACE`. The frame loop only calls `_sapp_android_frame` when this is true. The
**first** `_sapp_frame()` (10529) — which fires `init_cb` (TrussC `setup()`) on the very
first frame — therefore happens only once a surface exists AND the activity is
resumed+focused, i.e. after MSG_CREATE + MSG_SET_NATIVE_WINDOW + MSG_RESUME +
MSG_FOCUS. Background/foreground cycles destroy+recreate the surface and pause+resume the
frame loop, but `init_cb` fires **once** (guarded inside `_sapp_frame`); `cleanup_cb`
fires from `_sapp_android_cleanup` only while a surface is still bound (10508). Typical
minimise/restore = SET_NATIVE_WINDOW(NULL)+PAUSE+NO_FOCUS → surface destroyed, frames
stop → FOCUS+RESUME+SET_NATIVE_WINDOW(window) → surface recreated, frames resume, RESIZED
fires (10500).

### `on_destroy` + `exit(0)` (10934–10965)
UI thread posts MSG_DESTROY, blocks until `is_thread_stopped`, destroys mutex/cond,
`close()`s both fds, then **`exit(0)`** (10964) — a deliberate hard reset so static
globals are cleared for a clean relaunch. Port note: this bypasses normal C++ static
dtors — consistent with TrussC's "leak GPU singletons at exit" gotcha (MEMORY); do not
add teardown that relies on running after `exit(0)`.

### SUSPENDED/RESUMED are fired but TrussC drops them
`TrussC.h`'s `_event_cb` switch (2253–2503) handles KEY/MOUSE/TOUCHES/RESIZED/
FILES_DROPPED/CLIPBOARD_PASTED/QUIT_REQUESTED but has **no `case SAPP_EVENTTYPE_SUSPENDED`
/ `RESUMED`** — identical gap to iOS (§3 of the iOS spec). The events still fire at the
sokol layer (harmless); the port must keep firing them but need not surface them.

---

## 4. Frame loop — Choreographer (TrussC patch) with a poll-loop fallback

`_sapp_android_loop` (10753–10829) is the owned loop on the app thread. There are **two
frame-driving paths**; the Choreographer path is a **TrussC patch** (upstream sokol_app
has no Choreographer integration — verify: log items `ANDROID_CHOREOGRAPHER_ENABLED` /
`ANDROID_CHOREOGRAPHER_UNAVAILABLE` at 1803–1804, `AChoreographer* choreographer` +
`frame_callback_in_flight` in the struct at 3068–3071, all `#if __ANDROID_API__ >= 29`).

### Path A — Choreographer (API ≥ 29, the normal case) 10765–10797, 10726–10741
At loop start `choreographer = AChoreographer_getInstance()` (10766); log ENABLED/
UNAVAILABLE. Each outer iteration: if `!frame_callback_in_flight && should_update()`,
`AChoreographer_postFrameCallback64(choreographer, _sapp_android_frame_callback, NULL)`
and set `frame_callback_in_flight=true` (10789–10792), then `ALooper_pollOnce(-1, …)`
blocks until the next event (10795). The vsync callback `_sapp_android_frame_callback`
(10727–10740, `#if __ANDROID_API__>=29`) clears the in-flight flag, bails if
`is_thread_stopping`, and if `should_update()` **re-posts the next callback** and calls
`_sapp_android_frame((double)frame_time_nanos / 1e9)` (10738) — the choreographer
timestamp feeds `_sapp_timing_update` as the external clock. This is vsync-paced.

### Path B — poll-loop fallback (API < 29, or `choreographer==NULL`) 10799–10809
If no choreographer: `if (should_update()) _sapp_android_frame(0.0)` (0.0 → sokol uses
its own `CLOCK_MONOTONIC` clock, §below), then drain events with
`ALooper_pollOnce(block_until_event ? -1 : 0, …)` in a `while (process_events &&
!is_thread_stopping)` (10804–10809). `block_until_event = !should_update()` — when
nothing to render (backgrounded), block indefinitely on the looper; otherwise poll
non-blocking and spin frames. Both paths break when `is_thread_stopping`.

### `_sapp_android_frame(double external_now)` (10523–10533)
```c
SOKOL_ASSERT(display/context/surface valid);
_sapp_timing_update(&_sapp.timing, external_now);          // choreographer ts, or 0→monotonic
_sapp_android_update_dimensions(_sapp.android.current.window, false);   // §5, every frame
_sapp_frame();                                             // init_cb (first) then frame_cb
// Modified by tettou771 for TrussC: skip present support     10530
if (_sapp.skip_present) { _sapp.skip_present = false; return; }   // 10531 — TrussC patch
eglSwapBuffers(_sapp.android.display, _sapp.android.surface);     // 10532
```
The **`skip_present` early-return (10530–10531)** is TrussC mod §skip_present
(TRUSSC_MODIFICATIONS.md line 43 lists "Android EGL" among the backends patched) — lets
`sapp_skip_present()` (14597–14599) suppress the next `eglSwapBuffers`. Preserve it.

### Clock — CLOCK_MONOTONIC (posix branch)
Android uses the shared posix timestamp path (2586–2596 init, 2635–2639 now):
`_SAPP_CLOCK_MONOTONIC` = `CLOCK_MONOTONIC`, `clock_gettime` in ns. Live (unlike web,
which asserts false). Fed by `_sapp_timing_update` when `external_now==0`; when the
choreographer supplies `frame_time_nanos`, that is used as the external now instead.
`sapp_frame_duration()` uses the platform-agnostic EMA-filtered `_sapp.timing`; there is
no Android-specific `sapp_frame_duration` branch (contrast iOS's CADisplayLink timer).

---

## 5. Dimensions, DPI, rotation (`_sapp_android_update_dimensions`, 10444–10503)

Runs **every frame** from `_sapp_android_frame`, and once with `force_update=true` on
surface (re)creation (10680). This is how rotation and resize are picked up — there is no
dedicated resize/rotate callback (the NativeActivity resize/config callbacks are left
unregistered, 11024–11030; the manifest declares `configChanges="orientation|screenSize|
screenLayout|keyboardHidden"` so the activity is NOT recreated on rotation — §11).

```c
win_w = ANativeWindow_getWidth(window);  win_h = ANativeWindow_getHeight(window);   // 10450
win_changed = (win_w != window_width) || (win_h != window_height);
window_width = win_w; window_height = win_h;
if (win_changed || force_update) {
    if (!desc.high_dpi) {                                     // 10457 — half-res buffer
        ANativeWindow_setBuffersGeometry(window, win_w/2, win_h/2, format);   // 10468
        // NOTE (10463): setting geometry == window size causes display artefacts,
        // so only call it when buffer geometry differs from window size.
    }
}
eglQuerySurface(EGL_WIDTH/HEIGHT) → fb_w, fb_h;              // 10474–10476
framebuffer_width = fb_w; framebuffer_height = fb_h;
```

### DPI via `AConfiguration_getDensity` (10482–10497) — TrussC patch §8
Upstream computes `dpi_scale = fb_w/win_w`. TrussC replaces it:
```c
if (_sapp.android.activity) {
    AConfiguration* config = AConfiguration_new();
    AConfiguration_fromAssetManager(config, activity->assetManager);
    int32_t density = AConfiguration_getDensity(config);
    AConfiguration_delete(config);
    if (density > 0) _sapp.dpi_scale = (float)density / 160.0f;   // mdpi baseline
    else             _sapp.dpi_scale = (float)fb_w / (float)win_w;   // fallback
} else            _sapp.dpi_scale = (float)fb_w / (float)win_w;      // fallback
```
`density/160` matches macOS/Windows semantics (160dpi = scale 1.0). Preserve the
fb/win-ratio fallback for both the no-activity and density≤0 cases. `sapp_dpi_scale()`
(14060) returns this; `tcPlatform_android.cpp:22 dpiScale()` forwards it.

### RESIZED (10498–10502)
`if ((win_changed || fb_changed || force_update) && !_sapp.first_frame)
_sapp_android_app_event(SAPP_EVENTTYPE_RESIZED)`. Suppressed on `first_frame`; gated by
`_sapp_events_enabled`. TrussC's `_event_cb` handles RESIZED at TrussC.h:2463.

---

## 6. Touch input (`_sapp_android_touch_event`, 10535–10587)

Dispatched from `_sapp_android_input_cb` (10607–10627) on the app thread: for each event
pulled from the `AInputQueue`, `AInputQueue_preDispatchEvent` (IME pre-dispatch) is
honoured, then `_sapp_android_touch_event(e) || _sapp_android_key_event(e)` decides
`handled`, and `AInputQueue_finishEvent(…, handled)` acks it.

```c
if (AInputEvent_getType(e) != AINPUT_EVENT_TYPE_MOTION) return false;   // 10536
if (!_sapp_events_enabled()) return false;
action = AMotionEvent_getAction(e) & AMOTION_EVENT_ACTION_MASK;
// DOWN/POINTER_DOWN → TOUCHES_BEGAN; MOVE → TOUCHES_MOVED;
// UP/POINTER_UP → TOUCHES_ENDED; CANCEL → TOUCHES_CANCELLED               // 10545–10562
_sapp_init_event(type);
num_touches = min(AMotionEvent_getPointerCount(e), SAPP_MAX_TOUCHPOINTS);  // 10568–10571
for i in num_touches:
    dst->identifier      = AMotionEvent_getPointerId(e, i);                // 10574 stable id
    dst->pos_x = (AMotionEvent_getX(e,i) / window_width)  * framebuffer_width;   // 10575
    dst->pos_y = (AMotionEvent_getY(e,i) / window_height) * framebuffer_height;  // 10576 → fb space
    dst->android_tooltype = (sapp_android_tooltype)AMotionEvent_getToolType(e,i); // 10577
    dst->changed = (POINTER_DOWN/UP) ? (i == pointer_index) : true;        // 10578–10583
_sapp_call_event(&_sapp.event);   // 10585
```
- **All active pointers** are emitted each event; `changed` marks which pointer this
  event is about (for POINTER_DOWN/UP only the indexed one changed; DOWN/MOVE/UP/CANCEL
  mark all changed). Same `identifier`+`changed` contract as iOS — TrussC's touch
  tracking in `_event_cb` (TrussC.h:2392–2440, maps to `TouchEventArgs`, `cancelled`
  from CANCELLED) relies on it. **Preserve.**
- `android_tooltype` (finger/stylus/mouse/eraser; enum `sapp_android_tooltype` 1550–1555,
  field `touchpoint.android_tooltype` 1570) is **upstream sokol** (not a TrussC patch) —
  carry it through.
- Positions are already framebuffer-space (multiplied by fb/window ratio), unlike iOS
  which multiplies by `dpi_scale`. Same net effect (fb pixels).

---

## 7. Key input — BACK key consumed, no shutdown (`_sapp_android_key_event`, 10589–10605)

There is **no general keycode table** on Android in this backend — the only key handled
is BACK, and TrussC deliberately swallows it:
```c
if (AInputEvent_getType(e) != AINPUT_EVENT_TYPE_KEY) return false;
if (AKeyEvent_getKeyCode(e) == AKEYCODE_BACK) {
    /* [TrussC tettou771] Patched: BACK key no longer triggers shutdown. (10594–10601)
       Upstream calls _sapp_android_shutdown() here, which destroys the EGL surface and
       invokes cleanup_cb. Under screen pinning the OS blocks ANativeActivity_finish(),
       so the process keeps running but the EGL context is gone — app appears frozen.
       Consume without shutdown. Long-term fix: SAPP_EVENTTYPE_QUIT_REQUESTED for Android. */
    return true;               // consume, do NOT shut down
}
return false;                  // all other keys unhandled → OS default
```
This is TRUSSC_MODIFICATIONS.md **§8a (Android BACK Key No Shutdown)** — kiosk /
lock-task / screen-pinning deployments depend on it (the user's Sony XP / signage apps).
**Preserve verbatim.** Upstream's version calls `_sapp_android_shutdown()` (10516) here;
the port must NOT reintroduce that. No CHAR events are produced from hardware keys; text
comes from the soft keyboard indirectly (§8) but this backend does not bridge IME text to
`SAPP_EVENTTYPE_CHAR` (contrast iOS's UITextField bridge) — key/char input is effectively
BACK-consume only.

---

## 8. On-screen keyboard (`_sapp_android_show_keyboard`, 10743–10751)

```c
void _sapp_android_show_keyboard(bool shown) {
    SOKOL_ASSERT(_sapp.valid);
    if (shown) ANativeActivity_showSoftInput(activity, ANATIVEACTIVITY_SHOW_SOFT_INPUT_FORCED);
    else       ANativeActivity_hideSoftInput(activity, ANATIVEACTIVITY_HIDE_SOFT_INPUT_NOT_ALWAYS);
}
```
Comment (10745) notes the NDK soft-input API is flaky. `sapp_show_keyboard(bool)`
(14086–14094) routes `#elif defined(_SAPP_ANDROID) → _sapp_android_show_keyboard` (14089).
Consumer: `addons/tcxImGui/src/sokol_imgui.h` toggles it from `io.WantTextInput` (same
one consumer as iOS).

> **⚠ Gap (finding):** `sapp_keyboard_shown()` (14096–14098) returns
> `_sapp.onscreen_keyboard_shown`, but **nothing on the Android path ever sets that
> flag** (there is no IME text-change delegate as on iOS). So `sapp_keyboard_shown()`
> always returns `false` on Android, and tcxImGui's `if (WantTextInput &&
> !sapp_keyboard_shown()) sapp_show_keyboard(true)` will re-issue `show` every frame while
> a text field is focused. Harmless (the OS ignores redundant shows) but worth noting;
> the port does not need to fix it but must not regress the flakier behaviour.

### Orientation / immersive mode are NOT sokol's job on Android
Unlike iOS (§8 of the iOS spec: `_sapp_ios_immersive_mode` extern +
`sapp_ios_set_supported_orientations` + UIViewController subclass), Android does all of
this in `tcPlatform_android.cpp` **directly against the NativeActivity**, not through
sokol:
- `setImmersiveMode(bool)` (tcPlatform_android.cpp:29–38): `ANativeActivity_setWindowFlags`
  `FLAG_FULLSCREEN (0x0400)` on/off.
- `setKeepScreenOn` (125–131): `FLAG_KEEP_SCREEN_ON (0x0080)`.
- `setOrientation(Orientation)` (143–184): JNI `Activity.setRequestedOrientation(int)`
  via `activity->clazz` / `GetMethodID`.
- `getDeviceOrientation` (410), sensor/window queries — all via JNI.

The only sokol entry these use is `sapp_android_get_native_activity()` (§9). **There is
no sokol-side orientation/immersive patch to preserve** — a real divergence from iOS the
port must not "helpfully" add.

---

## 9. Public API: EGL getters, native activity, environment/swapchain (GLES3)

### `sapp_egl_get_display` / `sapp_egl_get_context` (14064–14084)
`#if defined(_SAPP_ANDROID) return _sapp.android.display / .context`. Asserts
`_sapp.valid`. (Also a `_SAPP_LINUX && _SAPP_EGL` branch.) These feed sokol_gfx's GLES3
context binding.

### `sapp_android_get_native_activity()` (14582–14590) — the one TrussC-consumed getter
```c
// _sapp.valid NOT asserted — callable from within sokol_main() (issue #708)   14583–14584
#if defined(_SAPP_ANDROID) return (void*)_sapp.android.activity; #else return 0; #endif
```
**Sole consumer:** `tcPlatform_android.cpp` (dpiScale, setImmersiveMode, setKeepScreenOn,
setOrientation, getDeviceOrientation, `getActivityClass`, JNI env — lines 30, 125, 144,
221, 461, 477). No other Android platform TU touches sokol
(`tcFileDialog_android.cpp`, `tcFbo_android.cpp`, `tcVideoRecorder_android.cpp`,
`tcSound_android.cpp` etc. use JNI/AAudio directly, not sapp). **Keep this getter with
its no-valid-assert contract** — it must be callable from `sokol_main` (§1) and from the
app thread.

### GLES3 environment / swapchain
Android has no Metal `sapp_get_environment`/`sapp_get_swapchain` device fields to fill;
the GLES3 path uses `glGetIntegerv(GL_FRAMEBUFFER_BINDING, &_sapp.gl.framebuffer)` read in
`_sapp_android_init_egl_surface` (10422) and consumed by `sglue_swapchain()`/sokol_glue's
GL branch (`_sapp.gl.framebuffer` is the default FBO name). `sapp_width()/height()` return
`framebuffer_width/height`; `sapp_color_format`/`depth_format` fall to the GLES3 defaults
(RGBA8 / DEPTH_STENCIL from the EGL config: RGB8 + `EGL_DEPTH_SIZE 16`,
`EGL_STENCIL_SIZE 0`, 10341–10342). No RGB10A2 / MSAA-texture machinery (contrast iOS).

---

## 10. Features NOT applicable on Android (explicitly not-ported)

| Feature | Android reality |
|---|---|
| **Mouse / cursor / pointer-lock** | No pointer model. `sapp_show_mouse`/`lock_mouse`/`set_mouse_cursor` have no `_SAPP_ANDROID` branch → inert. (A mouse *device* surfaces only as a touch with `android_tooltype == MOUSE`.) |
| **Clipboard** | No `_SAPP_ANDROID` branch in `sapp_set/get_clipboard_string`. Returns `""`. No `CLIPBOARD_PASTED`. |
| **Drag & drop** | No Android drop path. `enable_dragndrop` inert. File access is via `tcFileDialog_android.cpp` (JNI intents), not sokol. |
| **Window title / icon** | `sapp_set_window_title` / `sapp_set_icon` are `_SAPP_MACOS`-only → no-op. Label/icon come from the APK manifest + res (§11). |
| **Fullscreen toggle** | `sapp_toggle_fullscreen` (14104–14114) has no Android branch → no-op; `_sapp.fullscreen` unused. Immersive mode (JNI, §8) is the analogue and lives in TrussC, not sokol. |
| **Quit** | No `sapp_request_quit`→OS route. Upstream had BACK→shutdown; TrussC removed it (§7). App exit is OS-driven (task-swipe → onDestroy → `exit(0)`, §3). `SAPP_EVENTTYPE_QUIT_REQUESTED` is the documented long-term fix but is NOT implemented for Android. |
| **Orientation / immersive** | Done in TrussC via JNI/`setWindowFlags`, **not** sokol (§8). No sokol patch. |
| **`skip_present`** | Supported (TrussC patch, §4, 10530–10531). |
| **Secondary windows** | Single `ANativeWindow` bound to the activity. `sapp_create_window()` / plural-window getters → invalid handle + log (like web/iOS). Map window #0 to `_sapp.android.current.window`. |
| **Alternate 3D API** | GLES3 only. No Metal/Vulkan/EAGL branch exists in the Android backend — nothing dead to skip (contrast iOS). |
| **SUSPENDED / RESUMED dispatch** | Fired by sokol (§3) but dropped by TrussC's `_event_cb` (no case). Keep firing. |

---

## 11. TrussC-side wiring (verified against the repo)

**THE MIGRATION IN ONE LINE:** `core/platform/android/sokol_impl.cpp` currently does
`#define SOKOL_IMPL` + `#include "sokol_app.h"` (full upstream impl, incl. its
`ANativeActivity_onCreate`). The Android leg = give `sokol_app_tc.h` an `#elif
defined(__ANDROID__)` backend that **exports `ANativeActivity_onCreate`**, then switch
this TU to the decls-only + `SOKOL_APP_TC_IMPL` shim used by every desktop TU.

### The migration shape (mirror mac/win/linux desktop TUs)
Desktop `sokol_impl.*` (verified mac/win/linux) use:
```c
#include "sokol_app.h"          // declarations only (NO SOKOL_IMPL)
#define SOKOL_IMPL
#define SOKOL_APP_TC_IMPL
#include "sokol_app_tc.h"       // the real _sapp_tc implementation
```
Android's `sokol_impl.cpp` must adopt the same pattern **but must NOT define
`SOKOL_NO_ENTRY`** (2352 errors on it; desktop TUs define it to suppress sokol's `main`).
The Android tc.h branch supplies `ANativeActivity_onCreate` instead of a `main`. The
existing `sokol_main` definition + `extern int main()` bridge in `sokol_impl.cpp` (§1)
**stays** — sokol_app_tc.h only *declares* `sokol_main`.
- Current header comment already documents the constraint: *"SOKOL_NO_ENTRY is NOT
  defined on Android. sokol_app.h provides ANativeActivity_onCreate() … sapp_run() is NOT
  supported."* Keep and update it to say `sokol_app_tc.h`.

### `TrussC.h`
- `#define SOKOL_NO_ENTRY` (15) — but not passed to the Android sokol impl TU (§1).
- `runApp<App>` `__ANDROID__` specialisation stores `g_androidDesc` and returns without
  `sapp_run` (2642–2653); `sokol_main` returns it.
- `_event_cb` (2253–2503) handles TOUCHES (2392–2440) + RESIZED (2463); **no
  SUSPENDED/RESUMED** (§3).

### Build system
- `core/CMakeLists.txt`: `elseif(ANDROID)` → `TC_PLATFORM_ANDROID`, backend `GLES3`
  (56–60); `SOKOL_GLES3` defined PUBLIC (193, 308, 318 — `elseif(TC_PLATFORM_ANDROID)`).
  No Metal/Vulkan anywhere on Android.
- `core/cmake/trussc_app.cmake`: `if(ANDROID)` builds a `.so`, then packages an APK
  (736+); manifest from `core/resources/android/AndroidManifest.xml.in` (or project/addon
  override, 764–771); collects `android/java` + `android/res` from the app and every
  addon (780–811). Hot-reload forced off on Android (156–161).
- **API level 26 hardcode (verified):** `AndroidManifest.xml.in:7`
  `android:minSdkVersion="26" targetSdkVersion="35"`; `tools/src/ProjectGenerator.cpp:414`
  `androidPreset["cacheVariables"]["ANDROID_PLATFORM"] = "android-26"`. So the compile
  `__ANDROID_API__` is 26 by default → **the `>= 29` Choreographer path (§4) and the
  `frame_callback` are compiled out on a stock TrussC build**; the poll-loop fallback is
  what actually runs unless the project bumps `ANDROID_PLATFORM`. (MEMORY:
  `project_android_api_level` — bumping to 31 blocks on AMidi etc.; the density and BACK
  patches work at 26.) The port must keep both the `#if __ANDROID_API__ >= 29` guards and
  the fallback intact so a future API bump lights up the choreographer path.
- **Manifest**: `android.app.NativeActivity` with `android.app.lib_name` = the app's
  `.so`; `configChanges="orientation|screenSize|screenLayout|keyboardHidden"` (activity
  survives rotation — §5); `glEsVersion=0x00030000`.

### CI
`.github/workflows/build.yml` has a **`build-android` job (239)** — NDK **r29** (267),
caches `core/build-android`. So the port is **compile-tested in CI** (unlike iOS). CI
does not run on a device; runtime behaviour is verified on the user's Android hardware
(§12d).

**Used and must be preserved on Android:** `ANativeActivity_onCreate` export → app-thread
loop + message pipe; the `sokol_main`→`main()`→`runApp`→`g_androidDesc` entry chain; EGL
init/surface lifecycle + APP_CMD state machine; Choreographer frame loop **and** poll
fallback; per-frame `update_dimensions` with the AConfiguration DPI patch (§5) and
`setBuffersGeometry` half-res; `skip_present` early-return; touch events (identifier +
changed + tooltype); **BACK-key no-shutdown patch (§7)**; `sapp_android_get_native_activity`
no-valid-assert getter; `sapp_show_keyboard` → soft input; SUSPENDED/RESUMED firing.

**Not applicable / gaps on Android:** §10 table; `sapp_keyboard_shown()` always false
(§8); SUSPENDED/RESUMED dropped by TrussC (§3); Choreographer path compiled out at API 26
(§11 build); orientation/immersive live in TrussC-JNI, not sokol (§8).

---

## 12. PORT CHECKLIST (`sokol_app_tc.h` `#elif defined(__ANDROID__)` section)

### (a) Lift verbatim (line ranges in sokol_app.h → rename `_sapp`→`_sapp_tc`, `_sapp_*`→`_sapp_tc_*`)
1. **Platform detect + `#error` guards** — 2346–2354 (GLES3 required; **SOKOL_NO_ENTRY
   forbidden** — keep this).
2. **Includes** — 2520–2531 (pthread/unistd/time, native_activity, **configuration.h
   [TrussC]**, looper, `#if __ANDROID_API__>=29 choreographer.h`, EGL, GLES3).
3. **Log items** — 1795–1804 (NativeActivity events + `ANDROID_CHOREOGRAPHER_ENABLED/
   UNAVAILABLE` TrussC items).
4. **State structs** — 3027–3074 (msg enum, pt, resources, `_sapp_android_t` incl. the
   `#if __ANDROID_API__>=29 choreographer` fields) + embed 3311–3312.
5. **EGL** — `init_egl` 10322–10386, `cleanup_egl` 10388–10402, `init_egl_surface`
   10404–10424 (keep `gl.framebuffer` read 10422), `cleanup_egl_surface` 10426–10435.
6. **`app_event`** 10437–10442.
7. **`update_dimensions`** 10444–10503 — keep the **AConfiguration DPI patch (10482–10497)**
   and the `setBuffersGeometry` half-res / artefact note.
8. **`cleanup` / `shutdown`** 10505–10521.
9. **`_sapp_android_frame`** 10523–10533 — keep the **`skip_present` early-return
   (10530–10531)**.
10. **Touch** 10535–10587 (identifier, tooltype, changed) + **key/BACK-no-shutdown patch
    10589–10605** (do NOT reintroduce `_sapp_android_shutdown`).
11. **Input/main callbacks** — `input_cb` 10607–10627, `main_cb` (APP_CMD state machine)
    10629–10718, `should_update` 10720–10724.
12. **Choreographer callback** 10726–10741 (`#if __ANDROID_API__ >= 29`).
13. **`show_keyboard`** 10743–10751.
14. **Loop** 10753–10829 — keep BOTH the choreographer path (10765–10797) and the poll
    fallback (10799–10809), and the "heap corruption on ALooper_removeFd" comment
    (10817–10819, leave the removal commented out).
15. **UI-thread bridge + NativeActivity callbacks** 10832–10932.
16. **`on_destroy` + `exit(0)`** 10934–10965.
17. **`ANativeActivity_onCreate`** 10967–11036 — the exported entry (double `activity=`,
    thread spawn, MSG_CREATE handshake, callback registration; keep the "NOT A BUG: do NOT
    call sapp_discard_state()" comment).
18. **Public API Android branches** — already present in the tc.h public block; wire them:
    `sapp_egl_get_display/context` (sokol_app.h 14064–14084 → tc.h 3079–3092/11244–11257),
    `sapp_show_keyboard` (14086–14094 → tc.h 3102–3103/11268), `sapp_keyboard_shown`
    (14096–14098), `sapp_android_get_native_activity` (14582–14590 → tc.h 3593–3597),
    `sapp_skip_present` (14596–14599). desc GLES3 minor=1 (3537–3543 → tc.h 2225–2227/
    9341–9343).

### (b) Adapt to the tc window model (single window = window #0)
1. **`sapp_create_window()` + plural-window APIs → invalid handle + log** (like web/iOS).
   The activity owns exactly one `ANativeWindow`.
2. **`sapp_window_*` getters** → window #0 returns real values
   (`_sapp_tc.android.current.window`, framebuffer size, dpi_scale); other index → 0 + log.
3. **The "window handle" is `_sapp_tc.android.current.window`** (`ANativeWindow*`);
   there is no HWND/UIWindow analogue beyond `sapp_android_get_native_activity()`.
4. **DPI is single, display-wide** (`AConfiguration_getDensity/160`, §5). No per-window DPI.
5. **Plain C++ — NO ObjC `#error` guard** (contrast mac/iOS).
6. **The owned loop lives on the spawned app thread** (`_sapp_android_loop`), driven by
   Choreographer or `ALooper_pollOnce`. The process entry is `ANativeActivity_onCreate`,
   NOT `main`/`sapp_run` — this branch is the ONLY one that exports an activity entry
   point and does NOT provide a `sapp_run`-driven `main`.

### (c) Known hazards
1. **App thread ≠ Android UI thread ≠ where `sokol_main`/`runApp` ran** (§2). Don't assume
   TrussC "main thread" identity is fixed at `runApp` (that's the UI thread); it must be
   established on the app thread (setup/first frame). JNI from the app thread needs an
   attach.
2. **`SOKOL_NO_ENTRY` must be absent** from the Android impl TU, and the tc.h Android
   branch must **export `ANativeActivity_onCreate`** and keep the `#error` on
   SOKOL_NO_ENTRY. If the port accidentally guards onCreate behind `!SOKOL_NO_ENTRY` (as
   sokol does for desktop `main`), the app has no entry point and won't launch.
3. **BACK-key no-shutdown (§7)** — kiosk/screen-pinning depends on it. Do NOT let the
   verbatim lift silently reintroduce upstream's `_sapp_android_shutdown()` on BACK.
4. **Choreographer path is compiled out at API 26** (§11) — verify the poll fallback still
   renders; do not delete it as "dead" (a future `ANDROID_PLATFORM` bump enables the
   choreographer path).
5. **`exit(0)` in `on_destroy`** — no C++ static dtors run after it; consistent with
   TrussC's leak-GPU-singletons policy. Don't add teardown that must run post-exit.
6. **Surface can vanish mid-lifecycle** — every `_sapp_android_frame` asserts a valid
   surface; the render gate `should_update()` is what prevents drawing without one. Keep
   the gate; don't render on RESUME before SET_NATIVE_WINDOW re-creates the surface.
7. **DPI density patch + fb/win fallback** (§5) — both branches; density can be 0.
8. **`sapp_keyboard_shown()` never true** (§8) — a pre-existing quirk, not a port bug.

### (d) Device-verification checklist (Android IS compile-tested in CI; verify runtime on the user's device)
CI's `build-android` only compiles. On a real Android device, before calling the port done:
- [ ] App launches (NativeActivity → onCreate → app thread → EGL surface → `setup()`),
      renders, holds framerate; correct colors/orientation.
- [ ] **Touch**: multi-touch works; began/moved/ended/cancelled reach app; `identifier`
      stable across a drag; coordinates correct at the device's density; `android_tooltype`
      distinguishes finger/stylus if available.
- [ ] **BACK key**: pressing BACK does **not** kill or freeze the app (event consumed, EGL
      surface intact) — the whole point of §7.
- [ ] **Rotation / config change**: rotate → RESIZED fires, framebuffer resizes, activity
      is NOT recreated (manifest `configChanges`), no artefacts.
- [ ] **Home / recents / resume**: background then foreground → SET_NATIVE_WINDOW(NULL)+
      PAUSE stops frames and destroys the surface; resume recreates the surface and frames
      resume; no crash, no black frame, RESIZED fires on return.
- [ ] **Immersive mode**: `setImmersiveMode(true)` hides status/nav bars (JNI, §8);
      `false` restores.
- [ ] **Orientation lock**: `setOrientation(...)` (JNI `setRequestedOrientation`) actually
      constrains rotation.
- [ ] **Soft keyboard**: tcxImGui text field raises the keyboard via `sapp_show_keyboard`.
- [ ] **Screen pinning / kiosk (lock-task)**: enter screen pinning, press BACK/HOME →
      app survives (no frozen last-frame), proving the BACK patch under the OS's blocked
      `ANativeActivity_finish`.
- [ ] **DPI**: `sapp_dpi_scale()` ≈ device density/160 (e.g. xxhdpi ≈ 3.0), consistent
      with desktop semantics.
- [ ] **Task-swipe exit**: swipe the app away → onDestroy → clean `exit(0)`, relaunch works.
- [ ] **(If ANDROID_PLATFORM bumped ≥29)** Choreographer path engages (log
      `ANDROID_CHOREOGRAPHER_ENABLED`), vsync-paced frames.

---

## Appendix: function → line index (Android GLES3-relevant, sokol_app.h)

| symbol | line |
|---|---|
| Android platform detect (`_SAPP_ANDROID` @2348; GLES3 `#error` @2350; **NO_ENTRY `#error` @2352**) | 2346–2354 |
| Android includes (`configuration.h` [TrussC] @2525; choreographer `#if>=29` @2527) | 2520–2531 |
| Log items (Choreographer ENABLED/UNAVAILABLE @1803–1804) | 1795–1804 |
| posix/CLOCK_MONOTONIC timestamp init / now | 2586–2596 / 2635–2639 |
| `_sapp_android_msg_t` enum / pt / resources / `_sapp_android_t` (choreographer @3068) | 3028 / 3039 / 3047 / 3052 |
| `_sapp_android_t android;` embed | 3311 |
| desc defaults GLES3 minor=1 (Android/Linux) | 3537–3543 |
| `_sapp_android_init_egl` (ES3.1 ctx @10372) | 10322 |
| `_sapp_android_cleanup_egl` | 10388 |
| `_sapp_android_init_egl_surface` (gl.framebuffer @10422) | 10404 |
| `_sapp_android_cleanup_egl_surface` | 10426 |
| `_sapp_android_app_event` | 10437 |
| `_sapp_android_update_dimensions` (setBuffersGeometry @10468; **DPI patch @10482–10497**; RESIZED @10500) | 10444 |
| `_sapp_android_cleanup` / `_shutdown` | 10505 / 10516 |
| `_sapp_android_frame` (**skip_present @10530–10531**; eglSwapBuffers @10532) | 10523 |
| `_sapp_android_touch_event` (tooltype @10577; changed @10578) | 10535 |
| `_sapp_android_key_event` (**BACK no-shutdown @10593–10603**) | 10589 |
| `_sapp_android_input_cb` (AInputQueue drain) | 10607 |
| `_sapp_android_main_cb` (**APP_CMD state machine**) | 10629 |
| `_sapp_android_should_update` (render gate) | 10720 |
| `_sapp_android_frame_callback` (Choreographer, `#if>=29`) | 10726 |
| `_sapp_android_show_keyboard` | 10743 |
| `_sapp_android_loop` (choreographer path @10784; poll fallback @10799) | 10753 |
| `_sapp_android_msg` (UI→app write) | 10832 |
| NativeActivity callbacks (on_start/resume/pause/stop/focus/window/input/lowmem) | 10838–10932 |
| `_sapp_android_on_destroy` (**exit(0) @10964**) | 10934 |
| **`ANativeActivity_onCreate`** (sokol_main @10979; thread spawn @10997; callbacks @11016) | 10967 |
| `#endif /* _SAPP_ANDROID */` | 11038 |
| `sapp_egl_get_display` / `_context` (Android branch) | 14064 / 14075 |
| `sapp_show_keyboard` (Android @14089) / `sapp_keyboard_shown` | 14086 / 14096 |
| `sapp_toggle_fullscreen` (no Android branch → no-op) | 14104 |
| `sapp_dpi_scale` | 14060 |
| `sapp_android_get_native_activity` (no valid-assert) | 14582 |
| `sapp_skip_present` (TrussC) | 14596 |
| **tc.h port target:** top-level branch chain (apple @198, win @3670, linux @5650, emsc @8917, stub @11818); android public branches already present @3079–3103 / 11244–11268 / 3593–3597 | sokol_app_tc.h |
