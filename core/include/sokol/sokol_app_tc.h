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
    Native multi-window support with per-window vsync ticks. On macOS and
    Windows this header additionally IMPLEMENTS the public single-window
    sokol_app.h API (sapp_run, sapp_width, sapp_dpi_scale, ...): the "main
    window" is simply window #0, created from the sapp_desc, and the app's
    OS run loop is owned here. sokol_app.h must then be included WITHOUT its
    implementation (declarations only) in the implementation TU:

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
    Others: stubs that return an invalid handle (explicit platform gap);
    Linux (X11 + shared GL) is the next port.
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

/* native escape hatches (macOS: NSWindow*; Windows: HWND) */
SOKOL_APP_API_DECL const void* sapp_window_macos_get_window(sapp_window win);
SOKOL_APP_API_DECL const void* sapp_window_win32_get_hwnd(sapp_window win);

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

/* D3D11 / Win32 handles do not exist on macOS */
const void* sapp_window_d3d11_render_view(sapp_window win) { (void)win; return 0; }
const void* sapp_window_d3d11_resolve_view(sapp_window win) { (void)win; return 0; }
const void* sapp_window_d3d11_depth_stencil_view(sapp_window win) { (void)win; return 0; }
const void* sapp_window_win32_get_hwnd(sapp_window win) { (void)win; return 0; }

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

#else /* other platforms */
/*== stubs (explicit platform gap; the Linux/web/mobile ports replace these) */
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
