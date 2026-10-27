// Complete native constructor and dispatcher with real Mesa/X11 surfaces.
#define SOKOL_NO_ENTRY
#define SOKOL_APP_TC_IMPL
#include "sokol_app_tc.h"
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <initializer_list>
namespace trussc::internal { void setExitReasonIfEmpty(const char*) {} }
namespace {
int failures, callbacks, liveWindows, presents;
bool failSurface, noHighDpi, didInitialize, coveredFirst;
int launchHidden;
Window firstXwin;
template<class P> concept FourPropertyArguments = requires(P value) {
    _sapp_tc_x11_get_window_property(Window{}, Atom{}, Atom{}, value);
};
static_assert(!FourPropertyArguments<unsigned char**>);
sapp_event received;
uintptr_t presentedDrawable;
void check(const char* name, bool passed) {
    std::printf("nativeWindow: %s: %s\n", name, passed ? "PASS" : "FAIL");
    if (!passed) ++failures;
}
void onEvent(const sapp_event* event, sapp_window, void*) { received = *event; ++callbacks; }
void initialized() {
    didInitialize = true;
    auto* first = _sapp_tc.app.main;
    if (coveredFirst) {
        check("covered launch initializes before first frame", sapp_frame_count() == 0 && first->occluded);
        check("initial hidden admission is descriptor data", first->draw_while_hidden == (launchHidden >= 0));
        return;
    }
    check("hidden signals trusted by default", first->trust_hidden_signals);
    sapp_window_desc desc = first->desc;
    desc.event_cb = onEvent; desc.tick_cb = nullptr;
    const sapp_window second = sapp_create_window(&desc);
    auto* other = _sapp_tc_lookup(second);
    check("secondary created", other != nullptr);
    if (!other) { sapp_quit(); return; }
    check("secondary trusts hidden signals by default", other->trust_hidden_signals);
    check("initial drawing flags come from caller data", first->draw_while_hidden && !other->draw_while_hidden);
    const float expected = noHighDpi ? 1.0f : _sapp_tc.dpi / 96.0f;
    std::printf("nativeWindow: Xft.dpi=%.1f high_dpi=%d main=%.2f/%dx%d secondary=%.2f/%dx%d\n",
        _sapp_tc.dpi, !noHighDpi, first->dpi_scale, first->fb_width, first->fb_height,
        other->dpi_scale, other->fb_width, other->fb_height);
    check("initial dimensions precede first frame", sapp_frame_count() == 0 &&
        first->fb_width == int(101 * expected + 0.5f) && first->fb_height == int(77 * expected + 0.5f));
    check("initial scales and dimensions agree", first->dpi_scale == expected && other->dpi_scale == expected &&
        first->fb_width == other->fb_width && first->fb_height == other->fb_height &&
        first->win_width == other->win_width && first->win_height == other->win_height);
    for (auto* target : {first, other}) {
        target->desc.event_cb = onEvent; target->events_enabled = nullptr;
        target->mouse_x = 30; target->mouse_y = 40; target->mouse_dx = 2; target->mouse_dy = 3;
        for (auto type : {SAPP_EVENTTYPE_KEY_DOWN, SAPP_EVENTTYPE_KEY_UP}) {
            callbacks = 0;
            _sapp_tc_x11_key_event(target, type, SAPP_KEYCODE_INVALID, false, 0);
            check("invalid key delivered once without pointer metadata", callbacks == 1 &&
                received.key_code == SAPP_KEYCODE_INVALID && received.mouse_x == 0 && received.mouse_y == 0 &&
                received.mouse_dx == 0 && received.mouse_dy == 0);
        }
        XEvent event{}; event.type = LeaveNotify; event.xcrossing.window = target->xwin;
        event.xcrossing.x = 100; event.xcrossing.y = 200; callbacks = 0;
        _sapp_tc_x11_process_event(&event);
        check("leave preserves last position", callbacks == 1 && received.mouse_x == 30 && received.mouse_y == 40);
#if defined(SOKOL_GLES3)
        _sapp_tc.mouse_locked = true; _sapp_tc.lock_owner = target;
        event = {}; event.type = ButtonPress; event.xbutton.window = target->xwin;
        event.xbutton.button = Button4; event.xbutton.x = 100; event.xbutton.y = 200;
        callbacks = 0; _sapp_tc_x11_process_event(&event);
#else
        // GLX has no pointer-lock state; exercise the identical wheel builder
        // with an anchor and stored raw deltas as the locked EGL dispatch does.
        callbacks = 0; _sapp_tc_x11_scroll_event(target, 0, 1, 0);
#endif
        check("locked wheel retains anchor and has zero movement deltas for either window", callbacks == 1 &&
            received.type == SAPP_EVENTTYPE_MOUSE_SCROLL && received.mouse_x == 30 && received.mouse_y == 40 &&
            received.mouse_dx == 0 && received.mouse_dy == 0 && target->mouse_dx == 2 && target->mouse_dy == 3);
#if defined(SOKOL_GLES3)
        _sapp_tc.mouse_locked = false; _sapp_tc.lock_owner = nullptr;
#endif
        for (bool trust : {true, false}) {
            target->trust_hidden_signals = trust;
            target->occluded = target->iconified = false;
            event = {}; event.type = VisibilityNotify; event.xvisibility.window = target->xwin;
            for (int visibility : {VisibilityFullyObscured, VisibilityUnobscured}) {
                event.xvisibility.state = visibility; callbacks = 0;
                _sapp_tc_x11_process_event(&event);
                check("visibility input respects trust for either window", callbacks == int(trust) &&
                    sapp_window_occluded({target->win_id}) == (trust && visibility == VisibilityFullyObscured));
            }
            event = {}; event.type = PropertyNotify; event.xproperty.window = target->xwin;
            event.xproperty.atom = _sapp_tc.WM_STATE; event.xproperty.state = PropertyNewValue;
            for (long state : {IconicState, NormalState}) {
                long values[2] = {state, 0};
                XChangeProperty(_sapp_tc.display, target->xwin, _sapp_tc.WM_STATE, _sapp_tc.WM_STATE,
                    32, PropModeReplace, (unsigned char*)values, 2);
                callbacks = 0; _sapp_tc_x11_process_event(&event);
                check("minimize input respects trust for either window", callbacks == int(trust) &&
                    sapp_window_occluded({target->win_id}) == (trust && state == IconicState));
            }
        }
        target->trust_hidden_signals = true;
    }
    for (auto* target : {first, other}) {
        sapp_tc_window_set_fullscreen({target->win_id}, true);
        check("fullscreen intent belongs to the addressed native window",
            sapp_tc_window_fullscreen_phase({target->win_id}) != 0 &&
            sapp_tc_window_fullscreen_phase({target == first ? other->win_id : first->win_id}) == 0);
        sapp_tc_window_toggle_fullscreen({target->win_id});
        check("native toggle reverses intent", sapp_tc_window_fullscreen_phase({target->win_id}) == 0);
    }
    first->in_tick = false; // exercise native admission directly, outside the legacy client
    for (auto* target : {first, other}) {
        target->desc.tick_cb = [](sapp_window, void*) { ++callbacks; };
        for (bool trust : {true, false}) for (bool iconified : {false, true}) {
            target->trust_hidden_signals = trust;
            target->iconified = iconified; target->occluded = !iconified;
            for (bool allow : {false, true}) {
                sapp_tc_window_draw_while_hidden({target->win_id}, allow);
                callbacks = presents = 0;
                _sapp_tc_x11_tick(target);
                bool shouldPresent = !trust || allow;
#if defined(SOKOL_GLCORE)
                if (trust && iconified) shouldPresent = false;
#endif
                check("hidden drawing and trust are independent for either window", callbacks == 1 &&
                    presents == int(shouldPresent) && (!presents || presentedDrawable == (uintptr_t)target->drawable));
                check("all visibility readers use effective state", sapp_window_occluded({target->win_id}) == trust &&
                    _sapp_tc_native_iconified(target) == (trust && iconified) &&
                    _sapp_tc_native_occluded(target) == (trust && !iconified) &&
                    (target != first || sapp_window_occluded({0}) == trust));
                auto* peer = target == first ? other : first;
                check("hidden signal switch is per window", !sapp_window_occluded({peer->win_id}) && peer->trust_hidden_signals);
            }
        }
        target->occluded = target->iconified = false;
        target->trust_hidden_signals = true;
    }
    presents = 0; sapp_skip_present();
    _sapp_tc_x11_tick(other); _sapp_tc_x11_tick(first);
    check("first skip is isolated", presents == 1 && presentedDrawable == (uintptr_t)other->drawable);
    presents = 0; sapp_tc_window_skip_present(second);
    _sapp_tc_x11_tick(first); _sapp_tc_x11_tick(other);
    check("secondary skip is isolated", presents == 1 && presentedDrawable == (uintptr_t)first->drawable);
    presents = 0; sapp_tc_window_skip_present({0});
    _sapp_tc_x11_tick(other); _sapp_tc_x11_tick(first);
    check("zero skip suppresses only first presentation", presents == 1 && presentedDrawable == (uintptr_t)other->drawable);
    first->in_tick = true;
    const float savedDpi = _sapp_tc.dpi;
    _sapp_tc.dpi = 9.6e-7f;
    sapp_window_desc tooLarge = desc;
    tooLarge.no_high_dpi = false;
    tooLarge._tc_pixel_width = 640; tooLarge._tc_pixel_height = 400;
    const auto rejected = sapp_create_window(&tooLarge);
    check("unrepresentable logical size rejected", rejected.id == 0);
    if (rejected.id) sapp_destroy_window(rejected);
    _sapp_tc.dpi = savedDpi;
    const int before = liveWindows;
    failSurface = true;
    check("failed surface rejects creation", sapp_create_window(&desc).id == 0);
    failSurface = false;
    check("failure unwinds native window", liveWindows == before && sapp_window_valid(second));
    _sapp_tc.next_id = UINT32_MAX - 1;
    const auto last = sapp_create_window(&desc);
    check("last identity is valid", last.id == UINT32_MAX);
    if (last.id) sapp_destroy_window(last);
    check("identity exhaustion rejects cleanly", sapp_create_window(&desc).id == 0 && _sapp_tc.next_id == UINT32_MAX && liveWindows == before);
    sapp_destroy_window(second);
    sapp_quit();
}
}
extern "C" Window __real_XCreateWindow(Display*, Window, int, int, unsigned int, unsigned int,
    unsigned int, int, unsigned int, Visual*, unsigned long, XSetWindowAttributes*);
extern "C" Window __wrap_XCreateWindow(Display* d, Window p, int x, int y, unsigned int width, unsigned int height,
    unsigned int border, int depth, unsigned int kind, Visual* visual, unsigned long mask, XSetWindowAttributes* attrs) {
    const auto result = __real_XCreateWindow(d,p,x,y,width,height,border,depth,kind,visual,mask,attrs);
    if (result) { ++liveWindows; if (!firstXwin) firstXwin = result; }
    return result;
}
extern "C" int __real_XMapWindow(Display*, Window);
extern "C" int __wrap_XMapWindow(Display* display, Window window) {
    auto* native = _sapp_tc_lookup_xwin(window);
    check("published window has initial dimensions before mapping", native &&
        native->fb_width > 0 && native->fb_height > 0 && native->win_width > 0 && native->win_height > 0 &&
        native->dpi_scale == (noHighDpi ? 1.0f : _sapp_tc.dpi / 96.0f));
    const int result = __real_XMapWindow(display, window);
    if (coveredFirst && window == firstXwin) native->occluded = true;
    return result;
}
extern "C" int __real_XNextEvent(Display*, XEvent*);
extern "C" int __wrap_XNextEvent(Display* display, XEvent* event) {
    const int result = __real_XNextEvent(display, event);
    if (coveredFirst && !didInitialize && event->type == VisibilityNotify && event->xvisibility.window == firstXwin)
        event->xvisibility.state = VisibilityFullyObscured;
    return result;
}
extern "C" int __real_XDestroyWindow(Display*, Window);
extern "C" int __wrap_XDestroyWindow(Display* display, Window window) {
    --liveWindows; return __real_XDestroyWindow(display,window);
}
#if defined(SOKOL_GLES3)
extern "C" EGLBoolean __real_eglSwapBuffers(EGLDisplay,EGLSurface);
extern "C" EGLBoolean __wrap_eglSwapBuffers(EGLDisplay display,EGLSurface surface) {
    ++presents; presentedDrawable = (uintptr_t)surface; return __real_eglSwapBuffers(display,surface);
}
extern "C" EGLSurface __real_eglCreateWindowSurface(EGLDisplay,EGLConfig,EGLNativeWindowType,const EGLint*);
extern "C" EGLSurface __wrap_eglCreateWindowSurface(EGLDisplay d,EGLConfig c,EGLNativeWindowType w,const EGLint* a) {
    return failSurface ? EGL_NO_SURFACE : __real_eglCreateWindowSurface(d,c,w,a);
}
#else
extern "C" void __real_glXSwapBuffers(Display*,GLXDrawable);
extern "C" void __wrap_glXSwapBuffers(Display* display,GLXDrawable drawable) {
    ++presents; presentedDrawable = (uintptr_t)drawable; __real_glXSwapBuffers(display,drawable);
}
extern "C" GLXWindow __real_glXCreateWindow(Display*,GLXFBConfig,Window,const int*);
extern "C" GLXWindow __wrap_glXCreateWindow(Display* d,GLXFBConfig c,Window w,const int* a) {
    return failSurface ? 0 : __real_glXCreateWindow(d,c,w,a);
}
#endif
int main(int argc, char** argv) {
    if (argc < 2 || std::strcmp(argv[1], "--display")) {
        std::puts("SKIP: nativeWindow --display [--no-high-dpi] requires X11"); return 0;
    }
    noHighDpi = argc > 2 && !std::strcmp(argv[2], "--no-high-dpi");
    coveredFirst = argc > 2 && !std::strncmp(argv[2], "--covered-", 10);
    if (coveredFirst) {
        if (!std::strcmp(argv[2], "--covered-draw")) launchHidden = 1;
        if (!std::strcmp(argv[2], "--covered-pause")) launchHidden = -1;
    }
    sapp_desc desc{}; desc._tc_draw_while_hidden = launchHidden; desc.width = 101; desc.height = 77;
    desc.high_dpi = !noHighDpi; desc.init_cb = initialized;
    if (coveredFirst) desc.frame_cb = []() {
        check("covered first frame follows init", didInitialize && sapp_window_occluded({0}));
        ++callbacks; sapp_quit();
    };
    sapp_run(&desc);
    if (coveredFirst) check("covered first frame presents according to launch data",
        callbacks == 1 && presents == int(launchHidden >= 0));
    check("initial client callback ran", didInitialize);
    check("process cleanup releases every window", liveWindows == 0);
    return failures ? 1 : 0;
}
