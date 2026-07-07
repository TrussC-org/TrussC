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
    Additional native windows next to the single window owned by sokol_app.h.
    The API shape follows floooh's multi-window sketch (sokol PR #437 /
    issue #229): a small `sapp_window` handle, a `sapp_window_desc`, and
    window-parameterized query functions. What is NEW compared to #437 is the
    frame model, and it is the load-bearing idea:

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

@class _sapp_tc_view;
@class _sapp_tc_win_delegate;

typedef struct _sapp_tc_window_t {
    uint32_t win_id;
    sapp_window_desc desc;
    NSWindow* window;
    _sapp_tc_view* view;
    _sapp_tc_win_delegate* delegate;
    CAMetalLayer* layer;
    CADisplayLink* link;
    id<MTLTexture> depth_tex;           /* depth-stencil, drawable-sized */
    id<MTLTexture> msaa_tex;            /* MSAA color (sample_count > 1) */
    id<CAMetalDrawable> frame_drawable; /* valid during one tick only */
    int fb_width, fb_height;            /* aux/drawable size (pixels) */
    int win_width, win_height;          /* logical points (last seen) */
    float dpi_scale;
    double frame_duration;              /* measured tick interval */
    double last_tick_time;              /* CADisplayLink timestamp */
    bool occluded;
    bool in_tick;
} _sapp_tc_window_t;

#define _SAPP_TC_MAX_WINDOWS (32)

static struct {
    bool keytable_valid;
    sapp_keycode keycodes[SAPP_MAX_KEYCODES];
    uint32_t next_id;
    _sapp_tc_window_t* windows[_SAPP_TC_MAX_WINDOWS];
} _sapp_tc;

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
    ev->window_width = w->win_width;
    ev->window_height = w->win_height;
    ev->framebuffer_width = w->fb_width;
    ev->framebuffer_height = w->fb_height;
    sapp_window handle = { w->win_id };
    w->desc.event_cb(ev, handle, w->desc.user_data);
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
                 NSTrackingInVisibleRect)
        owner:self userInfo:nil];
    [self addTrackingArea:area];
    [super updateTrackingAreas];
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

@implementation _sapp_tc_win_delegate
- (void)windowWillClose:(NSNotification*)n {
    _sapp_tc_window_t* w = self.w;
    if (w && w->desc.close_cb) {
        sapp_window handle = { w->win_id };
        w->desc.close_cb(handle, w->desc.user_data);
    }
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
