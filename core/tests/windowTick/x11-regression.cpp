// Deterministic native boundaries around the complete production header/loop.
// Derived from the round-1 review's wake/state probes. No display is required.
#define SOKOL_NO_ENTRY
#define SOKOL_APP_TC_IMPL
#include "sokol_app_tc.h"
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <initializer_list>
namespace trussc::internal { void setExitReasonIfEmpty(const char*) {} }


static long observedState = NormalState;
static bool visibilityProbe;
static int visibilityFailures, visibilityChecks, visibilitySteps;
static sapp_event_type visibilityEvents[16];
static int visibilityEventCount;
static uint32_t visibilityWindow;
static void visibilityCheck(const char* label, bool ok) {
    ++visibilityChecks;
    std::printf("effective-visibility: %s: %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++visibilityFailures;
}
static void visibilityEvent(const sapp_event* ev, sapp_window window, void*) {
    // Bound (test trace): retain at most 16 events; count overflow so exact-sequence checks fail.
    if (visibilityEventCount < 16) visibilityEvents[visibilityEventCount] = ev->type;
    ++visibilityEventCount;
    visibilityCheck("callback targets the observed window", window.id == visibilityWindow);
}
static void effectiveVisibility(Display* display) {
    for (bool mainRole : {true, false}) for (bool trust : {true, false}) {
        _sapp_tc = {};
        _sapp_tc_window_t first{}, second{};
        first.win_id = first.xwin = 1; first.is_main = true;
        second.win_id = second.xwin = 2;
        first.ready = second.ready = true;
        first.trust_hidden_signals = second.trust_hidden_signals = true;
        first.desc.event_cb = second.desc.event_cb = visibilityEvent;
#if defined(SOKOL_GLES3)
        _sapp_tc_window_t* windows[2];
        _sapp_tc.windows = windows;
#endif
        _sapp_tc.windows[0] = &first; _sapp_tc.windows[1] = &second;
        _sapp_tc.display = display; _sapp_tc.window_slots = 2;
        _sapp_tc.app.main = &first; _sapp_tc.WM_STATE = 99;
        auto* target = mainRole ? &first : &second;
        auto* peer = mainRole ? &second : &first;
        target->trust_hidden_signals = trust;
        visibilityWindow = target->win_id; visibilityEventCount = 0;
        visibilityProbe = true;
        std::printf("effective-visibility: window=%u trust=%d\n", visibilityWindow, trust);
        auto observe = [&](const char* label, int type, int value,
                           std::initializer_list<sapp_event_type> expected) {
            ++visibilitySteps;
            const int before = visibilityEventCount;
            target->earliest_next = peer->earliest_next = 37.0;
            XEvent ev{}; ev.type = type;
            if (type == PropertyNotify) {
                observedState = value; ev.xproperty.window = target->xwin;
                ev.xproperty.atom = _sapp_tc.WM_STATE; ev.xproperty.state = PropertyNewValue;
            } else {
                ev.xvisibility.window = target->xwin; ev.xvisibility.state = value;
            }
            _sapp_tc_x11_process_event(&ev);
            bool exact = visibilityEventCount == before + (trust ? int(expected.size()) : 0);
            bool resumed = false;
            int index = before;
            if (trust) for (auto event : expected) {
                exact &= index < 16 && index < visibilityEventCount && visibilityEvents[index] == event;
                ++index; resumed |= event == SAPP_EVENTTYPE_RESUMED;
            }
            visibilityCheck(label, exact);
            visibilityCheck("only effective visibility return clears deadline",
                target->earliest_next == (resumed ? 0.0 : 37.0));
            visibilityCheck("peer observations and deadline remain unchanged",
                !peer->iconified && !peer->occluded && peer->earliest_next == 37.0);
            if (!trust) visibilityCheck("untrusted observations remain ignored",
                !target->iconified && !target->occluded && !_sapp_tc_native_hidden(target));
        };
        // One continuous native observation history per role/trust setting.
        observe("visible minimize: ICONIFIED then SUSPENDED", PropertyNotify, IconicState,
            {SAPP_EVENTTYPE_ICONIFIED, SAPP_EVENTTYPE_SUSPENDED});
        observe("duplicate minimize", PropertyNotify, IconicState, {});
        observe("unobscure while minimized: no RESUMED", VisibilityNotify, VisibilityUnobscured, {});
        observe("duplicate unobscure while minimized", VisibilityNotify, VisibilityUnobscured, {});
        observe("fully obscure while minimized: no second SUSPENDED", VisibilityNotify, VisibilityFullyObscured, {});
        observe("duplicate fully obscure while minimized", VisibilityNotify, VisibilityFullyObscured, {});
        observe("restore while obscured: RESTORED only", PropertyNotify, NormalState, {SAPP_EVENTTYPE_RESTORED});
        observe("duplicate restore while obscured", PropertyNotify, NormalState, {});
        observe("unobscure after restore: RESUMED once", VisibilityNotify, VisibilityUnobscured, {SAPP_EVENTTYPE_RESUMED});
        observe("duplicate visible notification", VisibilityNotify, VisibilityUnobscured, {});
        observe("visible obscure: SUSPENDED once", VisibilityNotify, VisibilityFullyObscured, {SAPP_EVENTTYPE_SUSPENDED});
        observe("duplicate obscured notification", VisibilityNotify, VisibilityFullyObscured, {});
        observe("obscured minimize: ICONIFIED only", PropertyNotify, IconicState, {SAPP_EVENTTYPE_ICONIFIED});
        observe("duplicate obscured minimize", PropertyNotify, IconicState, {});
        observe("unobscure iconic window: no RESUMED", VisibilityNotify, VisibilityUnobscured, {});
        observe("duplicate iconic unobscure", VisibilityNotify, VisibilityUnobscured, {});
        observe("restore while unobscured: RESTORED then RESUMED", PropertyNotify, NormalState,
            {SAPP_EVENTTYPE_RESTORED, SAPP_EVENTTYPE_RESUMED});
        observe("duplicate visible restore", PropertyNotify, NormalState, {});
        observe("partial visibility stays visible", VisibilityNotify, VisibilityPartiallyObscured, {});
        observe("duplicate partial visibility", VisibilityNotify, VisibilityPartiallyObscured, {});
        visibilityCheck("continuous trace has exactly eight trusted events", visibilityEventCount == (trust ? 8 : 0));
        visibilityProbe = false;
    }
    std::printf("effective-visibility summary: %d observations, %d checks, %d failures\n",
        visibilitySteps, visibilityChecks, visibilityFailures);
}

#if defined(SOKOL_GLES3)

static int failures, polls, pending, frames, swaps, owners, logs, mouseEvents, warpX, warpY;
static double now, firstFrame, nextFrame, inputAt, work;
static bool inject, buffered, interrupted, badFd, skip, cancelQuit;
static int quitEvents;
static int timeouts[128];
static int injectedType = FocusIn, eventsRead, floodEvents;
static Window injectedWindow = 1;
static bool flood;
static double nextExpose;
static int movingProperty, propertyFrees, notices, atomNames;
static bool typeList, excessItems, propertyError, nullReply;
static int uriIndex;
static unsigned long offeredCount;
static std::string noticeMessage;
static XEvent sentEvent;
static sapp_event lastEvent, lastKeyDown;
static int callbacks, keyCallbacks, charCallbacks, dropCallbacks;
static KeySym lookupKeysym = XK_a;
static EGLContext currentContext;
static EGLSurface currentDraw, currentRead, presentedSurface;
static std::string property;
static long requestedLength;
static int propertyCalls, propertyDeletes;
static bool stateProperty, wrongType, wrongFormat;
static Window owner;
static void check(const char* label, bool ok) {
    std::printf("x11-regression: %s: %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
extern "C" int __wrap_clock_gettime(clockid_t, timespec* ts) {
    now += .000001; // terminates a mutated busy loop without a real-time dependency
    ts->tv_sec = (time_t)now;
    ts->tv_nsec = (long)((now - ts->tv_sec) * 1e9);
    return 0;
}
extern "C" int __wrap_XPending(Display*) {
    if (flood && now >= nextExpose) pending = 1;
    return pending;
}
extern "C" int __wrap_XFlush(Display*) { return 0; }
extern "C" int __wrap_XNextEvent(Display*, XEvent* ev) {
    --pending; ++eventsRead; *ev = {}; ev->type = injectedType;
    ev->xany.window = injectedWindow;
    if (injectedType == FocusIn || injectedType == FocusOut) ev->xfocus.mode = NotifyNormal;
    if (injectedType == KeyPress || injectedType == KeyRelease) ev->xkey.keycode = 24;
    if (injectedType == ButtonPress || injectedType == ButtonRelease) ev->xbutton.button = Button1;
    if (flood) { nextExpose = 10 + (++floodEvents + 1) * .001; if (floodEvents == 80) sapp_quit(); }
    return 0;
}
extern "C" int __wrap_XGetWindowAttributes(Display*, Window, XWindowAttributes* a) {
    a->width = a->height = 64; return 1;
}
extern "C" int __wrap_poll(pollfd*, nfds_t, int timeout) {
    if (polls >= 128) std::abort();
    timeouts[polls++] = timeout;
    if (interrupted && polls == 1) { now += .010; errno = EINTR; return -1; }
    if (badFd) { errno = EINVAL; return -1; }
    if (flood && now + timeout * .001 >= nextExpose) { now = nextExpose; pending = 1; return 1; }
    if (inject) { now = 10.010; pending = 1; inject = false; return 1; }
    now += timeout * .001; return 0;
}
extern "C" EGLBoolean __wrap_eglMakeCurrent(EGLDisplay, EGLSurface draw, EGLSurface read, EGLContext context) {
    currentDraw = draw; currentRead = read; currentContext = context; return EGL_TRUE;
}
extern "C" EGLContext __wrap_eglGetCurrentContext() { return currentContext; }
extern "C" EGLSurface __wrap_eglGetCurrentSurface(EGLint which) { return which == EGL_DRAW ? currentDraw : currentRead; }
extern "C" EGLDisplay __wrap_eglGetCurrentDisplay() { return currentContext ? (EGLDisplay)1 : EGL_NO_DISPLAY; }
extern "C" EGLBoolean __wrap_eglSwapBuffers(EGLDisplay, EGLSurface surface) { presentedSurface = surface; ++swaps; now += .0664; return EGL_TRUE; }
extern "C" int __wrap_XGrabPointer(Display*, Window, Bool, unsigned int, int, int, Window, Cursor, Time) { return GrabSuccess; }
extern "C" int __wrap_XUngrabPointer(Display*, Time) { return 1; }
extern "C" int __wrap_XWarpPointer(Display*, Window, Window, int, int, unsigned int, unsigned int, int x, int y) {
    warpX = x; warpY = y; return 1;
}
extern "C" int __wrap_XSetSelectionOwner(Display*, Atom, Window window, Time) { owner = window; ++owners; return 1; }
extern "C" Window __wrap_XGetSelectionOwner(Display*, Atom) { return owner; }
extern "C" Atom __wrap_XInternAtom(Display*, const char*, Bool) { return 99; }
extern "C" int __wrap_XConvertSelection(Display*, Atom, Atom, Atom, Window, Time) { return 1; }
extern "C" int __wrap_XDeleteProperty(Display*, Window, Atom) { ++propertyDeletes; return 1; }
extern "C" Bool __wrap_XCheckTypedWindowEvent(Display*, Window window, int, XEvent* ev) {
    *ev = {}; ev->xselection.requestor = window; ev->xselection.property = 3; return True;
}
extern "C" int __wrap_XLookupString(XKeyEvent*, char*, int, KeySym* sym, XComposeStatus*) { *sym = lookupKeysym; return 0; }
extern "C" Bool __wrap_XFilterEvent(XEvent*, Window) { return False; }
extern "C" int __wrap_XSendEvent(Display*, Window, Bool, long, XEvent* ev) { sentEvent = *ev; return 1; }
extern "C" char* __wrap_XGetAtomName(Display*, Atom) { ++atomNames; return nullptr; }
extern "C" int __wrap_XFree(void* data) { if (data) ++propertyFrees; std::free(data); return 0; }
extern "C" int __wrap_XGetWindowProperty(Display*, Window, Atom, long offset, long length, Bool,
    Atom type, Atom* actual, int* format, unsigned long* count, unsigned long* after, unsigned char** data) {
    ++propertyCalls; requestedLength = length;
    *actual = wrongType ? type + 1 : type;
    *format = wrongFormat ? 16 : ((stateProperty || visibilityProbe || typeList) ? 32 : 8);
    if (typeList) {
        const unsigned long n = std::min(offeredCount, (unsigned long)length);
        *count = excessItems ? _sapp_tc.app.drop_max_types + 1 : n;
        *after = (offeredCount - n) * 4;
        auto* atoms = (Atom*)std::calloc(n + 1, sizeof(Atom));
        if (uriIndex >= 0 && (unsigned long)uriIndex < n) atoms[uriIndex] = _sapp_tc.xdnd.text_uri_list;
        *data = (unsigned char*)atoms;
    } else if (movingProperty) {
        if (propertyCalls > 8) { *data = nullptr; return BadValue; }
        *count = 8;
        *after = propertyCalls == 1 || movingProperty == 1 ? 9 : 0;
        if (movingProperty == 3 && propertyCalls == 1) *after = ULONG_MAX;
        if (movingProperty == 2 && propertyCalls > 1) *count = 4;
        *data = (unsigned char*)std::calloc(9, 1);
        std::memcpy(*data, propertyCalls == 1 ? "file:///" : "\r\r\r\r\r\r\r\r", 8);
    } else if (stateProperty || visibilityProbe) {
        *count = 2; *after = property.empty() ? 0 : 4;
        auto* record = (unsigned long*)std::calloc(2, sizeof(unsigned long));
        record[0] = observedState; *data = (unsigned char*)record;
    } else {
        const size_t begin = (size_t)offset * 4;
        if (begin > property.size()) std::abort();
        const size_t bytes = std::min(property.size() - begin, (size_t)length * 4);
        *count = bytes; *after = property.size() - begin - bytes;
        *data = (unsigned char*)std::calloc(bytes + 1, 1);
        std::memcpy(*data, property.data() + begin, bytes);
    }
    if (nullReply) { std::free(*data); *data = nullptr; }
    return propertyError ? BadValue : Success;
}
static void logger(const char*, uint32_t level, uint32_t item, const char* message, uint32_t, const char*, void*) {
    if (item == SAPP_LOGITEM_CLIPBOARD_STRING_TOO_BIG) ++logs;
    if (level == 3 && item == SAPP_LOGITEM_LINUX_X11_DROP_TYPES_TRUNCATED) { ++notices; noticeMessage = message; }
}
static void event(const sapp_event* ev) {
    lastEvent = *ev; ++callbacks;
    if (ev->type == SAPP_EVENTTYPE_KEY_DOWN) lastKeyDown = *ev;
    if (ev->type == SAPP_EVENTTYPE_KEY_DOWN || ev->type == SAPP_EVENTTYPE_KEY_UP) ++keyCallbacks;
    if (ev->type == SAPP_EVENTTYPE_CHAR) ++charCallbacks;
    if (ev->type == SAPP_EVENTTYPE_FILES_DROPPED) ++dropCallbacks;
    if (ev->type == SAPP_EVENTTYPE_QUIT_REQUESTED && cancelQuit) { ++quitEvents; sapp_cancel_quit(); }
    if (ev->type == SAPP_EVENTTYPE_FOCUSED || ev->type == SAPP_EVENTTYPE_KEY_DOWN ||
        ev->type == SAPP_EVENTTYPE_MOUSE_DOWN) inputAt = now;
    if (ev->type == SAPP_EVENTTYPE_MOUSE_DOWN || ev->type == SAPP_EVENTTYPE_MOUSE_UP ||
        ev->type == SAPP_EVENTTYPE_MOUSE_ENTER || ev->type == SAPP_EVENTTYPE_MOUSE_SCROLL) {
        ++mouseEvents;
        if (_sapp_tc.mouse_locked) {
            check("locked button/wheel/enter reports anchor", ev->mouse_x == 10 && ev->mouse_y == 20);
            if (ev->type == SAPP_EVENTTYPE_MOUSE_SCROLL)
                check("locked wheel has zero movement deltas", ev->mouse_dx == 0 && ev->mouse_dy == 0);
        }
    }
}
static void frame() {
    if (flood) { ++frames; sapp_skip_present(); return; }
    if (++frames == 1) {
        firstFrame = now; now += work;
        if (cancelQuit) sapp_request_quit();
        if (buffered) pending = 1;
        if (skip) sapp_skip_present();
    } else {
        nextFrame = now; sapp_skip_present(); sapp_quit();
    }
}
static void reset(_sapp_tc_window_t& main, _sapp_tc_window_t** windows, Display* display) {
    _sapp_tc = {}; main = {};
    main.win_id = 1;
    main.is_main = main.ready = main.trust_hidden_signals = true; main.xwin = 1; main.drawable = (EGLSurface)1;
    main.dpi_scale = 1; main.fb_width = main.fb_height = 64;
    main.desc.event_cb = _sapp_tc_main_event_tramp;
    main.desc.tick_cb = _sapp_tc_main_tick_tramp;
    main.events_enabled = _sapp_tc_app_events_enabled;
    main.swap_interval = 4; main.wake_on_input = true; main.draw_while_hidden = true;
    _sapp_tc.display = display; _sapp_tc.windows = windows; _sapp_tc.window_slots = 1;
    _sapp_tc.ctx = (EGLContext)1; _sapp_tc.egl_display = (EGLDisplay)1;
    _sapp_tc.app.main = &main; _sapp_tc.app.init_called = true;
    _sapp_tc.app.desc.event_cb = event; _sapp_tc.app.desc.frame_cb = frame;
    _sapp_tc.app.desc.swap_interval = 4; _sapp_tc.app.desc.logger.func = logger;
    _sapp_tc.refresh_period = .0166;
    _sapp_tc.keycodes[24] = SAPP_KEYCODE_A;
    lookupKeysym = XK_a; callbacks = keyCallbacks = charCallbacks = dropCallbacks = 0;
    polls = pending = frames = swaps = 0;
    firstFrame = nextFrame = inputAt = 0; now = 10; work = .005;
    inject = buffered = interrupted = badFd = cancelQuit = false; skip = true;
    injectedType = FocusIn; injectedWindow = 1; flood = false; eventsRead = floodEvents = 0;
    nextExpose = 10.001;
    currentContext = (EGLContext)1; currentDraw = currentRead = (EGLSurface)1;
}
static bool parseDrop(const std::string& text) {
    property = text; propertyCalls = 0;
    _sapp_tc_x11_uri_reader reader{};
    reader.window = 1; reader.property = 3; reader.type = 4;
    const bool ok = _sapp_tc_x11_parse_dropped_files_list(&reader);
    if (reader.data) XFree(reader.data);
    return ok;
}
static void offerTypes(Window window, Window source = 2) {
    XEvent ev{}; ev.type = ClientMessage; ev.xclient.window = window; ev.xclient.format = 32;
    ev.xclient.message_type = _sapp_tc.xdnd.XdndEnter;
    ev.xclient.data.l[0] = (long)source; ev.xclient.data.l[1] = (5L << 24) | 1;
    _sapp_tc_x11_process_event(&ev);
    ev.xclient.message_type = _sapp_tc.xdnd.XdndPosition;
    _sapp_tc_x11_process_event(&ev);
    _sapp_tc_x11_process_event(&ev);
}
int main() {
    auto* display = (Display*)std::calloc(1, sizeof(*(_XPrivDisplay)nullptr));
    _sapp_tc_window_t main{}; _sapp_tc_window_t* windows[] = { &main };
    effectiveVisibility(display);
    observedState = NormalState;
    uint32_t counter = UINT32_MAX - 1, identity = 0;
    check("last native identity is issued once", _sapp_tc_next_window_id(&counter, &identity) && identity == UINT32_MAX);
    check("native identity exhaustion never wraps", !_sapp_tc_next_window_id(&counter, &identity) && counter == UINT32_MAX);
    reset(main, windows, display); inject = true; _sapp_tc_x11_run_loop();
    check("production loop waits on X11 fd", polls == 1 && timeouts[0] >= 61 && timeouts[0] <= 62);
    check("dispatched input admits next main frame within 2ms", inputAt > 0 && nextFrame >= inputAt && nextFrame - inputAt < .002);
    std::printf("input-to-frame latency: %.3f ms\n", (nextFrame - inputAt) * 1000);
    for (int type : { KeyPress, ButtonPress }) {
        reset(main, windows, display); inject = true; injectedType = type; _sapp_tc_x11_run_loop();
        check("main key/button at 10ms re-admits", inputAt > 0 && nextFrame - inputAt < .002);
    }
    _sapp_tc_window_t secondary{}; secondary.ready = secondary.trust_hidden_signals = true; secondary.xwin = 2;
    secondary.drawable = (EGLSurface)2; secondary.earliest_next = 20;
    _sapp_tc_window_t* pair[] = { &main, &secondary };
    reset(main, pair, display); _sapp_tc.window_slots = 2;
    main.win_id = 1; secondary.win_id = 2;
    main.desc.tick_cb = secondary.desc.tick_cb = [](sapp_window, void*) { ++frames; };
    for (auto* target : {&main, &secondary}) {
        for (bool trust : {false, true}) for (bool iconified : {false, true}) {
            target->trust_hidden_signals = trust;
            target->iconified = iconified; target->occluded = !iconified;
            for (bool allow : {false, true}) {
                target->draw_while_hidden = allow; frames = swaps = 0;
                _sapp_tc_x11_tick(target);
                check("native hidden admission and trust follow data for either role",
                    frames == 1 && swaps == int(!trust || allow));
                check("untrusted hidden flags report visible", sapp_window_occluded({target->win_id}) == trust &&
                    (target != &main || sapp_window_occluded({0}) == trust));
            }
        }
        target->occluded = target->iconified = false;
        target->trust_hidden_signals = true;
    }
    swaps = 0;
    sapp_skip_present();
    _sapp_tc_x11_tick(&secondary);
    _sapp_tc_x11_tick(&main);
    check("main skip does not suppress secondary present", swaps == 1 && presentedSurface == secondary.drawable);
    swaps = 0;
    sapp_tc_window_skip_present({2});
    _sapp_tc_x11_tick(&main);
    _sapp_tc_x11_tick(&secondary);
    check("secondary skip does not suppress main present", swaps == 1 && presentedSurface == main.drawable);
    secondary.earliest_next = 20;
    for (int type : { Expose, ConfigureNotify, FocusIn, PropertyNotify }) {
        reset(main, pair, display); _sapp_tc.window_slots = 2;
        inject = true; injectedType = type; injectedWindow = type == FocusIn ? 999 : 2;
        _sapp_tc_x11_run_loop();
        check("secondary/unknown/property event leaves main deadline intact",
            eventsRead == 1 && inputAt == 0 && nextFrame - firstFrame >= .0664 && nextFrame - firstFrame < .068);
    }
    reset(main, pair, display); _sapp_tc.window_slots = 2;
    flood = true; work = 0; injectedType = Expose; injectedWindow = 2; _sapp_tc_x11_run_loop();
    check("80 secondary Exposes at 1ms retain two main frames", floodEvents == 80 && frames == 2 && now < 10.081);
    std::printf("Expose flood: %d events, %d main frames in %.3f ms\n", floodEvents, frames, (now - 10) * 1000);
    reset(main, windows, display); buffered = true; _sapp_tc_x11_run_loop();
    check("buffered input bypasses poll and admits main", polls == 0 && inputAt > 0 && nextFrame - inputAt < .002);
    reset(main, windows, display); interrupted = true; _sapp_tc_x11_run_loop();
    check("EINTR recomputes remaining production deadline", polls == 2 && timeouts[0] == 62 && timeouts[1] == 52);
    check("idle wait accounts for callback work", nextFrame - firstFrame >= .0664 && nextFrame - firstFrame < .068);
    reset(main, windows, display); work = .1; _sapp_tc_x11_run_loop();
    check("work beyond period adds no wait", polls == 0 && nextFrame - firstFrame >= .1);
    reset(main, windows, display); skip = false; _sapp_tc_x11_run_loop();
    check("blocking presentation supplies pacing", swaps == 1 && polls == 0);
    reset(main, windows, display); cancelQuit = true; _sapp_tc_x11_run_loop();
    check("quit request can be cancelled before a later ordered quit", quitEvents == 1 && frames == 2 && !_sapp_tc.app.quit_requested);
    reset(main, windows, display); pending = 1; _sapp_tc_x11_wait_until(now + .1);
    check("wait helper respects Xlib buffered input", polls == 0);
    pending = 0; badFd = true; _sapp_tc_x11_wait_until(now + .1);
    check("non-EINTR error returns to loop", polls == 1);

    // Exercise the shared production dispatcher for main and secondary focus.
    for (Window window : { Window(1), Window(2) }) {
        for (int type : { FocusIn, FocusOut }) {
            for (int mode : { NotifyNormal, NotifyWhileGrabbed }) {
                reset(main, pair, display); _sapp_tc.window_slots = 2;
                secondary.desc.event_cb = [](const sapp_event* ev, sapp_window, void*) { event(ev); };
                const sapp_event_type expected = type == FocusIn ? SAPP_EVENTTYPE_FOCUSED : SAPP_EVENTTYPE_UNFOCUSED;
                XEvent ev{}; ev.type = type; ev.xfocus.window = window; ev.xfocus.mode = mode;
                ev.xfocus.detail = NotifyNonlinear;
                _sapp_tc_x11_process_event(&ev);
                check("Nonlinear focus delivers once to the addressed window", callbacks == 1 && lastEvent.type == expected);
                check("real focus re-admits only EGL main", main.input_delivered == (window == 1));
                main.input_delivered = false;
                for (int pointerType : { type == FocusIn ? FocusOut : FocusIn, type }) {
                    ev.type = pointerType; ev.xfocus.detail = NotifyPointer;
                    _sapp_tc_x11_process_event(&ev);
                    check("pointer focus adds no callback or main admission", callbacks == 1 &&
                        lastEvent.type == expected && !main.input_delivered);
                }
                ev.type = type; ev.xfocus.detail = NotifyAncestor;
                _sapp_tc_x11_process_event(&ev);
                check("Ancestor focus still delivers and admits only main", callbacks == 2 &&
                    lastEvent.type == expected && main.input_delivered == (window == 1));
            }
            for (int mode : { NotifyGrab, NotifyUngrab }) {
                reset(main, pair, display); _sapp_tc.window_slots = 2;
                XEvent ev{}; ev.type = type; ev.xfocus.window = window;
                ev.xfocus.mode = mode; ev.xfocus.detail = NotifyNonlinear;
                _sapp_tc_x11_process_event(&ev);
                check("grab-cycle focus remains ignored", callbacks == 0 && !main.input_delivered);
            }
        }
    }
    reset(main, windows, display);
    main.mouse_x = 10; main.mouse_y = 20; sapp_lock_mouse(true);
    XEvent pointerOut{}; pointerOut.type = FocusOut; pointerOut.xfocus.window = 1;
    pointerOut.xfocus.mode = NotifyNormal; pointerOut.xfocus.detail = NotifyPointer;
    _sapp_tc_x11_process_event(&pointerOut);
    check("pointer FocusOut retains EGL main mouse unlock without admission", !_sapp_tc.mouse_locked &&
        warpX == 10 && warpY == 20 && callbacks == 0 && !main.input_delivered);

    reset(main, windows, display);
    mouseEvents = 0;
    main.mouse_x = 10; main.mouse_y = 20; main.mouse_pos_valid = true; sapp_lock_mouse(true);
    main.mouse_dx = 2; main.mouse_dy = -3; // most recent raw XI2 deltas
    for (int type : { ButtonPress, ButtonRelease, EnterNotify, ButtonPress }) {
        XEvent ev{}; ev.type = type; ev.xbutton.window = 1; ev.xbutton.x = 40; ev.xbutton.y = 50;
        ev.xbutton.button = mouseEvents == 3 ? Button4 : Button1;
        _sapp_tc_x11_process_event(&ev);
    }
    check("absolute events leave raw deltas alone", main.mouse_dx == 2 && main.mouse_dy == -3 && mouseEvents == 4);
    sapp_lock_mouse(false);
    check("unlock warps to saved anchor", warpX == 10 && warpY == 20);

    reset(main, windows, display);
    main.mouse_x = 30; main.mouse_y = 40; main.mouse_dx = 2; main.mouse_dy = 3;
    main.mouse_pos_valid = true;
    for (int type : { FocusIn, FocusOut, KeyPress, KeyRelease }) {
        XEvent ev{}; ev.type = type; ev.xany.window = 1;
        if (type == KeyPress || type == KeyRelease) ev.xkey.keycode = 24;
        _sapp_tc_x11_process_event(&ev);
        check("focus/key dispatch has no pointer metadata", lastEvent.mouse_x == 0 &&
            lastEvent.mouse_y == 0 && lastEvent.mouse_dx == 0 && lastEvent.mouse_dy == 0);
    }
    check("KEY_DOWN has no pointer metadata", lastKeyDown.key_code == SAPP_KEYCODE_A &&
        lastKeyDown.mouse_x == 0 && lastKeyDown.mouse_y == 0 && lastKeyDown.mouse_dx == 0 && lastKeyDown.mouse_dy == 0);
    check("positive key has a real mapping and CHAR is delivered", keyCallbacks == 2 && charCallbacks == 1 && lastEvent.key_code == SAPP_KEYCODE_A);
    _sapp_tc.app.clipboard_enabled = true;
    _sapp_tc.keycodes[24] = SAPP_KEYCODE_V;
    XEvent paste{}; paste.type = KeyPress; paste.xkey.window = 1; paste.xkey.keycode = 24;
    paste.xkey.state = ControlMask; lookupKeysym = NoSymbol;
    _sapp_tc_x11_process_event(&paste);
    check("clipboard notification has no pointer metadata", lastEvent.type == SAPP_EVENTTYPE_CLIPBOARD_PASTED &&
        lastEvent.mouse_x == 0 && lastEvent.mouse_y == 0 && lastEvent.mouse_dx == 0 && lastEvent.mouse_dy == 0);
    for (int type : { KeyPress, KeyRelease }) {
        XEvent ev{}; ev.type = type; ev.xkey.window = 1; ev.xkey.keycode = 200;
        lookupKeysym = 0x1008ff12; callbacks = 0; main.input_delivered = false;
        _sapp_tc_x11_process_event(&ev);
        check("unmapped non-text key is delivered", callbacks == 1 && main.input_delivered && lastEvent.key_code == SAPP_KEYCODE_INVALID);
    }
    XEvent text{}; text.type = KeyPress; text.xkey.window = 1; text.xkey.keycode = 200;
    lookupKeysym = XK_a; callbacks = 0; main.input_delivered = false;
    _sapp_tc_x11_process_event(&text);
    check("unmapped text key delivers KEY_DOWN and CHAR", callbacks == 2 &&
        lastEvent.type == SAPP_EVENTTYPE_CHAR && main.input_delivered);
    for (bool locked : { false, true }) {
        main.mouse_x = 30; main.mouse_y = 40; main.mouse_dx = 2; main.mouse_dy = 3;
        _sapp_tc.mouse_locked = locked;
        XEvent ev{}; ev.type = LeaveNotify; ev.xcrossing.window = 1; ev.xcrossing.x = 101; ev.xcrossing.y = 42;
        _sapp_tc_x11_process_event(&ev);
        check("leave retains last position and deltas", lastEvent.type == SAPP_EVENTTYPE_MOUSE_LEAVE &&
            lastEvent.mouse_x == 30 && lastEvent.mouse_y == 40 &&
            lastEvent.mouse_dx == 2 && lastEvent.mouse_dy == 3);
    }
    _sapp_tc.mouse_locked = false;

    for (sapp_event_type type : { SAPP_EVENTTYPE_KEY_DOWN, SAPP_EVENTTYPE_KEY_UP, SAPP_EVENTTYPE_CHAR,
            SAPP_EVENTTYPE_MOUSE_DOWN, SAPP_EVENTTYPE_MOUSE_UP, SAPP_EVENTTYPE_MOUSE_SCROLL,
            SAPP_EVENTTYPE_MOUSE_MOVE, SAPP_EVENTTYPE_MOUSE_ENTER, SAPP_EVENTTYPE_MOUSE_LEAVE,
            SAPP_EVENTTYPE_FOCUSED, SAPP_EVENTTYPE_UNFOCUSED, SAPP_EVENTTYPE_ICONIFIED,
            SAPP_EVENTTYPE_RESTORED, SAPP_EVENTTYPE_FILES_DROPPED, SAPP_EVENTTYPE_CLIPBOARD_PASTED }) {
        main.input_delivered = false;
        _sapp_tc_x11_app_event(&main, type);
        check("eligible delivered main event marks batch", main.input_delivered);
    }
    for (sapp_event_type type : { SAPP_EVENTTYPE_RESIZED, SAPP_EVENTTYPE_SUSPENDED, SAPP_EVENTTYPE_RESUMED }) {
        main.input_delivered = false;
        _sapp_tc_x11_app_event(&main, type);
        check("non-input main callback leaves batch unmarked", !main.input_delivered);
    }
    main.input_delivered = false;
    _sapp_tc.app.init_called = false;
    _sapp_tc_x11_app_event(&main, SAPP_EVENTTYPE_FOCUSED);
    check("suppressed pre-init main event leaves batch unmarked", !main.input_delivered);
    _sapp_tc.app.init_called = true;
    _sapp_tc.app.desc.event_cb = nullptr;
    _sapp_tc_x11_app_event(&main, SAPP_EVENTTYPE_FOCUSED);
    check("absent main callback leaves batch unmarked", !main.input_delivered);
    _sapp_tc.app.desc.event_cb = event;

    char clipboard[8]{};
    _sapp_tc.app.clipboard_enabled = true; _sapp_tc.app.clipboard = clipboard; _sapp_tc.app.clipboard_size = sizeof clipboard;
    _sapp_tc.UTF8_STRING = 4;
    sapp_set_clipboard_string("old"); owners = logs = 0;
    sapp_set_clipboard_string("123456789");
    check("oversize set truncates prefix, logs and takes ownership", std::strcmp(clipboard, "1234567") == 0 && owners == 1 && logs == 1 && owner == 1);
    sapp_set_clipboard_string("123456\xc3\xa9");
    check("truncation matches old byte-prefix contract", std::memcmp(clipboard, "123456\xc3", 8) == 0);
    owner = 2; property = "123456789"; propertyCalls = 0;
    const char* result = sapp_get_clipboard_string();
    check("clipboard acquisition bounded and excess rejects/logs/deletes", requestedLength == 2 && propertyCalls == 1 && result == nullptr && logs == 3 && propertyDeletes == 1);
    property = "paste";
    check("bounded clipboard accepts fitting value", std::strcmp(sapp_get_clipboard_string(), "paste") == 0);
    wrongType = true; check("clipboard rejects wrong type", sapp_get_clipboard_string() == nullptr); wrongType = false;
    wrongFormat = true; check("clipboard rejects wrong format", sapp_get_clipboard_string() == nullptr); wrongFormat = false;
    stateProperty = true; property.clear();
    check("WM_STATE requests two elements", _sapp_tc_x11_get_window_state(&main) == NormalState && requestedLength == 2);
    property = "extra";
    check("WM_STATE retains two-element prefix of longer record", _sapp_tc_x11_get_window_state(&main) == NormalState && requestedLength == 2);
    stateProperty = false;
    char paths[2 * 8]{};
    _sapp_tc.app.drop_buffer = paths; _sapp_tc.app.drop_max_files = 2; _sapp_tc.app.drop_max_path_length = 8;
    check("URI reads bounded chunks and preserves percent decoding across replies",
        parseDrop("file:///a%20b\r\nfile:///c\r\nfile:///ignored\r\n") && requestedLength == 2 &&
        propertyCalls == 4 && _sapp_tc.app.drop_num_files == 2 && std::strcmp(paths, "/a b") == 0 && std::strcmp(paths + 8, "/c") == 0);
    check("oversize URI path rejects and clears retained paths",
        !parseDrop("file:///123456789\r\n") && _sapp_tc.app.drop_num_files == 0 && paths[0] == 0);
    check("URI percent escape can straddle acquisition boundary",
        parseDrop("file://%2Fa\r\n") && _sapp_tc.app.drop_num_files == 1 && std::strcmp(paths, "/a") == 0);
    check("URI continuation preserves ignored CR without a new size limit",
        parseDrop("file:///a" + std::string(64, '\r') + "\n") && _sapp_tc.app.drop_num_files == 1 && std::strcmp(paths, "/a") == 0);
    wrongFormat = true;
    check("URI malformed reply rejects and clears", !parseDrop("file:///a\n") && paths[0] == 0);
    wrongFormat = false;
    for (int mode : { 1, 2, 3 }) {
        movingProperty = mode;
        check("changing/overflowing URI extent rejects within initial snapshot replies",
            !parseDrop("") && propertyCalls <= 2 && _sapp_tc.app.drop_num_files == 0 && paths[0] == 0);
    }
    movingProperty = 0;

    _sapp_tc.app.drop_enabled = true;
    _sapp_tc.xdnd.XdndEnter = 11; _sapp_tc.xdnd.XdndPosition = 12;
    _sapp_tc.xdnd.XdndLeave = 16; _sapp_tc.xdnd.XdndDrop = 17;
    _sapp_tc.xdnd.XdndFinished = 18; _sapp_tc.xdnd.XdndSelection = 19; _sapp_tc.xdnd.XdndActionCopy = 20;
    _sapp_tc.xdnd.version = 5; _sapp_tc.xdnd.source = 2;
    for (int mode : { 1, 2, 0, 4 }) {
        movingProperty = mode == 4 ? 0 : mode;
        wrongFormat = mode == 0; property = "file:///a\n";
        propertyCalls = dropCallbacks = 0;
        main.input_delivered = false;
        XEvent ev{}; ev.type = SelectionNotify; ev.xselection.requestor = 1;
        ev.xselection.property = 19; ev.xselection.target = 15;
        _sapp_tc_x11_process_event(&ev);
        const bool good = mode == 4;
        check("SelectionNotify reports final outcome in Finished flags and action", sentEvent.xclient.message_type == 18 &&
            sentEvent.xclient.data.l[1] == (good ? 1 : 0) && sentEvent.xclient.data.l[2] == (good ? 20 : None));
        check("drop delivery and retained paths match final outcome", good ?
            (dropCallbacks == 1 && main.input_delivered && lastEvent.mouse_x == 0 && lastEvent.mouse_y == 0 &&
                lastEvent.mouse_dx == 0 && lastEvent.mouse_dy == 0) :
            (propertyCalls <= 2 && dropCallbacks == 0 && !main.input_delivered && _sapp_tc.app.drop_num_files == 0 && paths[0] == 0));
    }
    movingProperty = 0; wrongFormat = false;
    struct DropCase {
        const char* payload;
        int retainedFiles;
        const char* retainedPath;
        const char* secondPath = "";
    };
    for (const DropCase& test : {
        DropCase{ "file", 0, "" },
        DropCase{ "file://", 0, "" },
        DropCase{ "file:///a", 0, "" },
        DropCase{ "file://\r\n", 0, "" },
        DropCase{ "file://\r\nfile://\r\n", 0, "" },
        DropCase{ "file://\r\nfile:///a\r\n", 1, "/a" },
        DropCase{ "file:///a\r\nfile://\r\nfile:///b\r\n", 2, "/a", "/b" },
        DropCase{ "file://\r\nfile:///a\r\nfile://\r\nfile:///b\r\nfile://\r\nfile:///ignored\r\n", 2, "/a", "/b" },
        DropCase{ "file:///a\r\n", 1, "/a" },
    }) {
        property = test.payload;
        propertyCalls = callbacks = dropCallbacks = 0;
        sentEvent = {};
        main.input_delivered = false;
        XEvent ev{}; ev.type = SelectionNotify; ev.xselection.requestor = 1;
        ev.xselection.property = 19; ev.xselection.target = 15;
        _sapp_tc_x11_process_event(&ev);
        const bool good = test.retainedFiles > 0;
        check("URI content SelectionNotify callback count and main admission", callbacks == (good ? 1 : 0) &&
            dropCallbacks == (good ? 1 : 0) && main.input_delivered == good);
        check("URI content SelectionNotify retains the parser's completed-entry policy",
            _sapp_tc.app.drop_num_files == test.retainedFiles &&
            (test.retainedFiles == 0 || std::strcmp(paths, test.retainedPath) == 0) &&
            (test.retainedFiles < 2 || std::strcmp(paths + _sapp_tc.app.drop_max_path_length, test.secondPath) == 0));
        check("URI content SelectionNotify Finished flags and action", sentEvent.xclient.message_type == 18 &&
            sentEvent.xclient.data.l[1] == (good ? 1 : 0) && sentEvent.xclient.data.l[2] == (good ? 20 : None));
    }
    _sapp_tc.xdnd.XdndStatus = 13; _sapp_tc.xdnd.XdndTypeList = 14; _sapp_tc.xdnd.text_uri_list = 15;
    typeList = true; offeredCount = _sapp_tc.app.drop_max_types + 1;
    uriIndex = _sapp_tc.app.drop_max_types - 1;
    propertyCalls = propertyFrees = notices = atomNames = 0;
    offerTypes(1);
    check("type-list request is bounded and last retained URI atom accepted",
        requestedLength == _sapp_tc.app.drop_max_types && _sapp_tc.xdnd.format == 15 && (sentEvent.xclient.data.l[1] & 1));
    check("one Notice per drag with counts only and no atom names",
        notices == 1 && noticeMessage == "XDND type counts: 1024 retained limit, 4 bytes remaining" && atomNames == 0);
    check("type-list success reply freed", propertyCalls == 1 && propertyFrees == 1);
    XEvent lifecycle{}; lifecycle.type = ClientMessage; lifecycle.xclient.window = 1; lifecycle.xclient.format = 32;
    lifecycle.xclient.data.l[0] = 2; lifecycle.xclient.message_type = _sapp_tc.xdnd.XdndLeave;
    _sapp_tc_x11_process_event(&lifecycle);
    offerTypes(1);
    check("same drag Leave and re-entry logs once", notices == 1);
    _sapp_tc.windows = pair; _sapp_tc.window_slots = 2;
    lifecycle.xclient.message_type = _sapp_tc.xdnd.XdndLeave;
    _sapp_tc_x11_process_event(&lifecycle);
    offerTypes(2);
    check("same drag across main and secondary logs once", notices == 1);
    lifecycle.xclient.message_type = _sapp_tc.xdnd.XdndDrop; lifecycle.xclient.window = 2;
    _sapp_tc_x11_process_event(&lifecycle);
    typeList = false; property = "file:///a\n"; propertyCalls = 0;
    XEvent done{}; done.type = SelectionNotify; done.xselection.requestor = 2;
    done.xselection.property = 19; done.xselection.target = 15;
    _sapp_tc_x11_process_event(&done);
    typeList = true;
    offerTypes(1);
    check("completed drop permits a new drag Notice", notices == 2);
    offerTypes(1, 3);
    check("different source permits a new Notice", notices == 3);
    uriIndex = _sapp_tc.app.drop_max_types;
    offerTypes(1, 3);
    check("URI atom beyond prefix is unsupported", _sapp_tc.xdnd.format == None && sentEvent.xclient.data.l[1] == 0 && notices == 3);
    for (int mode = 0; mode < 5; ++mode) {
        wrongType = mode == 0; wrongFormat = mode == 1; excessItems = mode == 2;
        propertyError = mode == 3; nullReply = mode == 4;
        uriIndex = 0; const int freesBefore = propertyFrees;
        offerTypes(1, 3);
        check("invalid type-list gives no types and frees reply",
            _sapp_tc.xdnd.format == None && sentEvent.xclient.data.l[1] == 0 &&
            propertyFrees - freesBefore == (nullReply ? 0 : 1) && notices == 3);
    }
    wrongType = wrongFormat = excessItems = propertyError = nullReply = false;
    offeredCount = _sapp_tc.app.drop_max_types;
    offerTypes(1, 3);
    check("complete type list accepts without excess Notice", _sapp_tc.xdnd.format == 15 && notices == 3 && atomNames == 0);
    std::free(display);
    return failures || visibilityFailures ? 1 : 0;
}

#else
// GLX runs the same production dispatcher and continuous visibility sequences.
extern "C" int __wrap_XGetWindowProperty(Display*, Window, Atom, long, long, Bool,
    Atom type, Atom* actual, int* format, unsigned long* count, unsigned long* after, unsigned char** data) {
    *actual = type; *format = 32; *count = 2; *after = 0;
    auto* record = (unsigned long*)std::calloc(2, sizeof(unsigned long));
    record[0] = observedState; *data = (unsigned char*)record;
    return Success;
}
int main() {
    auto* display = (Display*)std::calloc(1, sizeof(*(_XPrivDisplay)nullptr));
    effectiveVisibility(display);
    std::free(display);
    return visibilityFailures ? 1 : 0;
}
#endif
