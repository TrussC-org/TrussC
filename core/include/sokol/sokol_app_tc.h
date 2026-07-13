#if defined(SOKOL_APP_TC_IMPL) && !defined(SOKOL_APP_TC_IMPL_INCLUDED)
#define SOKOL_APP_TC_IMPL_INCLUDED (1)
#endif
#ifndef SOKOL_APP_TC_INCLUDED
/*
    sokol_app_tc.h -- multi-window extension for sokol_app.h

    Project URL: https://github.com/TrussC-org/TrussC
    Fork lineage: sokol_app.h by Andre Weissflog (https://github.com/floooh/sokol)
    License: zlib/libpng (same as sokol_app.h, see end of file)

    Do this:
        #define SOKOL_APP_TC_IMPL
    before you include this file in *one* C++/Objective-C++ source file to
    create the implementation. Include AFTER sokol_app.h (this header reuses
    sapp_event / sapp_keycode / SAPP_* constants so that events from any
    window flow through the exact same handling code as the main window's).

    WHAT IT IS
    ==========
    Native multi-window support with per-window vsync ticks. On macOS,
    Windows and Linux this header additionally IMPLEMENTS the public
    single-window sokol_app.h API (sapp_run, sapp_width, sapp_dpi_scale, ...):
    the "main window" is simply window #0, created from the sapp_desc, and the
    app's OS run loop is owned here. sokol_app.h must then be included WITHOUT
    its implementation (declarations only) in the implementation TU:

        #include "sokol_app.h"        // no SOKOL_IMPL / SOKOL_APP_IMPL here
        #define SOKOL_IMPL
        #define SOKOL_APP_TC_IMPL
        #include "sokol_app_tc.h"     // implements the sapp_* API
        #include "sokol_gfx.h"
        ...

    The single-window API behaves like sokol_app.h's native backends (the
    cancellable quit dance, RGB10A2 default color format, clipboard,
    drag&drop, cursor control, fullscreen; macOS: lazy drawable acquisition
    in sapp_get_swapchain(); Windows: skip_present honored on Present,
    per-monitor-v2 DPI), with one deliberate deviation: calling
    sapp_dpi_scale() before sapp_run() returns the OS display scale
    instead of 0.

    What replaces sokol_app.h's Windows render loop is the same per-window
    tick idea: instead of the upstream PeekMessage busy-render-loop (and
    instead of a ~64Hz WM_TIMER), every window's DXGI swapchain is created
    with DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT and the message
    loop blocks in MsgWaitForMultipleObjectsEx on the per-window frame
    latency waitable objects: each window ticks when ITS swapchain can
    accept a frame (true vsync pacing per display), and the loop sleeps
    otherwise. A tick that ends without a Present (skip_present, occluded,
    minimized) holds its waitable "credit" and is re-paced by a timer until
    the next real Present, so neither busy-spinning nor waitable starvation
    can occur.

    The multi-window API shape follows floooh's multi-window sketch (sokol
    PR #437 / issue #229): a small `sapp_window` handle, a `sapp_window_desc`,
    and window-parameterized query functions. What is NEW compared to #437 is
    the frame model, and it is the load-bearing idea:

      - There is no shared frame loop that iterates windows. Each window has
        its OWN vsync source (macOS: one CADisplayLink per window, delivered
        on the main run loop) which invokes the window's `tick_cb`.
      - A fully occluded window's display link suspends, and an explicit
        occlusion gate guards drawable acquisition. The `[CAMetalLayer
        nextDrawable]` ~1s stall that killed PR #437 cannot occur, because
        the acquiring code path is only ever entered for a visible window.
        An occluded window simply stops ticking (fps 0) and resumes on
        un-occlusion; SAPP_EVENTTYPE_SUSPENDED / _RESUMED are sent at the
        transitions.
      - Windows on displays with different refresh rates each tick at their
        own display's rate. Tick coalescing is depth-1 per window by
        construction (the run loop is the queue and a display link never
        stacks callbacks).

    Rendering: all windows share the one sokol_gfx context (post-2024
    sokol_gfx takes the swapchain per-pass). During `tick_cb`, acquire this
    window's swapchain handles via sapp_window_metal_current_drawable() /
    ..._depth_stencil_texture() / ..._msaa_color_texture() and start a pass
    with them. The drawable is acquired lazily right before tick_cb and is
    valid only for that callback.

    Events: delivered as plain sapp_event through the desc's `event_cb`,
    with the owning window passed alongside (sapp_event itself has no window
    field while we coexist with upstream sokol_app.h). Keyboard uses the
    same keycode translation table as sokol_app.h, so SAPP_KEYCODE_* values
    are identical across the main window and secondary windows.

    Coordinates follow sokol_app.h conventions: mouse positions arrive in
    framebuffer pixels (divide by sapp_window_dpi_scale() for logical
    points). dpi_scale is derived from ONE source (the ratio actually used
    to size the layer's drawable), never assumed to be 2.

    Everything runs on the main thread. All functions must be called from
    the main thread.

    PLATFORM SUPPORT
    ================
    macOS: implemented (NSWindow + CAMetalLayer + CADisplayLink, macOS 14+).
    Windows: implemented (HWND + D3D11 + DXGI flip swapchain, one frame
    latency waitable object per window; Windows 10+).
    Linux: implemented (X11 + GLX, one shared GL context with a GLXWindow
    drawable per window; main window paced by its blocking vsync'd
    glXSwapBuffers, secondary windows swap interval 0 + timer-paced).
    Others (web/mobile): stubs that return an invalid handle (explicit
    platform gap).
*/
#define SOKOL_APP_TC_INCLUDED (1)
#include <stdint.h>
#include <stdbool.h>

#if !defined(SOKOL_APP_INCLUDED)
#error "Please include sokol_app.h before sokol_app_tc.h"
#endif

#if defined(__cplusplus)
extern "C" {
#endif

/* an opaque window handle (0 is the invalid handle; the sokol_app.h main
   window is NOT addressable through this API while both coexist) */
typedef struct sapp_window { uint32_t id; } sapp_window;

typedef struct sapp_window_desc {
    int x, y;                   /* top-left position in screen points (-1 = default) */
    int width, height;          /* content size in logical points (0 = 640x480) */
    const char* title;
    bool borderless;            /* no title bar / decorations */
    bool no_high_dpi;           /* true: framebuffer = logical size (dpi_scale 1) */
    int sample_count;           /* MSAA sample count (0 = 1) */
    const void* mtl_device;     /* macOS: the id<MTLDevice> sokol_gfx renders with
                                   (Windows: unused; the shared D3D11 device is owned here) */

    /* callbacks -- all invoked on the main thread */
    void (*tick_cb)(sapp_window win, void* user);   /* this window's vsync; only while visible */
    void (*event_cb)(const sapp_event* ev, sapp_window win, void* user);
    void (*close_cb)(sapp_window win, void* user);  /* user closed the window; call sapp_destroy_window() to accept */
    void* user_data;
} sapp_window_desc;

SOKOL_APP_API_DECL sapp_window sapp_create_window(const sapp_window_desc* desc);
SOKOL_APP_API_DECL void sapp_destroy_window(sapp_window win);
SOKOL_APP_API_DECL bool sapp_window_valid(sapp_window win);

/* geometry (valid whenever the window is alive) */
SOKOL_APP_API_DECL int sapp_window_width(sapp_window win);               /* logical points */
SOKOL_APP_API_DECL int sapp_window_height(sapp_window win);
SOKOL_APP_API_DECL int sapp_window_framebuffer_width(sapp_window win);   /* pixels */
SOKOL_APP_API_DECL int sapp_window_framebuffer_height(sapp_window win);
SOKOL_APP_API_DECL float sapp_window_dpi_scale(sapp_window win);         /* fb / logical, ONE source */
SOKOL_APP_API_DECL int sapp_window_sample_count(sapp_window win);
SOKOL_APP_API_DECL bool sapp_window_occluded(sapp_window win);
SOKOL_APP_API_DECL void sapp_window_set_title(sapp_window win, const char* title);

/* measured interval between this window's ticks in seconds (its display's
   refresh period; a best-effort estimate before the second tick) */
SOKOL_APP_API_DECL double sapp_window_frame_duration(sapp_window win);

/* Metal swapchain handles -- valid ONLY inside this window's tick_cb */
SOKOL_APP_API_DECL const void* sapp_window_metal_current_drawable(sapp_window win);
SOKOL_APP_API_DECL const void* sapp_window_metal_depth_stencil_texture(sapp_window win);
SOKOL_APP_API_DECL const void* sapp_window_metal_msaa_color_texture(sapp_window win);

/* D3D11 swapchain handles -- valid ONLY inside this window's tick_cb
   (render_view is the MSAA target and resolve_view the backbuffer when
   sample_count > 1; otherwise render_view is the backbuffer and
   resolve_view is 0 -- same contract as sapp_swapchain.d3d11) */
SOKOL_APP_API_DECL const void* sapp_window_d3d11_render_view(sapp_window win);
SOKOL_APP_API_DECL const void* sapp_window_d3d11_resolve_view(sapp_window win);
SOKOL_APP_API_DECL const void* sapp_window_d3d11_depth_stencil_view(sapp_window win);

/* GL swapchain handle -- the default-framebuffer id (Linux/GLX: rendering
   targets the current drawable, which is this window's during its tick_cb) */
SOKOL_APP_API_DECL uint32_t sapp_window_gl_framebuffer(sapp_window win);

/* native escape hatches (macOS: NSWindow*; Windows: HWND; Linux: X11 Window XID) */
SOKOL_APP_API_DECL const void* sapp_window_macos_get_window(sapp_window win);
SOKOL_APP_API_DECL const void* sapp_window_win32_get_hwnd(sapp_window win);
SOKOL_APP_API_DECL const void* sapp_window_x11_get_window(sapp_window win);

#if defined(__cplusplus)
} /* extern "C" */
#endif
#endif /* SOKOL_APP_TC_INCLUDED */

/*=== IMPLEMENTATION =========================================================*/
#if defined(SOKOL_APP_TC_IMPL_INCLUDED) && !defined(SOKOL_APP_TC_IMPL_DONE)
#define SOKOL_APP_TC_IMPL_DONE (1)

#include <string.h>
#include <stdlib.h>
#include <math.h>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(__APPLE__) && TARGET_OS_OSX
/*== macOS ==================================================================*/
#if !defined(__OBJC__)
#error "sokol_app_tc.h macOS implementation must be compiled as Objective-C++ (.mm)"
#endif
#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CADisplayLink.h>

#if defined(SOKOL_APP_IMPL_INCLUDED)
#error "sokol_app_tc.h owns the macOS implementation of the sapp_* API; include sokol_app.h WITHOUT an implementation define in this TU"
#endif

@class _sapp_tc_view;
@class _sapp_tc_win_delegate;
@class _sapp_tc_app_delegate;

typedef struct _sapp_tc_window_t {
    uint32_t win_id;
    sapp_window_desc desc;
    bool is_main;                       /* window #0: created by sapp_run, drives the app callbacks */
    NSWindow* window;
    _sapp_tc_view* view;
    _sapp_tc_win_delegate* delegate;
    CAMetalLayer* layer;
    CADisplayLink* link;
    MTLPixelFormat color_fmt;           /* layer + MSAA color texture format */
    id<MTLTexture> depth_tex;           /* depth-stencil, drawable-sized */
    id<MTLTexture> msaa_tex;            /* MSAA color (sample_count > 1) */
    id<CAMetalDrawable> frame_drawable; /* valid during one tick only (main: lazily acquired) */
    int fb_width, fb_height;            /* aux/drawable size (pixels) */
    int win_width, win_height;          /* logical points (last seen) */
    float dpi_scale;
    double frame_duration;              /* measured tick interval */
    double last_tick_time;              /* CADisplayLink timestamp */
    bool occluded;
    bool in_tick;
} _sapp_tc_window_t;

#define _SAPP_TC_MAX_WINDOWS (32)

/* EMA-filtered frame timer, port of sokol_app.h's _sapp_timing_* — used only
   while the main window ticks from the occluded-fallback NSTimer (the display
   link timestamps are the source whenever the link is active) */
typedef struct {
    double last;        /* previous sample time, 0 = none yet */
    double ema;
    double smooth_dt;
    double dt;          /* last raw (clamped) delta */
} _sapp_tc_timing_t;

static struct {
    bool keytable_valid;
    sapp_keycode keycodes[SAPP_MAX_KEYCODES];
    uint32_t next_id;
    _sapp_tc_window_t* windows[_SAPP_TC_MAX_WINDOWS];

    /* ---- app / main-window state (this header owns sapp_run on macOS) ---- */
    struct {
        sapp_desc desc;                 /* sanitized copy; window_title points at the buffer below */
        char window_title[256];
        bool valid;
        bool init_called;
        bool cleanup_called;
        bool quit_requested;
        bool quit_ordered;
        bool fullscreen;
        bool event_consumed;
        bool skip_present;              /* stored for API parity; ignored on Metal (as upstream) */
        uint64_t frame_count;
        _sapp_tc_window_t* main;
        _sapp_tc_app_delegate* dlg;
        id keyup_monitor;               /* Cmd+key-up workaround (Cocoa swallows those) */
        id<MTLDevice> device;
        NSTimer* fallback_timer;        /* ~60Hz tick source while the main window is occluded */
        _sapp_tc_timing_t timing;
        /* mouse cursor */
        bool mouse_shown;
        sapp_mouse_cursor current_cursor;
        NSCursor* standard_cursors[_SAPP_MOUSECURSOR_NUM];
        NSCursor* custom_cursors[_SAPP_MOUSECURSOR_NUM];
        bool custom_cursor_bound[_SAPP_MOUSECURSOR_NUM];
        /* clipboard */
        bool clipboard_enabled;
        int clipboard_size;
        char* clipboard;
        /* drag & drop */
        bool drop_enabled;
        int drop_max_files;
        int drop_max_path_length;
        int drop_num_files;
        char* drop_buffer;
    } app;
} _sapp_tc;

static void _sapp_tc_main_tick(CADisplayLink* link);
static void _sapp_tc_update_main_dimensions(bool allow_event);
static void _sapp_tc_apply_cursor(sapp_mouse_cursor cursor, bool shown);

static void _sapp_tc_timing_reset(_sapp_tc_timing_t* t) {
    t->last = 0.0;
    t->ema = t->smooth_dt = t->dt = 1.0 / 60.0;
}

static double _sapp_tc_timing_clamp(double dt) {
    if (dt < 0.000001) return 0.000001;
    if (dt > 0.1) return 0.1;
    return dt;
}

static void _sapp_tc_timing_update(_sapp_tc_timing_t* t) {
    const double now = CACurrentMediaTime();
    if (t->last > 0.0) {
        double dt = _sapp_tc_timing_clamp(now - t->last);
        t->dt = dt;
        /* big jump: reset the filter; otherwise EMA-smooth */
        if (fabs(dt - t->smooth_dt) > 0.004) {
            t->ema = t->smooth_dt = dt;
        } else {
            t->ema += 0.025 * (dt - t->ema);
            t->smooth_dt = _sapp_tc_timing_clamp(t->ema);
        }
    }
    t->last = now;
}

/* keycode table lifted verbatim from sokol_app.h's _sapp_macos_init_keytable()
   so SAPP_KEYCODE_* values are identical across main and secondary windows */
static void _sapp_tc_init_keytable(void) {
    if (_sapp_tc.keytable_valid) return;
    _sapp_tc.keytable_valid = true;
    _sapp_tc.keycodes[0x1D] = SAPP_KEYCODE_0;
    _sapp_tc.keycodes[0x12] = SAPP_KEYCODE_1;
    _sapp_tc.keycodes[0x13] = SAPP_KEYCODE_2;
    _sapp_tc.keycodes[0x14] = SAPP_KEYCODE_3;
    _sapp_tc.keycodes[0x15] = SAPP_KEYCODE_4;
    _sapp_tc.keycodes[0x17] = SAPP_KEYCODE_5;
    _sapp_tc.keycodes[0x16] = SAPP_KEYCODE_6;
    _sapp_tc.keycodes[0x1A] = SAPP_KEYCODE_7;
    _sapp_tc.keycodes[0x1C] = SAPP_KEYCODE_8;
    _sapp_tc.keycodes[0x19] = SAPP_KEYCODE_9;
    _sapp_tc.keycodes[0x00] = SAPP_KEYCODE_A;
    _sapp_tc.keycodes[0x0B] = SAPP_KEYCODE_B;
    _sapp_tc.keycodes[0x08] = SAPP_KEYCODE_C;
    _sapp_tc.keycodes[0x02] = SAPP_KEYCODE_D;
    _sapp_tc.keycodes[0x0E] = SAPP_KEYCODE_E;
    _sapp_tc.keycodes[0x03] = SAPP_KEYCODE_F;
    _sapp_tc.keycodes[0x05] = SAPP_KEYCODE_G;
    _sapp_tc.keycodes[0x04] = SAPP_KEYCODE_H;
    _sapp_tc.keycodes[0x22] = SAPP_KEYCODE_I;
    _sapp_tc.keycodes[0x26] = SAPP_KEYCODE_J;
    _sapp_tc.keycodes[0x28] = SAPP_KEYCODE_K;
    _sapp_tc.keycodes[0x25] = SAPP_KEYCODE_L;
    _sapp_tc.keycodes[0x2E] = SAPP_KEYCODE_M;
    _sapp_tc.keycodes[0x2D] = SAPP_KEYCODE_N;
    _sapp_tc.keycodes[0x1F] = SAPP_KEYCODE_O;
    _sapp_tc.keycodes[0x23] = SAPP_KEYCODE_P;
    _sapp_tc.keycodes[0x0C] = SAPP_KEYCODE_Q;
    _sapp_tc.keycodes[0x0F] = SAPP_KEYCODE_R;
    _sapp_tc.keycodes[0x01] = SAPP_KEYCODE_S;
    _sapp_tc.keycodes[0x11] = SAPP_KEYCODE_T;
    _sapp_tc.keycodes[0x20] = SAPP_KEYCODE_U;
    _sapp_tc.keycodes[0x09] = SAPP_KEYCODE_V;
    _sapp_tc.keycodes[0x0D] = SAPP_KEYCODE_W;
    _sapp_tc.keycodes[0x07] = SAPP_KEYCODE_X;
    _sapp_tc.keycodes[0x10] = SAPP_KEYCODE_Y;
    _sapp_tc.keycodes[0x06] = SAPP_KEYCODE_Z;
    _sapp_tc.keycodes[0x27] = SAPP_KEYCODE_APOSTROPHE;
    _sapp_tc.keycodes[0x2A] = SAPP_KEYCODE_BACKSLASH;
    _sapp_tc.keycodes[0x2B] = SAPP_KEYCODE_COMMA;
    _sapp_tc.keycodes[0x18] = SAPP_KEYCODE_EQUAL;
    _sapp_tc.keycodes[0x32] = SAPP_KEYCODE_GRAVE_ACCENT;
    _sapp_tc.keycodes[0x21] = SAPP_KEYCODE_LEFT_BRACKET;
    _sapp_tc.keycodes[0x1B] = SAPP_KEYCODE_MINUS;
    _sapp_tc.keycodes[0x2F] = SAPP_KEYCODE_PERIOD;
    _sapp_tc.keycodes[0x1E] = SAPP_KEYCODE_RIGHT_BRACKET;
    _sapp_tc.keycodes[0x29] = SAPP_KEYCODE_SEMICOLON;
    _sapp_tc.keycodes[0x2C] = SAPP_KEYCODE_SLASH;
    _sapp_tc.keycodes[0x0A] = SAPP_KEYCODE_WORLD_1;
    _sapp_tc.keycodes[0x33] = SAPP_KEYCODE_BACKSPACE;
    _sapp_tc.keycodes[0x39] = SAPP_KEYCODE_CAPS_LOCK;
    _sapp_tc.keycodes[0x75] = SAPP_KEYCODE_DELETE;
    _sapp_tc.keycodes[0x7D] = SAPP_KEYCODE_DOWN;
    _sapp_tc.keycodes[0x77] = SAPP_KEYCODE_END;
    _sapp_tc.keycodes[0x24] = SAPP_KEYCODE_ENTER;
    _sapp_tc.keycodes[0x35] = SAPP_KEYCODE_ESCAPE;
    _sapp_tc.keycodes[0x7A] = SAPP_KEYCODE_F1;
    _sapp_tc.keycodes[0x78] = SAPP_KEYCODE_F2;
    _sapp_tc.keycodes[0x63] = SAPP_KEYCODE_F3;
    _sapp_tc.keycodes[0x76] = SAPP_KEYCODE_F4;
    _sapp_tc.keycodes[0x60] = SAPP_KEYCODE_F5;
    _sapp_tc.keycodes[0x61] = SAPP_KEYCODE_F6;
    _sapp_tc.keycodes[0x62] = SAPP_KEYCODE_F7;
    _sapp_tc.keycodes[0x64] = SAPP_KEYCODE_F8;
    _sapp_tc.keycodes[0x65] = SAPP_KEYCODE_F9;
    _sapp_tc.keycodes[0x6D] = SAPP_KEYCODE_F10;
    _sapp_tc.keycodes[0x67] = SAPP_KEYCODE_F11;
    _sapp_tc.keycodes[0x6F] = SAPP_KEYCODE_F12;
    _sapp_tc.keycodes[0x69] = SAPP_KEYCODE_F13;
    _sapp_tc.keycodes[0x6B] = SAPP_KEYCODE_F14;
    _sapp_tc.keycodes[0x71] = SAPP_KEYCODE_F15;
    _sapp_tc.keycodes[0x6A] = SAPP_KEYCODE_F16;
    _sapp_tc.keycodes[0x40] = SAPP_KEYCODE_F17;
    _sapp_tc.keycodes[0x4F] = SAPP_KEYCODE_F18;
    _sapp_tc.keycodes[0x50] = SAPP_KEYCODE_F19;
    _sapp_tc.keycodes[0x5A] = SAPP_KEYCODE_F20;
    _sapp_tc.keycodes[0x73] = SAPP_KEYCODE_HOME;
    _sapp_tc.keycodes[0x72] = SAPP_KEYCODE_INSERT;
    _sapp_tc.keycodes[0x7B] = SAPP_KEYCODE_LEFT;
    _sapp_tc.keycodes[0x3A] = SAPP_KEYCODE_LEFT_ALT;
    _sapp_tc.keycodes[0x3B] = SAPP_KEYCODE_LEFT_CONTROL;
    _sapp_tc.keycodes[0x38] = SAPP_KEYCODE_LEFT_SHIFT;
    _sapp_tc.keycodes[0x37] = SAPP_KEYCODE_LEFT_SUPER;
    _sapp_tc.keycodes[0x6E] = SAPP_KEYCODE_MENU;
    _sapp_tc.keycodes[0x47] = SAPP_KEYCODE_NUM_LOCK;
    _sapp_tc.keycodes[0x79] = SAPP_KEYCODE_PAGE_DOWN;
    _sapp_tc.keycodes[0x74] = SAPP_KEYCODE_PAGE_UP;
    _sapp_tc.keycodes[0x7C] = SAPP_KEYCODE_RIGHT;
    _sapp_tc.keycodes[0x3D] = SAPP_KEYCODE_RIGHT_ALT;
    _sapp_tc.keycodes[0x3E] = SAPP_KEYCODE_RIGHT_CONTROL;
    _sapp_tc.keycodes[0x3C] = SAPP_KEYCODE_RIGHT_SHIFT;
    _sapp_tc.keycodes[0x36] = SAPP_KEYCODE_RIGHT_SUPER;
    _sapp_tc.keycodes[0x31] = SAPP_KEYCODE_SPACE;
    _sapp_tc.keycodes[0x30] = SAPP_KEYCODE_TAB;
    _sapp_tc.keycodes[0x7E] = SAPP_KEYCODE_UP;
    _sapp_tc.keycodes[0x52] = SAPP_KEYCODE_KP_0;
    _sapp_tc.keycodes[0x53] = SAPP_KEYCODE_KP_1;
    _sapp_tc.keycodes[0x54] = SAPP_KEYCODE_KP_2;
    _sapp_tc.keycodes[0x55] = SAPP_KEYCODE_KP_3;
    _sapp_tc.keycodes[0x56] = SAPP_KEYCODE_KP_4;
    _sapp_tc.keycodes[0x57] = SAPP_KEYCODE_KP_5;
    _sapp_tc.keycodes[0x58] = SAPP_KEYCODE_KP_6;
    _sapp_tc.keycodes[0x59] = SAPP_KEYCODE_KP_7;
    _sapp_tc.keycodes[0x5B] = SAPP_KEYCODE_KP_8;
    _sapp_tc.keycodes[0x5C] = SAPP_KEYCODE_KP_9;
    _sapp_tc.keycodes[0x45] = SAPP_KEYCODE_KP_ADD;
    _sapp_tc.keycodes[0x41] = SAPP_KEYCODE_KP_DECIMAL;
    _sapp_tc.keycodes[0x4B] = SAPP_KEYCODE_KP_DIVIDE;
    _sapp_tc.keycodes[0x4C] = SAPP_KEYCODE_KP_ENTER;
    _sapp_tc.keycodes[0x51] = SAPP_KEYCODE_KP_EQUAL;
    _sapp_tc.keycodes[0x43] = SAPP_KEYCODE_KP_MULTIPLY;
    _sapp_tc.keycodes[0x4E] = SAPP_KEYCODE_KP_SUBTRACT;
}

static sapp_keycode _sapp_tc_translate_key(int scancode) {
    if ((scancode >= 0) && (scancode < SAPP_MAX_KEYCODES)) {
        return _sapp_tc.keycodes[scancode];
    }
    return SAPP_KEYCODE_INVALID;
}

static _sapp_tc_window_t* _sapp_tc_lookup(sapp_window win) {
    if (win.id == 0) return 0;
    for (int i = 0; i < _SAPP_TC_MAX_WINDOWS; i++) {
        _sapp_tc_window_t* w = _sapp_tc.windows[i];
        if (w && w->win_id == win.id) return w;
    }
    return 0;
}

static uint32_t _sapp_tc_mods(NSEvent* ev) {
    const NSEventModifierFlags f = ev.modifierFlags;
    uint32_t m = 0;
    if (f & NSEventModifierFlagShift)   m |= SAPP_MODIFIER_SHIFT;
    if (f & NSEventModifierFlagControl) m |= SAPP_MODIFIER_CTRL;
    if (f & NSEventModifierFlagOption)  m |= SAPP_MODIFIER_ALT;
    if (f & NSEventModifierFlagCommand) m |= SAPP_MODIFIER_SUPER;
    return m;
}

static void _sapp_tc_send(_sapp_tc_window_t* w, sapp_event* ev) {
    if (!w->desc.event_cb) return;
    ev->frame_count = _sapp_tc.app.frame_count;
    ev->window_width = w->win_width;
    ev->window_height = w->win_height;
    ev->framebuffer_width = w->fb_width;
    ev->framebuffer_height = w->fb_height;
    sapp_window handle = { w->win_id };
    w->desc.event_cb(ev, handle, w->desc.user_data);
}

/*-- main window: event routing to the sapp_desc callbacks ------------------*/
static bool _sapp_tc_app_events_enabled(void) {
    /* same gate as sokol_app.h: nothing fires before the first tick's init_cb */
    return _sapp_tc.app.init_called &&
        (_sapp_tc.app.desc.event_cb || _sapp_tc.app.desc.event_userdata_cb);
}

static void _sapp_tc_main_event_tramp(const sapp_event* ev, sapp_window win, void* user) {
    (void)win; (void)user;
    if (!_sapp_tc_app_events_enabled()) return;
    _sapp_tc.app.event_consumed = false;
    if (_sapp_tc.app.desc.event_cb) {
        _sapp_tc.app.desc.event_cb(ev);
    } else {
        _sapp_tc.app.desc.event_userdata_cb(ev, _sapp_tc.app.desc.user_data);
    }
}

/*-- mouse cursor (app-global, same model as sokol_app.h) -------------------*/
/* private-but-stable AppKit cursor selectors (same set sokol_app/GLFW use) */
@interface NSCursor(_sapp_tc_private_cursors)
+ (NSCursor*)_windowResizeEastWestCursor;
+ (NSCursor*)_windowResizeNorthSouthCursor;
+ (NSCursor*)_windowResizeNorthWestSouthEastCursor;
+ (NSCursor*)_windowResizeNorthEastSouthWestCursor;
@end

static NSCursor* _sapp_tc_priv_cursor(SEL sel, NSCursor* fallback) {
    if ([NSCursor respondsToSelector:sel]) {
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Warc-performSelector-leaks"
        NSCursor* cur = [NSCursor performSelector:sel];
        #pragma clang diagnostic pop
        if (cur) return cur;
    }
    return fallback;
}

static void _sapp_tc_init_cursors(void) {
    _sapp_tc.app.standard_cursors[SAPP_MOUSECURSOR_DEFAULT] = nil;  /* uses arrowCursor */
    _sapp_tc.app.standard_cursors[SAPP_MOUSECURSOR_ARROW] = [NSCursor arrowCursor];
    _sapp_tc.app.standard_cursors[SAPP_MOUSECURSOR_IBEAM] = [NSCursor IBeamCursor];
    _sapp_tc.app.standard_cursors[SAPP_MOUSECURSOR_CROSSHAIR] = [NSCursor crosshairCursor];
    _sapp_tc.app.standard_cursors[SAPP_MOUSECURSOR_POINTING_HAND] = [NSCursor pointingHandCursor];
    _sapp_tc.app.standard_cursors[SAPP_MOUSECURSOR_RESIZE_EW] =
        _sapp_tc_priv_cursor(@selector(_windowResizeEastWestCursor), [NSCursor resizeLeftRightCursor]);
    _sapp_tc.app.standard_cursors[SAPP_MOUSECURSOR_RESIZE_NS] =
        _sapp_tc_priv_cursor(@selector(_windowResizeNorthSouthCursor), [NSCursor resizeUpDownCursor]);
    _sapp_tc.app.standard_cursors[SAPP_MOUSECURSOR_RESIZE_NWSE] =
        _sapp_tc_priv_cursor(@selector(_windowResizeNorthWestSouthEastCursor), [NSCursor closedHandCursor]);
    _sapp_tc.app.standard_cursors[SAPP_MOUSECURSOR_RESIZE_NESW] =
        _sapp_tc_priv_cursor(@selector(_windowResizeNorthEastSouthWestCursor), [NSCursor closedHandCursor]);
    _sapp_tc.app.standard_cursors[SAPP_MOUSECURSOR_RESIZE_ALL] = [NSCursor closedHandCursor];
    _sapp_tc.app.standard_cursors[SAPP_MOUSECURSOR_NOT_ALLOWED] = [NSCursor operationNotAllowedCursor];
}

static void _sapp_tc_apply_cursor(sapp_mouse_cursor cursor, bool shown) {
    /* [NSCursor hide/unhide] stack, so only act on an actual change */
    if (shown != _sapp_tc.app.mouse_shown) {
        if (shown) [NSCursor unhide]; else [NSCursor hide];
    }
    NSCursor* ns = [NSCursor arrowCursor];
    if (_sapp_tc.app.custom_cursor_bound[cursor] && _sapp_tc.app.custom_cursors[cursor]) {
        ns = _sapp_tc.app.custom_cursors[cursor];
    } else if (_sapp_tc.app.standard_cursors[cursor]) {
        ns = _sapp_tc.app.standard_cursors[cursor];
    }
    [ns set];
    _sapp_tc.app.current_cursor = cursor;
    _sapp_tc.app.mouse_shown = shown;
}

/*-- content view -----------------------------------------------------------*/
@interface _sapp_tc_view : NSView
@property (assign) _sapp_tc_window_t* w;
@end

@implementation _sapp_tc_view

- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)isFlipped { return YES; }   /* top-left origin */

- (void)updateTrackingAreas {
    for (NSTrackingArea* a in self.trackingAreas) [self removeTrackingArea:a];
    /* ActiveAlways: hover must work while the window is NOT key (a control
       panel window tracks the pointer while the main window keeps focus) */
    NSTrackingArea* area = [[NSTrackingArea alloc] initWithRect:self.bounds
        options:(NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited |
                 NSTrackingActiveAlways | NSTrackingEnabledDuringMouseDrag |
                 NSTrackingCursorUpdate | NSTrackingInVisibleRect)
        owner:self userInfo:nil];
    [self addTrackingArea:area];
    [super updateTrackingAreas];
}

/* keeps a custom/hidden cursor sticky when the pointer (re)enters the view */
- (void)cursorUpdate:(NSEvent*)ev {
    (void)ev;
    _sapp_tc_apply_cursor(_sapp_tc.app.current_cursor, _sapp_tc.app.mouse_shown);
}

- (void)mouseEvt:(NSEvent*)ev type:(sapp_event_type)type button:(sapp_mousebutton)btn {
    _sapp_tc_window_t* w = self.w;
    if (!w) return;
    NSPoint p = [self convertPoint:ev.locationInWindow fromView:nil];
    sapp_event e;
    memset(&e, 0, sizeof(e));
    e.type = type;
    e.mouse_button = btn;
    /* framebuffer pixels, same convention as sokol_app.h with high_dpi */
    e.mouse_x = (float)p.x * w->dpi_scale;
    e.mouse_y = (float)p.y * w->dpi_scale;
    e.mouse_dx = (float)ev.deltaX * w->dpi_scale;
    e.mouse_dy = (float)ev.deltaY * w->dpi_scale;
    e.modifiers = _sapp_tc_mods(ev);
    _sapp_tc_send(w, &e);
}

- (void)mouseDown:(NSEvent*)ev       { [self mouseEvt:ev type:SAPP_EVENTTYPE_MOUSE_DOWN button:SAPP_MOUSEBUTTON_LEFT]; }
- (void)mouseUp:(NSEvent*)ev         { [self mouseEvt:ev type:SAPP_EVENTTYPE_MOUSE_UP button:SAPP_MOUSEBUTTON_LEFT]; }
- (void)rightMouseDown:(NSEvent*)ev  { [self mouseEvt:ev type:SAPP_EVENTTYPE_MOUSE_DOWN button:SAPP_MOUSEBUTTON_RIGHT]; }
- (void)rightMouseUp:(NSEvent*)ev    { [self mouseEvt:ev type:SAPP_EVENTTYPE_MOUSE_UP button:SAPP_MOUSEBUTTON_RIGHT]; }
- (void)otherMouseDown:(NSEvent*)ev  { [self mouseEvt:ev type:SAPP_EVENTTYPE_MOUSE_DOWN button:SAPP_MOUSEBUTTON_MIDDLE]; }
- (void)otherMouseUp:(NSEvent*)ev    { [self mouseEvt:ev type:SAPP_EVENTTYPE_MOUSE_UP button:SAPP_MOUSEBUTTON_MIDDLE]; }
- (void)mouseMoved:(NSEvent*)ev      { [self mouseEvt:ev type:SAPP_EVENTTYPE_MOUSE_MOVE button:SAPP_MOUSEBUTTON_INVALID]; }
- (void)mouseDragged:(NSEvent*)ev    { [self mouseEvt:ev type:SAPP_EVENTTYPE_MOUSE_MOVE button:SAPP_MOUSEBUTTON_INVALID]; }
- (void)rightMouseDragged:(NSEvent*)ev { [self mouseEvt:ev type:SAPP_EVENTTYPE_MOUSE_MOVE button:SAPP_MOUSEBUTTON_INVALID]; }
- (void)otherMouseDragged:(NSEvent*)ev { [self mouseEvt:ev type:SAPP_EVENTTYPE_MOUSE_MOVE button:SAPP_MOUSEBUTTON_INVALID]; }
- (void)mouseEntered:(NSEvent*)ev    { [self mouseEvt:ev type:SAPP_EVENTTYPE_MOUSE_ENTER button:SAPP_MOUSEBUTTON_INVALID]; }
- (void)mouseExited:(NSEvent*)ev     { [self mouseEvt:ev type:SAPP_EVENTTYPE_MOUSE_LEAVE button:SAPP_MOUSEBUTTON_INVALID]; }

- (void)scrollWheel:(NSEvent*)ev {
    _sapp_tc_window_t* w = self.w;
    if (!w) return;
    float dx = (float)ev.scrollingDeltaX;
    float dy = (float)ev.scrollingDeltaY;
    if (ev.hasPreciseScrollingDeltas) { dx *= 0.1f; dy *= 0.1f; }
    if ((fabsf(dx) > 0.0f) || (fabsf(dy) > 0.0f)) {
        NSPoint p = [self convertPoint:ev.locationInWindow fromView:nil];
        sapp_event e;
        memset(&e, 0, sizeof(e));
        e.type = SAPP_EVENTTYPE_MOUSE_SCROLL;
        e.mouse_x = (float)p.x * w->dpi_scale;
        e.mouse_y = (float)p.y * w->dpi_scale;
        e.scroll_x = dx;
        e.scroll_y = dy;
        e.modifiers = _sapp_tc_mods(ev);
        _sapp_tc_send(w, &e);
    }
}

- (void)keyEvt:(NSEvent*)ev pressed:(bool)down {
    _sapp_tc_window_t* w = self.w;
    if (!w) return;
    sapp_event e;
    memset(&e, 0, sizeof(e));
    e.type = down ? SAPP_EVENTTYPE_KEY_DOWN : SAPP_EVENTTYPE_KEY_UP;
    e.key_code = _sapp_tc_translate_key(ev.keyCode);
    e.key_repeat = ev.isARepeat;
    e.modifiers = _sapp_tc_mods(ev);
    _sapp_tc_send(w, &e);

    /* CHAR events on key down, same filtering as sokol_app.h (skip the
       function-key private range and control characters) */
    if (down) {
        NSString* chars = ev.characters;
        const NSUInteger len = chars.length;
        for (NSUInteger i = 0; i < len; i++) {
            uint32_t cp = [chars characterAtIndex:i];
            if (CFStringIsSurrogateHighCharacter((UniChar)cp) && (i + 1) < len) {
                const UniChar lo = [chars characterAtIndex:i + 1];
                if (CFStringIsSurrogateLowCharacter(lo)) {
                    cp = (uint32_t)CFStringGetLongCharacterForSurrogatePair((UniChar)cp, lo);
                    i++;
                }
            }
            if ((cp & 0xFF00) == 0xF700) continue;  /* NSF...FunctionKey range */
            if (cp < 32 || cp == 127) continue;
            sapp_event ce;
            memset(&ce, 0, sizeof(ce));
            ce.type = SAPP_EVENTTYPE_CHAR;
            ce.char_code = cp;
            ce.key_repeat = ev.isARepeat;
            ce.modifiers = _sapp_tc_mods(ev);
            _sapp_tc_send(w, &ce);
        }
    }

    /* Cmd+V paste notification, same trigger as sokol_app.h (exact-match on
       the Super modifier); the app reads the clipboard in its handler */
    if (down && _sapp_tc.app.clipboard_enabled &&
        (_sapp_tc_mods(ev) == SAPP_MODIFIER_SUPER) &&
        (_sapp_tc_translate_key(ev.keyCode) == SAPP_KEYCODE_V)) {
        sapp_event pe;
        memset(&pe, 0, sizeof(pe));
        pe.type = SAPP_EVENTTYPE_CLIPBOARD_PASTED;
        pe.modifiers = SAPP_MODIFIER_SUPER;
        _sapp_tc_send(w, &pe);
    }
}

/*-- drag & drop (NSDraggingDestination; registered on the main view) -------*/
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    (void)sender;
    return NSDragOperationCopy;
}

- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)sender {
    (void)sender;
    return NSDragOperationCopy;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    _sapp_tc_window_t* w = self.w;
    if (!w || !_sapp_tc.app.drop_enabled || !_sapp_tc.app.drop_buffer) return NO;
    NSPasteboard* pboard = sender.draggingPasteboard;
    if (![pboard.types containsObject:NSPasteboardTypeFileURL]) return NO;
    const int max_files = _sapp_tc.app.drop_max_files;
    const int max_path = _sapp_tc.app.drop_max_path_length;
    memset(_sapp_tc.app.drop_buffer, 0, (size_t)max_files * (size_t)max_path);
    _sapp_tc.app.drop_num_files = 0;
    int num = (int)pboard.pasteboardItems.count;
    if (num > max_files) num = max_files;
    for (int i = 0; i < num; i++) {
        NSString* str = [pboard.pasteboardItems[(NSUInteger)i] stringForType:NSPasteboardTypeFileURL];
        if (!str) return NO;
        NSURL* url = [NSURL URLWithString:str];
        const char* path = url.standardizedURL.path.UTF8String;
        if (!path || (int)strlen(path) >= max_path) return NO;  /* path too long: drop everything */
        strcpy(_sapp_tc.app.drop_buffer + (size_t)i * (size_t)max_path, path);
    }
    _sapp_tc.app.drop_num_files = num;
    sapp_event e;
    memset(&e, 0, sizeof(e));
    e.type = SAPP_EVENTTYPE_FILES_DROPPED;
    NSPoint p = [self convertPoint:sender.draggingLocation fromView:nil];
    e.mouse_x = (float)p.x * w->dpi_scale;
    e.mouse_y = (float)p.y * w->dpi_scale;
    _sapp_tc_send(w, &e);
    return YES;
}

- (void)keyDown:(NSEvent*)ev { [self keyEvt:ev pressed:true]; }
- (void)keyUp:(NSEvent*)ev   { [self keyEvt:ev pressed:false]; }

- (void)flagsChanged:(NSEvent*)ev {
    _sapp_tc_window_t* w = self.w;
    if (!w) return;
    const sapp_keycode kc = _sapp_tc_translate_key(ev.keyCode);
    if (kc == SAPP_KEYCODE_INVALID) return;
    /* device-dependent modifier bits (same values GLFW/sokol_app rely on) */
    const NSEventModifierFlags f = ev.modifierFlags;
    bool down = false;
    switch (kc) {
        case SAPP_KEYCODE_LEFT_SHIFT:    down = (f & 0x0002) != 0; break;
        case SAPP_KEYCODE_RIGHT_SHIFT:   down = (f & 0x0004) != 0; break;
        case SAPP_KEYCODE_LEFT_CONTROL:  down = (f & 0x0001) != 0; break;
        case SAPP_KEYCODE_RIGHT_CONTROL: down = (f & 0x2000) != 0; break;
        case SAPP_KEYCODE_LEFT_ALT:      down = (f & 0x0020) != 0; break;
        case SAPP_KEYCODE_RIGHT_ALT:     down = (f & 0x0040) != 0; break;
        case SAPP_KEYCODE_LEFT_SUPER:    down = (f & 0x0008) != 0; break;
        case SAPP_KEYCODE_RIGHT_SUPER:   down = (f & 0x0010) != 0; break;
        case SAPP_KEYCODE_CAPS_LOCK:     down = (f & NSEventModifierFlagCapsLock) != 0; break;
        default: return;
    }
    sapp_event e;
    memset(&e, 0, sizeof(e));
    e.type = down ? SAPP_EVENTTYPE_KEY_DOWN : SAPP_EVENTTYPE_KEY_UP;
    e.key_code = kc;
    e.modifiers = _sapp_tc_mods(ev);
    _sapp_tc_send(w, &e);
}

/*-- per-frame tick (CADisplayLink target) ---------------------------------*/
- (void)tick:(CADisplayLink*)link {
    _sapp_tc_window_t* w = self.w;
    if (!w) return;

    /* window #0 runs the app frame loop (sokol_app.h parity: no occlusion
       gate — while occluded the link auto-suspends and a ~60Hz fallback
       timer keeps the frame callback alive, exactly like upstream) */
    if (w->is_main) {
        _sapp_tc_main_tick(link);
        return;
    }

    /* Occluded => not due. THIS is what makes the nextDrawable stall
       structurally impossible: the acquiring code below only runs for a
       provably visible window. (The display link also auto-suspends for
       occluded views; this gate covers the transition race.) */
    const bool occluded = (w->window.occlusionState & NSWindowOcclusionStateVisible) == 0;
    if (occluded != w->occluded) {
        w->occluded = occluded;
        sapp_event e;
        memset(&e, 0, sizeof(e));
        e.type = occluded ? SAPP_EVENTTYPE_SUSPENDED : SAPP_EVENTTYPE_RESUMED;
        _sapp_tc_send(w, &e);
    }
    if (occluded) return;

    /* measured tick interval (the display's refresh period) */
    if (w->last_tick_time > 0.0) {
        w->frame_duration = link.timestamp - w->last_tick_time;
    } else {
        w->frame_duration = link.targetTimestamp - link.timestamp;
    }
    w->last_tick_time = link.timestamp;

    /* keep the layer sized to the view; ONE dpi ratio source: the scale we
       size the drawable with is the scale we report (and the scale mouse
       coordinates are multiplied by) -- never assume backingScale == 2 */
    const CGFloat scale = w->desc.no_high_dpi ? 1.0 : w->window.backingScaleFactor;
    const NSSize sz = self.bounds.size;
    const int fbw = (int)(sz.width * scale);
    const int fbh = (int)(sz.height * scale);
    if (fbw <= 0 || fbh <= 0) return;
    if ((int)w->layer.drawableSize.width != fbw || (int)w->layer.drawableSize.height != fbh) {
        w->layer.contentsScale = scale;
        w->layer.drawableSize = CGSizeMake(fbw, fbh);
    }
    const bool resized = (fbw != w->fb_width) || (fbh != w->fb_height) ||
                         ((int)sz.width != w->win_width) || ((int)sz.height != w->win_height);
    w->fb_width = fbw;
    w->fb_height = fbh;
    w->win_width = (int)sz.width;
    w->win_height = (int)sz.height;
    w->dpi_scale = (float)scale;
    if (resized) {
        sapp_event e;
        memset(&e, 0, sizeof(e));
        e.type = SAPP_EVENTTYPE_RESIZED;
        _sapp_tc_send(w, &e);
    }

    /* depth-stencil / MSAA textures, drawable-sized */
    if (w->depth_tex == nil || (int)w->depth_tex.width != fbw || (int)w->depth_tex.height != fbh) {
        id<MTLDevice> dev = w->layer.device;
        MTLTextureDescriptor* dd = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8
            width:fbw height:fbh mipmapped:NO];
        dd.usage = MTLTextureUsageRenderTarget;
        dd.storageMode = MTLStorageModePrivate;
        dd.sampleCount = w->desc.sample_count;
        dd.textureType = w->desc.sample_count > 1 ? MTLTextureType2DMultisample : MTLTextureType2D;
        w->depth_tex = [dev newTextureWithDescriptor:dd];
        if (w->desc.sample_count > 1) {
            MTLTextureDescriptor* md = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                width:fbw height:fbh mipmapped:NO];
            md.usage = MTLTextureUsageRenderTarget;
            md.storageMode = MTLStorageModePrivate;
            md.sampleCount = w->desc.sample_count;
            md.textureType = MTLTextureType2DMultisample;
            w->msaa_tex = [dev newTextureWithDescriptor:md];
        }
    }

    w->frame_drawable = [w->layer nextDrawable];
    if (w->frame_drawable == nil) return;
    w->in_tick = true;
    if (w->desc.tick_cb) {
        sapp_window handle = { w->win_id };
        w->desc.tick_cb(handle, w->desc.user_data);
    }
    w->in_tick = false;
    w->frame_drawable = nil;
}

@end

/*-- window delegate --------------------------------------------------------*/
@interface _sapp_tc_win_delegate : NSObject <NSWindowDelegate>
@property (assign) _sapp_tc_window_t* w;
@end

static void _sapp_tc_start_fallback_timer(void);
static void _sapp_tc_stop_fallback_timer(void);

@implementation _sapp_tc_win_delegate
- (BOOL)windowShouldClose:(id)sender {
    (void)sender;
    _sapp_tc_window_t* w = self.w;
    if (!w || !w->is_main) return YES;
    /* main window: sokol_app.h's cancellable quit dance. sapp_quit() pre-sets
       quit_ordered (unvetoable); otherwise QUIT_REQUESTED is delivered and a
       handler may call sapp_cancel_quit() to keep the window open. */
    if (!_sapp_tc.app.quit_ordered) {
        _sapp_tc.app.quit_requested = true;
        sapp_event e; memset(&e, 0, sizeof(e));
        e.type = SAPP_EVENTTYPE_QUIT_REQUESTED;
        _sapp_tc_send(w, &e);
        if (_sapp_tc.app.quit_requested) {
            _sapp_tc.app.quit_ordered = true;
        }
    }
    return _sapp_tc.app.quit_ordered ? YES : NO;
}
- (void)windowWillClose:(NSNotification*)n {
    (void)n;
    _sapp_tc_window_t* w = self.w;
    if (!w) return;
    if (w->is_main) {
        /* main window gone = app gone, even if secondary windows are still
           open. terminate: runs applicationWillTerminate (cleanup_cb) and
           never returns to the run loop. */
        [NSApp terminate:nil];
        return;
    }
    if (w->desc.close_cb) {
        sapp_window handle = { w->win_id };
        w->desc.close_cb(handle, w->desc.user_data);
    }
}
- (void)windowDidResize:(NSNotification*)n {
    (void)n;
    _sapp_tc_window_t* w = self.w;
    if (w && w->is_main) _sapp_tc_update_main_dimensions(true);
}
- (void)windowDidChangeScreen:(NSNotification*)n {
    (void)n;
    _sapp_tc_window_t* w = self.w;
    if (w && w->is_main) _sapp_tc_update_main_dimensions(true);
}
- (void)windowDidChangeOcclusionState:(NSNotification*)n {
    (void)n;
    _sapp_tc_window_t* w = self.w;
    if (!w || !w->is_main) return;
    if (w->window.occlusionState & NSWindowOcclusionStateVisible) {
        _sapp_tc_stop_fallback_timer();     /* the display link auto-resumes */
    } else {
        _sapp_tc_start_fallback_timer();    /* keep frame_cb alive while hidden */
    }
}
- (void)windowDidMiniaturize:(NSNotification*)n {
    (void)n;
    _sapp_tc_window_t* w = self.w;
    if (!w || !w->is_main) return;
    _sapp_tc_start_fallback_timer();
    sapp_event e; memset(&e, 0, sizeof(e));
    e.type = SAPP_EVENTTYPE_ICONIFIED;
    _sapp_tc_send(w, &e);
}
- (void)windowDidDeminiaturize:(NSNotification*)n {
    (void)n;
    _sapp_tc_window_t* w = self.w;
    if (!w || !w->is_main) return;
    _sapp_tc_stop_fallback_timer();
    sapp_event e; memset(&e, 0, sizeof(e));
    e.type = SAPP_EVENTTYPE_RESTORED;
    _sapp_tc_send(w, &e);
}
- (void)windowDidEnterFullScreen:(NSNotification*)n {
    (void)n;
    _sapp_tc_window_t* w = self.w;
    if (w && w->is_main) _sapp_tc.app.fullscreen = true;
}
- (void)windowDidExitFullScreen:(NSNotification*)n {
    (void)n;
    _sapp_tc_window_t* w = self.w;
    if (w && w->is_main) _sapp_tc.app.fullscreen = false;
}
- (void)windowDidBecomeKey:(NSNotification*)n {
    _sapp_tc_window_t* w = self.w;
    if (!w) return;
    sapp_event e; memset(&e, 0, sizeof(e));
    e.type = SAPP_EVENTTYPE_FOCUSED;
    _sapp_tc_send(w, &e);
}
- (void)windowDidResignKey:(NSNotification*)n {
    _sapp_tc_window_t* w = self.w;
    if (!w) return;
    sapp_event e; memset(&e, 0, sizeof(e));
    e.type = SAPP_EVENTTYPE_UNFOCUSED;
    _sapp_tc_send(w, &e);
}
@end

/*-- main window / app lifecycle (this header owns sapp_run on macOS) -------*/

/* drawable-sized depth-stencil + MSAA color textures for one window */
static void _sapp_tc_ensure_swapchain_textures(_sapp_tc_window_t* w) {
    if (w->fb_width <= 0 || w->fb_height <= 0) return;
    if (w->depth_tex != nil &&
        (int)w->depth_tex.width == w->fb_width &&
        (int)w->depth_tex.height == w->fb_height) return;
    id<MTLDevice> dev = w->layer.device;
    MTLTextureDescriptor* dd = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8
        width:(NSUInteger)w->fb_width height:(NSUInteger)w->fb_height mipmapped:NO];
    dd.usage = MTLTextureUsageRenderTarget;
    dd.storageMode = MTLStorageModePrivate;
    dd.sampleCount = (NSUInteger)w->desc.sample_count;
    dd.textureType = w->desc.sample_count > 1 ? MTLTextureType2DMultisample : MTLTextureType2D;
    w->depth_tex = [dev newTextureWithDescriptor:dd];
    if (w->desc.sample_count > 1) {
        MTLTextureDescriptor* md = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:w->color_fmt
            width:(NSUInteger)w->fb_width height:(NSUInteger)w->fb_height mipmapped:NO];
        md.usage = MTLTextureUsageRenderTarget;
        md.storageMode = MTLStorageModePrivate;
        md.sampleCount = (NSUInteger)w->desc.sample_count;
        md.textureType = MTLTextureType2DMultisample;
        w->msaa_tex = [dev newTextureWithDescriptor:md];
    }
}

/* re-sample scale + sizes, keep the layer drawable in sync, emit RESIZED.
   Called from the tick, windowDidResize / windowDidChangeScreen, and once
   silently right after window creation (allow_event=false, sokol parity:
   the startup update never fires RESIZED). */
static void _sapp_tc_update_main_dimensions(bool allow_event) {
    _sapp_tc_window_t* w = _sapp_tc.app.main;
    if (!w || !w->view) return;
    CGFloat scale = 1.0;
    if (_sapp_tc.app.desc.high_dpi) {
        NSScreen* screen = w->window.screen ? w->window.screen : [NSScreen mainScreen];
        scale = screen.backingScaleFactor;
        if (scale <= 0.0) scale = 1.0;
    }
    w->dpi_scale = (float)scale;
    const NSSize sz = w->view.bounds.size;
    int fbw = (int)((CGFloat)sz.width * scale);
    int fbh = (int)((CGFloat)sz.height * scale);
    if (fbw <= 0 || fbh <= 0) return;
    /* re-apply contentsScale every time: live-resize installs a non-scaling
       layer placement that would otherwise stick (sokol_app.h does the same) */
    w->layer.contentsScale = scale;
    if ((int)w->layer.drawableSize.width != fbw || (int)w->layer.drawableSize.height != fbh) {
        w->layer.drawableSize = CGSizeMake((CGFloat)fbw, (CGFloat)fbh);
    }
    const bool changed = (fbw != w->fb_width) || (fbh != w->fb_height) ||
                         ((int)sz.width != w->win_width) || ((int)sz.height != w->win_height);
    w->fb_width = fbw;
    w->fb_height = fbh;
    w->win_width = (int)sz.width;
    w->win_height = (int)sz.height;
    _sapp_tc_ensure_swapchain_textures(w);
    if (changed && allow_event) {
        sapp_event e;
        memset(&e, 0, sizeof(e));
        e.type = SAPP_EVENTTYPE_RESIZED;
        _sapp_tc_send(w, &e);
    }
}

/* one main-window frame: timing -> dimensions -> init_cb (first tick only)
   -> frame_cb -> quit check. The drawable is NOT acquired here; it is
   acquired lazily by sapp_get_swapchain() (upstream parity) and released
   when the tick ends. `link` is nil when driven by the fallback timer. */
static void _sapp_tc_main_tick(CADisplayLink* link) {
    _sapp_tc_window_t* w = _sapp_tc.app.main;
    if (!w || w->in_tick) return;
    _sapp_tc_timing_update(&_sapp_tc.app.timing);      /* feeds the fallback estimate */
    if (link) {
        if (w->last_tick_time > 0.0) {
            const double dt = link.timestamp - w->last_tick_time;
            if ((dt > 0.000001) && (dt < 0.1)) w->frame_duration = dt;
        }
        w->last_tick_time = link.timestamp;
    }
    _sapp_tc_update_main_dimensions(true);
    @autoreleasepool {
        w->in_tick = true;
        if (!_sapp_tc.app.init_called) {
            _sapp_tc.app.init_called = true;
            if (_sapp_tc.app.desc.init_cb) {
                _sapp_tc.app.desc.init_cb();
            } else if (_sapp_tc.app.desc.init_userdata_cb) {
                _sapp_tc.app.desc.init_userdata_cb(_sapp_tc.app.desc.user_data);
            }
        }
        if (_sapp_tc.app.desc.frame_cb) {
            _sapp_tc.app.desc.frame_cb();
        } else if (_sapp_tc.app.desc.frame_userdata_cb) {
            _sapp_tc.app.desc.frame_userdata_cb(_sapp_tc.app.desc.user_data);
        }
        _sapp_tc.app.frame_count++;
        w->in_tick = false;
        w->frame_drawable = nil;    /* presented by sg_commit; release our ref */
    }
    if (_sapp_tc.app.quit_requested || _sapp_tc.app.quit_ordered) {
        [w->window performClose:nil];
    }
}

static void _sapp_tc_start_fallback_timer(void) {
    if (_sapp_tc.app.fallback_timer != nil || _sapp_tc.app.dlg == nil) return;
    NSTimer* t = [NSTimer timerWithTimeInterval:(1.0 / 60.0)
                                         target:_sapp_tc.app.dlg
                                       selector:@selector(fallbackTick:)
                                       userInfo:nil
                                        repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:t forMode:NSRunLoopCommonModes];
    _sapp_tc.app.fallback_timer = t;
}

static void _sapp_tc_stop_fallback_timer(void) {
    if (_sapp_tc.app.fallback_timer == nil) return;
    [_sapp_tc.app.fallback_timer invalidate];
    _sapp_tc.app.fallback_timer = nil;
}

static void _sapp_tc_create_main_window(void) {
    const sapp_desc* d = &_sapp_tc.app.desc;
    int slot = -1;
    for (int i = 0; i < _SAPP_TC_MAX_WINDOWS; i++) {
        if (_sapp_tc.windows[i] == 0) { slot = i; break; }
    }
    if (slot < 0) return;

    _sapp_tc_window_t* w = new _sapp_tc_window_t();
    w->win_id = ++_sapp_tc.next_id;
    w->is_main = true;
    w->color_fmt = MTLPixelFormatRGB10A2Unorm;  /* TrussC 10-bit output */
    w->dpi_scale = 1.0f;

    /* content size in points; 0 = 4/5 of the main screen (sokol parity) */
    int width = d->width;
    int height = d->height;
    const NSRect screen_rect = NSScreen.mainScreen.frame;
    if (width <= 0)  width = (int)(screen_rect.size.width * 0.8);
    if (height <= 0) height = (int)(screen_rect.size.height * 0.8);

    /* per-window desc: the app callbacks are reached via the trampoline */
    w->desc.width = width;
    w->desc.height = height;
    w->desc.sample_count = d->sample_count;
    w->desc.no_high_dpi = !d->high_dpi;
    w->desc.event_cb = _sapp_tc_main_event_tramp;

    id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
    _sapp_tc.app.device = dev;

    const NSRect rect = NSMakeRect(0, 0, width, height);
    const NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                    NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
    w->window = [[NSWindow alloc] initWithContentRect:rect styleMask:style
                                  backing:NSBackingStoreBuffered defer:NO];
    w->window.title = [NSString stringWithUTF8String:_sapp_tc.app.window_title];
    w->window.releasedWhenClosed = NO;  /* the object must outlive performClose */
    w->window.acceptsMouseMovedEvents = YES;
    w->window.restorable = YES;

    _sapp_tc_view* view = [[_sapp_tc_view alloc] initWithFrame:rect];
    view.w = w;
    w->view = view;
    w->layer = [CAMetalLayer layer];
    w->layer.device = dev;
    w->layer.pixelFormat = w->color_fmt;
    w->layer.opaque = YES;
    w->layer.magnificationFilter = kCAFilterNearest;
    w->layer.framebufferOnly = NO;      /* captureWindow() reads the drawable */
    view.wantsLayer = YES;
    view.layer = w->layer;
    [view registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
    w->window.contentView = view;
    [w->window makeFirstResponder:view];

    _sapp_tc_win_delegate* del = [_sapp_tc_win_delegate new];
    del.w = w;
    w->delegate = del;
    w->window.delegate = del;

    [w->window center];
    _sapp_tc.windows[slot] = w;
    _sapp_tc.app.main = w;
    _sapp_tc.app.valid = true;

    if (d->fullscreen) {
        _sapp_tc.app.fullscreen = true;
        [w->window toggleFullScreen:nil];
    }
    [w->window makeKeyAndOrderFront:nil];
    _sapp_tc_update_main_dimensions(false);     /* silent startup sizing */

    /* window #0's vsync source; swap_interval > 1 divides the display rate */
    w->link = [view displayLinkWithTarget:view selector:@selector(tick:)];
    if (d->swap_interval > 1) {
        const float maxfps = (float)[NSScreen mainScreen].maximumFramesPerSecond;
        const float p = maxfps / (float)d->swap_interval;
        w->link.preferredFrameRateRange = CAFrameRateRangeMake(p, p, p);
    }
    [w->link addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
}

@interface _sapp_tc_app_delegate : NSObject <NSApplicationDelegate>
@end

@implementation _sapp_tc_app_delegate
- (void)applicationDidFinishLaunching:(NSNotification*)n {
    (void)n;
    /* activation policy must be set before window creation (sokol #1500) */
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    _sapp_tc_init_cursors();
    _sapp_tc_create_main_window();
    [NSEvent setMouseCoalescingEnabled:NO];
    [NSApp activateIgnoringOtherApps:YES];
    /* focus workaround (sokol #982): make sure the window has focus even if
       the first tick's init callback blocks for a long time */
    NSEvent* focusevent = [NSEvent otherEventWithType:NSEventTypeAppKitDefined
        location:NSZeroPoint
        modifierFlags:0
        timestamp:0
        windowNumber:0
        context:nil
        subtype:NSEventSubtypeApplicationActivated
        data1:0
        data2:0];
    [NSApp postEvent:focusevent atStart:YES];
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    (void)sender;
    return YES;
}
- (void)applicationWillTerminate:(NSNotification*)n {
    (void)n;
    /* stop all tick sources first so no frame runs during teardown */
    _sapp_tc_stop_fallback_timer();
    for (int i = 0; i < _SAPP_TC_MAX_WINDOWS; i++) {
        _sapp_tc_window_t* w = _sapp_tc.windows[i];
        if (w && w->link) { [w->link invalidate]; w->link = nil; }
    }
    /* user cleanup runs BEFORE any GPU object release (the app shuts down
       sokol_gfx here; we only hold the device/layer references) */
    if (!_sapp_tc.app.cleanup_called) {
        _sapp_tc.app.cleanup_called = true;
        if (_sapp_tc.app.desc.cleanup_cb) {
            _sapp_tc.app.desc.cleanup_cb();
        } else if (_sapp_tc.app.desc.cleanup_userdata_cb) {
            _sapp_tc.app.desc.cleanup_userdata_cb(_sapp_tc.app.desc.user_data);
        }
    }
    if (_sapp_tc.app.keyup_monitor) {
        [NSEvent removeMonitor:_sapp_tc.app.keyup_monitor];
        _sapp_tc.app.keyup_monitor = nil;
    }
    if (_sapp_tc.app.clipboard) { free(_sapp_tc.app.clipboard); _sapp_tc.app.clipboard = 0; }
    if (_sapp_tc.app.drop_buffer) { free(_sapp_tc.app.drop_buffer); _sapp_tc.app.drop_buffer = 0; }
}
- (void)fallbackTick:(NSTimer*)t {
    (void)t;
    _sapp_tc_main_tick(nil);
}
@end

/*-- public API -------------------------------------------------------------*/
extern "C" {

sapp_window sapp_create_window(const sapp_window_desc* desc) {
    sapp_window invalid = { 0 };
    if (!desc) return invalid;
    _sapp_tc_init_keytable();

    int slot = -1;
    for (int i = 0; i < _SAPP_TC_MAX_WINDOWS; i++) {
        if (_sapp_tc.windows[i] == 0) { slot = i; break; }
    }
    if (slot < 0) return invalid;

    _sapp_tc_window_t* w = new _sapp_tc_window_t();
    w->win_id = ++_sapp_tc.next_id;
    w->desc = *desc;
    if (w->desc.width <= 0) w->desc.width = 640;
    if (w->desc.height <= 0) w->desc.height = 480;
    if (w->desc.sample_count <= 0) w->desc.sample_count = 1;
    w->dpi_scale = 1.0f;

    const int px = (desc->x >= 0) ? desc->x : 120;
    const int py = (desc->y >= 0) ? desc->y : 120;
    NSRect rect = NSMakeRect(px, py, w->desc.width, w->desc.height);
    NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                              NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
    if (w->desc.borderless) style = NSWindowStyleMaskBorderless;
    w->window = [[NSWindow alloc] initWithContentRect:rect styleMask:style
                                  backing:NSBackingStoreBuffered defer:NO];
    if (w->desc.title) w->window.title = [NSString stringWithUTF8String:w->desc.title];
    w->window.releasedWhenClosed = NO;
    w->window.acceptsMouseMovedEvents = YES;

    _sapp_tc_view* view = [[_sapp_tc_view alloc] initWithFrame:rect];
    view.w = w;
    w->view = view;
    w->layer = [CAMetalLayer layer];
    w->layer.device = (__bridge id<MTLDevice>)w->desc.mtl_device;
    w->layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    w->layer.opaque = YES;
    view.wantsLayer = YES;
    view.layer = w->layer;
    w->window.contentView = view;

    _sapp_tc_win_delegate* del = [_sapp_tc_win_delegate new];
    del.w = w;
    w->delegate = del;
    w->window.delegate = del;

    _sapp_tc.windows[slot] = w;
    [w->window makeKeyAndOrderFront:nil];

    /* per-window display link: fires on the main run loop at THIS window's
       display rate; auto-suspends while the view is not visible (macOS 14+) */
    w->link = [view displayLinkWithTarget:view selector:@selector(tick:)];
    [w->link addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];

    sapp_window handle = { w->win_id };
    return handle;
}

void sapp_destroy_window(sapp_window win) {
    _sapp_tc_window_t* w = _sapp_tc_lookup(win);
    if (!w) return;
    for (int i = 0; i < _SAPP_TC_MAX_WINDOWS; i++) {
        if (_sapp_tc.windows[i] == w) { _sapp_tc.windows[i] = 0; break; }
    }
    w->desc.close_cb = 0;   /* no callback from the orderOut below */
    if (w->link) { [w->link invalidate]; w->link = nil; }
    w->view.w = 0;
    w->delegate.w = 0;
    if (w->window) {
        w->window.delegate = nil;
        [w->window orderOut:nil];
        w->window = nil;
    }
    w->view = nil;
    w->delegate = nil;
    w->layer = nil;
    w->depth_tex = nil;
    w->msaa_tex = nil;
    w->frame_drawable = nil;
    delete w;
}

bool sapp_window_valid(sapp_window win) {
    return _sapp_tc_lookup(win) != 0;
}

int sapp_window_width(sapp_window win) {
    _sapp_tc_window_t* w = _sapp_tc_lookup(win);
    if (!w || !w->view) return 0;
    return (int)w->view.bounds.size.width;
}

int sapp_window_height(sapp_window win) {
    _sapp_tc_window_t* w = _sapp_tc_lookup(win);
    if (!w || !w->view) return 0;
    return (int)w->view.bounds.size.height;
}

int sapp_window_framebuffer_width(sapp_window win) {
    _sapp_tc_window_t* w = _sapp_tc_lookup(win);
    return w ? w->fb_width : 0;
}

int sapp_window_framebuffer_height(sapp_window win) {
    _sapp_tc_window_t* w = _sapp_tc_lookup(win);
    return w ? w->fb_height : 0;
}

float sapp_window_dpi_scale(sapp_window win) {
    _sapp_tc_window_t* w = _sapp_tc_lookup(win);
    if (!w) return 1.0f;
    if (w->dpi_scale > 0.0f && w->fb_width > 0) return w->dpi_scale;
    return w->desc.no_high_dpi ? 1.0f : (float)w->window.backingScaleFactor;
}

int sapp_window_sample_count(sapp_window win) {
    _sapp_tc_window_t* w = _sapp_tc_lookup(win);
    return w ? w->desc.sample_count : 1;
}

bool sapp_window_occluded(sapp_window win) {
    _sapp_tc_window_t* w = _sapp_tc_lookup(win);
    return w ? w->occluded : false;
}

void sapp_window_set_title(sapp_window win, const char* title) {
    _sapp_tc_window_t* w = _sapp_tc_lookup(win);
    if (w && w->window && title) {
        w->window.title = [NSString stringWithUTF8String:title];
    }
}

double sapp_window_frame_duration(sapp_window win) {
    _sapp_tc_window_t* w = _sapp_tc_lookup(win);
    if (!w) return 1.0 / 60.0;
    return (w->frame_duration > 0.0) ? w->frame_duration : 1.0 / 60.0;
}

const void* sapp_window_metal_current_drawable(sapp_window win) {
    _sapp_tc_window_t* w = _sapp_tc_lookup(win);
    if (!w || !w->in_tick) return 0;
    return (__bridge const void*)w->frame_drawable;
}

const void* sapp_window_metal_depth_stencil_texture(sapp_window win) {
    _sapp_tc_window_t* w = _sapp_tc_lookup(win);
    if (!w || !w->in_tick) return 0;
    return (__bridge const void*)w->depth_tex;
}

const void* sapp_window_metal_msaa_color_texture(sapp_window win) {
    _sapp_tc_window_t* w = _sapp_tc_lookup(win);
    if (!w || !w->in_tick || w->desc.sample_count <= 1) return 0;
    return (__bridge const void*)w->msaa_tex;
}

const void* sapp_window_macos_get_window(sapp_window win) {
    _sapp_tc_window_t* w = _sapp_tc_lookup(win);
    return w ? (__bridge const void*)w->window : 0;
}

/* D3D11 / Win32 / GL / X11 handles do not exist on macOS */
const void* sapp_window_d3d11_render_view(sapp_window win) { (void)win; return 0; }
const void* sapp_window_d3d11_resolve_view(sapp_window win) { (void)win; return 0; }
const void* sapp_window_d3d11_depth_stencil_view(sapp_window win) { (void)win; return 0; }
const void* sapp_window_win32_get_hwnd(sapp_window win) { (void)win; return 0; }
const void* sapp_window_x11_get_window(sapp_window win) { (void)win; return 0; }
uint32_t sapp_window_gl_framebuffer(sapp_window win) { (void)win; return 0; }

/*-- sokol_app.h public API (macOS implementation lives here) ---------------*/

void sapp_run(const sapp_desc* desc) {
    if (!desc) return;
    _sapp_tc.app.desc = *desc;
    sapp_desc* d = &_sapp_tc.app.desc;
    if (d->sample_count <= 0) d->sample_count = 1;
    if (d->swap_interval <= 0) d->swap_interval = 1;
    if (d->clipboard_size <= 0) d->clipboard_size = 8192;
    if (d->max_dropped_files <= 0) d->max_dropped_files = 1;
    if (d->max_dropped_file_path_length <= 0) d->max_dropped_file_path_length = 2048;
    strncpy(_sapp_tc.app.window_title, d->window_title ? d->window_title : "sokol",
            sizeof(_sapp_tc.app.window_title) - 1);
    d->window_title = _sapp_tc.app.window_title;
    if (d->enable_clipboard) {
        _sapp_tc.app.clipboard_enabled = true;
        _sapp_tc.app.clipboard_size = d->clipboard_size;
        _sapp_tc.app.clipboard = (char*)calloc(1, (size_t)d->clipboard_size);
    }
    if (d->enable_dragndrop) {
        _sapp_tc.app.drop_enabled = true;
        _sapp_tc.app.drop_max_files = d->max_dropped_files;
        _sapp_tc.app.drop_max_path_length = d->max_dropped_file_path_length;
        _sapp_tc.app.drop_buffer = (char*)calloc(
            (size_t)d->max_dropped_files, (size_t)d->max_dropped_file_path_length);
    }
    _sapp_tc.app.mouse_shown = true;
    _sapp_tc.app.current_cursor = SAPP_MOUSECURSOR_DEFAULT;
    _sapp_tc_timing_reset(&_sapp_tc.app.timing);
    _sapp_tc_init_keytable();

    [NSApplication sharedApplication];
    _sapp_tc.app.dlg = [[_sapp_tc_app_delegate alloc] init];
    NSApp.delegate = _sapp_tc.app.dlg;
    /* Cocoa swallows key-up events while Cmd is held; force-forward them
       (same workaround as sokol_app.h / GLFW) */
    _sapp_tc.app.keyup_monitor = [NSEvent
        addLocalMonitorForEventsMatchingMask:NSEventMaskKeyUp
        handler:^NSEvent* (NSEvent* event) {
            if ([event modifierFlags] & NSEventModifierFlagCommand) {
                [[NSApp keyWindow] sendEvent:event];
            }
            return event;
        }];
    [NSApp run];
    /* never returns; cleanup runs in applicationWillTerminate */
}

bool sapp_isvalid(void) {
    return _sapp_tc.app.valid;
}

int sapp_width(void) {
    _sapp_tc_window_t* w = _sapp_tc.app.main;
    return (w && w->fb_width > 0) ? w->fb_width : 1;
}

float sapp_widthf(void) {
    return (float)sapp_width();
}

int sapp_height(void) {
    _sapp_tc_window_t* w = _sapp_tc.app.main;
    return (w && w->fb_height > 0) ? w->fb_height : 1;
}

float sapp_heightf(void) {
    return (float)sapp_height();
}

int sapp_sample_count(void) {
    return _sapp_tc.app.desc.sample_count > 0 ? _sapp_tc.app.desc.sample_count : 1;
}

bool sapp_high_dpi(void) {
    _sapp_tc_window_t* w = _sapp_tc.app.main;
    return _sapp_tc.app.desc.high_dpi && w && (w->dpi_scale >= 1.5f);
}

float sapp_dpi_scale(void) {
    _sapp_tc_window_t* w = _sapp_tc.app.main;
    if (w) return w->dpi_scale;
    /* before sapp_run: report the main screen's scale (deliberate deviation
       from sokol_app.h, which returns 0 here) */
    const float s = (float)[NSScreen mainScreen].backingScaleFactor;
    return s > 0.0f ? s : 1.0f;
}

uint64_t sapp_frame_count(void) {
    return _sapp_tc.app.frame_count;
}

double sapp_frame_duration(void) {
    _sapp_tc_window_t* w = _sapp_tc.app.main;
    if (!w) return 1.0 / 60.0;
    if (_sapp_tc.app.fallback_timer != nil) return _sapp_tc.app.timing.smooth_dt;
    if (w->frame_duration > 0.0) return w->frame_duration;
    const double maxfps = (double)[NSScreen mainScreen].maximumFramesPerSecond;
    return (maxfps > 0.0) ? (1.0 / maxfps) : (1.0 / 60.0);
}

void sapp_request_quit(void) {
    _sapp_tc.app.quit_requested = true;
}

void sapp_cancel_quit(void) {
    _sapp_tc.app.quit_requested = false;
}

void sapp_quit(void) {
    _sapp_tc.app.quit_ordered = true;
}

void sapp_consume_event(void) {
    _sapp_tc.app.event_consumed = true;
}

void sapp_skip_present(void) {
    /* API parity: stored but ignored on Metal (upstream behaves the same;
       not starting a swapchain pass already skips the present) */
    _sapp_tc.app.skip_present = true;
}

void sapp_show_keyboard(bool show) {
    (void)show;     /* on-screen keyboard: mobile only */
}

bool sapp_keyboard_shown(void) {
    return false;
}

void sapp_show_mouse(bool show) {
    if (_sapp_tc.app.mouse_shown != show) {
        _sapp_tc_apply_cursor(_sapp_tc.app.current_cursor, show);
    }
}

bool sapp_mouse_shown(void) {
    return _sapp_tc.app.mouse_shown;
}

void sapp_set_mouse_cursor(sapp_mouse_cursor cursor) {
    if ((int)cursor < 0 || (int)cursor >= _SAPP_MOUSECURSOR_NUM) return;
    if (cursor != _sapp_tc.app.current_cursor) {
        _sapp_tc_apply_cursor(cursor, _sapp_tc.app.mouse_shown);
    }
}

sapp_mouse_cursor sapp_get_mouse_cursor(void) {
    return _sapp_tc.app.current_cursor;
}

sapp_mouse_cursor sapp_bind_mouse_cursor_image(sapp_mouse_cursor cursor, const sapp_image_desc* desc) {
    if ((int)cursor < 0 || (int)cursor >= _SAPP_MOUSECURSOR_NUM) return cursor;
    if (!desc || !desc->pixels.ptr || desc->width <= 0 || desc->height <= 0) return cursor;
    if (desc->pixels.size < (size_t)(desc->width * desc->height * 4)) return cursor;
    sapp_unbind_mouse_cursor_image(cursor);
    NSBitmapImageRep* rep = [[NSBitmapImageRep alloc]
        initWithBitmapDataPlanes:NULL
        pixelsWide:desc->width pixelsHigh:desc->height
        bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO
        colorSpaceName:NSCalibratedRGBColorSpace
        bitmapFormat:NSBitmapFormatAlphaNonpremultiplied
        bytesPerRow:desc->width * 4 bitsPerPixel:32];
    if (rep == nil) return cursor;
    memcpy(rep.bitmapData, desc->pixels.ptr, (size_t)(desc->width * desc->height * 4));
    NSImage* img = [[NSImage alloc] initWithSize:NSMakeSize(desc->width, desc->height)];
    [img addRepresentation:rep];
    NSCursor* cur = [[NSCursor alloc] initWithImage:img
        hotSpot:NSMakePoint(desc->cursor_hotspot_x, desc->cursor_hotspot_y)];
    _sapp_tc.app.custom_cursors[cursor] = cur;
    _sapp_tc.app.custom_cursor_bound[cursor] = true;
    if (_sapp_tc.app.current_cursor == cursor) {
        _sapp_tc_apply_cursor(cursor, _sapp_tc.app.mouse_shown);
    }
    return cursor;
}

void sapp_unbind_mouse_cursor_image(sapp_mouse_cursor cursor) {
    if ((int)cursor < 0 || (int)cursor >= _SAPP_MOUSECURSOR_NUM) return;
    if (!_sapp_tc.app.custom_cursor_bound[cursor]) return;
    _sapp_tc.app.custom_cursor_bound[cursor] = false;
    if (_sapp_tc.app.current_cursor == cursor) {
        _sapp_tc_apply_cursor(cursor, _sapp_tc.app.mouse_shown);
    }
    _sapp_tc.app.custom_cursors[cursor] = nil;
}

void sapp_set_clipboard_string(const char* str) {
    if (!_sapp_tc.app.clipboard_enabled || !str) return;
    @autoreleasepool {
        NSPasteboard* pboard = [NSPasteboard generalPasteboard];
        [pboard declareTypes:@[NSPasteboardTypeString] owner:nil];
        [pboard setString:[NSString stringWithUTF8String:str] forType:NSPasteboardTypeString];
    }
    strncpy(_sapp_tc.app.clipboard, str, (size_t)_sapp_tc.app.clipboard_size - 1);
    _sapp_tc.app.clipboard[_sapp_tc.app.clipboard_size - 1] = 0;
}

const char* sapp_get_clipboard_string(void) {
    if (!_sapp_tc.app.clipboard_enabled || !_sapp_tc.app.clipboard) return "";
    @autoreleasepool {
        _sapp_tc.app.clipboard[0] = 0;
        NSPasteboard* pboard = [NSPasteboard generalPasteboard];
        if ([pboard.types containsObject:NSPasteboardTypeString]) {
            NSString* str = [pboard stringForType:NSPasteboardTypeString];
            if (str) {
                strncpy(_sapp_tc.app.clipboard, str.UTF8String,
                        (size_t)_sapp_tc.app.clipboard_size - 1);
                _sapp_tc.app.clipboard[_sapp_tc.app.clipboard_size - 1] = 0;
            }
        }
    }
    return _sapp_tc.app.clipboard;
}

void sapp_set_window_title(const char* str) {
    if (!str) return;
    strncpy(_sapp_tc.app.window_title, str, sizeof(_sapp_tc.app.window_title) - 1);
    _sapp_tc_window_t* w = _sapp_tc.app.main;
    if (w && w->window) {
        w->window.title = [NSString stringWithUTF8String:_sapp_tc.app.window_title];
    }
}

bool sapp_is_fullscreen(void) {
    return _sapp_tc.app.fullscreen;
}

void sapp_toggle_fullscreen(void) {
    _sapp_tc_window_t* w = _sapp_tc.app.main;
    if (!w || !w->window) return;
    /* optimistic flip; the delegate notifications are the authority */
    _sapp_tc.app.fullscreen = !_sapp_tc.app.fullscreen;
    [w->window toggleFullScreen:nil];
}

int sapp_get_num_dropped_files(void) {
    return _sapp_tc.app.drop_enabled ? _sapp_tc.app.drop_num_files : 0;
}

const char* sapp_get_dropped_file_path(int index) {
    if (!_sapp_tc.app.drop_enabled || !_sapp_tc.app.drop_buffer) return "";
    if (index < 0 || index >= _sapp_tc.app.drop_num_files) return "";
    return _sapp_tc.app.drop_buffer + (size_t)index * (size_t)_sapp_tc.app.drop_max_path_length;
}

sapp_pixel_format sapp_color_format(void) {
    return SAPP_PIXELFORMAT_RGB10A2;    /* TrussC 10-bit output on Metal */
}

sapp_pixel_format sapp_depth_format(void) {
    return SAPP_PIXELFORMAT_DEPTH_STENCIL;
}

const void* sapp_macos_get_window(void) {
    _sapp_tc_window_t* w = _sapp_tc.app.main;
    return w ? (__bridge const void*)w->window : 0;
}

const void* sapp_metal_get_device(void) {
    return (__bridge const void*)_sapp_tc.app.device;
}

/* main-window drawable: acquired lazily HERE (upstream parity — sokol_gfx's
   begin-pass is the acquisition point via sglue_swapchain()). Inside a tick
   the drawable is cached so repeated queries stay safe; outside a tick each
   call acquires a fresh drawable, exactly like sokol_app.h. */
static id<CAMetalDrawable> _sapp_tc_main_next_drawable(void) {
    _sapp_tc_window_t* w = _sapp_tc.app.main;
    if (!w || !w->layer) return nil;
    if (w->in_tick) {
        if (w->frame_drawable == nil) {
            w->frame_drawable = [w->layer nextDrawable];
        }
        return w->frame_drawable;
    }
    return [w->layer nextDrawable];
}

const void* sapp_metal_get_current_drawable(void) {
    return (__bridge const void*)_sapp_tc_main_next_drawable();
}

const void* sapp_metal_get_depth_stencil_texture(void) {
    _sapp_tc_window_t* w = _sapp_tc.app.main;
    return w ? (__bridge const void*)w->depth_tex : 0;
}

const void* sapp_metal_get_msaa_color_texture(void) {
    _sapp_tc_window_t* w = _sapp_tc.app.main;
    if (!w || w->desc.sample_count <= 1) return 0;
    return (__bridge const void*)w->msaa_tex;
}

sapp_environment sapp_get_environment(void) {
    sapp_environment env;
    memset(&env, 0, sizeof(env));
    env.defaults.color_format = sapp_color_format();
    env.defaults.depth_format = sapp_depth_format();
    env.defaults.sample_count = sapp_sample_count();
    env.metal.device = (__bridge const void*)_sapp_tc.app.device;
    return env;
}

sapp_swapchain sapp_get_swapchain(void) {
    sapp_swapchain sc;
    memset(&sc, 0, sizeof(sc));
    _sapp_tc_window_t* w = _sapp_tc.app.main;
    if (!w) return sc;
    sc.width = sapp_width();
    sc.height = sapp_height();
    sc.sample_count = sapp_sample_count();
    sc.color_format = sapp_color_format();
    sc.depth_format = sapp_depth_format();
    sc.metal.current_drawable = (__bridge const void*)_sapp_tc_main_next_drawable();
    sc.metal.depth_stencil_texture = (__bridge const void*)w->depth_tex;
    sc.metal.msaa_color_texture = (w->desc.sample_count > 1)
        ? (__bridge const void*)w->msaa_tex : 0;
    return sc;
}

} /* extern "C" */

#elif defined(_WIN32)
/*== Windows (D3D11) ========================================================
    Implements the public sokol_app.h API on Windows plus the multi-window
    API. Structure mirrors sokol_app.h's win32/D3D11 backend (quit dance in
    WM_CLOSE, resize coalesced outside WM_SIZE, scancode-indexed keytable,
    borderless fullscreen, refcounted mouse capture, TrussC RGB10A2 +
    skip_present patches) but replaces the PeekMessage busy-render-loop:
    every swapchain is created with FRAME_LATENCY_WAITABLE_OBJECT and the
    loop blocks in MsgWaitForMultipleObjectsEx on the per-window waitables,
    so each window ticks at its own display's rate and the process sleeps
    when nothing is due. */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>   /* GET_X_LPARAM / GET_Y_LPARAM */
#include <shellapi.h>   /* DragAcceptFiles / DragQueryFileW */
#include <d3d11.h>
#include <dxgi1_3.h>    /* IDXGIFactory2 / IDXGISwapChain2 (frame latency waitable) */

#if defined(_MSC_VER)
#pragma comment (lib, "kernel32")
#pragma comment (lib, "user32")
#pragma comment (lib, "shell32")
#pragma comment (lib, "gdi32")
#pragma comment (lib, "dxgi")
#pragma comment (lib, "d3d11")
#endif

#if defined(SOKOL_APP_IMPL_INCLUDED)
#error "sokol_app_tc.h owns the Windows implementation of the sapp_* API; include sokol_app.h WITHOUT an implementation define in this TU"
#endif

#include <stdio.h>      /* freopen_s (console redirection) */

#ifndef WM_MOUSEHWHEEL
#define WM_MOUSEHWHEEL (0x020E)
#endif
#ifndef WM_DPICHANGED
#define WM_DPICHANGED (0x02E0)
#endif

#define _SAPP_TC_MAX_WINDOWS (32)
#define _SAPP_TC_MODAL_TIMER (1)
#define _SAPP_TC_RELEASE(obj) do { if (obj) { (obj)->Release(); (obj) = 0; } } while (0)

/* EMA-filtered frame timer, port of sokol_app.h's _sapp_timing_* (QPC-based;
   Windows has no display-link timestamps, so this is the only dt source) */
typedef struct {
    double last;        /* previous sample time, 0 = none yet */
    double ema;
    double smooth_dt;
    double dt;          /* last raw (clamped) delta */
} _sapp_tc_timing_t;

typedef struct _sapp_tc_window_t {
    uint32_t win_id;
    sapp_window_desc desc;
    bool is_main;               /* window #0: created by sapp_run, drives the app callbacks */
    bool ready;                 /* creation finished; wndproc may deliver events */
    HWND hwnd;
    HMONITOR hmonitor;
    DXGI_FORMAT color_fmt;
    UINT swapchain_flags;       /* MUST be passed unchanged to every ResizeBuffers */
    IDXGISwapChain1* swap_chain;
    HANDLE frame_wait;          /* frame latency waitable (NULL: timer-paced fallback) */
    bool credit_held;           /* a waitable credit was consumed without a Present yet */
    bool waitable_ready;        /* the run-loop's MsgWait consumed this window's frame
                                   latency waitable signal (auto-reset): the due-check
                                   must NOT re-wait the same handle (it would miss the
                                   already-consumed signal and stall forever) */
    ID3D11Texture2D* rt;        /* swapchain backbuffer */
    ID3D11RenderTargetView* rtv;
    ID3D11Texture2D* msaa_rt;   /* MSAA color (sample_count > 1; flip model can't MSAA the backbuffer) */
    ID3D11RenderTargetView* msaa_rtv;
    ID3D11Texture2D* ds;        /* depth-stencil */
    ID3D11DepthStencilView* dsv;
    int fb_width, fb_height;    /* framebuffer pixels */
    int win_width, win_height;  /* logical points */
    float window_scale;         /* OS DPI / 96 (physical px per point) */
    float dpi_scale;            /* framebuffer px per point (content scale) */
    float mouse_scale;          /* client px -> event coordinate (framebuffer px) */
    double refresh_period;      /* this window's display refresh period (pacing fallback) */
    double earliest_next;       /* QPC seconds; window is not due before this */
    double frame_duration;
    _sapp_tc_timing_t timing;
    bool occluded;
    bool iconified;
    bool in_tick;
    bool mouse_tracked;         /* TrackMouseEvent enter/leave state */
    bool mouse_pos_valid;
    float mouse_x, mouse_y;     /* last position in event coordinates */
    float mouse_dx, mouse_dy;
    uint8_t capture_mask;       /* SetCapture refcount by button bit */
    WCHAR surrogate;            /* WM_CHAR high-surrogate accumulator */
    RECT stored_window_rect;    /* windowed rect for fullscreen restore */
} _sapp_tc_window_t;

static struct {
    bool keytable_valid;
    sapp_keycode keycodes[SAPP_MAX_KEYCODES];
    uint32_t next_id;
    _sapp_tc_window_t* windows[_SAPP_TC_MAX_WINDOWS];
    bool wndclass_registered;
    double qpc_period;              /* seconds per QueryPerformanceCounter tick */
    /* dynamically loaded DPI functions (user32.dll, Win10 1607+) */
    BOOL (WINAPI* SetProcessDpiAwarenessContext)(void*);
    UINT (WINAPI* GetDpiForWindow)(HWND);
    BOOL (WINAPI* AdjustWindowRectExForDpi)(LPRECT, DWORD, BOOL, DWORD, UINT);

    /* ---- app / main-window state (this header owns sapp_run on Windows) -- */
    struct {
        sapp_desc desc;             /* sanitized copy; window_title points at the buffer below */
        char window_title[256];
        WCHAR window_title_wide[256];
        bool valid;
        bool init_called;
        bool cleanup_called;
        bool quit_requested;
        bool quit_ordered;
        bool fullscreen;
        bool event_consumed;
        bool skip_present;          /* one-shot; honored on D3D11 (TrussC flicker patch) */
        bool dpi_aware;
        uint64_t frame_count;
        _sapp_tc_window_t* main;
        ID3D11Device* device;
        ID3D11DeviceContext* device_context;
        /* mouse cursor */
        bool mouse_shown;
        sapp_mouse_cursor current_cursor;
        HCURSOR standard_cursors[_SAPP_MOUSECURSOR_NUM];
        HCURSOR custom_cursors[_SAPP_MOUSECURSOR_NUM];
        bool custom_cursor_bound[_SAPP_MOUSECURSOR_NUM];
        /* console */
        bool console_utf8_set;
        UINT orig_codepage;
        /* clipboard */
        bool clipboard_enabled;
        int clipboard_size;
        char* clipboard;
        /* drag & drop */
        bool drop_enabled;
        int drop_max_files;
        int drop_max_path_length;
        int drop_num_files;
        char* drop_buffer;
    } app;
} _sapp_tc;

static void _sapp_tc_win32_tick(_sapp_tc_window_t* w, bool from_modal);
static bool _sapp_tc_win32_update_dimensions(_sapp_tc_window_t* w);
static void _sapp_tc_d3d11_resize(_sapp_tc_window_t* w);
static void _sapp_tc_win32_apply_cursor(sapp_mouse_cursor cursor, bool shown, bool skip_area_test);

/*-- timing -----------------------------------------------------------------*/
static double _sapp_tc_now(void) {
    LARGE_INTEGER qpc;
    QueryPerformanceCounter(&qpc);
    return (double)qpc.QuadPart * _sapp_tc.qpc_period;
}

static void _sapp_tc_timing_reset(_sapp_tc_timing_t* t) {
    t->last = 0.0;
    t->ema = t->smooth_dt = t->dt = 1.0 / 60.0;
}

static double _sapp_tc_timing_clamp(double dt) {
    if (dt < 0.000001) return 0.000001;
    if (dt > 0.1) return 0.1;
    return dt;
}

static void _sapp_tc_timing_update(_sapp_tc_timing_t* t) {
    const double now = _sapp_tc_now();
    if (t->last > 0.0) {
        double dt = _sapp_tc_timing_clamp(now - t->last);
        t->dt = dt;
        /* big jump: reset the filter; otherwise EMA-smooth */
        if (fabs(dt - t->smooth_dt) > 0.004) {
            t->ema = t->smooth_dt = dt;
        } else {
            t->ema += 0.025 * (dt - t->ema);
            t->smooth_dt = _sapp_tc_timing_clamp(t->ema);
        }
    }
    t->last = now;
}

/* keycode table lifted verbatim from sokol_app.h's _sapp_win32_init_keytable();
   indexed by the 9-bit Win32 scancode (HIWORD(lParam) & 0x1FF -- the 0x100
   extended bit distinguishes e.g. left/right Ctrl and keypad Enter) */
static void _sapp_tc_init_keytable(void) {
    if (_sapp_tc.keytable_valid) return;
    _sapp_tc.keytable_valid = true;
    _sapp_tc.keycodes[0x00B] = SAPP_KEYCODE_0;
    _sapp_tc.keycodes[0x002] = SAPP_KEYCODE_1;
    _sapp_tc.keycodes[0x003] = SAPP_KEYCODE_2;
    _sapp_tc.keycodes[0x004] = SAPP_KEYCODE_3;
    _sapp_tc.keycodes[0x005] = SAPP_KEYCODE_4;
    _sapp_tc.keycodes[0x006] = SAPP_KEYCODE_5;
    _sapp_tc.keycodes[0x007] = SAPP_KEYCODE_6;
    _sapp_tc.keycodes[0x008] = SAPP_KEYCODE_7;
    _sapp_tc.keycodes[0x009] = SAPP_KEYCODE_8;
    _sapp_tc.keycodes[0x00A] = SAPP_KEYCODE_9;
    _sapp_tc.keycodes[0x01E] = SAPP_KEYCODE_A;
    _sapp_tc.keycodes[0x030] = SAPP_KEYCODE_B;
    _sapp_tc.keycodes[0x02E] = SAPP_KEYCODE_C;
    _sapp_tc.keycodes[0x020] = SAPP_KEYCODE_D;
    _sapp_tc.keycodes[0x012] = SAPP_KEYCODE_E;
    _sapp_tc.keycodes[0x021] = SAPP_KEYCODE_F;
    _sapp_tc.keycodes[0x022] = SAPP_KEYCODE_G;
    _sapp_tc.keycodes[0x023] = SAPP_KEYCODE_H;
    _sapp_tc.keycodes[0x017] = SAPP_KEYCODE_I;
    _sapp_tc.keycodes[0x024] = SAPP_KEYCODE_J;
    _sapp_tc.keycodes[0x025] = SAPP_KEYCODE_K;
    _sapp_tc.keycodes[0x026] = SAPP_KEYCODE_L;
    _sapp_tc.keycodes[0x032] = SAPP_KEYCODE_M;
    _sapp_tc.keycodes[0x031] = SAPP_KEYCODE_N;
    _sapp_tc.keycodes[0x018] = SAPP_KEYCODE_O;
    _sapp_tc.keycodes[0x019] = SAPP_KEYCODE_P;
    _sapp_tc.keycodes[0x010] = SAPP_KEYCODE_Q;
    _sapp_tc.keycodes[0x013] = SAPP_KEYCODE_R;
    _sapp_tc.keycodes[0x01F] = SAPP_KEYCODE_S;
    _sapp_tc.keycodes[0x014] = SAPP_KEYCODE_T;
    _sapp_tc.keycodes[0x016] = SAPP_KEYCODE_U;
    _sapp_tc.keycodes[0x02F] = SAPP_KEYCODE_V;
    _sapp_tc.keycodes[0x011] = SAPP_KEYCODE_W;
    _sapp_tc.keycodes[0x02D] = SAPP_KEYCODE_X;
    _sapp_tc.keycodes[0x015] = SAPP_KEYCODE_Y;
    _sapp_tc.keycodes[0x02C] = SAPP_KEYCODE_Z;
    _sapp_tc.keycodes[0x028] = SAPP_KEYCODE_APOSTROPHE;
    _sapp_tc.keycodes[0x02B] = SAPP_KEYCODE_BACKSLASH;
    _sapp_tc.keycodes[0x033] = SAPP_KEYCODE_COMMA;
    _sapp_tc.keycodes[0x00D] = SAPP_KEYCODE_EQUAL;
    _sapp_tc.keycodes[0x029] = SAPP_KEYCODE_GRAVE_ACCENT;
    _sapp_tc.keycodes[0x01A] = SAPP_KEYCODE_LEFT_BRACKET;
    _sapp_tc.keycodes[0x00C] = SAPP_KEYCODE_MINUS;
    _sapp_tc.keycodes[0x034] = SAPP_KEYCODE_PERIOD;
    _sapp_tc.keycodes[0x01B] = SAPP_KEYCODE_RIGHT_BRACKET;
    _sapp_tc.keycodes[0x027] = SAPP_KEYCODE_SEMICOLON;
    _sapp_tc.keycodes[0x035] = SAPP_KEYCODE_SLASH;
    _sapp_tc.keycodes[0x056] = SAPP_KEYCODE_WORLD_2;
    _sapp_tc.keycodes[0x00E] = SAPP_KEYCODE_BACKSPACE;
    _sapp_tc.keycodes[0x153] = SAPP_KEYCODE_DELETE;
    _sapp_tc.keycodes[0x14F] = SAPP_KEYCODE_END;
    _sapp_tc.keycodes[0x01C] = SAPP_KEYCODE_ENTER;
    _sapp_tc.keycodes[0x001] = SAPP_KEYCODE_ESCAPE;
    _sapp_tc.keycodes[0x147] = SAPP_KEYCODE_HOME;
    _sapp_tc.keycodes[0x152] = SAPP_KEYCODE_INSERT;
    _sapp_tc.keycodes[0x15D] = SAPP_KEYCODE_MENU;
    _sapp_tc.keycodes[0x151] = SAPP_KEYCODE_PAGE_DOWN;
    _sapp_tc.keycodes[0x149] = SAPP_KEYCODE_PAGE_UP;
    _sapp_tc.keycodes[0x045] = SAPP_KEYCODE_PAUSE;
    _sapp_tc.keycodes[0x146] = SAPP_KEYCODE_PAUSE;
    _sapp_tc.keycodes[0x039] = SAPP_KEYCODE_SPACE;
    _sapp_tc.keycodes[0x00F] = SAPP_KEYCODE_TAB;
    _sapp_tc.keycodes[0x03A] = SAPP_KEYCODE_CAPS_LOCK;
    _sapp_tc.keycodes[0x145] = SAPP_KEYCODE_NUM_LOCK;
    _sapp_tc.keycodes[0x046] = SAPP_KEYCODE_SCROLL_LOCK;
    _sapp_tc.keycodes[0x03B] = SAPP_KEYCODE_F1;
    _sapp_tc.keycodes[0x03C] = SAPP_KEYCODE_F2;
    _sapp_tc.keycodes[0x03D] = SAPP_KEYCODE_F3;
    _sapp_tc.keycodes[0x03E] = SAPP_KEYCODE_F4;
    _sapp_tc.keycodes[0x03F] = SAPP_KEYCODE_F5;
    _sapp_tc.keycodes[0x040] = SAPP_KEYCODE_F6;
    _sapp_tc.keycodes[0x041] = SAPP_KEYCODE_F7;
    _sapp_tc.keycodes[0x042] = SAPP_KEYCODE_F8;
    _sapp_tc.keycodes[0x043] = SAPP_KEYCODE_F9;
    _sapp_tc.keycodes[0x044] = SAPP_KEYCODE_F10;
    _sapp_tc.keycodes[0x057] = SAPP_KEYCODE_F11;
    _sapp_tc.keycodes[0x058] = SAPP_KEYCODE_F12;
    _sapp_tc.keycodes[0x064] = SAPP_KEYCODE_F13;
    _sapp_tc.keycodes[0x065] = SAPP_KEYCODE_F14;
    _sapp_tc.keycodes[0x066] = SAPP_KEYCODE_F15;
    _sapp_tc.keycodes[0x067] = SAPP_KEYCODE_F16;
    _sapp_tc.keycodes[0x068] = SAPP_KEYCODE_F17;
    _sapp_tc.keycodes[0x069] = SAPP_KEYCODE_F18;
    _sapp_tc.keycodes[0x06A] = SAPP_KEYCODE_F19;
    _sapp_tc.keycodes[0x06B] = SAPP_KEYCODE_F20;
    _sapp_tc.keycodes[0x06C] = SAPP_KEYCODE_F21;
    _sapp_tc.keycodes[0x06D] = SAPP_KEYCODE_F22;
    _sapp_tc.keycodes[0x06E] = SAPP_KEYCODE_F23;
    _sapp_tc.keycodes[0x076] = SAPP_KEYCODE_F24;
    _sapp_tc.keycodes[0x038] = SAPP_KEYCODE_LEFT_ALT;
    _sapp_tc.keycodes[0x01D] = SAPP_KEYCODE_LEFT_CONTROL;
    _sapp_tc.keycodes[0x02A] = SAPP_KEYCODE_LEFT_SHIFT;
    _sapp_tc.keycodes[0x15B] = SAPP_KEYCODE_LEFT_SUPER;
    _sapp_tc.keycodes[0x137] = SAPP_KEYCODE_PRINT_SCREEN;
    _sapp_tc.keycodes[0x138] = SAPP_KEYCODE_RIGHT_ALT;
    _sapp_tc.keycodes[0x11D] = SAPP_KEYCODE_RIGHT_CONTROL;
    _sapp_tc.keycodes[0x036] = SAPP_KEYCODE_RIGHT_SHIFT;
    _sapp_tc.keycodes[0x136] = SAPP_KEYCODE_RIGHT_SHIFT;
    _sapp_tc.keycodes[0x15C] = SAPP_KEYCODE_RIGHT_SUPER;
    _sapp_tc.keycodes[0x150] = SAPP_KEYCODE_DOWN;
    _sapp_tc.keycodes[0x14B] = SAPP_KEYCODE_LEFT;
    _sapp_tc.keycodes[0x14D] = SAPP_KEYCODE_RIGHT;
    _sapp_tc.keycodes[0x148] = SAPP_KEYCODE_UP;
    _sapp_tc.keycodes[0x052] = SAPP_KEYCODE_KP_0;
    _sapp_tc.keycodes[0x04F] = SAPP_KEYCODE_KP_1;
    _sapp_tc.keycodes[0x050] = SAPP_KEYCODE_KP_2;
    _sapp_tc.keycodes[0x051] = SAPP_KEYCODE_KP_3;
    _sapp_tc.keycodes[0x04B] = SAPP_KEYCODE_KP_4;
    _sapp_tc.keycodes[0x04C] = SAPP_KEYCODE_KP_5;
    _sapp_tc.keycodes[0x04D] = SAPP_KEYCODE_KP_6;
    _sapp_tc.keycodes[0x047] = SAPP_KEYCODE_KP_7;
    _sapp_tc.keycodes[0x048] = SAPP_KEYCODE_KP_8;
    _sapp_tc.keycodes[0x049] = SAPP_KEYCODE_KP_9;
    _sapp_tc.keycodes[0x04E] = SAPP_KEYCODE_KP_ADD;
    _sapp_tc.keycodes[0x053] = SAPP_KEYCODE_KP_DECIMAL;
    _sapp_tc.keycodes[0x135] = SAPP_KEYCODE_KP_DIVIDE;
    _sapp_tc.keycodes[0x11C] = SAPP_KEYCODE_KP_ENTER;
    _sapp_tc.keycodes[0x037] = SAPP_KEYCODE_KP_MULTIPLY;
    _sapp_tc.keycodes[0x04A] = SAPP_KEYCODE_KP_SUBTRACT;
}

static sapp_keycode _sapp_tc_translate_key(int scancode) {
    if ((scancode >= 0) && (scancode < SAPP_MAX_KEYCODES)) {
        return _sapp_tc.keycodes[scancode];
    }
    return SAPP_KEYCODE_INVALID;
}

static _sapp_tc_window_t* _sapp_tc_lookup(sapp_window win) {
    if (win.id == 0) return 0;
    for (int i = 0; i < _SAPP_TC_MAX_WINDOWS; i++) {
        _sapp_tc_window_t* w = _sapp_tc.windows[i];
        if (w && w->win_id == win.id) return w;
    }
    return 0;
}

/*-- UTF-8 <-> UTF-16 -------------------------------------------------------*/
static bool _sapp_tc_win32_utf8_to_wide(const char* src, wchar_t* dst, int dst_num) {
    dst[0] = 0;
    if (0 == MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, dst_num)) {
        dst[dst_num - 1] = 0;
        return false;
    }
    return true;
}

static bool _sapp_tc_win32_wide_to_utf8(const wchar_t* src, char* dst, int dst_size) {
    dst[0] = 0;
    if (0 == WideCharToMultiByte(CP_UTF8, 0, src, -1, dst, dst_size, NULL, NULL)) {
        dst[dst_size - 1] = 0;
        return false;
    }
    return true;
}

/*-- event plumbing ---------------------------------------------------------*/
static uint32_t _sapp_tc_win32_mods(void) {
    uint32_t m = 0;
    if (GetKeyState(VK_SHIFT) & 0x8000)   m |= SAPP_MODIFIER_SHIFT;
    if (GetKeyState(VK_CONTROL) & 0x8000) m |= SAPP_MODIFIER_CTRL;
    if (GetKeyState(VK_MENU) & 0x8000)    m |= SAPP_MODIFIER_ALT;
    if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) m |= SAPP_MODIFIER_SUPER;
    const bool swapped = (0 != GetSystemMetrics(SM_SWAPBUTTON));
    if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) m |= swapped ? SAPP_MODIFIER_RMB : SAPP_MODIFIER_LMB;
    if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) m |= swapped ? SAPP_MODIFIER_LMB : SAPP_MODIFIER_RMB;
    if (GetAsyncKeyState(VK_MBUTTON) & 0x8000) m |= SAPP_MODIFIER_MMB;
    return m;
}

static void _sapp_tc_send(_sapp_tc_window_t* w, sapp_event* ev) {
    if (!w->desc.event_cb) return;
    ev->frame_count = _sapp_tc.app.frame_count;
    ev->window_width = w->win_width;
    ev->window_height = w->win_height;
    ev->framebuffer_width = w->fb_width;
    ev->framebuffer_height = w->fb_height;
    sapp_window handle = { w->win_id };
    w->desc.event_cb(ev, handle, w->desc.user_data);
}

/*-- main window: event routing to the sapp_desc callbacks ------------------*/
static bool _sapp_tc_app_events_enabled(void) {
    /* same gate as sokol_app.h: nothing fires before the first tick's init_cb */
    return _sapp_tc.app.init_called &&
        (_sapp_tc.app.desc.event_cb || _sapp_tc.app.desc.event_userdata_cb);
}

static void _sapp_tc_main_event_tramp(const sapp_event* ev, sapp_window win, void* user) {
    (void)win; (void)user;
    if (!_sapp_tc_app_events_enabled()) return;
    _sapp_tc.app.event_consumed = false;
    if (_sapp_tc.app.desc.event_cb) {
        _sapp_tc.app.desc.event_cb(ev);
    } else {
        _sapp_tc.app.desc.event_userdata_cb(ev, _sapp_tc.app.desc.user_data);
    }
}

/*-- mouse cursor (app-global, same model as sokol_app.h) -------------------*/
static void _sapp_tc_win32_init_cursor(sapp_mouse_cursor cursor, DWORD ocr_id) {
    HCURSOR c = (HCURSOR)LoadImageW(NULL, MAKEINTRESOURCEW(ocr_id), IMAGE_CURSOR,
                                    0, 0, LR_DEFAULTSIZE | LR_SHARED);
    if (!c) c = LoadCursor(NULL, IDC_ARROW);
    _sapp_tc.app.standard_cursors[cursor] = c;
}

static void _sapp_tc_win32_init_cursors(void) {
    /* OCR_* resource ids inlined (OEMRESOURCE may not be defined) */
    _sapp_tc_win32_init_cursor(SAPP_MOUSECURSOR_DEFAULT, 32512);        /* OCR_NORMAL */
    _sapp_tc_win32_init_cursor(SAPP_MOUSECURSOR_ARROW, 32512);          /* OCR_NORMAL */
    _sapp_tc_win32_init_cursor(SAPP_MOUSECURSOR_IBEAM, 32513);          /* OCR_IBEAM */
    _sapp_tc_win32_init_cursor(SAPP_MOUSECURSOR_CROSSHAIR, 32515);      /* OCR_CROSS */
    _sapp_tc_win32_init_cursor(SAPP_MOUSECURSOR_POINTING_HAND, 32649);  /* OCR_HAND */
    _sapp_tc_win32_init_cursor(SAPP_MOUSECURSOR_RESIZE_EW, 32644);      /* OCR_SIZEWE */
    _sapp_tc_win32_init_cursor(SAPP_MOUSECURSOR_RESIZE_NS, 32645);      /* OCR_SIZENS */
    _sapp_tc_win32_init_cursor(SAPP_MOUSECURSOR_RESIZE_NWSE, 32642);    /* OCR_SIZENWSE */
    _sapp_tc_win32_init_cursor(SAPP_MOUSECURSOR_RESIZE_NESW, 32643);    /* OCR_SIZENESW */
    _sapp_tc_win32_init_cursor(SAPP_MOUSECURSOR_RESIZE_ALL, 32646);     /* OCR_SIZEALL */
    _sapp_tc_win32_init_cursor(SAPP_MOUSECURSOR_NOT_ALLOWED, 32648);    /* OCR_NO */
}

static bool _sapp_tc_win32_cursor_in_content_area(void) {
    POINT pos;
    if (!GetCursorPos(&pos)) return false;
    HWND hovered = WindowFromPoint(pos);
    for (int i = 0; i < _SAPP_TC_MAX_WINDOWS; i++) {
        _sapp_tc_window_t* w = _sapp_tc.windows[i];
        if (!w || w->hwnd != hovered) continue;
        RECT area;
        if (!GetClientRect(w->hwnd, &area)) return false;
        ScreenToClient(w->hwnd, &pos);
        return PtInRect(&area, pos) == TRUE;
    }
    return false;
}

static void _sapp_tc_win32_apply_cursor(sapp_mouse_cursor cursor, bool shown, bool skip_area_test) {
    _sapp_tc.app.current_cursor = cursor;
    _sapp_tc.app.mouse_shown = shown;
    /* WM_SETCURSOR passes skip_area_test=true (it fires for HTCLIENT only);
       the public setters check that the pointer is actually over one of our
       client areas before touching the global cursor */
    if (!skip_area_test && !_sapp_tc_win32_cursor_in_content_area()) return;
    HCURSOR c = NULL;   /* SetCursor(NULL) hides (avoids the ShowCursor counter) */
    if (shown) {
        if (_sapp_tc.app.custom_cursor_bound[cursor] && _sapp_tc.app.custom_cursors[cursor]) {
            c = _sapp_tc.app.custom_cursors[cursor];
        } else {
            c = _sapp_tc.app.standard_cursors[cursor];
        }
        if (!c) c = LoadCursor(NULL, IDC_ARROW);
    }
    SetCursor(c);
}

static HCURSOR _sapp_tc_win32_create_cursor(const sapp_image_desc* desc) {
    BITMAPV5HEADER bi;
    memset(&bi, 0, sizeof(bi));
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = (LONG)desc->width;
    bi.bV5Height = -(LONG)desc->height;   /* top-down */
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask   = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask  = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;
    uint8_t* target = 0;
    HDC dc = GetDC(NULL);
    HBITMAP color = CreateDIBSection(dc, (BITMAPINFO*)&bi, DIB_RGB_COLORS,
                                     (void**)&target, NULL, 0);
    ReleaseDC(NULL, dc);
    if (!color) return NULL;
    HBITMAP mask = CreateBitmap(desc->width, desc->height, 1, 1, NULL);
    if (!mask) { DeleteObject(color); return NULL; }
    /* RGBA -> BGRA */
    const uint8_t* src = (const uint8_t*)desc->pixels.ptr;
    const int num_pixels = desc->width * desc->height;
    for (int i = 0; i < num_pixels; i++) {
        target[i * 4 + 0] = src[i * 4 + 2];
        target[i * 4 + 1] = src[i * 4 + 1];
        target[i * 4 + 2] = src[i * 4 + 0];
        target[i * 4 + 3] = src[i * 4 + 3];
    }
    ICONINFO ii;
    memset(&ii, 0, sizeof(ii));
    ii.fIcon = FALSE;   /* cursor, not icon (hotspot applies) */
    ii.xHotspot = (DWORD)desc->cursor_hotspot_x;
    ii.yHotspot = (DWORD)desc->cursor_hotspot_y;
    ii.hbmMask = mask;
    ii.hbmColor = color;
    HCURSOR c = (HCURSOR)CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return c;
}

/*-- DPI / display ----------------------------------------------------------*/
static void _sapp_tc_win32_init_dpi(void) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        _sapp_tc.SetProcessDpiAwarenessContext =
            (BOOL(WINAPI*)(void*))(void*)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        _sapp_tc.GetDpiForWindow =
            (UINT(WINAPI*)(HWND))(void*)GetProcAddress(user32, "GetDpiForWindow");
        _sapp_tc.AdjustWindowRectExForDpi =
            (BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT))(void*)GetProcAddress(user32, "AdjustWindowRectExForDpi");
    }
    /* D3D11 backend is always DPI-aware; PER_MONITOR_AWARE_V2 when available
       (Win10 1703+; the -4 magic constant avoids requiring a newer SDK) */
    if (_sapp_tc.SetProcessDpiAwarenessContext) {
        _sapp_tc.SetProcessDpiAwarenessContext((void*)(intptr_t)-4);
        _sapp_tc.app.dpi_aware = true;
    } else {
        _sapp_tc.app.dpi_aware = (TRUE == SetProcessDPIAware());
    }
}

/* one dpi ratio source per window: the scale used to size the framebuffer is
   the scale reported by sapp_window_dpi_scale() and applied to mouse coords */
static void _sapp_tc_win32_update_scale(_sapp_tc_window_t* w) {
    UINT dpi = 96;
    if (_sapp_tc.app.dpi_aware && _sapp_tc.GetDpiForWindow && w->hwnd) {
        dpi = _sapp_tc.GetDpiForWindow(w->hwnd);
        if (dpi == 0) dpi = 96;
    }
    w->window_scale = (float)dpi / 96.0f;
    const bool high_dpi = !w->desc.no_high_dpi;
    w->dpi_scale = high_dpi ? w->window_scale : 1.0f;
    w->mouse_scale = high_dpi ? 1.0f : 1.0f / w->window_scale;
}

static void _sapp_tc_win32_update_refresh(_sapp_tc_window_t* w) {
    /* this window's display refresh period: the timer-pacing fallback for
       ticks that end without a Present (not the primary pacing source --
       that is the swapchain's frame latency waitable) */
    w->refresh_period = 1.0 / 60.0;
    if (!w->hmonitor) return;
    MONITORINFOEXW mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(w->hmonitor, (MONITORINFO*)&mi)) {
        DEVMODEW dm;
        memset(&dm, 0, sizeof(dm));
        dm.dmSize = sizeof(dm);
        if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) &&
            (dm.dmDisplayFrequency > 1)) {
            w->refresh_period = 1.0 / (double)dm.dmDisplayFrequency;
        }
    }
}

/*-- console (process-global) -----------------------------------------------*/
static void _sapp_tc_win32_init_console(void) {
    const sapp_desc* d = &_sapp_tc.app.desc;
    if (d->win32.console_create || d->win32.console_attach) {
        BOOL con_valid = FALSE;
        if (d->win32.console_attach) {
            con_valid = AttachConsole(ATTACH_PARENT_PROCESS);
        }
        if (!con_valid && d->win32.console_create) {
            con_valid = AllocConsole();
        }
        if (con_valid) {
            FILE* res_o = NULL;
            FILE* res_e = NULL;
            freopen_s(&res_o, "CON", "w", stdout);
            freopen_s(&res_e, "CON", "w", stderr);
        }
    }
    if (d->win32.console_utf8) {
        _sapp_tc.app.orig_codepage = GetConsoleOutputCP();
        if (SetConsoleOutputCP(CP_UTF8)) {
            _sapp_tc.app.console_utf8_set = true;
        }
    }
}

static void _sapp_tc_win32_restore_console(void) {
    if (_sapp_tc.app.console_utf8_set) {
        SetConsoleOutputCP(_sapp_tc.app.orig_codepage);
    }
}

/*-- D3D11 / DXGI -----------------------------------------------------------*/
static HRESULT _sapp_tc_d3d11_try_create_device(UINT flags) {
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL out_level;
    HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags,
        levels, 2, D3D11_SDK_VERSION,
        &_sapp_tc.app.device, &out_level, &_sapp_tc.app.device_context);
    if (FAILED(hr)) {
        /* 11.1-unaware runtimes report E_INVALIDARG for the 11_1 entry */
        hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags,
            &levels[1], 1, D3D11_SDK_VERSION,
            &_sapp_tc.app.device, &out_level, &_sapp_tc.app.device_context);
    }
    return hr;
}

static bool _sapp_tc_d3d11_create_device(void) {
    UINT flags = D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(SOKOL_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    HRESULT hr = _sapp_tc_d3d11_try_create_device(flags);
#if defined(SOKOL_DEBUG)
    if (FAILED(hr)) {
        /* debug layer not installed: retry without it (upstream behavior) */
        hr = _sapp_tc_d3d11_try_create_device(flags & ~(UINT)D3D11_CREATE_DEVICE_DEBUG);
    }
#endif
    return SUCCEEDED(hr);
}

static void _sapp_tc_d3d11_destroy_render_targets(_sapp_tc_window_t* w) {
    _SAPP_TC_RELEASE(w->msaa_rtv);
    _SAPP_TC_RELEASE(w->msaa_rt);
    _SAPP_TC_RELEASE(w->dsv);
    _SAPP_TC_RELEASE(w->ds);
    _SAPP_TC_RELEASE(w->rtv);
    _SAPP_TC_RELEASE(w->rt);
}

static bool _sapp_tc_d3d11_create_render_targets(_sapp_tc_window_t* w) {
    ID3D11Device* dev = _sapp_tc.app.device;
    if (!dev || !w->swap_chain) return false;
    if (FAILED(w->swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&w->rt))) return false;
    if (FAILED(dev->CreateRenderTargetView((ID3D11Resource*)w->rt, NULL, &w->rtv))) return false;
    D3D11_TEXTURE2D_DESC td;
    memset(&td, 0, sizeof(td));
    td.Width = (UINT)(w->fb_width > 0 ? w->fb_width : 1);
    td.Height = (UINT)(w->fb_height > 0 ? w->fb_height : 1);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.SampleDesc.Count = (UINT)w->desc.sample_count;
    td.SampleDesc.Quality = (w->desc.sample_count > 1) ? (UINT)D3D11_STANDARD_MULTISAMPLE_PATTERN : 0;
    if (w->desc.sample_count > 1) {
        /* flip-model swapchains are always single-sampled; render into a
           separate MSAA texture, sokol_gfx resolves into the backbuffer */
        td.Format = w->color_fmt;
        td.BindFlags = D3D11_BIND_RENDER_TARGET;
        if (FAILED(dev->CreateTexture2D(&td, NULL, &w->msaa_rt))) return false;
        if (FAILED(dev->CreateRenderTargetView((ID3D11Resource*)w->msaa_rt, NULL, &w->msaa_rtv))) return false;
    }
    td.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(dev->CreateTexture2D(&td, NULL, &w->ds))) return false;
    if (FAILED(dev->CreateDepthStencilView((ID3D11Resource*)w->ds, NULL, &w->dsv))) return false;
    return true;
}

static void _sapp_tc_d3d11_resize(_sapp_tc_window_t* w) {
    if (!w->swap_chain) return;
    _sapp_tc_d3d11_destroy_render_targets(w);
    /* flip model: EVERY backbuffer reference must be gone before
       ResizeBuffers, including an RTV still bound on the shared immediate
       context -- otherwise ResizeBuffers fails and the next Present crashes */
    if (_sapp_tc.app.device_context) {
        _sapp_tc.app.device_context->OMSetRenderTargets(0, NULL, NULL);
        _sapp_tc.app.device_context->Flush();
    }
    /* the creation flags (frame latency waitable!) must be passed unchanged
       on every resize, or the waitable handle is silently invalidated */
    w->swap_chain->ResizeBuffers(2, (UINT)w->fb_width, (UINT)w->fb_height,
                                 w->color_fmt, w->swapchain_flags);
    _sapp_tc_d3d11_create_render_targets(w);
}

static bool _sapp_tc_d3d11_create_swapchain(_sapp_tc_window_t* w) {
    IDXGIDevice1* dxgi_device = 0;
    IDXGIAdapter* adapter = 0;
    IDXGIFactory2* factory = 0;
    if (FAILED(_sapp_tc.app.device->QueryInterface(__uuidof(IDXGIDevice1), (void**)&dxgi_device))) {
        return false;
    }
    bool ok = false;
    if (SUCCEEDED(dxgi_device->GetAdapter(&adapter)) &&
        SUCCEEDED(adapter->GetParent(__uuidof(IDXGIFactory2), (void**)&factory))) {
        DXGI_SWAP_CHAIN_DESC1 d;
        memset(&d, 0, sizeof(d));
        d.Width = (UINT)(w->fb_width > 0 ? w->fb_width : 1);
        d.Height = (UINT)(w->fb_height > 0 ? w->fb_height : 1);
        d.Format = w->color_fmt;
        d.SampleDesc.Count = 1;     /* MSAA lives in a separate render target */
        d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        d.BufferCount = 2;
        d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        d.Scaling = DXGI_SCALING_STRETCH;
        d.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        d.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        HRESULT hr = factory->CreateSwapChainForHwnd((IUnknown*)_sapp_tc.app.device,
            w->hwnd, &d, NULL, NULL, &w->swap_chain);
        if (FAILED(hr)) {
            /* no waitable support: plain flip swapchain, timer-paced ticks */
            d.Flags = 0;
            hr = factory->CreateSwapChainForHwnd((IUnknown*)_sapp_tc.app.device,
                w->hwnd, &d, NULL, NULL, &w->swap_chain);
        }
        if (SUCCEEDED(hr)) {
            w->swapchain_flags = d.Flags;
            /* fullscreen is driven here (borderless); kill DXGI's Alt-Enter */
            factory->MakeWindowAssociation(w->hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_PRINT_SCREEN);
            if (d.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT) {
                IDXGISwapChain2* sc2 = 0;
                if (SUCCEEDED(w->swap_chain->QueryInterface(__uuidof(IDXGISwapChain2), (void**)&sc2))) {
                    sc2->SetMaximumFrameLatency(1);
                    w->frame_wait = sc2->GetFrameLatencyWaitableObject();
                    sc2->Release();
                }
            }
            ok = _sapp_tc_d3d11_create_render_targets(w);
        }
    }
    _SAPP_TC_RELEASE(factory);
    _SAPP_TC_RELEASE(adapter);
    _SAPP_TC_RELEASE(dxgi_device);
    return ok;
}

/* re-sample client size + per-monitor DPI; true when the framebuffer size
   changed (drives ResizeBuffers). A 0x0 client area means minimized: report
   "no change" and keep the old sizes (sokol_app.h parity, sokol#1465). */
static bool _sapp_tc_win32_update_dimensions(_sapp_tc_window_t* w) {
    RECT rect;
    if (!GetClientRect(w->hwnd, &rect)) return false;
    const int cw = (int)(rect.right - rect.left);
    const int ch = (int)(rect.bottom - rect.top);
    if ((cw == 0) || (ch == 0)) return false;
    HMONITOR mon = MonitorFromWindow(w->hwnd, MONITOR_DEFAULTTONEAREST);
    if (mon != w->hmonitor) {
        w->hmonitor = mon;
        _sapp_tc_win32_update_refresh(w);
    }
    _sapp_tc_win32_update_scale(w);
    w->win_width = (int)((float)cw / w->window_scale + 0.5f);
    w->win_height = (int)((float)ch / w->window_scale + 0.5f);
    const bool high_dpi = !w->desc.no_high_dpi;
    int fbw = high_dpi ? cw : w->win_width;
    int fbh = high_dpi ? ch : w->win_height;
    if (fbw <= 0) fbw = 1;
    if (fbh <= 0) fbh = 1;
    if ((fbw != w->fb_width) || (fbh != w->fb_height)) {
        w->fb_width = fbw;
        w->fb_height = fbh;
        return true;
    }
    return false;
}

/*-- input event helpers ----------------------------------------------------*/
static void _sapp_tc_win32_mouse_update(_sapp_tc_window_t* w, LPARAM lParam) {
    /* event coordinates are framebuffer pixels (sokol_app.h convention):
       client px when high-dpi, logical points otherwise */
    const float x = (float)GET_X_LPARAM(lParam) * w->mouse_scale;
    const float y = (float)GET_Y_LPARAM(lParam) * w->mouse_scale;
    if (w->mouse_pos_valid) {
        w->mouse_dx = x - w->mouse_x;
        w->mouse_dy = y - w->mouse_y;
    } else {
        w->mouse_dx = 0.0f;
        w->mouse_dy = 0.0f;
        w->mouse_pos_valid = true;
    }
    w->mouse_x = x;
    w->mouse_y = y;
}

static void _sapp_tc_win32_mouse_event(_sapp_tc_window_t* w, sapp_event_type type, sapp_mousebutton btn) {
    sapp_event e;
    memset(&e, 0, sizeof(e));
    e.type = type;
    e.mouse_button = btn;
    e.mouse_x = w->mouse_x;
    e.mouse_y = w->mouse_y;
    e.mouse_dx = w->mouse_dx;
    e.mouse_dy = w->mouse_dy;
    e.modifiers = _sapp_tc_win32_mods();
    _sapp_tc_send(w, &e);
}

static void _sapp_tc_win32_scroll_event(_sapp_tc_window_t* w, float x, float y) {
    sapp_event e;
    memset(&e, 0, sizeof(e));
    e.type = SAPP_EVENTTYPE_MOUSE_SCROLL;
    e.mouse_x = w->mouse_x;
    e.mouse_y = w->mouse_y;
    e.scroll_x = x;
    e.scroll_y = y;
    e.modifiers = _sapp_tc_win32_mods();
    _sapp_tc_send(w, &e);
}

static void _sapp_tc_win32_key_event(_sapp_tc_window_t* w, sapp_event_type type, int scancode, bool repeat) {
    sapp_event e;
    memset(&e, 0, sizeof(e));
    e.type = type;
    e.key_code = _sapp_tc_translate_key(scancode);
    e.key_repeat = repeat;
    e.modifiers = _sapp_tc_win32_mods();
    _sapp_tc_send(w, &e);
    /* Ctrl+V paste notification (exact-CTRL match, sokol_app.h parity); the
       app reads the clipboard in its handler */
    if ((type == SAPP_EVENTTYPE_KEY_DOWN) && _sapp_tc.app.clipboard_enabled &&
        (e.modifiers == SAPP_MODIFIER_CTRL) && (e.key_code == SAPP_KEYCODE_V)) {
        sapp_event pe;
        memset(&pe, 0, sizeof(pe));
        pe.type = SAPP_EVENTTYPE_CLIPBOARD_PASTED;
        pe.modifiers = SAPP_MODIFIER_CTRL;
        _sapp_tc_send(w, &pe);
    }
}

static void _sapp_tc_win32_char_event(_sapp_tc_window_t* w, uint32_t c, bool repeat) {
    /* UTF-16 surrogate pair accumulation (per window) */
    if ((c >= 0xD800) && (c <= 0xDBFF)) {
        w->surrogate = (WCHAR)c;
        return;
    }
    if ((c >= 0xDC00) && (c <= 0xDFFF)) {
        if (!w->surrogate) return;
        c = ((((uint32_t)w->surrogate - 0xD800) << 10) | (c - 0xDC00)) + 0x10000;
        w->surrogate = 0;
    }
    if ((c < 32) || (c == 127)) return;     /* control characters */
    sapp_event e;
    memset(&e, 0, sizeof(e));
    e.type = SAPP_EVENTTYPE_CHAR;
    e.char_code = c;
    e.key_repeat = repeat;
    e.modifiers = _sapp_tc_win32_mods();
    _sapp_tc_send(w, &e);
}

static void _sapp_tc_win32_app_event(_sapp_tc_window_t* w, sapp_event_type type) {
    sapp_event e;
    memset(&e, 0, sizeof(e));
    e.type = type;
    _sapp_tc_send(w, &e);
}

/* refcounted capture across buttons: dragging with two buttons held must not
   drop capture when the first one is released */
static void _sapp_tc_win32_capture_mouse(_sapp_tc_window_t* w, uint8_t btn_mask) {
    if (w->capture_mask == 0) {
        SetCapture(w->hwnd);
    }
    w->capture_mask |= btn_mask;
}

static void _sapp_tc_win32_release_mouse(_sapp_tc_window_t* w, uint8_t btn_mask) {
    if (w->capture_mask != 0) {
        w->capture_mask &= (uint8_t)~btn_mask;
        if (w->capture_mask == 0) {
            ReleaseCapture();
        }
    }
}

static void _sapp_tc_win32_files_dropped(_sapp_tc_window_t* w, HDROP hdrop) {
    if (!_sapp_tc.app.drop_enabled || !_sapp_tc.app.drop_buffer) {
        DragFinish(hdrop);
        return;
    }
    const int max_files = _sapp_tc.app.drop_max_files;
    const int max_path = _sapp_tc.app.drop_max_path_length;
    memset(_sapp_tc.app.drop_buffer, 0, (size_t)max_files * (size_t)max_path);
    _sapp_tc.app.drop_num_files = 0;
    int num = (int)DragQueryFileW(hdrop, 0xFFFFFFFF, NULL, 0);
    if (num > max_files) num = max_files;
    bool ok = (num > 0);
    WCHAR* wpath = (WCHAR*)calloc((size_t)max_path, sizeof(WCHAR));
    if (!wpath) ok = false;
    for (int i = 0; ok && (i < num); i++) {
        if (0 == DragQueryFileW(hdrop, (UINT)i, wpath, (UINT)max_path)) {
            ok = false;
            break;
        }
        char* dst = _sapp_tc.app.drop_buffer + (size_t)i * (size_t)max_path;
        if (!_sapp_tc_win32_wide_to_utf8(wpath, dst, max_path)) {
            ok = false;     /* path too long: drop everything */
            break;
        }
    }
    POINT pt;
    const BOOL in_client = DragQueryPoint(hdrop, &pt);
    free(wpath);
    DragFinish(hdrop);
    if (!ok) {
        memset(_sapp_tc.app.drop_buffer, 0, (size_t)max_files * (size_t)max_path);
        return;
    }
    _sapp_tc.app.drop_num_files = num;
    sapp_event e;
    memset(&e, 0, sizeof(e));
    e.type = SAPP_EVENTTYPE_FILES_DROPPED;
    if (in_client) {
        e.mouse_x = (float)pt.x * w->mouse_scale;
        e.mouse_y = (float)pt.y * w->mouse_scale;
    } else {
        e.mouse_x = w->mouse_x;
        e.mouse_y = w->mouse_y;
    }
    e.modifiers = _sapp_tc_win32_mods();
    _sapp_tc_send(w, &e);
}

/*-- fullscreen (main window; borderless style-swap, upstream parity) -------*/
static void _sapp_tc_win32_set_fullscreen(bool fullscreen, UINT swp_flags) {
    _sapp_tc_window_t* w = _sapp_tc.app.main;
    if (!w) return;
    HMONITOR monitor = MonitorFromWindow(w->hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO minfo;
    memset(&minfo, 0, sizeof(minfo));
    minfo.cbSize = sizeof(minfo);
    if (!GetMonitorInfoW(monitor, &minfo)) return;
    _sapp_tc.app.fullscreen = fullscreen;
    RECT rect;
    DWORD style;
    if (fullscreen) {
        GetWindowRect(w->hwnd, &w->stored_window_rect);
        style = WS_POPUP | WS_SYSMENU | WS_VISIBLE;
        rect = minfo.rcMonitor;
    } else {
        style = WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_CAPTION | WS_SYSMENU |
                WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SIZEBOX;
        rect = w->stored_window_rect;
    }
    SetWindowLongPtrW(w->hwnd, GWL_STYLE, (LONG_PTR)style);
    SetWindowPos(w->hwnd, HWND_TOP, rect.left, rect.top,
                 rect.right - rect.left, rect.bottom - rect.top,
                 swp_flags | SWP_FRAMECHANGED);
}

/*-- per-window tick + pacing ------------------------------------------------
    Primary pacing: the swapchain's frame latency waitable signals when a new
    frame can be queued (true per-display vsync). One credit is consumed per
    successful zero-timeout wait in the due-check; a Present returns it via
    the swapchain. A tick that ends WITHOUT a Present (skip_present, occluded,
    minimized) keeps `credit_held` and is re-paced by `earliest_next` -- never
    waiting on the waitable again until the credit is spent -- so neither
    busy-spinning (waitable stays signaled) nor starvation (credits leak away)
    can occur. Windows without a waitable (fallback) are purely timer-paced. */
static void _sapp_tc_win32_pace(_sapp_tc_window_t* w, double seconds) {
    w->earliest_next = _sapp_tc_now() + seconds;
}

static bool _sapp_tc_win32_window_due(_sapp_tc_window_t* w) {
    if (!w || !w->swap_chain || w->in_tick) return false;
    if (w->earliest_next > _sapp_tc_now()) return false;
    /* Waitable pacing: the frame latency waitable is an auto-reset object that
       the run-loop's MsgWaitForMultipleObjectsEx already waits on. That wait
       CONSUMES the signal, so we must NOT re-wait the handle here (doing so
       missed the consumed signal and stalled the render loop after frame 1).
       Instead the run-loop records the consumed signal in waitable_ready. */
    if (w->frame_wait && !w->credit_held) {
        if (!w->waitable_ready) return false;
    }
    return true;
}

static void _sapp_tc_win32_tick(_sapp_tc_window_t* w, bool from_modal) {
    if (w->in_tick || !w->swap_chain) return;
    /* the due-check just consumed a waitable credit (unless one was already
       held); hold it until a real Present returns it through the swapchain.
       Clear waitable_ready: this signal is now spent (the next one comes from
       the run-loop's MsgWait after this frame is presented). */
    if (w->frame_wait) { w->credit_held = true; w->waitable_ready = false; }
    _sapp_tc_timing_update(&w->timing);
    w->frame_duration = w->timing.smooth_dt;
    const int swap_interval = (_sapp_tc.app.desc.swap_interval > 0) ? _sapp_tc.app.desc.swap_interval : 1;

    if (w->is_main) {
        /* resize is deliberately coalesced here, NOT in WM_SIZE (upstream:
           per-WM_SIZE ResizeBuffers blows up memory on some drivers). During
           a modal size-move loop the swapchain keeps its size (DXGI stretch)
           and one resize lands on the first normal tick after the drag. */
        if (!from_modal && _sapp_tc_win32_update_dimensions(w)) {
            _sapp_tc_d3d11_resize(w);
            _sapp_tc_win32_app_event(w, SAPP_EVENTTYPE_RESIZED);
        }
        w->in_tick = true;
        if (!_sapp_tc.app.init_called) {
            _sapp_tc.app.init_called = true;
            if (_sapp_tc.app.desc.init_cb) {
                _sapp_tc.app.desc.init_cb();
            } else if (_sapp_tc.app.desc.init_userdata_cb) {
                _sapp_tc.app.desc.init_userdata_cb(_sapp_tc.app.desc.user_data);
            }
        }
        if (_sapp_tc.app.desc.frame_cb) {
            _sapp_tc.app.desc.frame_cb();
        } else if (_sapp_tc.app.desc.frame_userdata_cb) {
            _sapp_tc.app.desc.frame_userdata_cb(_sapp_tc.app.desc.user_data);
        }
        _sapp_tc.app.frame_count++;
        w->in_tick = false;
        bool presented = false;
        if (_sapp_tc.app.skip_present) {
            /* one-shot event-driven present suppression (TrussC patch: keeps
               the last image on screen when a frame decides not to draw) */
            _sapp_tc.app.skip_present = false;
        } else {
            const UINT flags = from_modal ? DXGI_PRESENT_DO_NOT_WAIT : 0;
            const HRESULT hr = w->swap_chain->Present((UINT)swap_interval, flags);
            presented = SUCCEEDED(hr) && (hr != DXGI_STATUS_OCCLUDED);
        }
        if (presented) {
            w->credit_held = false;
        }
        if (w->iconified) {
            /* minimized: frame_cb keeps running, throttled (upstream Sleep(16)
               / mac fallback-timer parity) */
            _sapp_tc_win32_pace(w, 0.0166 * (double)swap_interval);
        } else if (presented && w->frame_wait) {
            w->earliest_next = 0.0;     /* vsync pacing via the waitable */
        } else {
            _sapp_tc_win32_pace(w, w->refresh_period * (double)swap_interval);
        }
    } else {
        /* secondary window */
        if (w->iconified) {
            _sapp_tc_win32_pace(w, w->refresh_period);
            return;
        }
        if (w->occluded) {
            /* cheap visibility poll: a fully covered window costs one
               present-test per pace period and never renders or stalls */
            const HRESULT hr = w->swap_chain->Present(0, DXGI_PRESENT_TEST);
            if (hr == DXGI_STATUS_OCCLUDED) {
                _sapp_tc_win32_pace(w, w->refresh_period);
                return;
            }
            w->occluded = false;
            _sapp_tc_win32_app_event(w, SAPP_EVENTTYPE_RESUMED);
        }
        if (!from_modal && _sapp_tc_win32_update_dimensions(w)) {
            _sapp_tc_d3d11_resize(w);
            _sapp_tc_win32_app_event(w, SAPP_EVENTTYPE_RESIZED);
        }
        w->in_tick = true;
        if (w->desc.tick_cb) {
            sapp_window handle = { w->win_id };
            w->desc.tick_cb(handle, w->desc.user_data);
        }
        w->in_tick = false;
        const UINT flags = from_modal ? DXGI_PRESENT_DO_NOT_WAIT : 0;
        const HRESULT hr = w->swap_chain->Present(1, flags);
        if (hr == DXGI_STATUS_OCCLUDED) {
            w->occluded = true;
            _sapp_tc_win32_app_event(w, SAPP_EVENTTYPE_SUSPENDED);
            _sapp_tc_win32_pace(w, w->refresh_period);
        } else if (SUCCEEDED(hr)) {
            w->credit_held = false;
            if (w->frame_wait) {
                w->earliest_next = 0.0;
            } else {
                _sapp_tc_win32_pace(w, w->refresh_period);
            }
        } else {
            _sapp_tc_win32_pace(w, w->refresh_period);
        }
    }
}

/*-- window procedure --------------------------------------------------------*/
static LRESULT CALLBACK _sapp_tc_wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        /* stash the window struct passed via CreateWindowExW lpCreateParams */
        const CREATESTRUCTW* cs = (const CREATESTRUCTW*)lParam;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    _sapp_tc_window_t* w = (_sapp_tc_window_t*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!w || !w->ready) {
        /* creation-time messages are swallowed (upstream in_create_window) */
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    switch (msg) {
        case WM_CLOSE:
            if (w->is_main) {
                /* the cancellable quit dance (sokol_app.h parity): sapp_quit()
                   pre-sets quit_ordered (unvetoable); otherwise QUIT_REQUESTED
                   is delivered and a handler may sapp_cancel_quit() */
                if (!_sapp_tc.app.quit_ordered) {
                    _sapp_tc.app.quit_requested = true;
                    _sapp_tc_win32_app_event(w, SAPP_EVENTTYPE_QUIT_REQUESTED);
                    if (_sapp_tc.app.quit_requested) {
                        _sapp_tc.app.quit_ordered = true;
                    }
                }
                if (_sapp_tc.app.quit_ordered) {
                    PostQuitMessage(0);
                }
            } else {
                /* user closed a secondary window: notify; the app accepts by
                   calling sapp_destroy_window() (w may be freed after this) */
                if (w->desc.close_cb) {
                    sapp_window handle = { w->win_id };
                    w->desc.close_cb(handle, w->desc.user_data);
                }
            }
            return 0;   /* never let DefWindowProc destroy the window here */
        case WM_SYSCOMMAND:
            switch (wParam & 0xFFF0) {
                case SC_SCREENSAVE:
                case SC_MONITORPOWER:
                    if (_sapp_tc.app.fullscreen) {
                        return 0;   /* no screensaver/monitor-off in fullscreen */
                    }
                    break;
                case SC_KEYMENU:
                    return 0;       /* don't let Alt freeze the render loop */
                default:
                    break;
            }
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_SIZE: {
            /* only iconify tracking here; RESIZED + ResizeBuffers are
               coalesced into the tick (upstream parity) */
            const bool iconified = (wParam == SIZE_MINIMIZED);
            if (iconified != w->iconified) {
                w->iconified = iconified;
                _sapp_tc_win32_app_event(w, iconified ? SAPP_EVENTTYPE_ICONIFIED
                                                      : SAPP_EVENTTYPE_RESTORED);
                if (!iconified) {
                    w->earliest_next = 0.0;     /* resume promptly */
                }
            }
            break;
        }
        case WM_SETFOCUS:
            _sapp_tc_win32_app_event(w, SAPP_EVENTTYPE_FOCUSED);
            break;
        case WM_KILLFOCUS:
            _sapp_tc_win32_app_event(w, SAPP_EVENTTYPE_UNFOCUSED);
            break;
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT) {
                _sapp_tc_win32_apply_cursor(_sapp_tc.app.current_cursor,
                                            _sapp_tc.app.mouse_shown, true);
                return TRUE;
            }
            break;
        case WM_DPICHANGED: {
            /* per-monitor-v2 only: adopt the OS-proposed rect; scale and
               sizes are re-sampled on the next tick */
            const RECT* r = (const RECT*)lParam;
            SetWindowPos(hwnd, NULL, r->left, r->top,
                         r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            _sapp_tc_win32_update_refresh(w);
            break;
        }
        case WM_LBUTTONDOWN:
            _sapp_tc_win32_mouse_update(w, lParam);
            _sapp_tc_win32_mouse_event(w, SAPP_EVENTTYPE_MOUSE_DOWN, SAPP_MOUSEBUTTON_LEFT);
            _sapp_tc_win32_capture_mouse(w, (uint8_t)(1 << SAPP_MOUSEBUTTON_LEFT));
            break;
        case WM_LBUTTONUP:
            _sapp_tc_win32_mouse_update(w, lParam);
            _sapp_tc_win32_mouse_event(w, SAPP_EVENTTYPE_MOUSE_UP, SAPP_MOUSEBUTTON_LEFT);
            _sapp_tc_win32_release_mouse(w, (uint8_t)(1 << SAPP_MOUSEBUTTON_LEFT));
            break;
        case WM_RBUTTONDOWN:
            _sapp_tc_win32_mouse_update(w, lParam);
            _sapp_tc_win32_mouse_event(w, SAPP_EVENTTYPE_MOUSE_DOWN, SAPP_MOUSEBUTTON_RIGHT);
            _sapp_tc_win32_capture_mouse(w, (uint8_t)(1 << SAPP_MOUSEBUTTON_RIGHT));
            break;
        case WM_RBUTTONUP:
            _sapp_tc_win32_mouse_update(w, lParam);
            _sapp_tc_win32_mouse_event(w, SAPP_EVENTTYPE_MOUSE_UP, SAPP_MOUSEBUTTON_RIGHT);
            _sapp_tc_win32_release_mouse(w, (uint8_t)(1 << SAPP_MOUSEBUTTON_RIGHT));
            break;
        case WM_MBUTTONDOWN:
            _sapp_tc_win32_mouse_update(w, lParam);
            _sapp_tc_win32_mouse_event(w, SAPP_EVENTTYPE_MOUSE_DOWN, SAPP_MOUSEBUTTON_MIDDLE);
            _sapp_tc_win32_capture_mouse(w, (uint8_t)(1 << SAPP_MOUSEBUTTON_MIDDLE));
            break;
        case WM_MBUTTONUP:
            _sapp_tc_win32_mouse_update(w, lParam);
            _sapp_tc_win32_mouse_event(w, SAPP_EVENTTYPE_MOUSE_UP, SAPP_MOUSEBUTTON_MIDDLE);
            _sapp_tc_win32_release_mouse(w, (uint8_t)(1 << SAPP_MOUSEBUTTON_MIDDLE));
            break;
        case WM_MOUSEMOVE:
            _sapp_tc_win32_mouse_update(w, lParam);
            if (!w->mouse_tracked) {
                w->mouse_tracked = true;
                TRACKMOUSEEVENT tme;
                memset(&tme, 0, sizeof(tme));
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = w->hwnd;
                TrackMouseEvent(&tme);
                w->mouse_dx = 0.0f;
                w->mouse_dy = 0.0f;
                _sapp_tc_win32_mouse_event(w, SAPP_EVENTTYPE_MOUSE_ENTER, SAPP_MOUSEBUTTON_INVALID);
            }
            _sapp_tc_win32_mouse_event(w, SAPP_EVENTTYPE_MOUSE_MOVE, SAPP_MOUSEBUTTON_INVALID);
            break;
        case WM_MOUSELEAVE:
            w->mouse_dx = 0.0f;
            w->mouse_dy = 0.0f;
            w->mouse_tracked = false;
            _sapp_tc_win32_mouse_event(w, SAPP_EVENTTYPE_MOUSE_LEAVE, SAPP_MOUSEBUTTON_INVALID);
            break;
        case WM_MOUSEWHEEL:
            _sapp_tc_win32_scroll_event(w, 0.0f,
                (float)GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA);
            break;
        case WM_MOUSEHWHEEL:
            _sapp_tc_win32_scroll_event(w,
                -(float)GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA, 0.0f);
            break;
        case WM_CHAR:
            _sapp_tc_win32_char_event(w, (uint32_t)wParam, 0 != (lParam & 0x40000000));
            break;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            /* 9-bit scancode incl. the extended bit (distinguishes L/R
               Ctrl/Alt and keypad Enter); SYS variants fall through to
               DefWindowProc below so Alt+F4 etc. keep working */
            _sapp_tc_win32_key_event(w, SAPP_EVENTTYPE_KEY_DOWN,
                (int)(HIWORD(lParam) & 0x1FF), 0 != (lParam & 0x40000000));
            break;
        case WM_KEYUP:
        case WM_SYSKEYUP:
            _sapp_tc_win32_key_event(w, SAPP_EVENTTYPE_KEY_UP,
                (int)(HIWORD(lParam) & 0x1FF), false);
            break;
        case WM_ENTERSIZEMOVE:
            /* the modal size-move loop blocks the outer message loop; a
               coarse WM_TIMER (~64Hz) keeps all windows rendering meanwhile */
            SetTimer(hwnd, _SAPP_TC_MODAL_TIMER, USER_TIMER_MINIMUM, NULL);
            break;
        case WM_EXITSIZEMOVE:
            KillTimer(hwnd, _SAPP_TC_MODAL_TIMER);
            break;
        case WM_TIMER:
            if (wParam == _SAPP_TC_MODAL_TIMER) {
                for (int i = 0; i < _SAPP_TC_MAX_WINDOWS; i++) {
                    _sapp_tc_window_t* tw = _sapp_tc.windows[i];
                    if (!tw) continue;
                    /* the outer run-loop (which records the waitable signal in
                       waitable_ready) is blocked in this modal size/move loop, so
                       poll the frame latency waitable here instead -- otherwise the
                       due-check never sees a credit and drag/resize stops rendering */
                    if (tw->frame_wait && !tw->credit_held && !tw->waitable_ready &&
                        WaitForSingleObject(tw->frame_wait, 0) == WAIT_OBJECT_0) {
                        tw->waitable_ready = true;
                    }
                    if (_sapp_tc_win32_window_due(tw)) {
                        _sapp_tc_win32_tick(tw, true);
                    }
                }
            }
            break;
        case WM_NCLBUTTONDOWN:
            /* grabbing the title bar stalls rendering for ~500ms; posting a
               synthetic mouse-move breaks the wait (upstream workaround) */
            if (wParam == HTCAPTION) {
                POINT point;
                if (GetCursorPos(&point)) {
                    ScreenToClient(hwnd, &point);
                    PostMessageW(hwnd, WM_MOUSEMOVE, 0,
                        (LPARAM)(((uint32_t)point.x) | (((uint32_t)point.y) << 16)));
                }
            }
            break;
        case WM_DROPFILES:
            _sapp_tc_win32_files_dropped(w, (HDROP)wParam);
            break;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/*-- window creation / destruction ------------------------------------------*/
static void _sapp_tc_win32_ensure_wndclass(void) {
    if (_sapp_tc.wndclass_registered) return;
    _sapp_tc.wndclass_registered = true;
    WNDCLASSW wndclassw;
    memset(&wndclassw, 0, sizeof(wndclassw));
    wndclassw.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wndclassw.lpfnWndProc = _sapp_tc_wndproc;
    wndclassw.hInstance = GetModuleHandleW(NULL);
    wndclassw.hCursor = LoadCursor(NULL, IDC_ARROW);
    wndclassw.hIcon = LoadIcon(NULL, IDI_WINLOGO);
    wndclassw.lpszClassName = L"SOKOLAPP_TC";
    RegisterClassW(&wndclassw);
}

/* create the HWND at the desc's logical size: first against 96 DPI, then --
   per-monitor-v2 -- resize the CLIENT area for the real DPI of the monitor
   the window actually landed on (the standard create-then-resize dance) */
static bool _sapp_tc_win32_create_native_window(_sapp_tc_window_t* w, const wchar_t* title,
        DWORD style, DWORD ex_style, int x, int y, bool use_default_size) {
    _sapp_tc_win32_ensure_wndclass();
    int outer_w = CW_USEDEFAULT;
    int outer_h = CW_USEDEFAULT;
    if (!use_default_size) {
        RECT rect = { 0, 0, (LONG)w->desc.width, (LONG)w->desc.height };
        AdjustWindowRectEx(&rect, style, FALSE, ex_style);
        outer_w = (int)(rect.right - rect.left);
        outer_h = (int)(rect.bottom - rect.top);
    }
    w->hwnd = CreateWindowExW(ex_style, L"SOKOLAPP_TC", title, style,
        (x >= 0) ? x : CW_USEDEFAULT,
        (y >= 0) ? y : CW_USEDEFAULT,
        outer_w, outer_h,
        NULL, NULL, GetModuleHandleW(NULL), w);
    if (!w->hwnd) return false;
    w->hmonitor = MonitorFromWindow(w->hwnd, MONITOR_DEFAULTTONEAREST);
    _sapp_tc_win32_update_refresh(w);
    _sapp_tc_win32_update_scale(w);
    if (!use_default_size && (w->window_scale != 1.0f)) {
        const LONG pw = (LONG)((float)w->desc.width * w->window_scale + 0.5f);
        const LONG ph = (LONG)((float)w->desc.height * w->window_scale + 0.5f);
        RECT rect = { 0, 0, pw, ph };
        if (_sapp_tc.GetDpiForWindow && _sapp_tc.AdjustWindowRectExForDpi) {
            _sapp_tc.AdjustWindowRectExForDpi(&rect, style, FALSE, ex_style,
                                              _sapp_tc.GetDpiForWindow(w->hwnd));
        } else {
            AdjustWindowRectEx(&rect, style, FALSE, ex_style);
        }
        SetWindowPos(w->hwnd, NULL, 0, 0,
                     (int)(rect.right - rect.left), (int)(rect.bottom - rect.top),
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    _sapp_tc_timing_reset(&w->timing);
    return true;
}

static void _sapp_tc_win32_destroy_window_resources(_sapp_tc_window_t* w) {
    _sapp_tc_d3d11_destroy_render_targets(w);
    if (w->frame_wait) {
        CloseHandle(w->frame_wait);
        w->frame_wait = NULL;
    }
    _SAPP_TC_RELEASE(w->swap_chain);
    if (w->hwnd) {
        KillTimer(w->hwnd, _SAPP_TC_MODAL_TIMER);
        SetWindowLongPtrW(w->hwnd, GWLP_USERDATA, 0);   /* wndproc goes inert */
        DestroyWindow(w->hwnd);
        w->hwnd = NULL;
    }
}

static bool _sapp_tc_win32_create_main_window(void) {
    const sapp_desc* d = &_sapp_tc.app.desc;
    int slot = -1;
    for (int i = 0; i < _SAPP_TC_MAX_WINDOWS; i++) {
        if (_sapp_tc.windows[i] == 0) { slot = i; break; }
    }
    if (slot < 0) return false;

    _sapp_tc_window_t* w = new _sapp_tc_window_t();
    w->win_id = ++_sapp_tc.next_id;
    w->is_main = true;
    w->color_fmt = DXGI_FORMAT_R10G10B10A2_UNORM;   /* TrussC 10-bit output */
    w->window_scale = w->dpi_scale = w->mouse_scale = 1.0f;
    w->refresh_period = 1.0 / 60.0;

    /* per-window desc: the app callbacks are reached via the trampoline */
    w->desc.width = d->width;
    w->desc.height = d->height;
    w->desc.sample_count = d->sample_count;
    w->desc.no_high_dpi = !d->high_dpi;
    w->desc.event_cb = _sapp_tc_main_event_tramp;

    const DWORD style = WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_CAPTION | WS_SYSMENU |
                        WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SIZEBOX;
    const DWORD ex_style = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;
    /* width/height 0: let the OS pick (CW_USEDEFAULT, upstream parity) */
    const bool use_default_size = (d->width <= 0) || (d->height <= 0);
    if (!_sapp_tc_win32_create_native_window(w, _sapp_tc.app.window_title_wide,
            style, ex_style, -1, -1, use_default_size)) {
        delete w;
        return false;
    }
    _sapp_tc.windows[slot] = w;
    _sapp_tc.app.main = w;
    w->ready = true;
    _sapp_tc_win32_update_dimensions(w);    /* silent startup sizing (no RESIZED) */
    if (d->fullscreen) {
        _sapp_tc_win32_set_fullscreen(true, SWP_HIDEWINDOW);
        _sapp_tc_win32_update_dimensions(w);
    }
    ShowWindow(w->hwnd, SW_SHOW);
    /* drop target is always registered; the handler gates on enable_dragndrop */
    DragAcceptFiles(w->hwnd, TRUE);
    if (!_sapp_tc_d3d11_create_swapchain(w)) {
        return false;
    }
    _sapp_tc.app.valid = true;
    return true;
}

/*-- the run loop -------------------------------------------------------------
    Blocks in MsgWaitForMultipleObjectsEx on the due windows' frame latency
    waitables (plus a timeout for timer-paced windows), drains ALL pending
    messages, then ticks every due window. Replaces upstream's PeekMessage
    busy-render-loop: the process sleeps whenever nothing is due. */
static void _sapp_tc_win32_run_loop(void) {
    bool done = false;
    while (!done && !_sapp_tc.app.quit_ordered) {
        HANDLE handles[_SAPP_TC_MAX_WINDOWS];
        _sapp_tc_window_t* handle_owner[_SAPP_TC_MAX_WINDOWS];
        DWORD num_handles = 0;
        const double now = _sapp_tc_now();
        double wake_at = now + 0.1;     /* robustness cap; messages wake us anyway */
        for (int i = 0; i < _SAPP_TC_MAX_WINDOWS; i++) {
            _sapp_tc_window_t* w = _sapp_tc.windows[i];
            if (!w || !w->swap_chain || w->in_tick) continue;
            if (w->earliest_next > now) {
                if (w->earliest_next < wake_at) wake_at = w->earliest_next;
            } else if (w->frame_wait && !w->credit_held && !w->waitable_ready) {
                handle_owner[num_handles] = w;
                handles[num_handles++] = w->frame_wait;
            } else {
                wake_at = now;          /* due immediately (timer-paced / credit or signal held) */
            }
        }
        double timeout_s = wake_at - now;
        if (timeout_s < 0.0) timeout_s = 0.0;
        DWORD wr = MsgWaitForMultipleObjectsEx(num_handles, handles,
            (DWORD)(timeout_s * 1000.0), QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        /* If a frame latency waitable satisfied the wait, it was auto-reset here.
           Record it so the due-check consumes THIS signal instead of re-waiting
           the (now-unsignaled) handle. Only one handle is reported per wait; the
           rest stay signaled and are caught on the next loop. */
        if (wr >= WAIT_OBJECT_0 && wr < WAIT_OBJECT_0 + num_handles) {
            handle_owner[wr - WAIT_OBJECT_0]->waitable_ready = true;
        }
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (WM_QUIT == msg.message) {
                done = true;
                continue;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (done) break;
        /* tick every due window (re-fetch each slot: a tick or a dispatched
           message may have destroyed windows) */
        for (int i = 0; i < _SAPP_TC_MAX_WINDOWS; i++) {
            _sapp_tc_window_t* w = _sapp_tc.windows[i];
            if (_sapp_tc_win32_window_due(w)) {
                _sapp_tc_win32_tick(w, false);
            }
        }
        /* route programmatic quits through the same WM_CLOSE dance so the
           QUIT_REQUESTED semantics stay identical (upstream parity) */
        if (_sapp_tc.app.quit_requested && _sapp_tc.app.main) {
            PostMessageW(_sapp_tc.app.main->hwnd, WM_CLOSE, 0, 0);
        }
    }
}

/*-- public API ---------------------------------------------------------------*/
extern "C" {

sapp_window sapp_create_window(const sapp_window_desc* desc) {
    sapp_window invalid = { 0 };
    if (!desc) return invalid;
    if (!_sapp_tc.app.valid || !_sapp_tc.app.device) return invalid;
    _sapp_tc_init_keytable();

    int slot = -1;
    for (int i = 0; i < _SAPP_TC_MAX_WINDOWS; i++) {
        if (_sapp_tc.windows[i] == 0) { slot = i; break; }
    }
    if (slot < 0) return invalid;

    _sapp_tc_window_t* w = new _sapp_tc_window_t();
    w->win_id = ++_sapp_tc.next_id;
    w->desc = *desc;
    if (w->desc.width <= 0) w->desc.width = 640;
    if (w->desc.height <= 0) w->desc.height = 480;
    if (w->desc.sample_count <= 0) w->desc.sample_count = 1;
    w->color_fmt = DXGI_FORMAT_B8G8R8A8_UNORM;  /* secondary windows are BGRA8 (mac parity) */
    w->window_scale = w->dpi_scale = w->mouse_scale = 1.0f;
    w->refresh_period = 1.0 / 60.0;

    DWORD style;
    if (w->desc.borderless) {
        style = WS_POPUP | WS_SYSMENU;
    } else {
        style = WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_CAPTION | WS_SYSMENU |
                WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SIZEBOX;
    }
    const DWORD ex_style = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;
    WCHAR title[256];
    _sapp_tc_win32_utf8_to_wide(w->desc.title ? w->desc.title : "TrussC", title, 256);
    const int px = (desc->x >= 0) ? desc->x : 120;
    const int py = (desc->y >= 0) ? desc->y : 120;
    if (!_sapp_tc_win32_create_native_window(w, title, style, ex_style, px, py, false)) {
        delete w;
        return invalid;
    }
    w->ready = true;
    _sapp_tc_win32_update_dimensions(w);
    ShowWindow(w->hwnd, SW_SHOW);
    DragAcceptFiles(w->hwnd, TRUE);
    if (!_sapp_tc_d3d11_create_swapchain(w)) {
        _sapp_tc_win32_destroy_window_resources(w);
        delete w;
        return invalid;
    }
    _sapp_tc.windows[slot] = w;
    sapp_window handle = { w->win_id };
    return handle;
}

void sapp_destroy_window(sapp_window win) {
    _sapp_tc_window_t* w = _sapp_tc_lookup(win);
    if (!w || w->is_main) return;   /* the main window is owned by sapp_run */
    for (int i = 0; i < _SAPP_TC_MAX_WINDOWS; i++) {
        if (_sapp_tc.windows[i] == w) { _sapp_tc.windows[i] = 0; break; }
    }
    w->desc.close_cb = 0;
    _sapp_tc_win32_destroy_window_resources(w);
    delete w;
}

bool sapp_window_valid(sapp_window win) {
    return _sapp_tc_lookup(win) != 0;
}

int sapp_window_width(sapp_window win) {
    _sapp_tc_window_t* w = _sapp_tc_lookup(win);
    return w ? w->win_width : 0;
}

int sapp_window_height(sapp_window win) {
    _sapp_tc_window_t* w = _sapp_tc_lookup(win);
    return w ? w->win_height : 0;
}

int sapp_window_framebuffer_width(sapp_window win) {
    _sapp_tc_window_t* w = _sapp_tc_lookup(win);
    return w ? w->fb_width : 0;
}

int sapp_window_framebuffer_height(sapp_window win) {
    _sapp_tc_window_t* w = _sapp_tc_lookup(win);
    return w ? w->fb_height : 0;
}

float sapp_window_dpi_scale(sapp_window win) {
    _sapp_tc_window_t* w = _sapp_tc_lookup(win);
    return w ? w->dpi_scale : 1.0f;
}

int sapp_window_sample_count(sapp_window win) {
    _sapp_tc_window_t* w = _sapp_tc_lookup(win);
    return w ? w->desc.sample_count : 1;
}

bool sapp_window_occluded(sapp_window win) {
    _sapp_tc_window_t* w = _sapp_tc_lookup(win);
    return w ? w->occluded : false;
}

void sapp_window_set_title(sapp_window win, const char* title) {
    _sapp_tc_window_t* w = _sapp_tc_lookup(win);
    if (!w || !w->hwnd || !title) return;
    WCHAR wide[256];
    if (_sapp_tc_win32_utf8_to_wide(title, wide, 256)) {
        SetWindowTextW(w->hwnd, wide);
    }
}

double sapp_window_frame_duration(sapp_window win) {
    _sapp_tc_window_t* w = _sapp_tc_lookup(win);
    if (!w) return 1.0 / 60.0;
    return (w->frame_duration > 0.0) ? w->frame_duration : w->refresh_period;
}

/* Metal handles do not exist on Windows */
const void* sapp_window_metal_current_drawable(sapp_window win) { (void)win; return 0; }
const void* sapp_window_metal_depth_stencil_texture(sapp_window win) { (void)win; return 0; }
const void* sapp_window_metal_msaa_color_texture(sapp_window win) { (void)win; return 0; }
const void* sapp_window_macos_get_window(sapp_window win) { (void)win; return 0; }

const void* sapp_window_d3d11_render_view(sapp_window win) {
    _sapp_tc_window_t* w = _sapp_tc_lookup(win);
    if (!w || !w->in_tick) return 0;
    return (w->desc.sample_count > 1) ? (const void*)w->msaa_rtv : (const void*)w->rtv;
}

const void* sapp_window_d3d11_resolve_view(sapp_window win) {
    _sapp_tc_window_t* w = _sapp_tc_lookup(win);
    if (!w || !w->in_tick || (w->desc.sample_count <= 1)) return 0;
    return (const void*)w->rtv;
}

const void* sapp_window_d3d11_depth_stencil_view(sapp_window win) {
    _sapp_tc_window_t* w = _sapp_tc_lookup(win);
    if (!w || !w->in_tick) return 0;
    return (const void*)w->dsv;
}

const void* sapp_window_win32_get_hwnd(sapp_window win) {
    _sapp_tc_window_t* w = _sapp_tc_lookup(win);
    return w ? (const void*)w->hwnd : 0;
}

/* GL / X11 handles do not exist on Windows (D3D11 backend) */
const void* sapp_window_x11_get_window(sapp_window win) { (void)win; return 0; }
uint32_t sapp_window_gl_framebuffer(sapp_window win) { (void)win; return 0; }

/*-- sokol_app.h public API (Windows implementation lives here) --------------*/

void sapp_run(const sapp_desc* desc) {
    if (!desc) return;
    _sapp_tc.app.desc = *desc;
    sapp_desc* d = &_sapp_tc.app.desc;
    if (d->sample_count <= 0) d->sample_count = 1;
    if (d->swap_interval <= 0) d->swap_interval = 1;
    if (d->clipboard_size <= 0) d->clipboard_size = 8192;
    if (d->max_dropped_files <= 0) d->max_dropped_files = 1;
    if (d->max_dropped_file_path_length <= 0) d->max_dropped_file_path_length = 2048;
    strncpy(_sapp_tc.app.window_title, d->window_title ? d->window_title : "sokol",
            sizeof(_sapp_tc.app.window_title) - 1);
    d->window_title = _sapp_tc.app.window_title;
    _sapp_tc_win32_utf8_to_wide(_sapp_tc.app.window_title,
        _sapp_tc.app.window_title_wide,
        (int)(sizeof(_sapp_tc.app.window_title_wide) / sizeof(WCHAR)));
    if (d->enable_clipboard) {
        _sapp_tc.app.clipboard_enabled = true;
        _sapp_tc.app.clipboard_size = d->clipboard_size;
        _sapp_tc.app.clipboard = (char*)calloc(1, (size_t)d->clipboard_size);
    }
    if (d->enable_dragndrop) {
        _sapp_tc.app.drop_enabled = true;
        _sapp_tc.app.drop_max_files = d->max_dropped_files;
        _sapp_tc.app.drop_max_path_length = d->max_dropped_file_path_length;
        _sapp_tc.app.drop_buffer = (char*)calloc(
            (size_t)d->max_dropped_files, (size_t)d->max_dropped_file_path_length);
    }
    _sapp_tc.app.mouse_shown = true;
    _sapp_tc.app.current_cursor = SAPP_MOUSECURSOR_DEFAULT;
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    _sapp_tc.qpc_period = 1.0 / (double)freq.QuadPart;
    _sapp_tc_win32_init_console();
    _sapp_tc_init_keytable();
    _sapp_tc_win32_init_dpi();
    _sapp_tc_win32_init_cursors();

    if (_sapp_tc_d3d11_create_device()) {
        if (_sapp_tc_win32_create_main_window()) {
            _sapp_tc_win32_run_loop();
        }
    }

    /* user cleanup runs BEFORE any GPU/window teardown (the app shuts down
       sokol_gfx here, which still needs the device) */
    if (!_sapp_tc.app.cleanup_called) {
        _sapp_tc.app.cleanup_called = true;
        if (_sapp_tc.app.desc.cleanup_cb) {
            _sapp_tc.app.desc.cleanup_cb();
        } else if (_sapp_tc.app.desc.cleanup_userdata_cb) {
            _sapp_tc.app.desc.cleanup_userdata_cb(_sapp_tc.app.desc.user_data);
        }
    }
    /* destroy any windows the app left open (secondary first, then main) */
    for (int i = 0; i < _SAPP_TC_MAX_WINDOWS; i++) {
        _sapp_tc_window_t* w = _sapp_tc.windows[i];
        if (!w || w->is_main) continue;
        _sapp_tc.windows[i] = 0;
        _sapp_tc_win32_destroy_window_resources(w);
        delete w;
    }
    if (_sapp_tc.app.main) {
        _sapp_tc_window_t* w = _sapp_tc.app.main;
        for (int i = 0; i < _SAPP_TC_MAX_WINDOWS; i++) {
            if (_sapp_tc.windows[i] == w) { _sapp_tc.windows[i] = 0; break; }
        }
        _sapp_tc.app.main = 0;
        _sapp_tc_win32_destroy_window_resources(w);
        delete w;
    }
    _SAPP_TC_RELEASE(_sapp_tc.app.device_context);
    _SAPP_TC_RELEASE(_sapp_tc.app.device);
    if (_sapp_tc.wndclass_registered) {
        UnregisterClassW(L"SOKOLAPP_TC", GetModuleHandleW(NULL));
        _sapp_tc.wndclass_registered = false;
    }
    for (int i = 0; i < _SAPP_MOUSECURSOR_NUM; i++) {
        if (_sapp_tc.app.custom_cursor_bound[i] && _sapp_tc.app.custom_cursors[i]) {
            DestroyIcon((HICON)_sapp_tc.app.custom_cursors[i]);
            _sapp_tc.app.custom_cursors[i] = NULL;
            _sapp_tc.app.custom_cursor_bound[i] = false;
        }
    }
    _sapp_tc_win32_restore_console();
    if (_sapp_tc.app.clipboard) { free(_sapp_tc.app.clipboard); _sapp_tc.app.clipboard = 0; }
    if (_sapp_tc.app.drop_buffer) { free(_sapp_tc.app.drop_buffer); _sapp_tc.app.drop_buffer = 0; }
    _sapp_tc.app.valid = false;
}

bool sapp_isvalid(void) {
    return _sapp_tc.app.valid;
}

int sapp_width(void) {
    _sapp_tc_window_t* w = _sapp_tc.app.main;
    return (w && w->fb_width > 0) ? w->fb_width : 1;
}

float sapp_widthf(void) {
    return (float)sapp_width();
}

int sapp_height(void) {
    _sapp_tc_window_t* w = _sapp_tc.app.main;
    return (w && w->fb_height > 0) ? w->fb_height : 1;
}

float sapp_heightf(void) {
    return (float)sapp_height();
}

int sapp_sample_count(void) {
    return _sapp_tc.app.desc.sample_count > 0 ? _sapp_tc.app.desc.sample_count : 1;
}

bool sapp_high_dpi(void) {
    _sapp_tc_window_t* w = _sapp_tc.app.main;
    return _sapp_tc.app.desc.high_dpi && w && (w->dpi_scale >= 1.5f);
}

float sapp_dpi_scale(void) {
    _sapp_tc_window_t* w = _sapp_tc.app.main;
    return w ? w->dpi_scale : 1.0f;
}

uint64_t sapp_frame_count(void) {
    return _sapp_tc.app.frame_count;
}

double sapp_frame_duration(void) {
    _sapp_tc_window_t* w = _sapp_tc.app.main;
    if (!w) return 1.0 / 60.0;
    return (w->timing.smooth_dt > 0.0) ? w->timing.smooth_dt : w->refresh_period;
}

void sapp_request_quit(void) {
    _sapp_tc.app.quit_requested = true;
}

void sapp_cancel_quit(void) {
    _sapp_tc.app.quit_requested = false;
}

void sapp_quit(void) {
    _sapp_tc.app.quit_ordered = true;
}

void sapp_consume_event(void) {
    _sapp_tc.app.event_consumed = true;
}

void sapp_skip_present(void) {
    /* one-shot; HONORED on D3D11 (TrussC patch: event-driven present
       suppression -- keeps the last image on screen, prevents flicker) */
    _sapp_tc.app.skip_present = true;
}

void sapp_show_keyboard(bool show) {
    (void)show;     /* on-screen keyboard: mobile only */
}

bool sapp_keyboard_shown(void) {
    return false;
}

void sapp_show_mouse(bool show) {
    if (_sapp_tc.app.mouse_shown != show) {
        _sapp_tc_win32_apply_cursor(_sapp_tc.app.current_cursor, show, false);
    }
}

bool sapp_mouse_shown(void) {
    return _sapp_tc.app.mouse_shown;
}

void sapp_set_mouse_cursor(sapp_mouse_cursor cursor) {
    if ((int)cursor < 0 || (int)cursor >= _SAPP_MOUSECURSOR_NUM) return;
    if (cursor != _sapp_tc.app.current_cursor) {
        _sapp_tc_win32_apply_cursor(cursor, _sapp_tc.app.mouse_shown, false);
    }
}

sapp_mouse_cursor sapp_get_mouse_cursor(void) {
    return _sapp_tc.app.current_cursor;
}

sapp_mouse_cursor sapp_bind_mouse_cursor_image(sapp_mouse_cursor cursor, const sapp_image_desc* desc) {
    if ((int)cursor < 0 || (int)cursor >= _SAPP_MOUSECURSOR_NUM) return cursor;
    if (!desc || !desc->pixels.ptr || desc->width <= 0 || desc->height <= 0) return cursor;
    if (desc->pixels.size < (size_t)(desc->width * desc->height * 4)) return cursor;
    sapp_unbind_mouse_cursor_image(cursor);
    HCURSOR c = _sapp_tc_win32_create_cursor(desc);
    if (!c) return cursor;
    _sapp_tc.app.custom_cursors[cursor] = c;
    _sapp_tc.app.custom_cursor_bound[cursor] = true;
    if (_sapp_tc.app.current_cursor == cursor) {
        _sapp_tc_win32_apply_cursor(cursor, _sapp_tc.app.mouse_shown, false);
    }
    return cursor;
}

void sapp_unbind_mouse_cursor_image(sapp_mouse_cursor cursor) {
    if ((int)cursor < 0 || (int)cursor >= _SAPP_MOUSECURSOR_NUM) return;
    if (!_sapp_tc.app.custom_cursor_bound[cursor]) return;
    _sapp_tc.app.custom_cursor_bound[cursor] = false;
    HCURSOR c = _sapp_tc.app.custom_cursors[cursor];
    _sapp_tc.app.custom_cursors[cursor] = NULL;
    if (_sapp_tc.app.current_cursor == cursor) {
        _sapp_tc_win32_apply_cursor(cursor, _sapp_tc.app.mouse_shown, false);
    }
    if (c) DestroyIcon((HICON)c);
}

void sapp_set_clipboard_string(const char* str) {
    if (!_sapp_tc.app.clipboard_enabled || !_sapp_tc.app.clipboard || !str) return;
    _sapp_tc_window_t* w = _sapp_tc.app.main;
    if (!w) return;
    const SIZE_T wchar_buf_size = (SIZE_T)_sapp_tc.app.clipboard_size * sizeof(wchar_t);
    HANDLE object = GlobalAlloc(GMEM_MOVEABLE, wchar_buf_size);
    if (!object) return;
    wchar_t* wchar_buf = (wchar_t*)GlobalLock(object);
    if (!wchar_buf) {
        GlobalFree(object);
        return;
    }
    if (!_sapp_tc_win32_utf8_to_wide(str, wchar_buf, (int)(wchar_buf_size / sizeof(wchar_t)))) {
        GlobalUnlock(object);
        GlobalFree(object);
        return;
    }
    GlobalUnlock(object);
    bool owned = false;
    if (OpenClipboard(w->hwnd)) {
        EmptyClipboard();
        /* the clipboard takes ownership of the global object on success */
        owned = (NULL != SetClipboardData(CF_UNICODETEXT, object));
        CloseClipboard();
    }
    if (!owned) {
        GlobalFree(object);
        return;
    }
    strncpy(_sapp_tc.app.clipboard, str, (size_t)_sapp_tc.app.clipboard_size - 1);
    _sapp_tc.app.clipboard[_sapp_tc.app.clipboard_size - 1] = 0;
}

const char* sapp_get_clipboard_string(void) {
    if (!_sapp_tc.app.clipboard_enabled || !_sapp_tc.app.clipboard) return "";
    _sapp_tc_window_t* w = _sapp_tc.app.main;
    if (!w) return "";
    _sapp_tc.app.clipboard[0] = 0;
    if (!OpenClipboard(w->hwnd)) {
        return _sapp_tc.app.clipboard;
    }
    HANDLE object = GetClipboardData(CF_UNICODETEXT);
    if (object) {
        const wchar_t* wchar_buf = (const wchar_t*)GlobalLock(object);
        if (wchar_buf) {
            _sapp_tc_win32_wide_to_utf8(wchar_buf, _sapp_tc.app.clipboard,
                                        _sapp_tc.app.clipboard_size);
            GlobalUnlock(object);
        }
    }
    CloseClipboard();
    return _sapp_tc.app.clipboard;
}

void sapp_set_window_title(const char* str) {
    if (!str) return;
    strncpy(_sapp_tc.app.window_title, str, sizeof(_sapp_tc.app.window_title) - 1);
    _sapp_tc_win32_utf8_to_wide(_sapp_tc.app.window_title,
        _sapp_tc.app.window_title_wide,
        (int)(sizeof(_sapp_tc.app.window_title_wide) / sizeof(WCHAR)));
    _sapp_tc_window_t* w = _sapp_tc.app.main;
    if (w && w->hwnd) {
        SetWindowTextW(w->hwnd, _sapp_tc.app.window_title_wide);
    }
}

bool sapp_is_fullscreen(void) {
    return _sapp_tc.app.fullscreen;
}

void sapp_toggle_fullscreen(void) {
    if (!_sapp_tc.app.main) return;
    _sapp_tc_win32_set_fullscreen(!_sapp_tc.app.fullscreen, SWP_SHOWWINDOW);
}

int sapp_get_num_dropped_files(void) {
    return _sapp_tc.app.drop_enabled ? _sapp_tc.app.drop_num_files : 0;
}

const char* sapp_get_dropped_file_path(int index) {
    if (!_sapp_tc.app.drop_enabled || !_sapp_tc.app.drop_buffer) return "";
    if (index < 0 || index >= _sapp_tc.app.drop_num_files) return "";
    return _sapp_tc.app.drop_buffer + (size_t)index * (size_t)_sapp_tc.app.drop_max_path_length;
}

sapp_pixel_format sapp_color_format(void) {
    return SAPP_PIXELFORMAT_RGB10A2;    /* TrussC 10-bit output on D3D11 */
}

sapp_pixel_format sapp_depth_format(void) {
    return SAPP_PIXELFORMAT_DEPTH_STENCIL;
}

const void* sapp_win32_get_hwnd(void) {
    _sapp_tc_window_t* w = _sapp_tc.app.main;
    return w ? (const void*)w->hwnd : 0;
}

const void* sapp_d3d11_get_device(void) {
    return (const void*)_sapp_tc.app.device;
}

const void* sapp_d3d11_get_device_context(void) {
    return (const void*)_sapp_tc.app.device_context;
}

const void* sapp_d3d11_get_swap_chain(void) {
    _sapp_tc_window_t* w = _sapp_tc.app.main;
    return w ? (const void*)w->swap_chain : 0;
}

sapp_environment sapp_get_environment(void) {
    sapp_environment env;
    memset(&env, 0, sizeof(env));
    env.defaults.color_format = sapp_color_format();
    env.defaults.depth_format = sapp_depth_format();
    env.defaults.sample_count = sapp_sample_count();
    env.d3d11.device = (const void*)_sapp_tc.app.device;
    env.d3d11.device_context = (const void*)_sapp_tc.app.device_context;
    return env;
}

sapp_swapchain sapp_get_swapchain(void) {
    /* unlike Metal there is no per-frame acquire: the flip-model swapchain
       rotates internally, the views are created once and reused (resize
       recreates them) -- calling this any number of times per frame is safe */
    sapp_swapchain sc;
    memset(&sc, 0, sizeof(sc));
    _sapp_tc_window_t* w = _sapp_tc.app.main;
    if (!w) return sc;
    sc.width = sapp_width();
    sc.height = sapp_height();
    sc.sample_count = sapp_sample_count();
    sc.color_format = sapp_color_format();
    sc.depth_format = sapp_depth_format();
    if (w->desc.sample_count > 1) {
        sc.d3d11.render_view = (const void*)w->msaa_rtv;
        sc.d3d11.resolve_view = (const void*)w->rtv;
    } else {
        sc.d3d11.render_view = (const void*)w->rtv;
        sc.d3d11.resolve_view = 0;
    }
    sc.d3d11.depth_stencil_view = (const void*)w->dsv;
    return sc;
}

} /* extern "C" */

#elif defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
/*== Linux (X11 + GLX, desktop GL core) =====================================
    Implements the public sokol_app.h API on Linux plus the multi-window API.
    Structure mirrors sokol_app.h's X11/GLX backend (quit dance on
    WM_DELETE_WINDOW, per-frame size polling instead of ConfigureNotify, XKB
    physical-key-name keytable + XkbSetDetectableAutoRepeat software repeat,
    XLookupString + keysym table CHAR events, CLIPBOARD selection, XDND v5,
    EWMH fullscreen, Xcursor cursors, TrussC skip_present patch).

    Frame pacing: ONE GLXContext is shared by all windows, each window has its
    own GLXWindow drawable. The MAIN window keeps upstream's model: the run
    loop blocks inside its vsync'd glXSwapBuffers (swap interval 1 via
    GLX_EXT_swap_control on its drawable) -- that blocking swap IS the pacing.
    SECONDARY windows swap with interval 0 (never block; vsyncing two or more
    drawables would serialize the loop to refresh/N) and are timer-paced to a
    refresh-period estimate measured from the main window's presents. If the
    main swap stops blocking (iconified, skip_present, no usable swap-control
    extension), the tick self-heals to timer pacing, so the loop can never
    busy-spin.

    Unlike upstream, GLX entry points are called directly (TrussC's Linux
    build always links libGL for sokol_gfx's GLCORE backend, so the dlopen
    dance is unnecessary); only extension functions go through
    glXGetProcAddressARB. */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/Xmd.h>
#include <X11/Xresource.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>
#include <X11/Xcursor/Xcursor.h>
/* GL_GLEXT_PROTOTYPES must be defined before the FIRST include of <GL/gl.h>
   in the TU: glext.h is include-guarded, so sokol_gfx.h's own define comes
   too late once this header has pulled the GL headers in (upstream
   sokol_app.h's Linux impl defines it the same way) */
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#endif
#include <GL/gl.h>
#include <GL/glx.h>
#include <time.h>
#include <poll.h>
#include <limits.h>
#include <stdio.h>

#if defined(SOKOL_APP_IMPL_INCLUDED)
#error "sokol_app_tc.h owns the Linux implementation of the sapp_* API; include sokol_app.h WITHOUT an implementation define in this TU"
#endif

#ifndef SOKOL_ASSERT
#include <assert.h>
#define SOKOL_ASSERT(c) assert(c)
#endif

/* GLX extension constants (present in <GL/glxext.h> on any current system;
   defined here so an exotic glx.h cannot break the build) */
#ifndef GLX_CONTEXT_MAJOR_VERSION_ARB
#define GLX_CONTEXT_MAJOR_VERSION_ARB 0x2091
#endif
#ifndef GLX_CONTEXT_MINOR_VERSION_ARB
#define GLX_CONTEXT_MINOR_VERSION_ARB 0x2092
#endif
#ifndef GLX_CONTEXT_PROFILE_MASK_ARB
#define GLX_CONTEXT_PROFILE_MASK_ARB 0x9126
#endif
#ifndef GLX_CONTEXT_CORE_PROFILE_BIT_ARB
#define GLX_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
#endif
#ifndef GLX_CONTEXT_FLAGS_ARB
#define GLX_CONTEXT_FLAGS_ARB 0x2094
#endif
#ifndef GLX_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB
#define GLX_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB 0x00000002
#endif
#ifndef GLX_SAMPLES
#define GLX_SAMPLES 0x186A1
#endif

#define _SAPP_TC_MAX_WINDOWS (32)
#define _SAPP_X11_MAX_X11_KEYCODES (256)
#define _SAPP_TC_XDND_VERSION (5)

/* EMA-filtered frame timer, port of sokol_app.h's _sapp_timing_* (X11 has no
   display-link timestamps, so this measured filter is the only dt source) */
typedef struct {
    double last;        /* previous sample time, 0 = none yet */
    double ema;
    double smooth_dt;
    double dt;          /* last raw (clamped) delta */
} _sapp_tc_timing_t;

typedef struct _sapp_tc_window_t {
    uint32_t win_id;
    sapp_window_desc desc;
    bool is_main;               /* window #0: created by sapp_run, drives the app callbacks */
    bool ready;                 /* creation finished; event dispatch may deliver events */
    Window xwin;                /* the X11 window XID */
    Colormap colormap;          /* per-window (created against the shared visual) */
    GLXWindow glx_win;          /* the GLX drawable wrapping xwin */
    int fb_width, fb_height;    /* framebuffer pixels (== X11 window pixels) */
    int win_width, win_height;  /* logical points */
    float dpi_scale;            /* framebuffer px per point (Xft.dpi / 96) */
    double earliest_next;       /* CLOCK_MONOTONIC seconds; window is not due before this */
    double last_present;        /* time of the previous real present (main pacing probe) */
    double frame_duration;
    _sapp_tc_timing_t timing;
    bool iconified;             /* WM_STATE == IconicState */
    bool occluded;              /* VisibilityFullyObscured (secondary windows only) */
    bool in_tick;
    bool mouse_pos_valid;
    float mouse_x, mouse_y;     /* last position in event coordinates (raw px) */
    float mouse_dx, mouse_dy;
    uint8_t mouse_buttons;      /* held-button bitmask (enter/leave suppression) */
    bool key_repeat[_SAPP_X11_MAX_X11_KEYCODES];  /* software repeat tracking */
} _sapp_tc_window_t;

static struct {
    bool keytable_valid;
    sapp_keycode keycodes[SAPP_MAX_KEYCODES];
    uint32_t next_id;
    _sapp_tc_window_t* windows[_SAPP_TC_MAX_WINDOWS];

    /* ---- X11 process-globals (one server connection for all windows) ----- */
    Display* display;
    int screen;
    Window root;
    float dpi;                  /* Xft.dpi, 96.0 fallback */
    unsigned char error_code;   /* set by the temporary X error handler */
    XErrorHandler prev_error_handler;
    Atom UTF8_STRING;
    Atom CLIPBOARD;
    Atom TARGETS;
    Atom WM_PROTOCOLS;
    Atom WM_DELETE_WINDOW;
    Atom WM_STATE;
    Atom NET_WM_NAME;
    Atom NET_WM_ICON_NAME;
    Atom NET_WM_STATE;
    Atom NET_WM_STATE_FULLSCREEN;
    Atom MOTIF_WM_HINTS;
    Atom SAPP_TC_SELECTION;     /* scratch property for clipboard transfers */
    struct {
        int version;
        Window source;
        Atom format;
        _sapp_tc_window_t* over;    /* which of OUR windows the drag targets */
        Atom XdndAware, XdndEnter, XdndPosition, XdndStatus, XdndActionCopy,
             XdndDrop, XdndFinished, XdndSelection, XdndTypeList, text_uri_list;
    } xdnd;
    Cursor hidden_cursor;
    Cursor standard_cursors[_SAPP_MOUSECURSOR_NUM];
    Cursor custom_cursors[_SAPP_MOUSECURSOR_NUM];

    /* ---- GLX (one shared context; per-window GLXWindow drawables) -------- */
    GLXFBConfig fbconfig;
    GLXContext ctx;
    uint32_t gl_framebuffer;    /* default-FBO id captured after make-current */
    void (*SwapIntervalEXT)(Display*, GLXDrawable, int);    /* per-drawable */
    int (*SwapIntervalMESA)(int);                           /* context-wide */
    GLXContext (*CreateContextAttribsARB)(Display*, GLXFBConfig, GLXContext, Bool, const int*);
    double refresh_period;      /* estimate from the main window's vsync'd presents */

    /* ---- app / main-window state (this header owns sapp_run on Linux) ---- */
    struct {
        sapp_desc desc;             /* sanitized copy; window_title points at the buffer below */
        char window_title[256];
        bool valid;
        bool init_called;
        bool cleanup_called;
        bool quit_requested;
        bool quit_ordered;
        bool fullscreen;
        bool event_consumed;
        bool skip_present;          /* one-shot; honored on glXSwapBuffers (TrussC flicker patch) */
        uint64_t frame_count;
        _sapp_tc_window_t* main;
        /* mouse cursor */
        bool mouse_shown;
        sapp_mouse_cursor current_cursor;
        bool custom_cursor_bound[_SAPP_MOUSECURSOR_NUM];
        /* clipboard */
        bool clipboard_enabled;
        int clipboard_size;
        char* clipboard;
        /* drag & drop */
        bool drop_enabled;
        int drop_max_files;
        int drop_max_path_length;
        int drop_num_files;
        char* drop_buffer;
    } app;
} _sapp_tc;

static void _sapp_tc_x11_tick(_sapp_tc_window_t* w);
static bool _sapp_tc_x11_update_dimensions(_sapp_tc_window_t* w);
static void _sapp_tc_x11_apply_cursor(void);

/*-- timing -----------------------------------------------------------------*/
static double _sapp_tc_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1.0e-9;
}

static void _sapp_tc_timing_reset(_sapp_tc_timing_t* t) {
    t->last = 0.0;
    t->ema = t->smooth_dt = t->dt = 1.0 / 60.0;
}

static double _sapp_tc_timing_clamp(double dt) {
    if (dt < 0.000001) return 0.000001;
    if (dt > 0.1) return 0.1;
    return dt;
}

static void _sapp_tc_timing_update(_sapp_tc_timing_t* t) {
    const double now = _sapp_tc_now();
    if (t->last > 0.0) {
        double dt = _sapp_tc_timing_clamp(now - t->last);
        t->dt = dt;
        /* big jump: reset the filter; otherwise EMA-smooth */
        if (fabs(dt - t->smooth_dt) > 0.004) {
            t->ema = t->smooth_dt = dt;
        } else {
            t->ema += 0.025 * (dt - t->ema);
            t->smooth_dt = _sapp_tc_timing_clamp(t->ema);
        }
    }
    t->last = now;
}

/*-- window lookup ----------------------------------------------------------*/
static _sapp_tc_window_t* _sapp_tc_lookup(sapp_window win) {
    if (win.id == 0) return 0;
    for (int i = 0; i < _SAPP_TC_MAX_WINDOWS; i++) {
        _sapp_tc_window_t* w = _sapp_tc.windows[i];
        if (w && w->win_id == win.id) return w;
    }
    return 0;
}

static _sapp_tc_window_t* _sapp_tc_lookup_xwin(Window xwin) {
    if (!xwin) return 0;
    for (int i = 0; i < _SAPP_TC_MAX_WINDOWS; i++) {
        _sapp_tc_window_t* w = _sapp_tc.windows[i];
        if (w && w->ready && w->xwin == xwin) return w;
    }
    return 0;
}

/*-- event plumbing ---------------------------------------------------------*/
static void _sapp_tc_send(_sapp_tc_window_t* w, sapp_event* ev) {
    if (!w->desc.event_cb) return;
    ev->frame_count = _sapp_tc.app.frame_count;
    ev->window_width = w->win_width;
    ev->window_height = w->win_height;
    ev->framebuffer_width = w->fb_width;
    ev->framebuffer_height = w->fb_height;
    sapp_window handle = { w->win_id };
    w->desc.event_cb(ev, handle, w->desc.user_data);
}

/*-- main window: event routing to the sapp_desc callbacks ------------------*/
static bool _sapp_tc_app_events_enabled(void) {
    /* same gate as sokol_app.h: nothing fires before the first tick's init_cb */
    return _sapp_tc.app.init_called &&
        (_sapp_tc.app.desc.event_cb || _sapp_tc.app.desc.event_userdata_cb);
}

static void _sapp_tc_main_event_tramp(const sapp_event* ev, sapp_window win, void* user) {
    (void)win; (void)user;
    if (!_sapp_tc_app_events_enabled()) return;
    _sapp_tc.app.event_consumed = false;
    if (_sapp_tc.app.desc.event_cb) {
        _sapp_tc.app.desc.event_cb(ev);
    } else {
        _sapp_tc.app.desc.event_userdata_cb(ev, _sapp_tc.app.desc.user_data);
    }
}

/* see GLFW's xkb_unicode.c */
static const struct _sapp_tc_x11_codepair {
  uint16_t keysym;
  uint16_t ucs;
} _sapp_tc_x11_keysymtab[] = {
  { 0x01a1, 0x0104 },
  { 0x01a2, 0x02d8 },
  { 0x01a3, 0x0141 },
  { 0x01a5, 0x013d },
  { 0x01a6, 0x015a },
  { 0x01a9, 0x0160 },
  { 0x01aa, 0x015e },
  { 0x01ab, 0x0164 },
  { 0x01ac, 0x0179 },
  { 0x01ae, 0x017d },
  { 0x01af, 0x017b },
  { 0x01b1, 0x0105 },
  { 0x01b2, 0x02db },
  { 0x01b3, 0x0142 },
  { 0x01b5, 0x013e },
  { 0x01b6, 0x015b },
  { 0x01b7, 0x02c7 },
  { 0x01b9, 0x0161 },
  { 0x01ba, 0x015f },
  { 0x01bb, 0x0165 },
  { 0x01bc, 0x017a },
  { 0x01bd, 0x02dd },
  { 0x01be, 0x017e },
  { 0x01bf, 0x017c },
  { 0x01c0, 0x0154 },
  { 0x01c3, 0x0102 },
  { 0x01c5, 0x0139 },
  { 0x01c6, 0x0106 },
  { 0x01c8, 0x010c },
  { 0x01ca, 0x0118 },
  { 0x01cc, 0x011a },
  { 0x01cf, 0x010e },
  { 0x01d0, 0x0110 },
  { 0x01d1, 0x0143 },
  { 0x01d2, 0x0147 },
  { 0x01d5, 0x0150 },
  { 0x01d8, 0x0158 },
  { 0x01d9, 0x016e },
  { 0x01db, 0x0170 },
  { 0x01de, 0x0162 },
  { 0x01e0, 0x0155 },
  { 0x01e3, 0x0103 },
  { 0x01e5, 0x013a },
  { 0x01e6, 0x0107 },
  { 0x01e8, 0x010d },
  { 0x01ea, 0x0119 },
  { 0x01ec, 0x011b },
  { 0x01ef, 0x010f },
  { 0x01f0, 0x0111 },
  { 0x01f1, 0x0144 },
  { 0x01f2, 0x0148 },
  { 0x01f5, 0x0151 },
  { 0x01f8, 0x0159 },
  { 0x01f9, 0x016f },
  { 0x01fb, 0x0171 },
  { 0x01fe, 0x0163 },
  { 0x01ff, 0x02d9 },
  { 0x02a1, 0x0126 },
  { 0x02a6, 0x0124 },
  { 0x02a9, 0x0130 },
  { 0x02ab, 0x011e },
  { 0x02ac, 0x0134 },
  { 0x02b1, 0x0127 },
  { 0x02b6, 0x0125 },
  { 0x02b9, 0x0131 },
  { 0x02bb, 0x011f },
  { 0x02bc, 0x0135 },
  { 0x02c5, 0x010a },
  { 0x02c6, 0x0108 },
  { 0x02d5, 0x0120 },
  { 0x02d8, 0x011c },
  { 0x02dd, 0x016c },
  { 0x02de, 0x015c },
  { 0x02e5, 0x010b },
  { 0x02e6, 0x0109 },
  { 0x02f5, 0x0121 },
  { 0x02f8, 0x011d },
  { 0x02fd, 0x016d },
  { 0x02fe, 0x015d },
  { 0x03a2, 0x0138 },
  { 0x03a3, 0x0156 },
  { 0x03a5, 0x0128 },
  { 0x03a6, 0x013b },
  { 0x03aa, 0x0112 },
  { 0x03ab, 0x0122 },
  { 0x03ac, 0x0166 },
  { 0x03b3, 0x0157 },
  { 0x03b5, 0x0129 },
  { 0x03b6, 0x013c },
  { 0x03ba, 0x0113 },
  { 0x03bb, 0x0123 },
  { 0x03bc, 0x0167 },
  { 0x03bd, 0x014a },
  { 0x03bf, 0x014b },
  { 0x03c0, 0x0100 },
  { 0x03c7, 0x012e },
  { 0x03cc, 0x0116 },
  { 0x03cf, 0x012a },
  { 0x03d1, 0x0145 },
  { 0x03d2, 0x014c },
  { 0x03d3, 0x0136 },
  { 0x03d9, 0x0172 },
  { 0x03dd, 0x0168 },
  { 0x03de, 0x016a },
  { 0x03e0, 0x0101 },
  { 0x03e7, 0x012f },
  { 0x03ec, 0x0117 },
  { 0x03ef, 0x012b },
  { 0x03f1, 0x0146 },
  { 0x03f2, 0x014d },
  { 0x03f3, 0x0137 },
  { 0x03f9, 0x0173 },
  { 0x03fd, 0x0169 },
  { 0x03fe, 0x016b },
  { 0x047e, 0x203e },
  { 0x04a1, 0x3002 },
  { 0x04a2, 0x300c },
  { 0x04a3, 0x300d },
  { 0x04a4, 0x3001 },
  { 0x04a5, 0x30fb },
  { 0x04a6, 0x30f2 },
  { 0x04a7, 0x30a1 },
  { 0x04a8, 0x30a3 },
  { 0x04a9, 0x30a5 },
  { 0x04aa, 0x30a7 },
  { 0x04ab, 0x30a9 },
  { 0x04ac, 0x30e3 },
  { 0x04ad, 0x30e5 },
  { 0x04ae, 0x30e7 },
  { 0x04af, 0x30c3 },
  { 0x04b0, 0x30fc },
  { 0x04b1, 0x30a2 },
  { 0x04b2, 0x30a4 },
  { 0x04b3, 0x30a6 },
  { 0x04b4, 0x30a8 },
  { 0x04b5, 0x30aa },
  { 0x04b6, 0x30ab },
  { 0x04b7, 0x30ad },
  { 0x04b8, 0x30af },
  { 0x04b9, 0x30b1 },
  { 0x04ba, 0x30b3 },
  { 0x04bb, 0x30b5 },
  { 0x04bc, 0x30b7 },
  { 0x04bd, 0x30b9 },
  { 0x04be, 0x30bb },
  { 0x04bf, 0x30bd },
  { 0x04c0, 0x30bf },
  { 0x04c1, 0x30c1 },
  { 0x04c2, 0x30c4 },
  { 0x04c3, 0x30c6 },
  { 0x04c4, 0x30c8 },
  { 0x04c5, 0x30ca },
  { 0x04c6, 0x30cb },
  { 0x04c7, 0x30cc },
  { 0x04c8, 0x30cd },
  { 0x04c9, 0x30ce },
  { 0x04ca, 0x30cf },
  { 0x04cb, 0x30d2 },
  { 0x04cc, 0x30d5 },
  { 0x04cd, 0x30d8 },
  { 0x04ce, 0x30db },
  { 0x04cf, 0x30de },
  { 0x04d0, 0x30df },
  { 0x04d1, 0x30e0 },
  { 0x04d2, 0x30e1 },
  { 0x04d3, 0x30e2 },
  { 0x04d4, 0x30e4 },
  { 0x04d5, 0x30e6 },
  { 0x04d6, 0x30e8 },
  { 0x04d7, 0x30e9 },
  { 0x04d8, 0x30ea },
  { 0x04d9, 0x30eb },
  { 0x04da, 0x30ec },
  { 0x04db, 0x30ed },
  { 0x04dc, 0x30ef },
  { 0x04dd, 0x30f3 },
  { 0x04de, 0x309b },
  { 0x04df, 0x309c },
  { 0x05ac, 0x060c },
  { 0x05bb, 0x061b },
  { 0x05bf, 0x061f },
  { 0x05c1, 0x0621 },
  { 0x05c2, 0x0622 },
  { 0x05c3, 0x0623 },
  { 0x05c4, 0x0624 },
  { 0x05c5, 0x0625 },
  { 0x05c6, 0x0626 },
  { 0x05c7, 0x0627 },
  { 0x05c8, 0x0628 },
  { 0x05c9, 0x0629 },
  { 0x05ca, 0x062a },
  { 0x05cb, 0x062b },
  { 0x05cc, 0x062c },
  { 0x05cd, 0x062d },
  { 0x05ce, 0x062e },
  { 0x05cf, 0x062f },
  { 0x05d0, 0x0630 },
  { 0x05d1, 0x0631 },
  { 0x05d2, 0x0632 },
  { 0x05d3, 0x0633 },
  { 0x05d4, 0x0634 },
  { 0x05d5, 0x0635 },
  { 0x05d6, 0x0636 },
  { 0x05d7, 0x0637 },
  { 0x05d8, 0x0638 },
  { 0x05d9, 0x0639 },
  { 0x05da, 0x063a },
  { 0x05e0, 0x0640 },
  { 0x05e1, 0x0641 },
  { 0x05e2, 0x0642 },
  { 0x05e3, 0x0643 },
  { 0x05e4, 0x0644 },
  { 0x05e5, 0x0645 },
  { 0x05e6, 0x0646 },
  { 0x05e7, 0x0647 },
  { 0x05e8, 0x0648 },
  { 0x05e9, 0x0649 },
  { 0x05ea, 0x064a },
  { 0x05eb, 0x064b },
  { 0x05ec, 0x064c },
  { 0x05ed, 0x064d },
  { 0x05ee, 0x064e },
  { 0x05ef, 0x064f },
  { 0x05f0, 0x0650 },
  { 0x05f1, 0x0651 },
  { 0x05f2, 0x0652 },
  { 0x06a1, 0x0452 },
  { 0x06a2, 0x0453 },
  { 0x06a3, 0x0451 },
  { 0x06a4, 0x0454 },
  { 0x06a5, 0x0455 },
  { 0x06a6, 0x0456 },
  { 0x06a7, 0x0457 },
  { 0x06a8, 0x0458 },
  { 0x06a9, 0x0459 },
  { 0x06aa, 0x045a },
  { 0x06ab, 0x045b },
  { 0x06ac, 0x045c },
  { 0x06ae, 0x045e },
  { 0x06af, 0x045f },
  { 0x06b0, 0x2116 },
  { 0x06b1, 0x0402 },
  { 0x06b2, 0x0403 },
  { 0x06b3, 0x0401 },
  { 0x06b4, 0x0404 },
  { 0x06b5, 0x0405 },
  { 0x06b6, 0x0406 },
  { 0x06b7, 0x0407 },
  { 0x06b8, 0x0408 },
  { 0x06b9, 0x0409 },
  { 0x06ba, 0x040a },
  { 0x06bb, 0x040b },
  { 0x06bc, 0x040c },
  { 0x06be, 0x040e },
  { 0x06bf, 0x040f },
  { 0x06c0, 0x044e },
  { 0x06c1, 0x0430 },
  { 0x06c2, 0x0431 },
  { 0x06c3, 0x0446 },
  { 0x06c4, 0x0434 },
  { 0x06c5, 0x0435 },
  { 0x06c6, 0x0444 },
  { 0x06c7, 0x0433 },
  { 0x06c8, 0x0445 },
  { 0x06c9, 0x0438 },
  { 0x06ca, 0x0439 },
  { 0x06cb, 0x043a },
  { 0x06cc, 0x043b },
  { 0x06cd, 0x043c },
  { 0x06ce, 0x043d },
  { 0x06cf, 0x043e },
  { 0x06d0, 0x043f },
  { 0x06d1, 0x044f },
  { 0x06d2, 0x0440 },
  { 0x06d3, 0x0441 },
  { 0x06d4, 0x0442 },
  { 0x06d5, 0x0443 },
  { 0x06d6, 0x0436 },
  { 0x06d7, 0x0432 },
  { 0x06d8, 0x044c },
  { 0x06d9, 0x044b },
  { 0x06da, 0x0437 },
  { 0x06db, 0x0448 },
  { 0x06dc, 0x044d },
  { 0x06dd, 0x0449 },
  { 0x06de, 0x0447 },
  { 0x06df, 0x044a },
  { 0x06e0, 0x042e },
  { 0x06e1, 0x0410 },
  { 0x06e2, 0x0411 },
  { 0x06e3, 0x0426 },
  { 0x06e4, 0x0414 },
  { 0x06e5, 0x0415 },
  { 0x06e6, 0x0424 },
  { 0x06e7, 0x0413 },
  { 0x06e8, 0x0425 },
  { 0x06e9, 0x0418 },
  { 0x06ea, 0x0419 },
  { 0x06eb, 0x041a },
  { 0x06ec, 0x041b },
  { 0x06ed, 0x041c },
  { 0x06ee, 0x041d },
  { 0x06ef, 0x041e },
  { 0x06f0, 0x041f },
  { 0x06f1, 0x042f },
  { 0x06f2, 0x0420 },
  { 0x06f3, 0x0421 },
  { 0x06f4, 0x0422 },
  { 0x06f5, 0x0423 },
  { 0x06f6, 0x0416 },
  { 0x06f7, 0x0412 },
  { 0x06f8, 0x042c },
  { 0x06f9, 0x042b },
  { 0x06fa, 0x0417 },
  { 0x06fb, 0x0428 },
  { 0x06fc, 0x042d },
  { 0x06fd, 0x0429 },
  { 0x06fe, 0x0427 },
  { 0x06ff, 0x042a },
  { 0x07a1, 0x0386 },
  { 0x07a2, 0x0388 },
  { 0x07a3, 0x0389 },
  { 0x07a4, 0x038a },
  { 0x07a5, 0x03aa },
  { 0x07a7, 0x038c },
  { 0x07a8, 0x038e },
  { 0x07a9, 0x03ab },
  { 0x07ab, 0x038f },
  { 0x07ae, 0x0385 },
  { 0x07af, 0x2015 },
  { 0x07b1, 0x03ac },
  { 0x07b2, 0x03ad },
  { 0x07b3, 0x03ae },
  { 0x07b4, 0x03af },
  { 0x07b5, 0x03ca },
  { 0x07b6, 0x0390 },
  { 0x07b7, 0x03cc },
  { 0x07b8, 0x03cd },
  { 0x07b9, 0x03cb },
  { 0x07ba, 0x03b0 },
  { 0x07bb, 0x03ce },
  { 0x07c1, 0x0391 },
  { 0x07c2, 0x0392 },
  { 0x07c3, 0x0393 },
  { 0x07c4, 0x0394 },
  { 0x07c5, 0x0395 },
  { 0x07c6, 0x0396 },
  { 0x07c7, 0x0397 },
  { 0x07c8, 0x0398 },
  { 0x07c9, 0x0399 },
  { 0x07ca, 0x039a },
  { 0x07cb, 0x039b },
  { 0x07cc, 0x039c },
  { 0x07cd, 0x039d },
  { 0x07ce, 0x039e },
  { 0x07cf, 0x039f },
  { 0x07d0, 0x03a0 },
  { 0x07d1, 0x03a1 },
  { 0x07d2, 0x03a3 },
  { 0x07d4, 0x03a4 },
  { 0x07d5, 0x03a5 },
  { 0x07d6, 0x03a6 },
  { 0x07d7, 0x03a7 },
  { 0x07d8, 0x03a8 },
  { 0x07d9, 0x03a9 },
  { 0x07e1, 0x03b1 },
  { 0x07e2, 0x03b2 },
  { 0x07e3, 0x03b3 },
  { 0x07e4, 0x03b4 },
  { 0x07e5, 0x03b5 },
  { 0x07e6, 0x03b6 },
  { 0x07e7, 0x03b7 },
  { 0x07e8, 0x03b8 },
  { 0x07e9, 0x03b9 },
  { 0x07ea, 0x03ba },
  { 0x07eb, 0x03bb },
  { 0x07ec, 0x03bc },
  { 0x07ed, 0x03bd },
  { 0x07ee, 0x03be },
  { 0x07ef, 0x03bf },
  { 0x07f0, 0x03c0 },
  { 0x07f1, 0x03c1 },
  { 0x07f2, 0x03c3 },
  { 0x07f3, 0x03c2 },
  { 0x07f4, 0x03c4 },
  { 0x07f5, 0x03c5 },
  { 0x07f6, 0x03c6 },
  { 0x07f7, 0x03c7 },
  { 0x07f8, 0x03c8 },
  { 0x07f9, 0x03c9 },
  { 0x08a1, 0x23b7 },
  { 0x08a2, 0x250c },
  { 0x08a3, 0x2500 },
  { 0x08a4, 0x2320 },
  { 0x08a5, 0x2321 },
  { 0x08a6, 0x2502 },
  { 0x08a7, 0x23a1 },
  { 0x08a8, 0x23a3 },
  { 0x08a9, 0x23a4 },
  { 0x08aa, 0x23a6 },
  { 0x08ab, 0x239b },
  { 0x08ac, 0x239d },
  { 0x08ad, 0x239e },
  { 0x08ae, 0x23a0 },
  { 0x08af, 0x23a8 },
  { 0x08b0, 0x23ac },
  { 0x08bc, 0x2264 },
  { 0x08bd, 0x2260 },
  { 0x08be, 0x2265 },
  { 0x08bf, 0x222b },
  { 0x08c0, 0x2234 },
  { 0x08c1, 0x221d },
  { 0x08c2, 0x221e },
  { 0x08c5, 0x2207 },
  { 0x08c8, 0x223c },
  { 0x08c9, 0x2243 },
  { 0x08cd, 0x21d4 },
  { 0x08ce, 0x21d2 },
  { 0x08cf, 0x2261 },
  { 0x08d6, 0x221a },
  { 0x08da, 0x2282 },
  { 0x08db, 0x2283 },
  { 0x08dc, 0x2229 },
  { 0x08dd, 0x222a },
  { 0x08de, 0x2227 },
  { 0x08df, 0x2228 },
  { 0x08ef, 0x2202 },
  { 0x08f6, 0x0192 },
  { 0x08fb, 0x2190 },
  { 0x08fc, 0x2191 },
  { 0x08fd, 0x2192 },
  { 0x08fe, 0x2193 },
  { 0x09e0, 0x25c6 },
  { 0x09e1, 0x2592 },
  { 0x09e2, 0x2409 },
  { 0x09e3, 0x240c },
  { 0x09e4, 0x240d },
  { 0x09e5, 0x240a },
  { 0x09e8, 0x2424 },
  { 0x09e9, 0x240b },
  { 0x09ea, 0x2518 },
  { 0x09eb, 0x2510 },
  { 0x09ec, 0x250c },
  { 0x09ed, 0x2514 },
  { 0x09ee, 0x253c },
  { 0x09ef, 0x23ba },
  { 0x09f0, 0x23bb },
  { 0x09f1, 0x2500 },
  { 0x09f2, 0x23bc },
  { 0x09f3, 0x23bd },
  { 0x09f4, 0x251c },
  { 0x09f5, 0x2524 },
  { 0x09f6, 0x2534 },
  { 0x09f7, 0x252c },
  { 0x09f8, 0x2502 },
  { 0x0aa1, 0x2003 },
  { 0x0aa2, 0x2002 },
  { 0x0aa3, 0x2004 },
  { 0x0aa4, 0x2005 },
  { 0x0aa5, 0x2007 },
  { 0x0aa6, 0x2008 },
  { 0x0aa7, 0x2009 },
  { 0x0aa8, 0x200a },
  { 0x0aa9, 0x2014 },
  { 0x0aaa, 0x2013 },
  { 0x0aae, 0x2026 },
  { 0x0aaf, 0x2025 },
  { 0x0ab0, 0x2153 },
  { 0x0ab1, 0x2154 },
  { 0x0ab2, 0x2155 },
  { 0x0ab3, 0x2156 },
  { 0x0ab4, 0x2157 },
  { 0x0ab5, 0x2158 },
  { 0x0ab6, 0x2159 },
  { 0x0ab7, 0x215a },
  { 0x0ab8, 0x2105 },
  { 0x0abb, 0x2012 },
  { 0x0abc, 0x2329 },
  { 0x0abe, 0x232a },
  { 0x0ac3, 0x215b },
  { 0x0ac4, 0x215c },
  { 0x0ac5, 0x215d },
  { 0x0ac6, 0x215e },
  { 0x0ac9, 0x2122 },
  { 0x0aca, 0x2613 },
  { 0x0acc, 0x25c1 },
  { 0x0acd, 0x25b7 },
  { 0x0ace, 0x25cb },
  { 0x0acf, 0x25af },
  { 0x0ad0, 0x2018 },
  { 0x0ad1, 0x2019 },
  { 0x0ad2, 0x201c },
  { 0x0ad3, 0x201d },
  { 0x0ad4, 0x211e },
  { 0x0ad6, 0x2032 },
  { 0x0ad7, 0x2033 },
  { 0x0ad9, 0x271d },
  { 0x0adb, 0x25ac },
  { 0x0adc, 0x25c0 },
  { 0x0add, 0x25b6 },
  { 0x0ade, 0x25cf },
  { 0x0adf, 0x25ae },
  { 0x0ae0, 0x25e6 },
  { 0x0ae1, 0x25ab },
  { 0x0ae2, 0x25ad },
  { 0x0ae3, 0x25b3 },
  { 0x0ae4, 0x25bd },
  { 0x0ae5, 0x2606 },
  { 0x0ae6, 0x2022 },
  { 0x0ae7, 0x25aa },
  { 0x0ae8, 0x25b2 },
  { 0x0ae9, 0x25bc },
  { 0x0aea, 0x261c },
  { 0x0aeb, 0x261e },
  { 0x0aec, 0x2663 },
  { 0x0aed, 0x2666 },
  { 0x0aee, 0x2665 },
  { 0x0af0, 0x2720 },
  { 0x0af1, 0x2020 },
  { 0x0af2, 0x2021 },
  { 0x0af3, 0x2713 },
  { 0x0af4, 0x2717 },
  { 0x0af5, 0x266f },
  { 0x0af6, 0x266d },
  { 0x0af7, 0x2642 },
  { 0x0af8, 0x2640 },
  { 0x0af9, 0x260e },
  { 0x0afa, 0x2315 },
  { 0x0afb, 0x2117 },
  { 0x0afc, 0x2038 },
  { 0x0afd, 0x201a },
  { 0x0afe, 0x201e },
  { 0x0ba3, 0x003c },
  { 0x0ba6, 0x003e },
  { 0x0ba8, 0x2228 },
  { 0x0ba9, 0x2227 },
  { 0x0bc0, 0x00af },
  { 0x0bc2, 0x22a5 },
  { 0x0bc3, 0x2229 },
  { 0x0bc4, 0x230a },
  { 0x0bc6, 0x005f },
  { 0x0bca, 0x2218 },
  { 0x0bcc, 0x2395 },
  { 0x0bce, 0x22a4 },
  { 0x0bcf, 0x25cb },
  { 0x0bd3, 0x2308 },
  { 0x0bd6, 0x222a },
  { 0x0bd8, 0x2283 },
  { 0x0bda, 0x2282 },
  { 0x0bdc, 0x22a2 },
  { 0x0bfc, 0x22a3 },
  { 0x0cdf, 0x2017 },
  { 0x0ce0, 0x05d0 },
  { 0x0ce1, 0x05d1 },
  { 0x0ce2, 0x05d2 },
  { 0x0ce3, 0x05d3 },
  { 0x0ce4, 0x05d4 },
  { 0x0ce5, 0x05d5 },
  { 0x0ce6, 0x05d6 },
  { 0x0ce7, 0x05d7 },
  { 0x0ce8, 0x05d8 },
  { 0x0ce9, 0x05d9 },
  { 0x0cea, 0x05da },
  { 0x0ceb, 0x05db },
  { 0x0cec, 0x05dc },
  { 0x0ced, 0x05dd },
  { 0x0cee, 0x05de },
  { 0x0cef, 0x05df },
  { 0x0cf0, 0x05e0 },
  { 0x0cf1, 0x05e1 },
  { 0x0cf2, 0x05e2 },
  { 0x0cf3, 0x05e3 },
  { 0x0cf4, 0x05e4 },
  { 0x0cf5, 0x05e5 },
  { 0x0cf6, 0x05e6 },
  { 0x0cf7, 0x05e7 },
  { 0x0cf8, 0x05e8 },
  { 0x0cf9, 0x05e9 },
  { 0x0cfa, 0x05ea },
  { 0x0da1, 0x0e01 },
  { 0x0da2, 0x0e02 },
  { 0x0da3, 0x0e03 },
  { 0x0da4, 0x0e04 },
  { 0x0da5, 0x0e05 },
  { 0x0da6, 0x0e06 },
  { 0x0da7, 0x0e07 },
  { 0x0da8, 0x0e08 },
  { 0x0da9, 0x0e09 },
  { 0x0daa, 0x0e0a },
  { 0x0dab, 0x0e0b },
  { 0x0dac, 0x0e0c },
  { 0x0dad, 0x0e0d },
  { 0x0dae, 0x0e0e },
  { 0x0daf, 0x0e0f },
  { 0x0db0, 0x0e10 },
  { 0x0db1, 0x0e11 },
  { 0x0db2, 0x0e12 },
  { 0x0db3, 0x0e13 },
  { 0x0db4, 0x0e14 },
  { 0x0db5, 0x0e15 },
  { 0x0db6, 0x0e16 },
  { 0x0db7, 0x0e17 },
  { 0x0db8, 0x0e18 },
  { 0x0db9, 0x0e19 },
  { 0x0dba, 0x0e1a },
  { 0x0dbb, 0x0e1b },
  { 0x0dbc, 0x0e1c },
  { 0x0dbd, 0x0e1d },
  { 0x0dbe, 0x0e1e },
  { 0x0dbf, 0x0e1f },
  { 0x0dc0, 0x0e20 },
  { 0x0dc1, 0x0e21 },
  { 0x0dc2, 0x0e22 },
  { 0x0dc3, 0x0e23 },
  { 0x0dc4, 0x0e24 },
  { 0x0dc5, 0x0e25 },
  { 0x0dc6, 0x0e26 },
  { 0x0dc7, 0x0e27 },
  { 0x0dc8, 0x0e28 },
  { 0x0dc9, 0x0e29 },
  { 0x0dca, 0x0e2a },
  { 0x0dcb, 0x0e2b },
  { 0x0dcc, 0x0e2c },
  { 0x0dcd, 0x0e2d },
  { 0x0dce, 0x0e2e },
  { 0x0dcf, 0x0e2f },
  { 0x0dd0, 0x0e30 },
  { 0x0dd1, 0x0e31 },
  { 0x0dd2, 0x0e32 },
  { 0x0dd3, 0x0e33 },
  { 0x0dd4, 0x0e34 },
  { 0x0dd5, 0x0e35 },
  { 0x0dd6, 0x0e36 },
  { 0x0dd7, 0x0e37 },
  { 0x0dd8, 0x0e38 },
  { 0x0dd9, 0x0e39 },
  { 0x0dda, 0x0e3a },
  { 0x0ddf, 0x0e3f },
  { 0x0de0, 0x0e40 },
  { 0x0de1, 0x0e41 },
  { 0x0de2, 0x0e42 },
  { 0x0de3, 0x0e43 },
  { 0x0de4, 0x0e44 },
  { 0x0de5, 0x0e45 },
  { 0x0de6, 0x0e46 },
  { 0x0de7, 0x0e47 },
  { 0x0de8, 0x0e48 },
  { 0x0de9, 0x0e49 },
  { 0x0dea, 0x0e4a },
  { 0x0deb, 0x0e4b },
  { 0x0dec, 0x0e4c },
  { 0x0ded, 0x0e4d },
  { 0x0df0, 0x0e50 },
  { 0x0df1, 0x0e51 },
  { 0x0df2, 0x0e52 },
  { 0x0df3, 0x0e53 },
  { 0x0df4, 0x0e54 },
  { 0x0df5, 0x0e55 },
  { 0x0df6, 0x0e56 },
  { 0x0df7, 0x0e57 },
  { 0x0df8, 0x0e58 },
  { 0x0df9, 0x0e59 },
  { 0x0ea1, 0x3131 },
  { 0x0ea2, 0x3132 },
  { 0x0ea3, 0x3133 },
  { 0x0ea4, 0x3134 },
  { 0x0ea5, 0x3135 },
  { 0x0ea6, 0x3136 },
  { 0x0ea7, 0x3137 },
  { 0x0ea8, 0x3138 },
  { 0x0ea9, 0x3139 },
  { 0x0eaa, 0x313a },
  { 0x0eab, 0x313b },
  { 0x0eac, 0x313c },
  { 0x0ead, 0x313d },
  { 0x0eae, 0x313e },
  { 0x0eaf, 0x313f },
  { 0x0eb0, 0x3140 },
  { 0x0eb1, 0x3141 },
  { 0x0eb2, 0x3142 },
  { 0x0eb3, 0x3143 },
  { 0x0eb4, 0x3144 },
  { 0x0eb5, 0x3145 },
  { 0x0eb6, 0x3146 },
  { 0x0eb7, 0x3147 },
  { 0x0eb8, 0x3148 },
  { 0x0eb9, 0x3149 },
  { 0x0eba, 0x314a },
  { 0x0ebb, 0x314b },
  { 0x0ebc, 0x314c },
  { 0x0ebd, 0x314d },
  { 0x0ebe, 0x314e },
  { 0x0ebf, 0x314f },
  { 0x0ec0, 0x3150 },
  { 0x0ec1, 0x3151 },
  { 0x0ec2, 0x3152 },
  { 0x0ec3, 0x3153 },
  { 0x0ec4, 0x3154 },
  { 0x0ec5, 0x3155 },
  { 0x0ec6, 0x3156 },
  { 0x0ec7, 0x3157 },
  { 0x0ec8, 0x3158 },
  { 0x0ec9, 0x3159 },
  { 0x0eca, 0x315a },
  { 0x0ecb, 0x315b },
  { 0x0ecc, 0x315c },
  { 0x0ecd, 0x315d },
  { 0x0ece, 0x315e },
  { 0x0ecf, 0x315f },
  { 0x0ed0, 0x3160 },
  { 0x0ed1, 0x3161 },
  { 0x0ed2, 0x3162 },
  { 0x0ed3, 0x3163 },
  { 0x0ed4, 0x11a8 },
  { 0x0ed5, 0x11a9 },
  { 0x0ed6, 0x11aa },
  { 0x0ed7, 0x11ab },
  { 0x0ed8, 0x11ac },
  { 0x0ed9, 0x11ad },
  { 0x0eda, 0x11ae },
  { 0x0edb, 0x11af },
  { 0x0edc, 0x11b0 },
  { 0x0edd, 0x11b1 },
  { 0x0ede, 0x11b2 },
  { 0x0edf, 0x11b3 },
  { 0x0ee0, 0x11b4 },
  { 0x0ee1, 0x11b5 },
  { 0x0ee2, 0x11b6 },
  { 0x0ee3, 0x11b7 },
  { 0x0ee4, 0x11b8 },
  { 0x0ee5, 0x11b9 },
  { 0x0ee6, 0x11ba },
  { 0x0ee7, 0x11bb },
  { 0x0ee8, 0x11bc },
  { 0x0ee9, 0x11bd },
  { 0x0eea, 0x11be },
  { 0x0eeb, 0x11bf },
  { 0x0eec, 0x11c0 },
  { 0x0eed, 0x11c1 },
  { 0x0eee, 0x11c2 },
  { 0x0eef, 0x316d },
  { 0x0ef0, 0x3171 },
  { 0x0ef1, 0x3178 },
  { 0x0ef2, 0x317f },
  { 0x0ef3, 0x3181 },
  { 0x0ef4, 0x3184 },
  { 0x0ef5, 0x3186 },
  { 0x0ef6, 0x318d },
  { 0x0ef7, 0x318e },
  { 0x0ef8, 0x11eb },
  { 0x0ef9, 0x11f0 },
  { 0x0efa, 0x11f9 },
  { 0x0eff, 0x20a9 },
  { 0x13a4, 0x20ac },
  { 0x13bc, 0x0152 },
  { 0x13bd, 0x0153 },
  { 0x13be, 0x0178 },
  { 0x20ac, 0x20ac },
  { 0xfe50,    '`' },
  { 0xfe51, 0x00b4 },
  { 0xfe52,    '^' },
  { 0xfe53,    '~' },
  { 0xfe54, 0x00af },
  { 0xfe55, 0x02d8 },
  { 0xfe56, 0x02d9 },
  { 0xfe57, 0x00a8 },
  { 0xfe58, 0x02da },
  { 0xfe59, 0x02dd },
  { 0xfe5a, 0x02c7 },
  { 0xfe5b, 0x00b8 },
  { 0xfe5c, 0x02db },
  { 0xfe5d, 0x037a },
  { 0xfe5e, 0x309b },
  { 0xfe5f, 0x309c },
  { 0xfe63,    '/' },
  { 0xfe64, 0x02bc },
  { 0xfe65, 0x02bd },
  { 0xfe66, 0x02f5 },
  { 0xfe67, 0x02f3 },
  { 0xfe68, 0x02cd },
  { 0xfe69, 0xa788 },
  { 0xfe6a, 0x02f7 },
  { 0xfe6e,    ',' },
  { 0xfe6f, 0x00a4 },
  { 0xfe80,    'a' }, /* XK_dead_a */
  { 0xfe81,    'A' }, /* XK_dead_A */
  { 0xfe82,    'e' }, /* XK_dead_e */
  { 0xfe83,    'E' }, /* XK_dead_E */
  { 0xfe84,    'i' }, /* XK_dead_i */
  { 0xfe85,    'I' }, /* XK_dead_I */
  { 0xfe86,    'o' }, /* XK_dead_o */
  { 0xfe87,    'O' }, /* XK_dead_O */
  { 0xfe88,    'u' }, /* XK_dead_u */
  { 0xfe89,    'U' }, /* XK_dead_U */
  { 0xfe8a, 0x0259 },
  { 0xfe8b, 0x018f },
  { 0xfe8c, 0x00b5 },
  { 0xfe90,    '_' },
  { 0xfe91, 0x02c8 },
  { 0xfe92, 0x02cc },
  { 0xff80 /*XKB_KEY_KP_Space*/,     ' ' },
  { 0xff95 /*XKB_KEY_KP_7*/, 0x0037 },
  { 0xff96 /*XKB_KEY_KP_4*/, 0x0034 },
  { 0xff97 /*XKB_KEY_KP_8*/, 0x0038 },
  { 0xff98 /*XKB_KEY_KP_6*/, 0x0036 },
  { 0xff99 /*XKB_KEY_KP_2*/, 0x0032 },
  { 0xff9a /*XKB_KEY_KP_9*/, 0x0039 },
  { 0xff9b /*XKB_KEY_KP_3*/, 0x0033 },
  { 0xff9c /*XKB_KEY_KP_1*/, 0x0031 },
  { 0xff9d /*XKB_KEY_KP_5*/, 0x0035 },
  { 0xff9e /*XKB_KEY_KP_0*/, 0x0030 },
  { 0xffaa /*XKB_KEY_KP_Multiply*/,  '*' },
  { 0xffab /*XKB_KEY_KP_Add*/,       '+' },
  { 0xffac /*XKB_KEY_KP_Separator*/, ',' },
  { 0xffad /*XKB_KEY_KP_Subtract*/,  '-' },
  { 0xffae /*XKB_KEY_KP_Decimal*/,   '.' },
  { 0xffaf /*XKB_KEY_KP_Divide*/,    '/' },
  { 0xffb0 /*XKB_KEY_KP_0*/, 0x0030 },
  { 0xffb1 /*XKB_KEY_KP_1*/, 0x0031 },
  { 0xffb2 /*XKB_KEY_KP_2*/, 0x0032 },
  { 0xffb3 /*XKB_KEY_KP_3*/, 0x0033 },
  { 0xffb4 /*XKB_KEY_KP_4*/, 0x0034 },
  { 0xffb5 /*XKB_KEY_KP_5*/, 0x0035 },
  { 0xffb6 /*XKB_KEY_KP_6*/, 0x0036 },
  { 0xffb7 /*XKB_KEY_KP_7*/, 0x0037 },
  { 0xffb8 /*XKB_KEY_KP_8*/, 0x0038 },
  { 0xffb9 /*XKB_KEY_KP_9*/, 0x0039 },
  { 0xffbd /*XKB_KEY_KP_Equal*/,     '=' }
};

// translate the X11 KeySyms for a key to sokol-app key code
// NOTE: this is only used as a fallback, in case the XBK method fails
//       it is layout-dependent and will fail partially on most non-US layouts.
//
static sapp_keycode _sapp_tc_x11_translate_keysyms(const KeySym* keysyms, int width) {
    if (width > 1) {
        switch (keysyms[1]) {
            case XK_KP_0:           return SAPP_KEYCODE_KP_0;
            case XK_KP_1:           return SAPP_KEYCODE_KP_1;
            case XK_KP_2:           return SAPP_KEYCODE_KP_2;
            case XK_KP_3:           return SAPP_KEYCODE_KP_3;
            case XK_KP_4:           return SAPP_KEYCODE_KP_4;
            case XK_KP_5:           return SAPP_KEYCODE_KP_5;
            case XK_KP_6:           return SAPP_KEYCODE_KP_6;
            case XK_KP_7:           return SAPP_KEYCODE_KP_7;
            case XK_KP_8:           return SAPP_KEYCODE_KP_8;
            case XK_KP_9:           return SAPP_KEYCODE_KP_9;
            case XK_KP_Separator:
            case XK_KP_Decimal:     return SAPP_KEYCODE_KP_DECIMAL;
            case XK_KP_Equal:       return SAPP_KEYCODE_KP_EQUAL;
            case XK_KP_Enter:       return SAPP_KEYCODE_KP_ENTER;
            default:                break;
        }
    }

    switch (keysyms[0]) {
        case XK_Escape:         return SAPP_KEYCODE_ESCAPE;
        case XK_Tab:            return SAPP_KEYCODE_TAB;
        case XK_Shift_L:        return SAPP_KEYCODE_LEFT_SHIFT;
        case XK_Shift_R:        return SAPP_KEYCODE_RIGHT_SHIFT;
        case XK_Control_L:      return SAPP_KEYCODE_LEFT_CONTROL;
        case XK_Control_R:      return SAPP_KEYCODE_RIGHT_CONTROL;
        case XK_Meta_L:
        case XK_Alt_L:          return SAPP_KEYCODE_LEFT_ALT;
        case XK_Mode_switch: // Mapped to Alt_R on many keyboards
        case XK_ISO_Level3_Shift: // AltGr on at least some machines
        case XK_Meta_R:
        case XK_Alt_R:          return SAPP_KEYCODE_RIGHT_ALT;
        case XK_Super_L:        return SAPP_KEYCODE_LEFT_SUPER;
        case XK_Super_R:        return SAPP_KEYCODE_RIGHT_SUPER;
        case XK_Menu:           return SAPP_KEYCODE_MENU;
        case XK_Num_Lock:       return SAPP_KEYCODE_NUM_LOCK;
        case XK_Caps_Lock:      return SAPP_KEYCODE_CAPS_LOCK;
        case XK_Print:          return SAPP_KEYCODE_PRINT_SCREEN;
        case XK_Scroll_Lock:    return SAPP_KEYCODE_SCROLL_LOCK;
        case XK_Pause:          return SAPP_KEYCODE_PAUSE;
        case XK_Delete:         return SAPP_KEYCODE_DELETE;
        case XK_BackSpace:      return SAPP_KEYCODE_BACKSPACE;
        case XK_Return:         return SAPP_KEYCODE_ENTER;
        case XK_Home:           return SAPP_KEYCODE_HOME;
        case XK_End:            return SAPP_KEYCODE_END;
        case XK_Page_Up:        return SAPP_KEYCODE_PAGE_UP;
        case XK_Page_Down:      return SAPP_KEYCODE_PAGE_DOWN;
        case XK_Insert:         return SAPP_KEYCODE_INSERT;
        case XK_Left:           return SAPP_KEYCODE_LEFT;
        case XK_Right:          return SAPP_KEYCODE_RIGHT;
        case XK_Down:           return SAPP_KEYCODE_DOWN;
        case XK_Up:             return SAPP_KEYCODE_UP;
        case XK_F1:             return SAPP_KEYCODE_F1;
        case XK_F2:             return SAPP_KEYCODE_F2;
        case XK_F3:             return SAPP_KEYCODE_F3;
        case XK_F4:             return SAPP_KEYCODE_F4;
        case XK_F5:             return SAPP_KEYCODE_F5;
        case XK_F6:             return SAPP_KEYCODE_F6;
        case XK_F7:             return SAPP_KEYCODE_F7;
        case XK_F8:             return SAPP_KEYCODE_F8;
        case XK_F9:             return SAPP_KEYCODE_F9;
        case XK_F10:            return SAPP_KEYCODE_F10;
        case XK_F11:            return SAPP_KEYCODE_F11;
        case XK_F12:            return SAPP_KEYCODE_F12;
        case XK_F13:            return SAPP_KEYCODE_F13;
        case XK_F14:            return SAPP_KEYCODE_F14;
        case XK_F15:            return SAPP_KEYCODE_F15;
        case XK_F16:            return SAPP_KEYCODE_F16;
        case XK_F17:            return SAPP_KEYCODE_F17;
        case XK_F18:            return SAPP_KEYCODE_F18;
        case XK_F19:            return SAPP_KEYCODE_F19;
        case XK_F20:            return SAPP_KEYCODE_F20;
        case XK_F21:            return SAPP_KEYCODE_F21;
        case XK_F22:            return SAPP_KEYCODE_F22;
        case XK_F23:            return SAPP_KEYCODE_F23;
        case XK_F24:            return SAPP_KEYCODE_F24;
        case XK_F25:            return SAPP_KEYCODE_F25;

        // numeric keypad
        case XK_KP_Divide:      return SAPP_KEYCODE_KP_DIVIDE;
        case XK_KP_Multiply:    return SAPP_KEYCODE_KP_MULTIPLY;
        case XK_KP_Subtract:    return SAPP_KEYCODE_KP_SUBTRACT;
        case XK_KP_Add:         return SAPP_KEYCODE_KP_ADD;

        // these should have been detected in secondary keysym test above!
        case XK_KP_Insert:      return SAPP_KEYCODE_KP_0;
        case XK_KP_End:         return SAPP_KEYCODE_KP_1;
        case XK_KP_Down:        return SAPP_KEYCODE_KP_2;
        case XK_KP_Page_Down:   return SAPP_KEYCODE_KP_3;
        case XK_KP_Left:        return SAPP_KEYCODE_KP_4;
        case XK_KP_Right:       return SAPP_KEYCODE_KP_6;
        case XK_KP_Home:        return SAPP_KEYCODE_KP_7;
        case XK_KP_Up:          return SAPP_KEYCODE_KP_8;
        case XK_KP_Page_Up:     return SAPP_KEYCODE_KP_9;
        case XK_KP_Delete:      return SAPP_KEYCODE_KP_DECIMAL;
        case XK_KP_Equal:       return SAPP_KEYCODE_KP_EQUAL;
        case XK_KP_Enter:       return SAPP_KEYCODE_KP_ENTER;

        // last resort: Check for printable keys (should not happen if the XKB
        // extension is available). This will give a layout dependent mapping
        // (which is wrong, and we may miss some keys, especially on non-US
        // keyboards), but it's better than nothing...
        case XK_a:              return SAPP_KEYCODE_A;
        case XK_b:              return SAPP_KEYCODE_B;
        case XK_c:              return SAPP_KEYCODE_C;
        case XK_d:              return SAPP_KEYCODE_D;
        case XK_e:              return SAPP_KEYCODE_E;
        case XK_f:              return SAPP_KEYCODE_F;
        case XK_g:              return SAPP_KEYCODE_G;
        case XK_h:              return SAPP_KEYCODE_H;
        case XK_i:              return SAPP_KEYCODE_I;
        case XK_j:              return SAPP_KEYCODE_J;
        case XK_k:              return SAPP_KEYCODE_K;
        case XK_l:              return SAPP_KEYCODE_L;
        case XK_m:              return SAPP_KEYCODE_M;
        case XK_n:              return SAPP_KEYCODE_N;
        case XK_o:              return SAPP_KEYCODE_O;
        case XK_p:              return SAPP_KEYCODE_P;
        case XK_q:              return SAPP_KEYCODE_Q;
        case XK_r:              return SAPP_KEYCODE_R;
        case XK_s:              return SAPP_KEYCODE_S;
        case XK_t:              return SAPP_KEYCODE_T;
        case XK_u:              return SAPP_KEYCODE_U;
        case XK_v:              return SAPP_KEYCODE_V;
        case XK_w:              return SAPP_KEYCODE_W;
        case XK_x:              return SAPP_KEYCODE_X;
        case XK_y:              return SAPP_KEYCODE_Y;
        case XK_z:              return SAPP_KEYCODE_Z;
        case XK_1:              return SAPP_KEYCODE_1;
        case XK_2:              return SAPP_KEYCODE_2;
        case XK_3:              return SAPP_KEYCODE_3;
        case XK_4:              return SAPP_KEYCODE_4;
        case XK_5:              return SAPP_KEYCODE_5;
        case XK_6:              return SAPP_KEYCODE_6;
        case XK_7:              return SAPP_KEYCODE_7;
        case XK_8:              return SAPP_KEYCODE_8;
        case XK_9:              return SAPP_KEYCODE_9;
        case XK_0:              return SAPP_KEYCODE_0;
        case XK_space:          return SAPP_KEYCODE_SPACE;
        case XK_minus:          return SAPP_KEYCODE_MINUS;
        case XK_equal:          return SAPP_KEYCODE_EQUAL;
        case XK_bracketleft:    return SAPP_KEYCODE_LEFT_BRACKET;
        case XK_bracketright:   return SAPP_KEYCODE_RIGHT_BRACKET;
        case XK_backslash:      return SAPP_KEYCODE_BACKSLASH;
        case XK_semicolon:      return SAPP_KEYCODE_SEMICOLON;
        case XK_apostrophe:     return SAPP_KEYCODE_APOSTROPHE;
        case XK_grave:          return SAPP_KEYCODE_GRAVE_ACCENT;
        case XK_comma:          return SAPP_KEYCODE_COMMA;
        case XK_period:         return SAPP_KEYCODE_PERIOD;
        case XK_slash:          return SAPP_KEYCODE_SLASH;
        case XK_less:           return SAPP_KEYCODE_WORLD_1; // At least in some layouts...
        default:                break;
    }

    // no matching translation was found
    return SAPP_KEYCODE_INVALID;
}
static void _sapp_tc_x11_init_keytable(void) {
    for (int i = 0; i < SAPP_MAX_KEYCODES; i++) {
        _sapp_tc.keycodes[i] = SAPP_KEYCODE_INVALID;
    }
    // use XKB to determine physical key locations independently of the current keyboard layout
    XkbDescPtr desc = XkbGetMap(_sapp_tc.display, 0, XkbUseCoreKbd);
    SOKOL_ASSERT(desc);
    XkbGetNames(_sapp_tc.display, XkbKeyNamesMask | XkbKeyAliasesMask, desc);

    const int scancode_min = desc->min_key_code;
    const int scancode_max = desc->max_key_code;

    const struct { sapp_keycode key; const char* name; } keymap[] = {
        { SAPP_KEYCODE_GRAVE_ACCENT, "TLDE" },
        { SAPP_KEYCODE_1, "AE01" },
        { SAPP_KEYCODE_2, "AE02" },
        { SAPP_KEYCODE_3, "AE03" },
        { SAPP_KEYCODE_4, "AE04" },
        { SAPP_KEYCODE_5, "AE05" },
        { SAPP_KEYCODE_6, "AE06" },
        { SAPP_KEYCODE_7, "AE07" },
        { SAPP_KEYCODE_8, "AE08" },
        { SAPP_KEYCODE_9, "AE09" },
        { SAPP_KEYCODE_0, "AE10" },
        { SAPP_KEYCODE_MINUS, "AE11" },
        { SAPP_KEYCODE_EQUAL, "AE12" },
        { SAPP_KEYCODE_Q, "AD01" },
        { SAPP_KEYCODE_W, "AD02" },
        { SAPP_KEYCODE_E, "AD03" },
        { SAPP_KEYCODE_R, "AD04" },
        { SAPP_KEYCODE_T, "AD05" },
        { SAPP_KEYCODE_Y, "AD06" },
        { SAPP_KEYCODE_U, "AD07" },
        { SAPP_KEYCODE_I, "AD08" },
        { SAPP_KEYCODE_O, "AD09" },
        { SAPP_KEYCODE_P, "AD10" },
        { SAPP_KEYCODE_LEFT_BRACKET, "AD11" },
        { SAPP_KEYCODE_RIGHT_BRACKET, "AD12" },
        { SAPP_KEYCODE_A, "AC01" },
        { SAPP_KEYCODE_S, "AC02" },
        { SAPP_KEYCODE_D, "AC03" },
        { SAPP_KEYCODE_F, "AC04" },
        { SAPP_KEYCODE_G, "AC05" },
        { SAPP_KEYCODE_H, "AC06" },
        { SAPP_KEYCODE_J, "AC07" },
        { SAPP_KEYCODE_K, "AC08" },
        { SAPP_KEYCODE_L, "AC09" },
        { SAPP_KEYCODE_SEMICOLON, "AC10" },
        { SAPP_KEYCODE_APOSTROPHE, "AC11" },
        { SAPP_KEYCODE_Z, "AB01" },
        { SAPP_KEYCODE_X, "AB02" },
        { SAPP_KEYCODE_C, "AB03" },
        { SAPP_KEYCODE_V, "AB04" },
        { SAPP_KEYCODE_B, "AB05" },
        { SAPP_KEYCODE_N, "AB06" },
        { SAPP_KEYCODE_M, "AB07" },
        { SAPP_KEYCODE_COMMA, "AB08" },
        { SAPP_KEYCODE_PERIOD, "AB09" },
        { SAPP_KEYCODE_SLASH, "AB10" },
        { SAPP_KEYCODE_BACKSLASH, "BKSL" },
        { SAPP_KEYCODE_WORLD_1, "LSGT" },
        { SAPP_KEYCODE_SPACE, "SPCE" },
        { SAPP_KEYCODE_ESCAPE, "ESC" },
        { SAPP_KEYCODE_ENTER, "RTRN" },
        { SAPP_KEYCODE_TAB, "TAB" },
        { SAPP_KEYCODE_BACKSPACE, "BKSP" },
        { SAPP_KEYCODE_INSERT, "INS" },
        { SAPP_KEYCODE_DELETE, "DELE" },
        { SAPP_KEYCODE_RIGHT, "RGHT" },
        { SAPP_KEYCODE_LEFT, "LEFT" },
        { SAPP_KEYCODE_DOWN, "DOWN" },
        { SAPP_KEYCODE_UP, "UP" },
        { SAPP_KEYCODE_PAGE_UP, "PGUP" },
        { SAPP_KEYCODE_PAGE_DOWN, "PGDN" },
        { SAPP_KEYCODE_HOME, "HOME" },
        { SAPP_KEYCODE_END, "END" },
        { SAPP_KEYCODE_CAPS_LOCK, "CAPS" },
        { SAPP_KEYCODE_SCROLL_LOCK, "SCLK" },
        { SAPP_KEYCODE_NUM_LOCK, "NMLK" },
        { SAPP_KEYCODE_PRINT_SCREEN, "PRSC" },
        { SAPP_KEYCODE_PAUSE, "PAUS" },
        { SAPP_KEYCODE_F1, "FK01" },
        { SAPP_KEYCODE_F2, "FK02" },
        { SAPP_KEYCODE_F3, "FK03" },
        { SAPP_KEYCODE_F4, "FK04" },
        { SAPP_KEYCODE_F5, "FK05" },
        { SAPP_KEYCODE_F6, "FK06" },
        { SAPP_KEYCODE_F7, "FK07" },
        { SAPP_KEYCODE_F8, "FK08" },
        { SAPP_KEYCODE_F9, "FK09" },
        { SAPP_KEYCODE_F10, "FK10" },
        { SAPP_KEYCODE_F11, "FK11" },
        { SAPP_KEYCODE_F12, "FK12" },
        { SAPP_KEYCODE_F13, "FK13" },
        { SAPP_KEYCODE_F14, "FK14" },
        { SAPP_KEYCODE_F15, "FK15" },
        { SAPP_KEYCODE_F16, "FK16" },
        { SAPP_KEYCODE_F17, "FK17" },
        { SAPP_KEYCODE_F18, "FK18" },
        { SAPP_KEYCODE_F19, "FK19" },
        { SAPP_KEYCODE_F20, "FK20" },
        { SAPP_KEYCODE_F21, "FK21" },
        { SAPP_KEYCODE_F22, "FK22" },
        { SAPP_KEYCODE_F23, "FK23" },
        { SAPP_KEYCODE_F24, "FK24" },
        { SAPP_KEYCODE_F25, "FK25" },
        { SAPP_KEYCODE_KP_0, "KP0" },
        { SAPP_KEYCODE_KP_1, "KP1" },
        { SAPP_KEYCODE_KP_2, "KP2" },
        { SAPP_KEYCODE_KP_3, "KP3" },
        { SAPP_KEYCODE_KP_4, "KP4" },
        { SAPP_KEYCODE_KP_5, "KP5" },
        { SAPP_KEYCODE_KP_6, "KP6" },
        { SAPP_KEYCODE_KP_7, "KP7" },
        { SAPP_KEYCODE_KP_8, "KP8" },
        { SAPP_KEYCODE_KP_9, "KP9" },
        { SAPP_KEYCODE_KP_DECIMAL, "KPDL" },
        { SAPP_KEYCODE_KP_DIVIDE, "KPDV" },
        { SAPP_KEYCODE_KP_MULTIPLY, "KPMU" },
        { SAPP_KEYCODE_KP_SUBTRACT, "KPSU" },
        { SAPP_KEYCODE_KP_ADD, "KPAD" },
        { SAPP_KEYCODE_KP_ENTER, "KPEN" },
        { SAPP_KEYCODE_KP_EQUAL, "KPEQ" },
        { SAPP_KEYCODE_LEFT_SHIFT, "LFSH" },
        { SAPP_KEYCODE_LEFT_CONTROL, "LCTL" },
        { SAPP_KEYCODE_LEFT_ALT, "LALT" },
        { SAPP_KEYCODE_LEFT_SUPER, "LWIN" },
        { SAPP_KEYCODE_RIGHT_SHIFT, "RTSH" },
        { SAPP_KEYCODE_RIGHT_CONTROL, "RCTL" },
        { SAPP_KEYCODE_RIGHT_ALT, "RALT" },
        { SAPP_KEYCODE_RIGHT_ALT, "LVL3" },
        { SAPP_KEYCODE_RIGHT_ALT, "MDSW" },
        { SAPP_KEYCODE_RIGHT_SUPER, "RWIN" },
        { SAPP_KEYCODE_MENU, "MENU" }
    };
    const int num_keymap_items = (int)(sizeof(keymap) / sizeof(keymap[0]));

    // find X11 keycode to sokol-app key code mapping
    for (int scancode = scancode_min; scancode <= scancode_max; scancode++) {
        sapp_keycode key = SAPP_KEYCODE_INVALID;
        for (int i = 0; i < num_keymap_items; i++) {
            if (strncmp(desc->names->keys[scancode].name, keymap[i].name, XkbKeyNameLength) == 0) {
                key = keymap[i].key;
                break;
            }
        }

        // fall back to key aliases in case the key name did not match
        for (int i = 0; i < desc->names->num_key_aliases; i++) {
            if (key != SAPP_KEYCODE_INVALID) {
                break;
            }
            if (strncmp(desc->names->key_aliases[i].real, desc->names->keys[scancode].name, XkbKeyNameLength) != 0) {
                continue;
            }
            for (int j = 0; j < num_keymap_items; j++) {
                if (strncmp(desc->names->key_aliases[i].alias, keymap[j].name, XkbKeyNameLength) == 0) {
                    key = keymap[j].key;
                    break;
                }
            }
        }
        _sapp_tc.keycodes[scancode] = key;
    }
    XkbFreeNames(desc, XkbKeyNamesMask, True);
    XkbFreeKeyboard(desc, 0, True);

    int width = 0;
    KeySym* keysyms = XGetKeyboardMapping(_sapp_tc.display, scancode_min, scancode_max - scancode_min + 1, &width);
    for (int scancode = scancode_min; scancode <= scancode_max; scancode++) {
        // translate untranslated key codes using the traditional X11 KeySym lookups
        if (_sapp_tc.keycodes[scancode] == SAPP_KEYCODE_INVALID) {
            const size_t base = (size_t)((scancode - scancode_min) * width);
            _sapp_tc.keycodes[scancode] = _sapp_tc_x11_translate_keysyms(&keysyms[base], width);
        }
    }
    XFree(keysyms);
}
static int32_t _sapp_tc_x11_keysym_to_unicode(KeySym keysym) {
    int min = 0;
    int max = sizeof(_sapp_tc_x11_keysymtab) / sizeof(struct _sapp_tc_x11_codepair) - 1;
    int mid;

    /* First check for Latin-1 characters (1:1 mapping) */
    if ((keysym >= 0x0020 && keysym <= 0x007e) ||
        (keysym >= 0x00a0 && keysym <= 0x00ff))
    {
        return keysym;
    }

    /* Also check for directly encoded 24-bit UCS characters */
    if ((keysym & 0xff000000) == 0x01000000) {
        return keysym & 0x00ffffff;
    }

    /* Binary search in table */
    while (max >= min) {
        mid = (min + max) / 2;
        if (_sapp_tc_x11_keysymtab[mid].keysym < keysym) {
            min = mid + 1;
        } else if (_sapp_tc_x11_keysymtab[mid].keysym > keysym) {
            max = mid - 1;
        } else {
            return _sapp_tc_x11_keysymtab[mid].ucs;
        }
    }

    /* No matching Unicode value found */
    return -1;
}

/*-- keycode helpers (per-window software key repeat) ------------------------*/
static sapp_keycode _sapp_tc_translate_key(int scancode) {
    if ((scancode >= 0) && (scancode < _SAPP_X11_MAX_X11_KEYCODES)) {
        return _sapp_tc.keycodes[scancode];
    }
    return SAPP_KEYCODE_INVALID;
}

static bool _sapp_tc_x11_keypress_repeat(_sapp_tc_window_t* w, int keycode) {
    /* XkbSetDetectableAutoRepeat is enabled at startup, so a held key emits
       repeated KeyPress without paired KeyRelease; the first press reports
       repeat=false, subsequent ones true until release */
    bool repeat = false;
    if ((keycode >= 0) && (keycode < _SAPP_X11_MAX_X11_KEYCODES)) {
        repeat = w->key_repeat[keycode];
        w->key_repeat[keycode] = true;
    }
    return repeat;
}

static void _sapp_tc_x11_keyrelease_repeat(_sapp_tc_window_t* w, int keycode) {
    if ((keycode >= 0) && (keycode < _SAPP_X11_MAX_X11_KEYCODES)) {
        w->key_repeat[keycode] = false;
    }
}

/*-- X error handling (controlled failure instead of an Xlib abort) ----------*/
static int _sapp_tc_x11_error_handler(Display* display, XErrorEvent* event) {
    (void)display;
    _sapp_tc.error_code = event->error_code;
    return 0;
}

static void _sapp_tc_x11_grab_error_handler(void) {
    _sapp_tc.error_code = 0;
    _sapp_tc.prev_error_handler = XSetErrorHandler(_sapp_tc_x11_error_handler);
}

static void _sapp_tc_x11_release_error_handler(void) {
    XSync(_sapp_tc.display, False);
    XSetErrorHandler(_sapp_tc.prev_error_handler);
    _sapp_tc.prev_error_handler = 0;
}

/*-- window properties -------------------------------------------------------*/
static unsigned long _sapp_tc_x11_get_window_property(Window xwin, Atom property, Atom type, unsigned char** value) {
    Atom actual_type;
    int actual_format;
    unsigned long item_count, bytes_after;
    XGetWindowProperty(_sapp_tc.display, xwin, property,
                       0, LONG_MAX, False, type,
                       &actual_type, &actual_format, &item_count, &bytes_after, value);
    return item_count;
}

static int _sapp_tc_x11_get_window_state(_sapp_tc_window_t* w) {
    int result = WithdrawnState;
    struct {
        CARD32 state;
        Window icon;
    } *state = NULL;
    if (_sapp_tc_x11_get_window_property(w->xwin, _sapp_tc.WM_STATE, _sapp_tc.WM_STATE, (unsigned char**)&state) >= 2) {
        result = (int)state->state;
    }
    if (state) {
        XFree(state);
    }
    return result;
}

static void _sapp_tc_x11_send_event(Window xwin, Atom type, long a, long b, long c, long d, long e) {
    XEvent event;
    memset(&event, 0, sizeof(event));
    event.type = ClientMessage;
    event.xclient.window = xwin;
    event.xclient.format = 32;
    event.xclient.message_type = type;
    event.xclient.data.l[0] = a;
    event.xclient.data.l[1] = b;
    event.xclient.data.l[2] = c;
    event.xclient.data.l[3] = d;
    event.xclient.data.l[4] = e;
    XSendEvent(_sapp_tc.display, _sapp_tc.root, False,
               SubstructureNotifyMask | SubstructureRedirectMask, &event);
}

/*-- atoms / extensions -------------------------------------------------------*/
static void _sapp_tc_x11_init_extensions(void) {
    Display* d = _sapp_tc.display;
    _sapp_tc.UTF8_STRING             = XInternAtom(d, "UTF8_STRING", False);
    _sapp_tc.CLIPBOARD               = XInternAtom(d, "CLIPBOARD", False);
    _sapp_tc.TARGETS                 = XInternAtom(d, "TARGETS", False);
    _sapp_tc.WM_PROTOCOLS            = XInternAtom(d, "WM_PROTOCOLS", False);
    _sapp_tc.WM_DELETE_WINDOW        = XInternAtom(d, "WM_DELETE_WINDOW", False);
    _sapp_tc.WM_STATE                = XInternAtom(d, "WM_STATE", False);
    _sapp_tc.NET_WM_NAME             = XInternAtom(d, "_NET_WM_NAME", False);
    _sapp_tc.NET_WM_ICON_NAME        = XInternAtom(d, "_NET_WM_ICON_NAME", False);
    _sapp_tc.NET_WM_STATE            = XInternAtom(d, "_NET_WM_STATE", False);
    _sapp_tc.NET_WM_STATE_FULLSCREEN = XInternAtom(d, "_NET_WM_STATE_FULLSCREEN", False);
    _sapp_tc.MOTIF_WM_HINTS          = XInternAtom(d, "_MOTIF_WM_HINTS", False);
    _sapp_tc.SAPP_TC_SELECTION       = XInternAtom(d, "SAPP_TC_SELECTION", False);
    if (_sapp_tc.app.drop_enabled) {
        _sapp_tc.xdnd.XdndAware      = XInternAtom(d, "XdndAware", False);
        _sapp_tc.xdnd.XdndEnter      = XInternAtom(d, "XdndEnter", False);
        _sapp_tc.xdnd.XdndPosition   = XInternAtom(d, "XdndPosition", False);
        _sapp_tc.xdnd.XdndStatus     = XInternAtom(d, "XdndStatus", False);
        _sapp_tc.xdnd.XdndActionCopy = XInternAtom(d, "XdndActionCopy", False);
        _sapp_tc.xdnd.XdndDrop       = XInternAtom(d, "XdndDrop", False);
        _sapp_tc.xdnd.XdndFinished   = XInternAtom(d, "XdndFinished", False);
        _sapp_tc.xdnd.XdndSelection  = XInternAtom(d, "XdndSelection", False);
        _sapp_tc.xdnd.XdndTypeList   = XInternAtom(d, "XdndTypeList", False);
        _sapp_tc.xdnd.text_uri_list  = XInternAtom(d, "text/uri-list", False);
    }
}

/*-- DPI (Xft.dpi resource, Qt/GTK-compatible; 96 fallback) -------------------*/
static void _sapp_tc_x11_query_system_dpi(void) {
    _sapp_tc.dpi = 96.0f;
    const char* rms = XResourceManagerString(_sapp_tc.display);
    if (rms) {
        XrmDatabase db = XrmGetStringDatabase(rms);
        if (db) {
            XrmValue value;
            char* type = NULL;
            if (XrmGetResource(db, "Xft.dpi", "Xft.Dpi", &type, &value)) {
                if (type && (0 == strcmp(type, "String")) && value.addr) {
                    _sapp_tc.dpi = (float)atof(value.addr);
                    if (_sapp_tc.dpi <= 0.0f) {
                        _sapp_tc.dpi = 96.0f;
                    }
                }
            }
            XrmDestroyDatabase(db);
        }
    }
}

/*-- mouse cursors (Xcursor themed, X font cursor fallback) -------------------*/
static void _sapp_tc_x11_create_hidden_cursor(void) {
    XcursorImage* img = XcursorImageCreate(16, 16);
    if (!img) return;
    img->xhot = 0;
    img->yhot = 0;
    memset(img->pixels, 0, (size_t)(16 * 16) * sizeof(XcursorPixel));
    _sapp_tc.hidden_cursor = XcursorImageLoadCursor(_sapp_tc.display, img);
    XcursorImageDestroy(img);
}

static void _sapp_tc_x11_create_standard_cursor(sapp_mouse_cursor cursor, const char* name, const char* theme, int size, uint32_t fallback_native) {
    if (theme) {
        XcursorImage* img = XcursorLibraryLoadImage(name, theme, size);
        if (img) {
            _sapp_tc.standard_cursors[cursor] = XcursorImageLoadCursor(_sapp_tc.display, img);
            XcursorImageDestroy(img);
        }
    }
    if ((0 == _sapp_tc.standard_cursors[cursor]) && (0 != fallback_native)) {
        _sapp_tc.standard_cursors[cursor] = XCreateFontCursor(_sapp_tc.display, fallback_native);
    }
}

static void _sapp_tc_x11_init_cursors(void) {
    const char* theme = XcursorGetTheme(_sapp_tc.display);
    const int size = XcursorGetDefaultSize(_sapp_tc.display);
    _sapp_tc_x11_create_standard_cursor(SAPP_MOUSECURSOR_DEFAULT, "default", theme, size, XC_left_ptr);
    _sapp_tc_x11_create_standard_cursor(SAPP_MOUSECURSOR_ARROW, "default", theme, size, XC_left_ptr);
    _sapp_tc_x11_create_standard_cursor(SAPP_MOUSECURSOR_IBEAM, "text", theme, size, XC_xterm);
    _sapp_tc_x11_create_standard_cursor(SAPP_MOUSECURSOR_CROSSHAIR, "crosshair", theme, size, XC_crosshair);
    _sapp_tc_x11_create_standard_cursor(SAPP_MOUSECURSOR_POINTING_HAND, "pointer", theme, size, XC_hand2);
    _sapp_tc_x11_create_standard_cursor(SAPP_MOUSECURSOR_RESIZE_EW, "ew-resize", theme, size, XC_sb_h_double_arrow);
    _sapp_tc_x11_create_standard_cursor(SAPP_MOUSECURSOR_RESIZE_NS, "ns-resize", theme, size, XC_sb_v_double_arrow);
    _sapp_tc_x11_create_standard_cursor(SAPP_MOUSECURSOR_RESIZE_NWSE, "nwse-resize", theme, size, 0);
    _sapp_tc_x11_create_standard_cursor(SAPP_MOUSECURSOR_RESIZE_NESW, "nesw-resize", theme, size, 0);
    _sapp_tc_x11_create_standard_cursor(SAPP_MOUSECURSOR_RESIZE_ALL, "all-scroll", theme, size, XC_fleur);
    _sapp_tc_x11_create_standard_cursor(SAPP_MOUSECURSOR_NOT_ALLOWED, "not-allowed", theme, size, 0);
    _sapp_tc_x11_create_hidden_cursor();
}

static void _sapp_tc_x11_destroy_cursors(void) {
    if (_sapp_tc.hidden_cursor) {
        XFreeCursor(_sapp_tc.display, _sapp_tc.hidden_cursor);
        _sapp_tc.hidden_cursor = 0;
    }
    for (int i = 0; i < _SAPP_MOUSECURSOR_NUM; i++) {
        if (_sapp_tc.standard_cursors[i]) {
            XFreeCursor(_sapp_tc.display, _sapp_tc.standard_cursors[i]);
            _sapp_tc.standard_cursors[i] = 0;
        }
        if (_sapp_tc.custom_cursors[i]) {
            XFreeCursor(_sapp_tc.display, _sapp_tc.custom_cursors[i]);
            _sapp_tc.custom_cursors[i] = 0;
            _sapp_tc.app.custom_cursor_bound[i] = false;
        }
    }
}

static void _sapp_tc_x11_apply_cursor(void) {
    /* app-global cursor (same model as sokol_app.h), applied to every window */
    const sapp_mouse_cursor cur = _sapp_tc.app.current_cursor;
    Cursor c = 0;
    if (_sapp_tc.app.mouse_shown) {
        c = _sapp_tc.app.custom_cursor_bound[cur] ? _sapp_tc.custom_cursors[cur]
                                                  : _sapp_tc.standard_cursors[cur];
    } else {
        c = _sapp_tc.hidden_cursor;
    }
    for (int i = 0; i < _SAPP_TC_MAX_WINDOWS; i++) {
        _sapp_tc_window_t* w = _sapp_tc.windows[i];
        if (!w || !w->xwin) continue;
        if (c) {
            XDefineCursor(_sapp_tc.display, w->xwin, c);
        } else {
            XUndefineCursor(_sapp_tc.display, w->xwin);
        }
    }
    XFlush(_sapp_tc.display);
}

/*-- title / decorations ------------------------------------------------------*/
static void _sapp_tc_x11_update_window_title(_sapp_tc_window_t* w, const char* title) {
    Xutf8SetWMProperties(_sapp_tc.display, w->xwin, title, title, NULL, 0, NULL, NULL, NULL);
    XChangeProperty(_sapp_tc.display, w->xwin, _sapp_tc.NET_WM_NAME, _sapp_tc.UTF8_STRING,
                    8, PropModeReplace, (unsigned char*)title, (int)strlen(title));
    XChangeProperty(_sapp_tc.display, w->xwin, _sapp_tc.NET_WM_ICON_NAME, _sapp_tc.UTF8_STRING,
                    8, PropModeReplace, (unsigned char*)title, (int)strlen(title));
    XFlush(_sapp_tc.display);
}

static void _sapp_tc_x11_set_decorated(_sapp_tc_window_t* w, bool decorated) {
    struct {
        unsigned long flags;
        unsigned long functions;
        unsigned long decorations;
        long          input_mode;
        unsigned long status;
    } hints;
    memset(&hints, 0, sizeof(hints));
    hints.flags = (1L << 1);                    /* MWM_HINTS_DECORATIONS */
    hints.decorations = decorated ? 1 : 0;
    XChangeProperty(_sapp_tc.display, w->xwin, _sapp_tc.MOTIF_WM_HINTS, _sapp_tc.MOTIF_WM_HINTS,
                    32, PropModeReplace, (unsigned char*)&hints, 5);
}

/*-- dimensions (polled per tick; ConfigureNotify is deliberately unused) -----*/
static bool _sapp_tc_x11_update_dimensions(_sapp_tc_window_t* w) {
    XWindowAttributes attribs;
    memset(&attribs, 0, sizeof(attribs));
    XGetWindowAttributes(_sapp_tc.display, w->xwin, &attribs);
    if ((attribs.width <= 0) || (attribs.height <= 0)) {
        return false;
    }
    /* X11 has no separate backing scale: framebuffer == window pixels */
    const bool changed = (attribs.width != w->fb_width) || (attribs.height != w->fb_height);
    w->fb_width = attribs.width;
    w->fb_height = attribs.height;
    const float scale = (w->dpi_scale > 0.0f) ? w->dpi_scale : 1.0f;
    w->win_width = (int)((float)attribs.width / scale + 0.5f);
    w->win_height = (int)((float)attribs.height / scale + 0.5f);
    return changed;
}

/*-- fullscreen (EWMH _NET_WM_STATE; must be called after XMapWindow) ---------*/
static void _sapp_tc_x11_set_fullscreen(bool enable) {
    _sapp_tc_window_t* w = _sapp_tc.app.main;
    if (!w) return;
    if (_sapp_tc.NET_WM_STATE && _sapp_tc.NET_WM_STATE_FULLSCREEN) {
        const long op = enable ? 1 : 0;     /* _NET_WM_STATE_ADD : _NET_WM_STATE_REMOVE */
        _sapp_tc_x11_send_event(w->xwin, _sapp_tc.NET_WM_STATE,
                                op, (long)_sapp_tc.NET_WM_STATE_FULLSCREEN, 0, 1, 0);
    }
    _sapp_tc.app.fullscreen = enable;
    XFlush(_sapp_tc.display);
}

/*-- synchronous event wait (poll on the connection fd with a deadline) ------*/
static bool _sapp_tc_x11_wait_for_event(Window xwin, int type, double timeout_sec, XEvent* out_event) {
    const double deadline = _sapp_tc_now() + timeout_sec;
    for (;;) {
        if (XCheckTypedWindowEvent(_sapp_tc.display, xwin, type, out_event)) {
            return true;
        }
        const double remaining = deadline - _sapp_tc_now();
        if (remaining <= 0.0) {
            return false;
        }
        struct pollfd pfd;
        pfd.fd = ConnectionNumber(_sapp_tc.display);
        pfd.events = POLLIN;
        pfd.revents = 0;
        poll(&pfd, 1, (int)(remaining * 1000.0) + 1);
    }
}

/*-- clipboard (CLIPBOARD selection, UTF8_STRING only, no INCR) ---------------*/
static void _sapp_tc_x11_serve_selection_request(XEvent* event) {
    XSelectionRequestEvent* req = &event->xselectionrequest;
    if (req->selection != _sapp_tc.CLIPBOARD) return;
    if (!_sapp_tc.app.clipboard_enabled || !_sapp_tc.app.clipboard) return;
    XSelectionEvent reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = SelectionNotify;
    reply.display = req->display;
    reply.requestor = req->requestor;
    reply.selection = req->selection;
    reply.target = req->target;
    reply.property = req->property;
    reply.time = req->time;
    if (req->target == _sapp_tc.UTF8_STRING) {
        XChangeProperty(_sapp_tc.display, req->requestor, req->property,
                        _sapp_tc.UTF8_STRING, 8, PropModeReplace,
                        (unsigned char*)_sapp_tc.app.clipboard,
                        (int)strlen(_sapp_tc.app.clipboard));
    } else if (req->target == _sapp_tc.TARGETS) {
        XChangeProperty(_sapp_tc.display, req->requestor, req->property,
                        XA_ATOM, 32, PropModeReplace,
                        (unsigned char*)&_sapp_tc.UTF8_STRING, 1);
    } else {
        reply.property = None;
    }
    XSendEvent(_sapp_tc.display, req->requestor, False, 0, (XEvent*)&reply);
    XFlush(_sapp_tc.display);
}

/*-- drag & drop (XDND v5, text/uri-list) -------------------------------------*/
static bool _sapp_tc_x11_parse_dropped_files_list(const char* src) {
    SOKOL_ASSERT(src);
    SOKOL_ASSERT(_sapp_tc.app.drop_buffer);

    memset(_sapp_tc.app.drop_buffer, 0, (size_t)_sapp_tc.app.drop_max_files * (size_t)_sapp_tc.app.drop_max_path_length);
    _sapp_tc.app.drop_num_files = 0;

    /*
        src is (potentially percent-encoded) string made of one or multiple paths
        separated by \r\n, each path starting with 'file://'
    */
    bool err = false;
    int src_count = 0;
    char src_chr = 0;
    char* dst_ptr = _sapp_tc.app.drop_buffer;
    const char* dst_end_ptr = dst_ptr + (_sapp_tc.app.drop_max_path_length - 1); // room for terminating 0
    while (0 != (src_chr = *src++)) {
        src_count++;
        char dst_chr = 0;
        /* check leading 'file://' */
        if (src_count <= 7) {
            if (((src_count == 1) && (src_chr != 'f')) ||
                ((src_count == 2) && (src_chr != 'i')) ||
                ((src_count == 3) && (src_chr != 'l')) ||
                ((src_count == 4) && (src_chr != 'e')) ||
                ((src_count == 5) && (src_chr != ':')) ||
                ((src_count == 6) && (src_chr != '/')) ||
                ((src_count == 7) && (src_chr != '/')))
            {
                /* non-file: URI in drop list: ignore entry */
                err = true;
                break;
            }
        } else if (src_chr == '\r') {
            // skip
        } else if (src_chr == '\n') {
            src_count = 0;
            _sapp_tc.app.drop_num_files++;
            // too many files is not an error
            if (_sapp_tc.app.drop_num_files >= _sapp_tc.app.drop_max_files) {
                break;
            }
            dst_ptr = _sapp_tc.app.drop_buffer + _sapp_tc.app.drop_num_files * _sapp_tc.app.drop_max_path_length;
            dst_end_ptr = dst_ptr + (_sapp_tc.app.drop_max_path_length - 1);
        } else if ((src_chr == '%') && src[0] && src[1]) {
            // a percent-encoded byte (most likely UTF-8 multibyte sequence)
            const char digits[3] = { src[0], src[1], 0 };
            src += 2;
            dst_chr = (char) strtol(digits, 0, 16);
        } else {
            dst_chr = src_chr;
        }
        if (dst_chr) {
            // dst_end_ptr already has adjustment for terminating zero
            if (dst_ptr < dst_end_ptr) {
                *dst_ptr++ = dst_chr;
            } else {
                /* dropped file path too long: abort parse */
                err = true;
                break;
            }
        }
    }
    if (err) {
        memset(_sapp_tc.app.drop_buffer, 0, (size_t)_sapp_tc.app.drop_max_files * (size_t)_sapp_tc.app.drop_max_path_length);
        _sapp_tc.app.drop_num_files = 0;
        return false;
    } else {
        return true;
    }
}

/*-- per-window event senders --------------------------------------------------*/
static uint32_t _sapp_tc_x11_mods(uint32_t x11_state) {
    uint32_t m = 0;
    if (x11_state & ShiftMask)   m |= SAPP_MODIFIER_SHIFT;
    if (x11_state & ControlMask) m |= SAPP_MODIFIER_CTRL;
    if (x11_state & Mod1Mask)    m |= SAPP_MODIFIER_ALT;
    if (x11_state & Mod4Mask)    m |= SAPP_MODIFIER_SUPER;
    if (x11_state & Button1Mask) m |= SAPP_MODIFIER_LMB;
    if (x11_state & Button2Mask) m |= SAPP_MODIFIER_MMB;
    if (x11_state & Button3Mask) m |= SAPP_MODIFIER_RMB;
    return m;
}

/* X11 doesn't set a modifier's own bit on the event that presses/releases it;
   emulate (key-down ORs the bit in, key-up masks it out; same for buttons) */
static uint32_t _sapp_tc_x11_key_modifier_bit(sapp_keycode key) {
    switch (key) {
        case SAPP_KEYCODE_LEFT_SHIFT:
        case SAPP_KEYCODE_RIGHT_SHIFT:   return SAPP_MODIFIER_SHIFT;
        case SAPP_KEYCODE_LEFT_CONTROL:
        case SAPP_KEYCODE_RIGHT_CONTROL: return SAPP_MODIFIER_CTRL;
        case SAPP_KEYCODE_LEFT_ALT:
        case SAPP_KEYCODE_RIGHT_ALT:     return SAPP_MODIFIER_ALT;
        case SAPP_KEYCODE_LEFT_SUPER:
        case SAPP_KEYCODE_RIGHT_SUPER:   return SAPP_MODIFIER_SUPER;
        default:                         return 0;
    }
}

static uint32_t _sapp_tc_x11_button_modifier_bit(sapp_mousebutton btn) {
    switch (btn) {
        case SAPP_MOUSEBUTTON_LEFT:   return SAPP_MODIFIER_LMB;
        case SAPP_MOUSEBUTTON_RIGHT:  return SAPP_MODIFIER_RMB;
        case SAPP_MOUSEBUTTON_MIDDLE: return SAPP_MODIFIER_MMB;
        default:                      return 0;
    }
}

static sapp_mousebutton _sapp_tc_x11_translate_button(unsigned int button) {
    switch (button) {
        case Button1: return SAPP_MOUSEBUTTON_LEFT;
        case Button2: return SAPP_MOUSEBUTTON_MIDDLE;
        case Button3: return SAPP_MOUSEBUTTON_RIGHT;
        default:      return SAPP_MOUSEBUTTON_INVALID;
    }
}

static void _sapp_tc_x11_mouse_update(_sapp_tc_window_t* w, int x, int y, bool clear_dxdy) {
    /* event coordinates are raw window pixels == framebuffer pixels on X11 */
    const float fx = (float)x;
    const float fy = (float)y;
    if (clear_dxdy || !w->mouse_pos_valid) {
        w->mouse_dx = 0.0f;
        w->mouse_dy = 0.0f;
        w->mouse_pos_valid = true;
    } else {
        w->mouse_dx = fx - w->mouse_x;
        w->mouse_dy = fy - w->mouse_y;
    }
    w->mouse_x = fx;
    w->mouse_y = fy;
}

static void _sapp_tc_x11_mouse_event(_sapp_tc_window_t* w, sapp_event_type type, sapp_mousebutton btn, uint32_t mods) {
    sapp_event e;
    memset(&e, 0, sizeof(e));
    e.type = type;
    e.mouse_button = btn;
    e.mouse_x = w->mouse_x;
    e.mouse_y = w->mouse_y;
    e.mouse_dx = w->mouse_dx;
    e.mouse_dy = w->mouse_dy;
    e.modifiers = mods;
    _sapp_tc_send(w, &e);
}

static void _sapp_tc_x11_scroll_event(_sapp_tc_window_t* w, float x, float y, uint32_t mods) {
    sapp_event e;
    memset(&e, 0, sizeof(e));
    e.type = SAPP_EVENTTYPE_MOUSE_SCROLL;
    e.mouse_x = w->mouse_x;
    e.mouse_y = w->mouse_y;
    e.scroll_x = x;
    e.scroll_y = y;
    e.modifiers = mods;
    _sapp_tc_send(w, &e);
}

static void _sapp_tc_x11_key_event(_sapp_tc_window_t* w, sapp_event_type type, sapp_keycode key, bool repeat, uint32_t mods) {
    sapp_event e;
    memset(&e, 0, sizeof(e));
    e.type = type;
    e.key_code = key;
    e.key_repeat = repeat;
    e.modifiers = mods;
    _sapp_tc_send(w, &e);
    /* Ctrl+V paste notification (exact-CTRL match, sokol_app.h parity); the
       app reads the clipboard in its handler */
    if ((type == SAPP_EVENTTYPE_KEY_DOWN) && _sapp_tc.app.clipboard_enabled &&
        (mods == SAPP_MODIFIER_CTRL) && (key == SAPP_KEYCODE_V)) {
        sapp_event pe;
        memset(&pe, 0, sizeof(pe));
        pe.type = SAPP_EVENTTYPE_CLIPBOARD_PASTED;
        pe.modifiers = SAPP_MODIFIER_CTRL;
        _sapp_tc_send(w, &pe);
    }
}

static void _sapp_tc_x11_char_event(_sapp_tc_window_t* w, uint32_t c, bool repeat, uint32_t mods) {
    if ((c < 32) || (c == 127)) return;     /* control characters */
    sapp_event e;
    memset(&e, 0, sizeof(e));
    e.type = SAPP_EVENTTYPE_CHAR;
    e.char_code = c;
    e.key_repeat = repeat;
    e.modifiers = mods;
    _sapp_tc_send(w, &e);
}

static void _sapp_tc_x11_app_event(_sapp_tc_window_t* w, sapp_event_type type) {
    sapp_event e;
    memset(&e, 0, sizeof(e));
    e.type = type;
    _sapp_tc_send(w, &e);
}

/*-- XDND handlers -------------------------------------------------------------*/
static void _sapp_tc_x11_on_selectionnotify(XEvent* event) {
    /* XDND drop data arrival (clipboard replies are consumed synchronously by
       _sapp_tc_x11_wait_for_event inside sapp_get_clipboard_string) */
    if (!_sapp_tc.app.drop_enabled) return;
    if (event->xselection.property != _sapp_tc.xdnd.XdndSelection) return;
    char* data = 0;
    const uint32_t result = (uint32_t)_sapp_tc_x11_get_window_property(
        event->xselection.requestor,
        event->xselection.property,
        event->xselection.target,
        (unsigned char**)&data);
    if (result && data) {
        if (_sapp_tc_x11_parse_dropped_files_list(data)) {
            _sapp_tc_window_t* w = _sapp_tc_lookup_xwin(event->xselection.requestor);
            if (!w) w = _sapp_tc.xdnd.over;
            if (!w) w = _sapp_tc.app.main;
            if (w) {
                /* FIXME (upstream parity): no modifier state available here */
                _sapp_tc_x11_app_event(w, SAPP_EVENTTYPE_FILES_DROPPED);
            }
        }
    }
    if (_sapp_tc.xdnd.version >= 2) {
        XEvent reply;
        memset(&reply, 0, sizeof(reply));
        reply.type = ClientMessage;
        reply.xclient.window = _sapp_tc.xdnd.source;
        reply.xclient.message_type = _sapp_tc.xdnd.XdndFinished;
        reply.xclient.format = 32;
        reply.xclient.data.l[0] = (long)event->xselection.requestor;
        reply.xclient.data.l[1] = (long)result;
        reply.xclient.data.l[2] = (long)_sapp_tc.xdnd.XdndActionCopy;
        XSendEvent(_sapp_tc.display, _sapp_tc.xdnd.source, False, NoEventMask, &reply);
        XFlush(_sapp_tc.display);
    }
    if (data) {
        XFree(data);
    }
}

static void _sapp_tc_x11_on_clientmessage(XEvent* event) {
    if (XFilterEvent(event, None)) {
        return;
    }
    _sapp_tc_window_t* w = _sapp_tc_lookup_xwin(event->xclient.window);
    if (event->xclient.message_type == _sapp_tc.WM_PROTOCOLS) {
        const Atom protocol = (Atom)event->xclient.data.l[0];
        if ((protocol == _sapp_tc.WM_DELETE_WINDOW) && w) {
            if (w->is_main) {
                /* the cancellable quit dance runs at the loop tail (upstream
                   parity): QUIT_REQUESTED is delivered there and a handler
                   may sapp_cancel_quit() */
                _sapp_tc.app.quit_requested = true;
            } else if (w->desc.close_cb) {
                /* user closed a secondary window: notify; the app accepts by
                   calling sapp_destroy_window() (w may be freed after this) */
                sapp_window handle = { w->win_id };
                w->desc.close_cb(handle, w->desc.user_data);
            }
        }
    } else if (!_sapp_tc.app.drop_enabled) {
        return;
    } else if (event->xclient.message_type == _sapp_tc.xdnd.XdndEnter) {
        const bool is_list = 0 != (event->xclient.data.l[1] & 1);
        _sapp_tc.xdnd.source = (Window)event->xclient.data.l[0];
        _sapp_tc.xdnd.version = (int)(event->xclient.data.l[1] >> 24);
        _sapp_tc.xdnd.format = None;
        _sapp_tc.xdnd.over = w;
        if (_sapp_tc.xdnd.version > _SAPP_TC_XDND_VERSION) {
            return;
        }
        uint32_t count = 0;
        Atom* formats = 0;
        if (is_list) {
            count = (uint32_t)_sapp_tc_x11_get_window_property(_sapp_tc.xdnd.source,
                _sapp_tc.xdnd.XdndTypeList, XA_ATOM, (unsigned char**)&formats);
        } else {
            count = 3;
            formats = (Atom*)event->xclient.data.l + 2;
        }
        for (uint32_t i = 0; i < count; i++) {
            if (formats[i] == _sapp_tc.xdnd.text_uri_list) {
                _sapp_tc.xdnd.format = _sapp_tc.xdnd.text_uri_list;
                break;
            }
        }
        if (is_list && formats) {
            XFree(formats);
        }
    } else if (event->xclient.message_type == _sapp_tc.xdnd.XdndDrop) {
        if (_sapp_tc.xdnd.version > _SAPP_TC_XDND_VERSION) {
            return;
        }
        Time time = CurrentTime;
        if (_sapp_tc.xdnd.format) {
            if (_sapp_tc.xdnd.version >= 1) {
                time = (Time)event->xclient.data.l[2];
            }
            XConvertSelection(_sapp_tc.display, _sapp_tc.xdnd.XdndSelection,
                              _sapp_tc.xdnd.format, _sapp_tc.xdnd.XdndSelection,
                              event->xclient.window, time);
        } else if (_sapp_tc.xdnd.version >= 2) {
            XEvent reply;
            memset(&reply, 0, sizeof(reply));
            reply.type = ClientMessage;
            reply.xclient.window = _sapp_tc.xdnd.source;
            reply.xclient.message_type = _sapp_tc.xdnd.XdndFinished;
            reply.xclient.format = 32;
            reply.xclient.data.l[0] = (long)event->xclient.window;
            reply.xclient.data.l[1] = 0;    /* drag was rejected */
            reply.xclient.data.l[2] = None;
            XSendEvent(_sapp_tc.display, _sapp_tc.xdnd.source, False, NoEventMask, &reply);
            XFlush(_sapp_tc.display);
        }
    } else if (event->xclient.message_type == _sapp_tc.xdnd.XdndPosition) {
        if (_sapp_tc.xdnd.version > _SAPP_TC_XDND_VERSION) {
            return;
        }
        _sapp_tc.xdnd.over = w;
        XEvent reply;
        memset(&reply, 0, sizeof(reply));
        reply.type = ClientMessage;
        reply.xclient.window = _sapp_tc.xdnd.source;
        reply.xclient.message_type = _sapp_tc.xdnd.XdndStatus;
        reply.xclient.format = 32;
        reply.xclient.data.l[0] = (long)event->xclient.window;
        if (_sapp_tc.xdnd.format) {
            reply.xclient.data.l[1] = 1;    /* accept with no rectangle */
            if (_sapp_tc.xdnd.version >= 2) {
                reply.xclient.data.l[4] = (long)_sapp_tc.xdnd.XdndActionCopy;
            }
        }
        XSendEvent(_sapp_tc.display, _sapp_tc.xdnd.source, False, NoEventMask, &reply);
        XFlush(_sapp_tc.display);
    }
}

/*-- GLX bring-up ---------------------------------------------------------------*/
static bool _sapp_tc_glx_ext_supported(const char* ext, const char* extensions) {
    if (!ext || !extensions) return false;
    const char* start = extensions;
    const size_t len = strlen(ext);
    for (;;) {
        const char* at = strstr(start, ext);
        if (!at) return false;
        const char* terminator = at + len;
        if ((at == extensions || *(at - 1) == ' ') &&
            (*terminator == ' ' || *terminator == '\0')) {
            return true;
        }
        start = terminator;
    }
}

typedef void (*_sapp_tc_glx_voidfn_t)(void);
static _sapp_tc_glx_voidfn_t _sapp_tc_glx_getprocaddr(const char* name) {
    return (_sapp_tc_glx_voidfn_t)glXGetProcAddressARB((const GLubyte*)name);
}

static bool _sapp_tc_glx_init(void) {
    int error_base = 0, event_base = 0;
    if (!glXQueryExtension(_sapp_tc.display, &error_base, &event_base)) {
        fprintf(stderr, "sokol_app_tc.h: GLX extension not present on the X server\n");
        return false;
    }
    int major = 0, minor = 0;
    if (!glXQueryVersion(_sapp_tc.display, &major, &minor)) {
        fprintf(stderr, "sokol_app_tc.h: glXQueryVersion() failed\n");
        return false;
    }
    if ((major < 1) || ((major == 1) && (minor < 3))) {
        fprintf(stderr, "sokol_app_tc.h: GLX version 1.3 or higher required\n");
        return false;
    }
    const char* exts = glXQueryExtensionsString(_sapp_tc.display, _sapp_tc.screen);
    if (_sapp_tc_glx_ext_supported("GLX_EXT_swap_control", exts)) {
        _sapp_tc.SwapIntervalEXT = (void (*)(Display*, GLXDrawable, int))
            _sapp_tc_glx_getprocaddr("glXSwapIntervalEXT");
    }
    if (_sapp_tc_glx_ext_supported("GLX_MESA_swap_control", exts)) {
        _sapp_tc.SwapIntervalMESA = (int (*)(int))
            _sapp_tc_glx_getprocaddr("glXSwapIntervalMESA");
    }
    if (_sapp_tc_glx_ext_supported("GLX_ARB_create_context", exts) &&
        _sapp_tc_glx_ext_supported("GLX_ARB_create_context_profile", exts)) {
        _sapp_tc.CreateContextAttribsARB =
            (GLXContext (*)(Display*, GLXFBConfig, GLXContext, Bool, const int*))
            _sapp_tc_glx_getprocaddr("glXCreateContextAttribsARB");
    }
    if (!_sapp_tc.CreateContextAttribsARB) {
        fprintf(stderr, "sokol_app_tc.h: GLX_ARB_create_context(_profile) required\n");
        return false;
    }
    return true;
}

static int _sapp_tc_glx_attrib(GLXFBConfig cfg, int attrib) {
    int value = 0;
    glXGetFBConfigAttrib(_sapp_tc.display, cfg, attrib, &value);
    return value;
}

/* pick ONE fbconfig for the whole app (shared context; every window's visual
   must match it): RGBA8, >= depth24/stencil8, doublebuffered, closest MSAA */
static bool _sapp_tc_glx_choose_fbconfig(void) {
    int count = 0;
    GLXFBConfig* configs = glXGetFBConfigs(_sapp_tc.display, _sapp_tc.screen, &count);
    if (!configs || (count == 0)) {
        if (configs) XFree(configs);
        return false;
    }
    const int want_samples = (_sapp_tc.app.desc.sample_count > 1) ? _sapp_tc.app.desc.sample_count : 0;
    int best = -1;
    int best_score = INT_MAX;
    for (int i = 0; i < count; i++) {
        if (!(_sapp_tc_glx_attrib(configs[i], GLX_RENDER_TYPE) & GLX_RGBA_BIT)) continue;
        if (!(_sapp_tc_glx_attrib(configs[i], GLX_DRAWABLE_TYPE) & GLX_WINDOW_BIT)) continue;
        if (!_sapp_tc_glx_attrib(configs[i], GLX_DOUBLEBUFFER)) continue;
        if (_sapp_tc_glx_attrib(configs[i], GLX_RED_SIZE) != 8) continue;
        if (_sapp_tc_glx_attrib(configs[i], GLX_GREEN_SIZE) != 8) continue;
        if (_sapp_tc_glx_attrib(configs[i], GLX_BLUE_SIZE) != 8) continue;
        if (_sapp_tc_glx_attrib(configs[i], GLX_ALPHA_SIZE) != 8) continue;
        const int depth = _sapp_tc_glx_attrib(configs[i], GLX_DEPTH_SIZE);
        const int stencil = _sapp_tc_glx_attrib(configs[i], GLX_STENCIL_SIZE);
        if ((depth < 24) || (stencil < 8)) continue;
        const int samples = _sapp_tc_glx_attrib(configs[i], GLX_SAMPLES);
        int sample_diff = samples - want_samples;
        if (sample_diff < 0) sample_diff = -sample_diff;
        const int score = sample_diff * 100 + (depth - 24) + (stencil - 8);
        if (score < best_score) {
            best_score = score;
            best = i;
        }
    }
    bool ok = false;
    if (best >= 0) {
        _sapp_tc.fbconfig = configs[best];
        /* report the ACTUAL sample count (a request the hardware can't satisfy
           degrades to the closest supported config) */
        const int got_samples = _sapp_tc_glx_attrib(configs[best], GLX_SAMPLES);
        _sapp_tc.app.desc.sample_count = (got_samples > 1) ? got_samples : 1;
        ok = true;
    } else {
        fprintf(stderr, "sokol_app_tc.h: no suitable GLXFBConfig found\n");
    }
    XFree(configs);
    return ok;
}

static bool _sapp_tc_glx_create_context(void) {
    int gl_major = _sapp_tc.app.desc.gl.major_version;
    int gl_minor = _sapp_tc.app.desc.gl.minor_version;
    if (gl_major == 0) {
        gl_major = 4;   /* upstream Linux GLCORE default: 4.3 core */
        gl_minor = 3;
    }
    const int attribs[] = {
        GLX_CONTEXT_MAJOR_VERSION_ARB, gl_major,
        GLX_CONTEXT_MINOR_VERSION_ARB, gl_minor,
        GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
        GLX_CONTEXT_FLAGS_ARB, GLX_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB,
        None
    };
    _sapp_tc_x11_grab_error_handler();
    _sapp_tc.ctx = _sapp_tc.CreateContextAttribsARB(_sapp_tc.display, _sapp_tc.fbconfig,
                                                    NULL, True, attribs);
    _sapp_tc_x11_release_error_handler();
    if (!_sapp_tc.ctx) {
        fprintf(stderr, "sokol_app_tc.h: failed to create GL %d.%d core context\n", gl_major, gl_minor);
        return false;
    }
    return true;
}

static void _sapp_tc_glx_set_swapinterval(_sapp_tc_window_t* w, int interval) {
    /* GLX_EXT_swap_control is per-drawable: each window gets its own interval.
       The MESA fallback is context-wide -- with more than one window it is set
       to 0 (never block) and the main tick self-heals to timer pacing. */
    if (_sapp_tc.SwapIntervalEXT) {
        _sapp_tc.SwapIntervalEXT(_sapp_tc.display, w->glx_win, interval);
    } else if (_sapp_tc.SwapIntervalMESA) {
        _sapp_tc.SwapIntervalMESA(interval);
    }
}

/*-- window creation / destruction ---------------------------------------------*/
static bool _sapp_tc_x11_create_native_window(_sapp_tc_window_t* w, const char* title,
        int width_pt, int height_pt, bool borderless) {
    XVisualInfo* vi = glXGetVisualFromFBConfig(_sapp_tc.display, _sapp_tc.fbconfig);
    if (!vi) {
        return false;
    }
    w->colormap = XCreateColormap(_sapp_tc.display, _sapp_tc.root, vi->visual, AllocNone);
    XSetWindowAttributes wa;
    memset(&wa, 0, sizeof(wa));
    const unsigned long wamask = CWBorderPixel | CWColormap | CWEventMask;
    wa.colormap = w->colormap;
    wa.border_pixel = 0;
    wa.event_mask = StructureNotifyMask | KeyPressMask | KeyReleaseMask |
                    PointerMotionMask | ButtonPressMask | ButtonReleaseMask |
                    ExposureMask | FocusChangeMask | VisibilityChangeMask |
                    EnterWindowMask | LeaveWindowMask | PropertyChangeMask;
    /* NOTE: sizing uses the same scale that dimension reporting divides by */
    const float scale = (w->dpi_scale > 0.0f) ? w->dpi_scale : 1.0f;
    int px_w = (int)((float)width_pt * scale + 0.5f);
    int px_h = (int)((float)height_pt * scale + 0.5f);
    if (width_pt <= 0) {
        px_w = (DisplayWidth(_sapp_tc.display, _sapp_tc.screen) * 4) / 5;
    }
    if (height_pt <= 0) {
        px_h = (DisplayHeight(_sapp_tc.display, _sapp_tc.screen) * 4) / 5;
    }
    _sapp_tc_x11_grab_error_handler();
    w->xwin = XCreateWindow(_sapp_tc.display, _sapp_tc.root,
                            0, 0, (unsigned int)px_w, (unsigned int)px_h,
                            0,          /* border width */
                            vi->depth,  /* color depth */
                            InputOutput, vi->visual, wamask, &wa);
    _sapp_tc_x11_release_error_handler();
    XFree(vi);
    if (!w->xwin) {
        return false;
    }
    Atom protocols[] = { _sapp_tc.WM_DELETE_WINDOW };
    XSetWMProtocols(_sapp_tc.display, w->xwin, protocols, 1);
    /* NOTE: PPosition and PSize are obsolete and ignored */
    XSizeHints* hints = XAllocSizeHints();
    if (hints) {
        hints->flags = PWinGravity;
        hints->win_gravity = CenterGravity;
        XSetWMNormalHints(_sapp_tc.display, w->xwin, hints);
        XFree(hints);
    }
    if (borderless) {
        _sapp_tc_x11_set_decorated(w, false);
    }
    /* announce XDND support */
    if (_sapp_tc.app.drop_enabled) {
        const Atom version = _SAPP_TC_XDND_VERSION;
        XChangeProperty(_sapp_tc.display, w->xwin, _sapp_tc.xdnd.XdndAware, XA_ATOM,
                        32, PropModeReplace, (unsigned char*)&version, 1);
    }
    _sapp_tc_x11_update_window_title(w, title ? title : "TrussC");
    /* GLX drawable wrapping the X window (the per-window object; the context
       is shared) */
    _sapp_tc_x11_grab_error_handler();
    w->glx_win = glXCreateWindow(_sapp_tc.display, _sapp_tc.fbconfig, w->xwin, NULL);
    _sapp_tc_x11_release_error_handler();
    if (!w->glx_win) {
        return false;
    }
    _sapp_tc_timing_reset(&w->timing);
    return true;
}

static void _sapp_tc_x11_show_window(_sapp_tc_window_t* w) {
    XMapWindow(_sapp_tc.display, w->xwin);
    XEvent dummy;
    _sapp_tc_x11_wait_for_event(w->xwin, VisibilityNotify, 0.1, &dummy);
    XRaiseWindow(_sapp_tc.display, w->xwin);
    XFlush(_sapp_tc.display);
}

static void _sapp_tc_x11_destroy_window_resources(_sapp_tc_window_t* w) {
    if (w->glx_win) {
        /* never destroy the drawable that is current */
        if (glXGetCurrentDrawable() == w->glx_win) {
            glXMakeCurrent(_sapp_tc.display, None, NULL);
        }
        glXDestroyWindow(_sapp_tc.display, w->glx_win);
        w->glx_win = 0;
    }
    if (w->xwin) {
        XUnmapWindow(_sapp_tc.display, w->xwin);
        XDestroyWindow(_sapp_tc.display, w->xwin);
        w->xwin = 0;
    }
    if (w->colormap) {
        XFreeColormap(_sapp_tc.display, w->colormap);
        w->colormap = 0;
    }
    XFlush(_sapp_tc.display);
}

/*-- event dispatch ---------------------------------------------------------------*/
static void _sapp_tc_x11_process_event(XEvent* ev) {
    _sapp_tc_window_t* w = _sapp_tc_lookup_xwin(ev->xany.window);
    switch (ev->type) {
        case FocusIn:
            /* ignore grab-cycle focus notifications (GLFW parity) */
            if (w && (ev->xfocus.mode != NotifyGrab) && (ev->xfocus.mode != NotifyUngrab)) {
                _sapp_tc_x11_app_event(w, SAPP_EVENTTYPE_FOCUSED);
            }
            break;
        case FocusOut:
            if (w && (ev->xfocus.mode != NotifyGrab) && (ev->xfocus.mode != NotifyUngrab)) {
                _sapp_tc_x11_app_event(w, SAPP_EVENTTYPE_UNFOCUSED);
            }
            break;
        case KeyPress: {
            if (!w) break;
            const int keycode = (int)ev->xkey.keycode;
            const sapp_keycode key = _sapp_tc_translate_key(keycode);
            const bool repeat = _sapp_tc_x11_keypress_repeat(w, keycode);
            uint32_t mods = _sapp_tc_x11_mods(ev->xkey.state);
            mods |= _sapp_tc_x11_key_modifier_bit(key);
            _sapp_tc_x11_key_event(w, SAPP_EVENTTYPE_KEY_DOWN, key, repeat, mods);
            KeySym keysym = 0;
            XLookupString(&ev->xkey, NULL, 0, &keysym, NULL);
            const int32_t chr = _sapp_tc_x11_keysym_to_unicode(keysym);
            if (chr > 0) {
                _sapp_tc_x11_char_event(w, (uint32_t)chr, repeat, mods);
            }
            break;
        }
        case KeyRelease: {
            if (!w) break;
            const int keycode = (int)ev->xkey.keycode;
            const sapp_keycode key = _sapp_tc_translate_key(keycode);
            _sapp_tc_x11_keyrelease_repeat(w, keycode);
            uint32_t mods = _sapp_tc_x11_mods(ev->xkey.state);
            mods &= ~_sapp_tc_x11_key_modifier_bit(key);
            _sapp_tc_x11_key_event(w, SAPP_EVENTTYPE_KEY_UP, key, false, mods);
            break;
        }
        case ButtonPress: {
            if (!w) break;
            _sapp_tc_x11_mouse_update(w, ev->xbutton.x, ev->xbutton.y, false);
            uint32_t mods = _sapp_tc_x11_mods(ev->xbutton.state);
            const unsigned int xbtn = ev->xbutton.button;
            if (xbtn == Button4)      { _sapp_tc_x11_scroll_event(w, 0.0f,  1.0f, mods); }
            else if (xbtn == Button5) { _sapp_tc_x11_scroll_event(w, 0.0f, -1.0f, mods); }
            else if (xbtn == 6)       { _sapp_tc_x11_scroll_event(w,  1.0f, 0.0f, mods); }
            else if (xbtn == 7)       { _sapp_tc_x11_scroll_event(w, -1.0f, 0.0f, mods); }
            else {
                const sapp_mousebutton btn = _sapp_tc_x11_translate_button(xbtn);
                if (btn != SAPP_MOUSEBUTTON_INVALID) {
                    mods |= _sapp_tc_x11_button_modifier_bit(btn);
                    w->mouse_buttons |= (uint8_t)(1 << btn);
                    _sapp_tc_x11_mouse_event(w, SAPP_EVENTTYPE_MOUSE_DOWN, btn, mods);
                }
            }
            break;
        }
        case ButtonRelease: {
            if (!w) break;
            _sapp_tc_x11_mouse_update(w, ev->xbutton.x, ev->xbutton.y, false);
            const sapp_mousebutton btn = _sapp_tc_x11_translate_button(ev->xbutton.button);
            if (btn != SAPP_MOUSEBUTTON_INVALID) {
                uint32_t mods = _sapp_tc_x11_mods(ev->xbutton.state);
                mods &= ~_sapp_tc_x11_button_modifier_bit(btn);
                w->mouse_buttons &= (uint8_t)~(1 << btn);
                _sapp_tc_x11_mouse_event(w, SAPP_EVENTTYPE_MOUSE_UP, btn, mods);
            }
            break;
        }
        case EnterNotify:
            if (w) {
                _sapp_tc_x11_mouse_update(w, ev->xcrossing.x, ev->xcrossing.y, true);
                /* suppressed while a button is held (upstream parity) */
                if (w->mouse_buttons == 0) {
                    _sapp_tc_x11_mouse_event(w, SAPP_EVENTTYPE_MOUSE_ENTER,
                        SAPP_MOUSEBUTTON_INVALID, _sapp_tc_x11_mods(ev->xcrossing.state));
                }
            }
            break;
        case LeaveNotify:
            if (w && (w->mouse_buttons == 0)) {
                _sapp_tc_x11_mouse_event(w, SAPP_EVENTTYPE_MOUSE_LEAVE,
                    SAPP_MOUSEBUTTON_INVALID, _sapp_tc_x11_mods(ev->xcrossing.state));
            }
            break;
        case MotionNotify:
            if (w) {
                _sapp_tc_x11_mouse_update(w, ev->xmotion.x, ev->xmotion.y, false);
                _sapp_tc_x11_mouse_event(w, SAPP_EVENTTYPE_MOUSE_MOVE,
                    SAPP_MOUSEBUTTON_INVALID, _sapp_tc_x11_mods(ev->xmotion.state));
            }
            break;
        case PropertyNotify:
            /* iconify/restore detection (there is no map/unmap handling) */
            if (w && (ev->xproperty.state == PropertyNewValue) &&
                (ev->xproperty.atom == _sapp_tc.WM_STATE)) {
                const int state = _sapp_tc_x11_get_window_state(w);
                if ((state == IconicState) && !w->iconified) {
                    w->iconified = true;
                    _sapp_tc_x11_app_event(w, SAPP_EVENTTYPE_ICONIFIED);
                } else if ((state == NormalState) && w->iconified) {
                    w->iconified = false;
                    w->earliest_next = 0.0;     /* resume promptly */
                    _sapp_tc_x11_app_event(w, SAPP_EVENTTYPE_RESTORED);
                }
            }
            break;
        case VisibilityNotify:
            /* occlusion gate for secondary windows: a fully covered window
               stops rendering (the main window keeps upstream's behavior) */
            if (w && !w->is_main) {
                const bool obscured = (ev->xvisibility.state == VisibilityFullyObscured);
                if (obscured != w->occluded) {
                    w->occluded = obscured;
                    _sapp_tc_x11_app_event(w, obscured ? SAPP_EVENTTYPE_SUSPENDED
                                                       : SAPP_EVENTTYPE_RESUMED);
                    if (!obscured) {
                        w->earliest_next = 0.0;
                    }
                }
            }
            break;
        case SelectionNotify:
            _sapp_tc_x11_on_selectionnotify(ev);
            break;
        case SelectionRequest:
            _sapp_tc_x11_serve_selection_request(ev);
            break;
        case ClientMessage:
            _sapp_tc_x11_on_clientmessage(ev);
            break;
        case DestroyNotify:
            /* not a bug (upstream parity) */
            break;
        default:
            break;
    }
}

/*-- pacing / tick ------------------------------------------------------------------*/
static void _sapp_tc_x11_pace(_sapp_tc_window_t* w, double seconds) {
    w->earliest_next = _sapp_tc_now() + seconds;
}

/* absolute periodic schedule (timer-paced secondary windows): pacing from the
   previous deadline instead of "now" keeps the average rate at the period
   despite tick jitter; resync when far off (first tick, after occlusion) */
static void _sapp_tc_x11_pace_periodic(_sapp_tc_window_t* w, double period) {
    const double now = _sapp_tc_now();
    double next = w->earliest_next + period;
    if ((next <= now) || (next > now + 2.0 * period)) {
        next = now + period;
    }
    w->earliest_next = next;
}

static bool _sapp_tc_x11_window_due(_sapp_tc_window_t* w, double now) {
    if (!w || !w->ready || !w->glx_win || w->in_tick) return false;
    return w->earliest_next <= now;
}

static void _sapp_tc_x11_tick(_sapp_tc_window_t* w) {
    if (w->in_tick || !w->glx_win) return;
    _sapp_tc_timing_update(&w->timing);
    w->frame_duration = w->timing.smooth_dt;
    const int swap_interval = (_sapp_tc.app.desc.swap_interval > 0) ? _sapp_tc.app.desc.swap_interval : 1;

    if (w->is_main) {
        /* resize is polled here every frame (upstream parity: the X11 backend
           has no ConfigureNotify handler; this poll is the size authority) */
        if (_sapp_tc_x11_update_dimensions(w) && _sapp_tc.app.init_called) {
            _sapp_tc_x11_app_event(w, SAPP_EVENTTYPE_RESIZED);
        }
        glXMakeCurrent(_sapp_tc.display, w->glx_win, _sapp_tc.ctx);
        w->in_tick = true;
        if (!_sapp_tc.app.init_called) {
            _sapp_tc.app.init_called = true;
            if (_sapp_tc.app.desc.init_cb) {
                _sapp_tc.app.desc.init_cb();
            } else if (_sapp_tc.app.desc.init_userdata_cb) {
                _sapp_tc.app.desc.init_userdata_cb(_sapp_tc.app.desc.user_data);
            }
        }
        if (_sapp_tc.app.desc.frame_cb) {
            _sapp_tc.app.desc.frame_cb();
        } else if (_sapp_tc.app.desc.frame_userdata_cb) {
            _sapp_tc.app.desc.frame_userdata_cb(_sapp_tc.app.desc.user_data);
        }
        _sapp_tc.app.frame_count++;
        w->in_tick = false;
        bool presented = false;
        if (_sapp_tc.app.skip_present) {
            /* one-shot event-driven present suppression (TrussC patch: keeps
               the last image on screen when a frame decides not to draw) */
            _sapp_tc.app.skip_present = false;
        } else if (!w->iconified) {
            /* with swap interval >= 1 this call BLOCKS until vsync -- that
               blocking swap is the run loop's pacing (upstream parity) */
            glXSwapBuffers(_sapp_tc.display, w->glx_win);
            presented = true;
        }
        const double now = _sapp_tc_now();
        if (w->iconified) {
            /* minimized: frame_cb keeps running, throttled (win32 parity) */
            _sapp_tc_x11_pace(w, 0.0166 * (double)swap_interval);
        } else if (presented) {
            const double swap_dt = now - w->last_present;
            w->last_present = now;
            if (swap_dt > 0.002) {
                /* the swap blocked (vsync paces the loop); refine the refresh
                   estimate that paces the secondary windows */
                const double per_frame = swap_dt / (double)swap_interval;
                if ((per_frame > (1.0 / 240.0)) && (per_frame < (1.0 / 30.0))) {
                    _sapp_tc.refresh_period += 0.1 * (per_frame - _sapp_tc.refresh_period);
                }
                w->earliest_next = 0.0;
            } else {
                /* the swap returned immediately (unmapped/unredirected window,
                   MESA context-wide interval 0, driver without vsync): fall
                   back to timer pacing so the loop can never busy-spin */
                _sapp_tc_x11_pace_periodic(w, _sapp_tc.refresh_period * (double)swap_interval);
            }
        } else {
            _sapp_tc_x11_pace(w, _sapp_tc.refresh_period * (double)swap_interval);
        }
    } else {
        /* secondary window: swap interval 0 (vsyncing N drawables would
           serialize the loop to refresh/N), timer-paced to the measured
           refresh estimate */
        if (w->iconified || w->occluded) {
            _sapp_tc_x11_pace(w, _sapp_tc.refresh_period);
            return;
        }
        if (_sapp_tc_x11_update_dimensions(w)) {
            _sapp_tc_x11_app_event(w, SAPP_EVENTTYPE_RESIZED);
        }
        glXMakeCurrent(_sapp_tc.display, w->glx_win, _sapp_tc.ctx);
        w->in_tick = true;
        if (w->desc.tick_cb) {
            sapp_window handle = { w->win_id };
            w->desc.tick_cb(handle, w->desc.user_data);
        }
        w->in_tick = false;
        glXSwapBuffers(_sapp_tc.display, w->glx_win);
        _sapp_tc_x11_pace_periodic(w, _sapp_tc.refresh_period);
    }
}

/*-- the run loop --------------------------------------------------------------------
    While the main window presents normally, its blocking vsync'd swap paces
    the loop and the XPending drain runs once per frame (upstream parity).
    When every window is timer-paced (minimized, skip_present), the loop
    sleeps in poll() on the X connection fd until the next deadline or an
    incoming event, so the process never busy-spins. */
static void _sapp_tc_x11_run_loop(void) {
    XFlush(_sapp_tc.display);
    while (!_sapp_tc.app.quit_ordered) {
        double now = _sapp_tc_now();
        double wake_at = now + 0.1;     /* robustness cap */
        bool any_due = false;
        for (int i = 0; i < _SAPP_TC_MAX_WINDOWS; i++) {
            _sapp_tc_window_t* w = _sapp_tc.windows[i];
            if (!w || !w->ready || !w->glx_win || w->in_tick) continue;
            if (w->earliest_next > now) {
                if (w->earliest_next < wake_at) wake_at = w->earliest_next;
            } else {
                any_due = true;
            }
        }
        if (!any_due && !XPending(_sapp_tc.display)) {
            struct pollfd pfd;
            pfd.fd = ConnectionNumber(_sapp_tc.display);
            pfd.events = POLLIN;
            pfd.revents = 0;
            const double timeout_s = wake_at - now;
            poll(&pfd, 1, (timeout_s > 0.0) ? (int)(timeout_s * 1000.0) + 1 : 0);
        }
        /* drain exactly the already-queued events (can't block: count bounded) */
        int count = XPending(_sapp_tc.display);
        while (count--) {
            XEvent event;
            XNextEvent(_sapp_tc.display, &event);
            _sapp_tc_x11_process_event(&event);
        }
        /* tick every due window (re-fetch each slot: a tick or a processed
           event may have destroyed windows) */
        now = _sapp_tc_now();
        for (int i = 0; i < _SAPP_TC_MAX_WINDOWS; i++) {
            _sapp_tc_window_t* w = _sapp_tc.windows[i];
            if (_sapp_tc_x11_window_due(w, now)) {
                _sapp_tc_x11_tick(w);
            }
        }
        XFlush(_sapp_tc.display);
        /* the cancellable quit dance (upstream parity): WM_DELETE_WINDOW or
           sapp_request_quit() land here; sapp_quit() pre-sets quit_ordered */
        if (_sapp_tc.app.quit_requested && !_sapp_tc.app.quit_ordered) {
            if (_sapp_tc.app.main) {
                _sapp_tc_x11_app_event(_sapp_tc.app.main, SAPP_EVENTTYPE_QUIT_REQUESTED);
            }
            if (_sapp_tc.app.quit_requested) {
                _sapp_tc.app.quit_ordered = true;
            }
        }
    }
}

/*-- main window creation --------------------------------------------------------*/
static bool _sapp_tc_x11_create_main_window(void) {
    const sapp_desc* d = &_sapp_tc.app.desc;
    int slot = -1;
    for (int i = 0; i < _SAPP_TC_MAX_WINDOWS; i++) {
        if (_sapp_tc.windows[i] == 0) { slot = i; break; }
    }
    if (slot < 0) return false;

    _sapp_tc_window_t* w = new _sapp_tc_window_t();
    w->win_id = ++_sapp_tc.next_id;
    w->is_main = true;
    w->dpi_scale = d->high_dpi ? (_sapp_tc.dpi / 96.0f) : 1.0f;
    if (w->dpi_scale <= 0.0f) w->dpi_scale = 1.0f;

    /* per-window desc: the app callbacks are reached via the trampoline */
    w->desc.width = d->width;
    w->desc.height = d->height;
    w->desc.sample_count = d->sample_count;
    w->desc.no_high_dpi = !d->high_dpi;
    w->desc.event_cb = _sapp_tc_main_event_tramp;

    if (!_sapp_tc_x11_create_native_window(w, _sapp_tc.app.window_title,
            d->width, d->height, false)) {
        _sapp_tc_x11_destroy_window_resources(w);
        delete w;
        return false;
    }
    _sapp_tc.windows[slot] = w;
    _sapp_tc.app.main = w;
    w->ready = true;
    _sapp_tc_x11_update_dimensions(w);      /* silent startup sizing (no RESIZED) */
    glXMakeCurrent(_sapp_tc.display, w->glx_win, _sapp_tc.ctx);
    {
        /* capture the default-framebuffer id sapp_get_swapchain() reports */
        GLint fb = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
        _sapp_tc.gl_framebuffer = (uint32_t)fb;
    }
    _sapp_tc_glx_set_swapinterval(w, (d->swap_interval > 0) ? d->swap_interval : 1);
    _sapp_tc_x11_show_window(w);
    if (d->fullscreen) {
        /* must be after XMapWindow */
        _sapp_tc_x11_set_fullscreen(true);
        _sapp_tc_x11_update_dimensions(w);
    }
    _sapp_tc.app.valid = true;
    return true;
}

/*-- public API --------------------------------------------------------------------*/
extern "C" {

sapp_window sapp_create_window(const sapp_window_desc* desc) {
    sapp_window invalid = { 0 };
    if (!desc) return invalid;
    if (!_sapp_tc.app.valid || !_sapp_tc.ctx) return invalid;

    int slot = -1;
    for (int i = 0; i < _SAPP_TC_MAX_WINDOWS; i++) {
        if (_sapp_tc.windows[i] == 0) { slot = i; break; }
    }
    if (slot < 0) return invalid;

    _sapp_tc_window_t* w = new _sapp_tc_window_t();
    w->win_id = ++_sapp_tc.next_id;
    w->desc = *desc;
    if (w->desc.width <= 0) w->desc.width = 640;
    if (w->desc.height <= 0) w->desc.height = 480;
    /* all windows share ONE fbconfig/visual/context: the MSAA sample count is
       the one negotiated at startup, not per-window */
    w->desc.sample_count = _sapp_tc.app.desc.sample_count;
    w->dpi_scale = w->desc.no_high_dpi ? 1.0f : (_sapp_tc.dpi / 96.0f);
    if (w->dpi_scale <= 0.0f) w->dpi_scale = 1.0f;

    if (!_sapp_tc_x11_create_native_window(w, w->desc.title,
            w->desc.width, w->desc.height, w->desc.borderless)) {
        _sapp_tc_x11_destroy_window_resources(w);
        delete w;
        return invalid;
    }
    /* never vsync a second drawable (it would serialize the run loop to
       refresh/N); this window is timer-paced instead. Requires a current
       context (there always is one while the app runs). With only the
       context-wide MESA fallback this turns vsync off for the WHOLE context
       and the main tick self-heals to timer pacing. */
    _sapp_tc_glx_set_swapinterval(w, 0);
    _sapp_tc.windows[slot] = w;
    w->ready = true;
    _sapp_tc_x11_update_dimensions(w);
    _sapp_tc_x11_show_window(w);
    sapp_window handle = { w->win_id };
    return handle;
}

void sapp_destroy_window(sapp_window win) {
    _sapp_tc_window_t* w = _sapp_tc_lookup(win);
    if (!w || w->is_main) return;   /* the main window is owned by sapp_run */
    for (int i = 0; i < _SAPP_TC_MAX_WINDOWS; i++) {
        if (_sapp_tc.windows[i] == w) { _sapp_tc.windows[i] = 0; break; }
    }
    if (_sapp_tc.xdnd.over == w) _sapp_tc.xdnd.over = 0;
    w->desc.close_cb = 0;
    _sapp_tc_x11_destroy_window_resources(w);
    delete w;
    /* restore the main window's drawable if the destroy released it */
    if (!glXGetCurrentContext() && _sapp_tc.app.main && _sapp_tc.app.main->glx_win) {
        glXMakeCurrent(_sapp_tc.display, _sapp_tc.app.main->glx_win, _sapp_tc.ctx);
    }
}

bool sapp_window_valid(sapp_window win) {
    return _sapp_tc_lookup(win) != 0;
}

int sapp_window_width(sapp_window win) {
    _sapp_tc_window_t* w = _sapp_tc_lookup(win);
    return w ? w->win_width : 0;
}

int sapp_window_height(sapp_window win) {
    _sapp_tc_window_t* w = _sapp_tc_lookup(win);
    return w ? w->win_height : 0;
}

int sapp_window_framebuffer_width(sapp_window win) {
    _sapp_tc_window_t* w = _sapp_tc_lookup(win);
    return w ? w->fb_width : 0;
}

int sapp_window_framebuffer_height(sapp_window win) {
    _sapp_tc_window_t* w = _sapp_tc_lookup(win);
    return w ? w->fb_height : 0;
}

float sapp_window_dpi_scale(sapp_window win) {
    _sapp_tc_window_t* w = _sapp_tc_lookup(win);
    return w ? w->dpi_scale : 1.0f;
}

int sapp_window_sample_count(sapp_window win) {
    _sapp_tc_window_t* w = _sapp_tc_lookup(win);
    return w ? w->desc.sample_count : 1;
}

bool sapp_window_occluded(sapp_window win) {
    _sapp_tc_window_t* w = _sapp_tc_lookup(win);
    return w ? w->occluded : false;
}

void sapp_window_set_title(sapp_window win, const char* title) {
    _sapp_tc_window_t* w = _sapp_tc_lookup(win);
    if (!w || !w->xwin || !title) return;
    _sapp_tc_x11_update_window_title(w, title);
}

double sapp_window_frame_duration(sapp_window win) {
    _sapp_tc_window_t* w = _sapp_tc_lookup(win);
    if (!w) return 1.0 / 60.0;
    return (w->frame_duration > 0.0) ? w->frame_duration : _sapp_tc.refresh_period;
}

/* Metal / D3D11 handles do not exist on Linux */
const void* sapp_window_metal_current_drawable(sapp_window win) { (void)win; return 0; }
const void* sapp_window_metal_depth_stencil_texture(sapp_window win) { (void)win; return 0; }
const void* sapp_window_metal_msaa_color_texture(sapp_window win) { (void)win; return 0; }
const void* sapp_window_macos_get_window(sapp_window win) { (void)win; return 0; }
const void* sapp_window_d3d11_render_view(sapp_window win) { (void)win; return 0; }
const void* sapp_window_d3d11_resolve_view(sapp_window win) { (void)win; return 0; }
const void* sapp_window_d3d11_depth_stencil_view(sapp_window win) { (void)win; return 0; }
const void* sapp_window_win32_get_hwnd(sapp_window win) { (void)win; return 0; }

uint32_t sapp_window_gl_framebuffer(sapp_window win) {
    /* the GL "swapchain" is just the default framebuffer of whatever drawable
       is current -- which is this window's during its tick_cb */
    (void)win;
    return _sapp_tc.gl_framebuffer;
}

const void* sapp_window_x11_get_window(sapp_window win) {
    _sapp_tc_window_t* w = _sapp_tc_lookup(win);
    return w ? (const void*)(uintptr_t)w->xwin : 0;
}

/*-- sokol_app.h public API (Linux implementation lives here) ---------------------*/

void sapp_run(const sapp_desc* desc) {
    if (!desc) return;
    _sapp_tc.app.desc = *desc;
    sapp_desc* d = &_sapp_tc.app.desc;
    if (d->sample_count <= 0) d->sample_count = 1;
    if (d->swap_interval <= 0) d->swap_interval = 1;
    if (d->clipboard_size <= 0) d->clipboard_size = 8192;
    if (d->max_dropped_files <= 0) d->max_dropped_files = 1;
    if (d->max_dropped_file_path_length <= 0) d->max_dropped_file_path_length = 2048;
    strncpy(_sapp_tc.app.window_title, d->window_title ? d->window_title : "sokol",
            sizeof(_sapp_tc.app.window_title) - 1);
    d->window_title = _sapp_tc.app.window_title;
    if (d->enable_clipboard) {
        _sapp_tc.app.clipboard_enabled = true;
        _sapp_tc.app.clipboard_size = d->clipboard_size;
        _sapp_tc.app.clipboard = (char*)calloc(1, (size_t)d->clipboard_size);
    }
    if (d->enable_dragndrop) {
        _sapp_tc.app.drop_enabled = true;
        _sapp_tc.app.drop_max_files = d->max_dropped_files;
        _sapp_tc.app.drop_max_path_length = d->max_dropped_file_path_length;
        _sapp_tc.app.drop_buffer = (char*)calloc(
            (size_t)d->max_dropped_files, (size_t)d->max_dropped_file_path_length);
    }
    _sapp_tc.app.mouse_shown = true;
    _sapp_tc.app.current_cursor = SAPP_MOUSECURSOR_DEFAULT;
    _sapp_tc.refresh_period = 1.0 / 60.0;

    /* XInitThreads before ANY other Xlib call (TrussC runs off-thread work) */
    XInitThreads();
    XrmInitialize();
    _sapp_tc.display = XOpenDisplay(NULL);
    if (!_sapp_tc.display) {
        fprintf(stderr, "sokol_app_tc.h: XOpenDisplay() failed (no X server / DISPLAY not set)\n");
        abort();
    }
    _sapp_tc.screen = DefaultScreen(_sapp_tc.display);
    _sapp_tc.root = DefaultRootWindow(_sapp_tc.display);
    _sapp_tc_x11_query_system_dpi();
    _sapp_tc_x11_init_extensions();
    _sapp_tc_x11_init_cursors();
    /* detectable auto-repeat: held keys emit repeated KeyPress WITHOUT paired
       KeyRelease -- required by the software key-repeat tracking */
    XkbSetDetectableAutoRepeat(_sapp_tc.display, True, NULL);
    _sapp_tc_x11_init_keytable();

    if (_sapp_tc_glx_init() && _sapp_tc_glx_choose_fbconfig() && _sapp_tc_glx_create_context()) {
        if (_sapp_tc_x11_create_main_window()) {
            _sapp_tc_x11_run_loop();
        }
    }

    /* user cleanup runs BEFORE any GL/window teardown (the app shuts down
       sokol_gfx here, which still needs a current context) */
    if (!_sapp_tc.app.cleanup_called) {
        _sapp_tc.app.cleanup_called = true;
        if (_sapp_tc.app.desc.cleanup_cb) {
            _sapp_tc.app.desc.cleanup_cb();
        } else if (_sapp_tc.app.desc.cleanup_userdata_cb) {
            _sapp_tc.app.desc.cleanup_userdata_cb(_sapp_tc.app.desc.user_data);
        }
    }
    /* destroy any windows the app left open (secondary first, then main) */
    for (int i = 0; i < _SAPP_TC_MAX_WINDOWS; i++) {
        _sapp_tc_window_t* w = _sapp_tc.windows[i];
        if (!w || w->is_main) continue;
        _sapp_tc.windows[i] = 0;
        _sapp_tc_x11_destroy_window_resources(w);
        delete w;
    }
    if (_sapp_tc.app.main) {
        _sapp_tc_window_t* w = _sapp_tc.app.main;
        for (int i = 0; i < _SAPP_TC_MAX_WINDOWS; i++) {
            if (_sapp_tc.windows[i] == w) { _sapp_tc.windows[i] = 0; break; }
        }
        _sapp_tc.app.main = 0;
        _sapp_tc_x11_destroy_window_resources(w);
        delete w;
    }
    if (_sapp_tc.ctx) {
        glXMakeCurrent(_sapp_tc.display, None, NULL);
        glXDestroyContext(_sapp_tc.display, _sapp_tc.ctx);
        _sapp_tc.ctx = 0;
    }
    _sapp_tc_x11_destroy_cursors();
    XCloseDisplay(_sapp_tc.display);
    _sapp_tc.display = 0;
    if (_sapp_tc.app.clipboard) { free(_sapp_tc.app.clipboard); _sapp_tc.app.clipboard = 0; }
    if (_sapp_tc.app.drop_buffer) { free(_sapp_tc.app.drop_buffer); _sapp_tc.app.drop_buffer = 0; }
    _sapp_tc.app.valid = false;
}

bool sapp_isvalid(void) {
    return _sapp_tc.app.valid;
}

int sapp_width(void) {
    _sapp_tc_window_t* w = _sapp_tc.app.main;
    return (w && w->fb_width > 0) ? w->fb_width : 1;
}

float sapp_widthf(void) {
    return (float)sapp_width();
}

int sapp_height(void) {
    _sapp_tc_window_t* w = _sapp_tc.app.main;
    return (w && w->fb_height > 0) ? w->fb_height : 1;
}

float sapp_heightf(void) {
    return (float)sapp_height();
}

int sapp_sample_count(void) {
    return _sapp_tc.app.desc.sample_count > 0 ? _sapp_tc.app.desc.sample_count : 1;
}

bool sapp_high_dpi(void) {
    _sapp_tc_window_t* w = _sapp_tc.app.main;
    return _sapp_tc.app.desc.high_dpi && w && (w->dpi_scale >= 1.5f);
}

float sapp_dpi_scale(void) {
    _sapp_tc_window_t* w = _sapp_tc.app.main;
    return w ? w->dpi_scale : 1.0f;
}

uint64_t sapp_frame_count(void) {
    return _sapp_tc.app.frame_count;
}

double sapp_frame_duration(void) {
    _sapp_tc_window_t* w = _sapp_tc.app.main;
    if (!w) return 1.0 / 60.0;
    return (w->timing.smooth_dt > 0.0) ? w->timing.smooth_dt : _sapp_tc.refresh_period;
}

void sapp_request_quit(void) {
    _sapp_tc.app.quit_requested = true;
}

void sapp_cancel_quit(void) {
    _sapp_tc.app.quit_requested = false;
}

void sapp_quit(void) {
    _sapp_tc.app.quit_ordered = true;
}

void sapp_consume_event(void) {
    _sapp_tc.app.event_consumed = true;
}

void sapp_skip_present(void) {
    /* one-shot; HONORED on glXSwapBuffers (TrussC patch: event-driven present
       suppression -- keeps the last image on screen, prevents flicker) */
    _sapp_tc.app.skip_present = true;
}

void sapp_show_keyboard(bool show) {
    (void)show;     /* on-screen keyboard: mobile only */
}

bool sapp_keyboard_shown(void) {
    return false;
}

void sapp_show_mouse(bool show) {
    if (_sapp_tc.app.mouse_shown != show) {
        _sapp_tc.app.mouse_shown = show;
        _sapp_tc_x11_apply_cursor();
    }
}

bool sapp_mouse_shown(void) {
    return _sapp_tc.app.mouse_shown;
}

void sapp_set_mouse_cursor(sapp_mouse_cursor cursor) {
    if ((int)cursor < 0 || (int)cursor >= _SAPP_MOUSECURSOR_NUM) return;
    if (cursor != _sapp_tc.app.current_cursor) {
        _sapp_tc.app.current_cursor = cursor;
        _sapp_tc_x11_apply_cursor();
    }
}

sapp_mouse_cursor sapp_get_mouse_cursor(void) {
    return _sapp_tc.app.current_cursor;
}

sapp_mouse_cursor sapp_bind_mouse_cursor_image(sapp_mouse_cursor cursor, const sapp_image_desc* desc) {
    if ((int)cursor < 0 || (int)cursor >= _SAPP_MOUSECURSOR_NUM) return cursor;
    if (!desc || !desc->pixels.ptr || desc->width <= 0 || desc->height <= 0) return cursor;
    if (desc->pixels.size < (size_t)(desc->width * desc->height * 4)) return cursor;
    sapp_unbind_mouse_cursor_image(cursor);
    XcursorImage* img = XcursorImageCreate(desc->width, desc->height);
    if (!img) return cursor;
    img->xhot = (XcursorDim)desc->cursor_hotspot_x;
    img->yhot = (XcursorDim)desc->cursor_hotspot_y;
    /* Xcursor wants BGRA */
    const size_t num_bytes = (size_t)(desc->width * desc->height) * 4;
    for (size_t i = 0; i < num_bytes; i += 4) {
        ((uint8_t*)img->pixels)[i + 0] = ((const uint8_t*)desc->pixels.ptr)[i + 2];
        ((uint8_t*)img->pixels)[i + 1] = ((const uint8_t*)desc->pixels.ptr)[i + 1];
        ((uint8_t*)img->pixels)[i + 2] = ((const uint8_t*)desc->pixels.ptr)[i + 0];
        ((uint8_t*)img->pixels)[i + 3] = ((const uint8_t*)desc->pixels.ptr)[i + 3];
    }
    Cursor c = XcursorImageLoadCursor(_sapp_tc.display, img);
    XcursorImageDestroy(img);
    if (!c) return cursor;
    _sapp_tc.custom_cursors[cursor] = c;
    _sapp_tc.app.custom_cursor_bound[cursor] = true;
    if (_sapp_tc.app.current_cursor == cursor) {
        _sapp_tc_x11_apply_cursor();
    }
    return cursor;
}

void sapp_unbind_mouse_cursor_image(sapp_mouse_cursor cursor) {
    if ((int)cursor < 0 || (int)cursor >= _SAPP_MOUSECURSOR_NUM) return;
    if (!_sapp_tc.app.custom_cursor_bound[cursor]) return;
    _sapp_tc.app.custom_cursor_bound[cursor] = false;
    Cursor c = _sapp_tc.custom_cursors[cursor];
    _sapp_tc.custom_cursors[cursor] = 0;
    if (_sapp_tc.app.current_cursor == cursor) {
        _sapp_tc_x11_apply_cursor();
    }
    if (c) XFreeCursor(_sapp_tc.display, c);
}

void sapp_set_clipboard_string(const char* str) {
    if (!_sapp_tc.app.clipboard_enabled || !_sapp_tc.app.clipboard || !str) return;
    _sapp_tc_window_t* w = _sapp_tc.app.main;
    if (!w) return;
    if (strlen(str) >= (size_t)_sapp_tc.app.clipboard_size) {
        return;     /* string too big for the clipboard buffer */
    }
    strncpy(_sapp_tc.app.clipboard, str, (size_t)_sapp_tc.app.clipboard_size - 1);
    _sapp_tc.app.clipboard[_sapp_tc.app.clipboard_size - 1] = 0;
    XSetSelectionOwner(_sapp_tc.display, _sapp_tc.CLIPBOARD, w->xwin, CurrentTime);
    if (XGetSelectionOwner(_sapp_tc.display, _sapp_tc.CLIPBOARD) != w->xwin) {
        _sapp_tc.app.clipboard[0] = 0;  /* failed to become selection owner */
    }
}

const char* sapp_get_clipboard_string(void) {
    if (!_sapp_tc.app.clipboard_enabled || !_sapp_tc.app.clipboard) return "";
    _sapp_tc_window_t* w = _sapp_tc.app.main;
    if (!w) return "";
    if (XGetSelectionOwner(_sapp_tc.display, _sapp_tc.CLIPBOARD) == w->xwin) {
        /* we own the selection: skip the X round-trip */
        return _sapp_tc.app.clipboard;
    }
    _sapp_tc.app.clipboard[0] = 0;
    const Atom incremental = XInternAtom(_sapp_tc.display, "INCR", False);
    XConvertSelection(_sapp_tc.display, _sapp_tc.CLIPBOARD, _sapp_tc.UTF8_STRING,
                      _sapp_tc.SAPP_TC_SELECTION, w->xwin, CurrentTime);
    XEvent event;
    if (!_sapp_tc_x11_wait_for_event(w->xwin, SelectionNotify, 0.1, &event)) {
        return _sapp_tc.app.clipboard;
    }
    if (event.xselection.property == None) {
        return _sapp_tc.app.clipboard;
    }
    char* data = NULL;
    Atom actual_type;
    int actual_format;
    unsigned long item_count, bytes_after;
    const int ret = XGetWindowProperty(_sapp_tc.display,
        event.xselection.requestor, event.xselection.property,
        0, LONG_MAX, True, _sapp_tc.UTF8_STRING,
        &actual_type, &actual_format, &item_count, &bytes_after,
        (unsigned char**)&data);
    if ((ret != Success) || (data == NULL)) {
        if (data) XFree(data);
        return _sapp_tc.app.clipboard;
    }
    /* INCR (incremental transfer) is rejected: pastes beyond the buffer fail */
    if ((actual_type != incremental) && (item_count < (unsigned long)_sapp_tc.app.clipboard_size)) {
        strncpy(_sapp_tc.app.clipboard, data, (size_t)_sapp_tc.app.clipboard_size - 1);
        _sapp_tc.app.clipboard[_sapp_tc.app.clipboard_size - 1] = 0;
    }
    XFree(data);
    return _sapp_tc.app.clipboard;
}

void sapp_set_window_title(const char* str) {
    if (!str) return;
    strncpy(_sapp_tc.app.window_title, str, sizeof(_sapp_tc.app.window_title) - 1);
    _sapp_tc.app.window_title[sizeof(_sapp_tc.app.window_title) - 1] = 0;
    _sapp_tc_window_t* w = _sapp_tc.app.main;
    if (w && w->xwin) {
        _sapp_tc_x11_update_window_title(w, _sapp_tc.app.window_title);
    }
}

bool sapp_is_fullscreen(void) {
    return _sapp_tc.app.fullscreen;
}

void sapp_toggle_fullscreen(void) {
    if (!_sapp_tc.app.main) return;
    _sapp_tc_x11_set_fullscreen(!_sapp_tc.app.fullscreen);
    _sapp_tc_x11_update_dimensions(_sapp_tc.app.main);
}

int sapp_get_num_dropped_files(void) {
    return _sapp_tc.app.drop_enabled ? _sapp_tc.app.drop_num_files : 0;
}

const char* sapp_get_dropped_file_path(int index) {
    if (!_sapp_tc.app.drop_enabled || !_sapp_tc.app.drop_buffer) return "";
    if (index < 0 || index >= _sapp_tc.app.drop_num_files) return "";
    return _sapp_tc.app.drop_buffer + (size_t)index * (size_t)_sapp_tc.app.drop_max_path_length;
}

sapp_pixel_format sapp_color_format(void) {
    return SAPP_PIXELFORMAT_RGBA8;      /* plain 8-bit RGBA on GL (no 10-bit path) */
}

sapp_pixel_format sapp_depth_format(void) {
    return SAPP_PIXELFORMAT_DEPTH_STENCIL;
}

const void* sapp_x11_get_window(void) {
    _sapp_tc_window_t* w = _sapp_tc.app.main;
    return w ? (const void*)(uintptr_t)w->xwin : 0;
}

const void* sapp_x11_get_display(void) {
    return (const void*)_sapp_tc.display;
}

/* Metal / D3D11 / win32 / macOS handles do not exist on Linux */
const void* sapp_metal_get_device(void) { return 0; }
const void* sapp_metal_get_current_drawable(void) { return 0; }
const void* sapp_metal_get_depth_stencil_texture(void) { return 0; }
const void* sapp_metal_get_msaa_color_texture(void) { return 0; }
const void* sapp_macos_get_window(void) { return 0; }
const void* sapp_win32_get_hwnd(void) { return 0; }
const void* sapp_d3d11_get_device(void) { return 0; }
const void* sapp_d3d11_get_device_context(void) { return 0; }
const void* sapp_d3d11_get_swap_chain(void) { return 0; }

sapp_environment sapp_get_environment(void) {
    sapp_environment env;
    memset(&env, 0, sizeof(env));
    env.defaults.color_format = sapp_color_format();
    env.defaults.depth_format = sapp_depth_format();
    env.defaults.sample_count = sapp_sample_count();
    /* GL has no device handle: the shared context is current on this thread */
    return env;
}

sapp_swapchain sapp_get_swapchain(void) {
    /* the GL swapchain contract is just the default-framebuffer id: rendering
       targets whatever drawable is current (the main window's during its
       frame_cb) -- calling this any number of times per frame is safe */
    sapp_swapchain sc;
    memset(&sc, 0, sizeof(sc));
    _sapp_tc_window_t* w = _sapp_tc.app.main;
    if (!w) return sc;
    sc.width = sapp_width();
    sc.height = sapp_height();
    sc.sample_count = sapp_sample_count();
    sc.color_format = sapp_color_format();
    sc.depth_format = sapp_depth_format();
    sc.gl.framebuffer = _sapp_tc.gl_framebuffer;
    return sc;
}

} /* extern "C" */
#else /* other platforms */
/*== stubs (explicit platform gap; the web/mobile ports replace these) ======*/
#if defined(__cplusplus)
extern "C" {
#endif
sapp_window sapp_create_window(const sapp_window_desc* desc) { (void)desc; sapp_window w = {0}; return w; }
void sapp_destroy_window(sapp_window win) { (void)win; }
bool sapp_window_valid(sapp_window win) { (void)win; return false; }
int sapp_window_width(sapp_window win) { (void)win; return 0; }
int sapp_window_height(sapp_window win) { (void)win; return 0; }
int sapp_window_framebuffer_width(sapp_window win) { (void)win; return 0; }
int sapp_window_framebuffer_height(sapp_window win) { (void)win; return 0; }
float sapp_window_dpi_scale(sapp_window win) { (void)win; return 1.0f; }
int sapp_window_sample_count(sapp_window win) { (void)win; return 1; }
bool sapp_window_occluded(sapp_window win) { (void)win; return false; }
void sapp_window_set_title(sapp_window win, const char* title) { (void)win; (void)title; }
double sapp_window_frame_duration(sapp_window win) { (void)win; return 1.0 / 60.0; }
const void* sapp_window_metal_current_drawable(sapp_window win) { (void)win; return 0; }
const void* sapp_window_metal_depth_stencil_texture(sapp_window win) { (void)win; return 0; }
const void* sapp_window_metal_msaa_color_texture(sapp_window win) { (void)win; return 0; }
const void* sapp_window_d3d11_render_view(sapp_window win) { (void)win; return 0; }
const void* sapp_window_d3d11_resolve_view(sapp_window win) { (void)win; return 0; }
const void* sapp_window_d3d11_depth_stencil_view(sapp_window win) { (void)win; return 0; }
const void* sapp_window_macos_get_window(sapp_window win) { (void)win; return 0; }
const void* sapp_window_win32_get_hwnd(sapp_window win) { (void)win; return 0; }
const void* sapp_window_x11_get_window(sapp_window win) { (void)win; return 0; }
uint32_t sapp_window_gl_framebuffer(sapp_window win) { (void)win; return 0; }
#if defined(__cplusplus)
} /* extern "C" */
#endif
#endif /* platform select */

#endif /* SOKOL_APP_TC_IMPL_INCLUDED */

/*
    zlib/libpng license

    Copyright (c) 2018 Andre Weissflog
    Copyright (c) 2026 tettou771 (multi-window extension)

    This software is provided 'as-is', without any express or implied warranty.
    In no event will the authors be held liable for any damages arising from the
    use of this software.

    Permission is granted to anyone to use this software for any purpose,
    including commercial applications, and to alter it and redistribute it
    freely, subject to the following restrictions:

        1. The origin of this software must not be misrepresented; you must not
        claim that you wrote the original software. If you use this software in a
        product, an acknowledgment in the product documentation would be
        appreciated but is not required.

        2. Altered source versions must be plainly marked as such, and must not
        be misrepresented as being the original software.

        3. This notice may not be removed or altered from any source
        distribution.
*/
