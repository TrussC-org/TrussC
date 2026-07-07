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
    Native multi-window support with per-window vsync ticks. On macOS this
    header additionally IMPLEMENTS the public single-window sokol_app.h API
    (sapp_run, sapp_width, sapp_dpi_scale, ...): the "main window" is simply
    window #0, created from the sapp_desc, and the app's OS run loop is owned
    here. sokol_app.h must then be included WITHOUT its implementation
    (declarations only) in the implementation TU:

        #include "sokol_app.h"        // no SOKOL_IMPL / SOKOL_APP_IMPL here
        #define SOKOL_IMPL
        #define SOKOL_APP_TC_IMPL
        #include "sokol_app_tc.h"     // implements the sapp_* API on macOS
        #include "sokol_gfx.h"
        ...

    The single-window API behaves like sokol_app.h's macOS backend (quit
    flow via windowShouldClose, lazy drawable acquisition in
    sapp_get_swapchain(), RGB10A2 default color format, clipboard, drag&drop,
    cursor control, fullscreen), with one deliberate deviation: calling
    sapp_dpi_scale() before sapp_run() returns the main screen's backing
    scale factor instead of 0.

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
    Others: stubs that return an invalid handle (explicit platform gap);
    Windows (HWND + DXGI flip swapchain + waitable-object ticks) and Linux
    (X11 + shared GL) are the next ports.
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
    const void* mtl_device;     /* macOS: the id<MTLDevice> sokol_gfx renders with */

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

/* native escape hatch (macOS: NSWindow*) */
SOKOL_APP_API_DECL const void* sapp_window_macos_get_window(sapp_window win);

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

#else /* !macOS */
/*== stubs (explicit platform gap; Windows/Linux ports replace these) =======*/
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
const void* sapp_window_macos_get_window(sapp_window win) { (void)win; return 0; }
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
