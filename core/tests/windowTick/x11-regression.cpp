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
namespace trussc::internal { void setExitReasonIfEmpty(const char*) {} }

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
static EGLContext currentContext;
static EGLSurface currentDraw, currentRead;
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
extern "C" EGLBoolean __wrap_eglSwapBuffers(EGLDisplay, EGLSurface) { ++swaps; now += .0664; return EGL_TRUE; }
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
extern "C" int __wrap_XLookupString(XKeyEvent*, char*, int, KeySym* sym, XComposeStatus*) { *sym = XK_a; return 0; }
extern "C" Bool __wrap_XFilterEvent(XEvent*, Window) { return False; }
extern "C" int __wrap_XSendEvent(Display*, Window, Bool, long, XEvent* ev) { sentEvent = *ev; return 1; }
extern "C" char* __wrap_XGetAtomName(Display*, Atom) { ++atomNames; return nullptr; }
extern "C" int __wrap_XFree(void* data) { if (data) ++propertyFrees; std::free(data); return 0; }
extern "C" int __wrap_XGetWindowProperty(Display*, Window, Atom, long offset, long length, Bool,
    Atom type, Atom* actual, int* format, unsigned long* count, unsigned long* after, unsigned char** data) {
    ++propertyCalls; requestedLength = length;
    *actual = wrongType ? type + 1 : type;
    *format = wrongFormat ? 16 : ((stateProperty || typeList) ? 32 : 8);
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
        *after = propertyCalls == 1 || movingProperty == 1 ? 8 : 0;
        if (movingProperty == 3 && propertyCalls == 1) *after = ULONG_MAX;
        if (movingProperty == 2 && propertyCalls > 1) *count = 4;
        *data = (unsigned char*)std::calloc(9, 1);
        std::memcpy(*data, propertyCalls == 1 ? "file:///" : "\r\r\r\r\r\r\r\r", 8);
    } else if (stateProperty) {
        *count = 2; *after = property.empty() ? 0 : 4;
        auto* record = (unsigned long*)std::calloc(2, sizeof(unsigned long));
        record[0] = NormalState; *data = (unsigned char*)record;
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
    if (level == 3) { ++notices; noticeMessage = message; }
}
static void event(const sapp_event* ev) {
    if (ev->type == SAPP_EVENTTYPE_QUIT_REQUESTED && cancelQuit) { ++quitEvents; sapp_cancel_quit(); }
    if (ev->type == SAPP_EVENTTYPE_FOCUSED || ev->type == SAPP_EVENTTYPE_KEY_DOWN ||
        ev->type == SAPP_EVENTTYPE_MOUSE_DOWN) inputAt = now;
    if (ev->type == SAPP_EVENTTYPE_MOUSE_DOWN || ev->type == SAPP_EVENTTYPE_MOUSE_UP ||
        ev->type == SAPP_EVENTTYPE_MOUSE_ENTER || ev->type == SAPP_EVENTTYPE_MOUSE_SCROLL) {
        ++mouseEvents;
        if (_sapp_tc.mouse_locked) check("locked button/wheel/enter reports anchor", ev->mouse_x == 10 && ev->mouse_y == 20);
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
    main.is_main = main.ready = true; main.xwin = 1; main.drawable = (EGLSurface)1;
    main.dpi_scale = 1; main.fb_width = main.fb_height = 64;
    main.desc.event_cb = _sapp_tc_main_event_tramp;
    _sapp_tc.display = display; _sapp_tc.windows = windows; _sapp_tc.window_slots = 1;
    _sapp_tc.ctx = (EGLContext)1; _sapp_tc.egl_display = (EGLDisplay)1;
    _sapp_tc.app.main = &main; _sapp_tc.app.init_called = true;
    _sapp_tc.app.desc.event_cb = event; _sapp_tc.app.desc.frame_cb = frame;
    _sapp_tc.app.desc.swap_interval = 4; _sapp_tc.app.desc.logger.func = logger;
    _sapp_tc.refresh_period = .0166;
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
static void offerTypes(Window window) {
    XEvent ev{}; ev.type = ClientMessage; ev.xclient.window = window; ev.xclient.format = 32;
    ev.xclient.message_type = _sapp_tc.xdnd.XdndEnter;
    ev.xclient.data.l[0] = 2; ev.xclient.data.l[1] = (5L << 24) | 1;
    _sapp_tc_x11_process_event(&ev);
    ev.xclient.message_type = _sapp_tc.xdnd.XdndPosition;
    _sapp_tc_x11_process_event(&ev);
    _sapp_tc_x11_process_event(&ev);
}
int main() {
    auto* display = (Display*)std::calloc(1, sizeof(*(_XPrivDisplay)nullptr));
    _sapp_tc_window_t main{}; _sapp_tc_window_t* windows[] = { &main };
    reset(main, windows, display); inject = true; _sapp_tc_x11_run_loop();
    check("production loop waits on X11 fd", polls == 1 && timeouts[0] >= 61 && timeouts[0] <= 62);
    check("dispatched input admits next main frame within 2ms", inputAt > 0 && nextFrame >= inputAt && nextFrame - inputAt < .002);
    std::printf("input-to-frame latency: %.3f ms\n", (nextFrame - inputAt) * 1000);
    for (int type : { KeyPress, ButtonPress }) {
        reset(main, windows, display); inject = true; injectedType = type; _sapp_tc_x11_run_loop();
        check("main key/button at 10ms re-admits", inputAt > 0 && nextFrame - inputAt < .002);
    }
    _sapp_tc_window_t secondary{}; secondary.ready = true; secondary.xwin = 2;
    secondary.drawable = (EGLSurface)2; secondary.earliest_next = 20;
    _sapp_tc_window_t* pair[] = { &main, &secondary };
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

    for (sapp_event_type type : { SAPP_EVENTTYPE_KEY_DOWN, SAPP_EVENTTYPE_KEY_UP, SAPP_EVENTTYPE_CHAR,
            SAPP_EVENTTYPE_MOUSE_DOWN, SAPP_EVENTTYPE_MOUSE_UP, SAPP_EVENTTYPE_MOUSE_SCROLL,
            SAPP_EVENTTYPE_MOUSE_MOVE, SAPP_EVENTTYPE_MOUSE_ENTER, SAPP_EVENTTYPE_MOUSE_LEAVE,
            SAPP_EVENTTYPE_FOCUSED, SAPP_EVENTTYPE_UNFOCUSED, SAPP_EVENTTYPE_ICONIFIED,
            SAPP_EVENTTYPE_RESTORED, SAPP_EVENTTYPE_FILES_DROPPED, SAPP_EVENTTYPE_CLIPBOARD_PASTED }) {
        _sapp_tc.main_input_delivered = false;
        _sapp_tc_x11_app_event(&main, type);
        check("eligible delivered main event marks batch", _sapp_tc.main_input_delivered);
    }
    for (sapp_event_type type : { SAPP_EVENTTYPE_RESIZED, SAPP_EVENTTYPE_SUSPENDED, SAPP_EVENTTYPE_RESUMED }) {
        _sapp_tc.main_input_delivered = false;
        _sapp_tc_x11_app_event(&main, type);
        check("non-input main callback leaves batch unmarked", !_sapp_tc.main_input_delivered);
    }
    _sapp_tc.main_input_delivered = false;
    _sapp_tc.app.init_called = false;
    _sapp_tc_x11_app_event(&main, SAPP_EVENTTYPE_FOCUSED);
    check("suppressed pre-init main event leaves batch unmarked", !_sapp_tc.main_input_delivered);
    _sapp_tc.app.init_called = true;
    _sapp_tc.app.desc.event_cb = nullptr;
    _sapp_tc_x11_app_event(&main, SAPP_EVENTTYPE_FOCUSED);
    check("absent main callback leaves batch unmarked", !_sapp_tc.main_input_delivered);
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
    uriIndex = _sapp_tc.app.drop_max_types;
    offerTypes(1);
    check("URI atom beyond prefix is unsupported", _sapp_tc.xdnd.format == None && sentEvent.xclient.data.l[1] == 0 && notices == 2);
    for (int mode = 0; mode < 5; ++mode) {
        wrongType = mode == 0; wrongFormat = mode == 1; excessItems = mode == 2;
        propertyError = mode == 3; nullReply = mode == 4;
        uriIndex = 0; const int freesBefore = propertyFrees;
        offerTypes(1);
        check("invalid type-list gives no types and frees reply",
            _sapp_tc.xdnd.format == None && sentEvent.xclient.data.l[1] == 0 &&
            propertyFrees - freesBefore == (nullReply ? 0 : 1) && notices == 2);
    }
    wrongType = wrongFormat = excessItems = propertyError = nullReply = false;
    offeredCount = _sapp_tc.app.drop_max_types;
    offerTypes(1);
    check("complete type list accepts without excess Notice", _sapp_tc.xdnd.format == 15 && notices == 2 && atomNames == 0);
    std::free(display);
    return failures ? 1 : 0;
}
