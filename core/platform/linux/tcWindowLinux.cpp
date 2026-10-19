// =============================================================================
// tcWindowLinux.cpp - TrussC adapter for secondary windows (Linux / X11)
// =============================================================================
// The native windowing layer (X11 window + shared GLX context with one
// GLXWindow drawable per window, timer-paced ticks at the measured refresh
// rate, sapp_event conversion with the full sokol_app XKB keytable) lives in
// sokol/sokol_app_tc.h and knows nothing about TrussC. This file maps its C
// API onto TrussC:
//   tick_cb   -> WindowContext switch + per-window dt + update/draw the tree
//   event_cb  -> CoreEvents + App hooks + Node-tree dispatch (same keycode
//                semantics as the main window: key == SAPP_KEYCODE_*)
//   close_cb  -> Window::teardown() (close button, or a close() request)
// All rendering shares the one sokol_gfx context; each window gets its own
// sokol_gl context (same pattern as Fbo). Mirror of platform/win/tcWindowWin.cpp.

#include "TrussC.h"

#if defined(__linux__) && defined(SOKOL_GLCORE) && !defined(__ANDROID__)
#include <X11/Xlib.h>       // XResizeWindow (per-window setSize)
#include <X11/Xutil.h>      // XSizeHints (win_gravity for setPosition)
#include <X11/Xatom.h>      // XA_CARDINAL (_NET_FRAME_EXTENTS)
#include "sokol_app_tc.h"   // declarations only (impl lives in sokol_impl.cpp)

using namespace trussc;

namespace {

struct AdapterState {
    sapp_window win{0};
    // Last size asked for (createWindow / setSize), logical pixels. The WM may
    // override the actual size (auto-maximize), so it is kept to restore from.
    int requestedWidth = 0;
    int requestedHeight = 0;
};

// EWMH: ask the window manager to add/remove up to two _NET_WM_STATE_* atoms
// via a ClientMessage sent to the root window with SubstructureRedirect.
void sendWmState(Display* dpy, ::Window xwin, bool add,
                 const char* state1, const char* state2 = nullptr) {
    Atom wmState = XInternAtom(dpy, "_NET_WM_STATE", False);
    Atom atom1   = XInternAtom(dpy, state1, False);
    Atom atom2   = state2 ? XInternAtom(dpy, state2, False) : None;
    if (wmState == None || atom1 == None) return;

    XEvent xev = {};
    xev.type = ClientMessage;
    xev.xclient.window = xwin;
    xev.xclient.message_type = wmState;
    xev.xclient.format = 32;
    xev.xclient.data.l[0] = add ? 1 : 0;    // _NET_WM_STATE_ADD / _REMOVE
    xev.xclient.data.l[1] = (long)atom1;
    xev.xclient.data.l[2] = (long)atom2;
    xev.xclient.data.l[3] = 1;               // source indication: normal app
    xev.xclient.data.l[4] = 0;

    XSendEvent(dpy, DefaultRootWindow(dpy), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &xev);
}

// The WM's decoration thickness, [left, right, top, bottom]. Zeroed when the
// property is missing (no decorations, or a WM that does not publish it).
void frameExtents(Display* dpy, ::Window xwin, long out[4]) {
    out[0] = out[1] = out[2] = out[3] = 0;
    Atom prop = XInternAtom(dpy, "_NET_FRAME_EXTENTS", False);
    Atom type = None;
    int format = 0;
    unsigned long count = 0, remaining = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(dpy, xwin, prop, 0, 4, False, XA_CARDINAL, &type, &format,
                           &count, &remaining, &data) != Success || !data) {
        return;
    }
    const long* values = (const long*)data;
    for (unsigned long i = 0; i < count && i < 4; i++) out[i] = values[i];
    XFree(data);
}

// win_gravity decides which corner a move request names. sokol_app asks for
// CenterGravity (it wants resizes to grow around the center), and WMs read that
// literally for moves too: KWin then offsets by half the title bar, so the same
// request lands differently per WM and getPosition() cannot round-trip.
// NorthWestGravity is the one the other platforms already implement -- the
// request names the decorated frame's top-left, like Windows SetWindowPos and
// macOS setFrameTopLeftPoint:.
void pinMoveGravity(Display* dpy, ::Window xwin) {
    XSizeHints hints = {};
    long supplied = 0;
    if (!XGetWMNormalHints(dpy, xwin, &hints, &supplied)) hints = XSizeHints{};
    if (hints.win_gravity == NorthWestGravity && (hints.flags & PWinGravity)) return;
    hints.flags |= PWinGravity;
    hints.win_gravity = NorthWestGravity;
    XSetWMNormalHints(dpy, xwin, &hints);
}

// Swapchain provider handed to WindowContext::acquireSwapchain. On GL the
// "swapchain" is just the default framebuffer of whatever drawable is
// current -- which is this window's during its tick.
sg_swapchain acquireSecondarySwapchain(void* user) {
    auto* st = static_cast<AdapterState*>(user);
    sg_swapchain sc = {};
    if (!st) return sc;
    const int fbw = sapp_window_framebuffer_width(st->win);
    const int fbh = sapp_window_framebuffer_height(st->win);
    if (fbw <= 0 || fbh <= 0) return sc;
    sc.width = fbw;
    sc.height = fbh;
    sc.sample_count = sapp_window_sample_count(st->win);
    sc.color_format = SG_PIXELFORMAT_RGBA8;
    sc.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    sc.gl.framebuffer = sapp_window_gl_framebuffer(st->win);
    return sc;
}

// --- tick: drive this window's update/draw with its context active ---------
void windowTick(sapp_window swin, void* user) {
    trussc::Window* win = static_cast<trussc::Window*>(user);
    // A closed Window may outlive its native side. Never dispatch callbacks
    // or lazily recreate GPU resources after teardown.
    if (!win || !win->isOpen()) return;
    // Frame boundary: a setApp() requested before or during this tick is
    // applied when the scope is entered and left, never inside the tick.
    internal::WindowDispatchScope scope(*win);
    auto& ctx = win->context();

    const int fbw = sapp_window_framebuffer_width(swin);
    const int fbh = sapp_window_framebuffer_height(swin);
    if (fbw <= 0 || fbh <= 0) return;
    ctx.fbWidth = fbw;
    ctx.fbHeight = fbh;
    // Keep target metadata current before update/draw, including resize ticks.
    ctx.swapchainTarget.width = fbw;
    ctx.swapchainTarget.height = fbh;
    ctx.swapchainTarget.sampleCount = sapp_window_sample_count(swin);
    ctx.swapchainTarget.viewport = {0, 0, (float)fbw, (float)fbh, true};
    ctx.dpiScale = sapp_window_dpi_scale(swin);

    // Per-window frame-rate throttle (Window::setFps): the timer keeps pacing at
    // the display's refresh rate; skip the tick cheaply (before beginFrame,
    // before advancing the per-window delta/frame count) when throttled down.
    if (!internal::windowThrottleShouldTick(ctx)) return;

    // Keep a RectNode root in sync with the window's logical size (same
    // contract as the main App on SAPP_EVENTTYPE_RESIZED).
    win->syncRootSize((float)sapp_window_width(swin), (float)sapp_window_height(swin));

    // Per-window delta time: wall-clock between THIS window's processed
    // ticks, so getDeltaTime() inside this window's update/draw is correct
    // even when its display runs at a different refresh rate than the main
    // window's. A long gap (occlusion) yields one large delta, exactly like
    // an event-driven main window.
    {
        auto callNow = std::chrono::high_resolution_clock::now();
        if (!ctx.lastUpdateCallTimeInitialized) {
            ctx.lastUpdateCallTimeInitialized = true;
            ctx.updateDeltaTime = sapp_window_frame_duration(swin);
        } else {
            ctx.updateDeltaTime = std::chrono::duration<double>(callNow - ctx.lastUpdateCallTime).count();
        }
        ctx.lastUpdateCallTime = callNow;
    }

    auto* prev = internal::currentWindowCtx();
    internal::currentWindowCtx() = &ctx;

    // Per-window frame count (Fix 3): advance this window's own tick counter so
    // getFrameCount()/getUpdateCount() report it while this context is active.
    ctx.updateCount++;

    // Own sokol_gl context, created lazily on the first tick (sgl is set up
    // by then). New windows use the current shared budget after any resize.
    if (ctx.swapchainTarget.resources().context.id == SG_INVALID_ID) {
        sgl_context_desc_t cdesc = {};
        cdesc.max_vertices = internal::sglBudget().maxVertices;
        cdesc.max_commands = internal::sglBudget().maxCommands;
        cdesc.color_format = SG_PIXELFORMAT_RGBA8;
        cdesc.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
        cdesc.sample_count = sapp_window_sample_count(swin);
        ctx.swapchainTarget.resources().makeContext(cdesc, "a window");
    }
    sgl_set_context(ctx.swapchainTarget.resources().context);
    sgl_defaults();

    beginFrame();

    {
        // This window's update() is an entry point (#349); its draw is
        // contained at present().
        internal::EntryStackGuard guard(internal::AppEntry::Update);
        win->events().update.notify();
        win->tickTree();
    }

    const Color& cc = win->clearColor_;
    clear(cc.r, cc.g, cc.b, cc.a);
    win->events().draw.notify();
    win->drawTreeNow();

    present();
    {
        // This window's afterFrame listeners are an entry point (#349).
        internal::EntryStackGuard guard(internal::AppEntry::AfterFrame);
        win->events().afterFrame.notify();
    }
    // Drain this window's saveScreenshot() queue while ITS context (and its
    // lastSwapchainDrawable) is current — must run before we restore prev.
    internal::drainPendingScreenshots();

    internal::restoreWindowContext(prev);
}

// --- events: map sapp_event onto CoreEvents / App hooks / tree dispatch ----
// One event is an entry point (#349): the guard restores the stacks while
// this window's context is still current.
void dispatchWindowEvent(const sapp_event* ev, sapp_window swin, trussc::Window* win,
                         internal::WindowContext& ctx) {
    internal::EntryStackGuard guard(internal::AppEntry::Event,
        internal::eventEntryName(ev, ctx.mousePressed && ctx.mouseButton >= 0));

    // Raw listeners can change pointer lock; snapshot this move's reliability.
    const bool recoverButtonsOnMove = internal::mouseMoveHasReliableButtonBits(ev);

    // Raw event pass-through (same hook addons use on the main window)
    win->events().rawEvent.notify(*ev);

    // sapp coords arrive in framebuffer pixels; TrussC windows use logical
    // points (secondary windows have no pixelPerfect mode for now)
    const float dpi = sapp_window_dpi_scale(swin);
    const float scale = (dpi > 0.0f) ? (1.0f / dpi) : 1.0f;
    const bool hasShift = (ev->modifiers & SAPP_MODIFIER_SHIFT) != 0;
    const bool hasCtrl  = (ev->modifiers & SAPP_MODIFIER_CTRL) != 0;
    const bool hasAlt   = (ev->modifiers & SAPP_MODIFIER_ALT) != 0;
    const bool hasSuper = (ev->modifiers & SAPP_MODIFIER_SUPER) != 0;

    auto releaseMouse = [&](int button) {
        const unsigned int bit = internal::mouseButtonBit(button);
        if ((ctx.pressedMouseButtons & bit) == 0) return;
        ctx.pressedMouseButtons &= ~bit;
        // Match the base drag-button semantics independently of release tracking.
        ctx.mouseButton = -1;
        ctx.mousePressed = false;
        MouseEventArgs e;
        e.pos = e.globalPos = Vec2(ctx.lastPointerX, ctx.lastPointerY);
        e.button = button;
        e.shift = hasShift; e.ctrl = hasCtrl; e.alt = hasAlt; e.super = hasSuper;
        e.syncLegacy();
        win->events().mouseReleased.notify(e);
        if (win->getApp()) win->getApp()->mouseReleased(e);
        if (!e.consumed) win->dispatchMouseReleaseToTree(e);
    };

    for (int button = SAPP_MOUSEBUTTON_LEFT; button <= SAPP_MOUSEBUTTON_MIDDLE; ++button) {
        if (recoverButtonsOnMove && (ctx.pressedMouseButtons & internal::mouseButtonBit(button)) && internal::mouseButtonLostOnMove(ev, button))
            releaseMouse(button); // last real position, before updating the move
    }

    switch (ev->type) {
        case SAPP_EVENTTYPE_UNFOCUSED:
            for (int button = SAPP_MOUSEBUTTON_LEFT; button <= SAPP_MOUSEBUTTON_MIDDLE; ++button)
                releaseMouse(button);
            break;
        case SAPP_EVENTTYPE_MOUSE_DOWN: {
            const float x = ev->mouse_x * scale, y = ev->mouse_y * scale;
            ctx.mouseX = ctx.lastPointerX = x; ctx.mouseY = ctx.lastPointerY = y;
            ctx.pressedMouseButtons |= internal::mouseButtonBit(ev->mouse_button);
            ctx.mouseButton = (int)ev->mouse_button;
            ctx.mousePressed = true;
            MouseEventArgs e;
            e.pos = e.globalPos = Vec2(x, y);
            e.button = (int)ev->mouse_button;
            e.shift = hasShift; e.ctrl = hasCtrl; e.alt = hasAlt; e.super = hasSuper;
            e.syncLegacy();
            win->events().mousePressed.notify(e);
            if (win->getApp()) win->getApp()->mousePressed(e);
            if (!e.consumed) win->dispatchMousePressToTree(e);
            break;
        }
        case SAPP_EVENTTYPE_MOUSE_UP: {
            const float x = ev->mouse_x * scale, y = ev->mouse_y * scale;
            ctx.mouseX = ctx.lastPointerX = x; ctx.mouseY = ctx.lastPointerY = y;
            releaseMouse((int)ev->mouse_button);
            break;
        }
        case SAPP_EVENTTYPE_MOUSE_ENTER:
        case SAPP_EVENTTYPE_MOUSE_MOVE: {
            const float x = ev->mouse_x * scale, y = ev->mouse_y * scale;
            ctx.pmouseX = ctx.mouseX; ctx.pmouseY = ctx.mouseY;
            ctx.mouseX = ctx.lastPointerX = x; ctx.mouseY = ctx.lastPointerY = y;
            internal::MouseEventRaw raw;
            raw.pos = raw.globalPos = Vec2(x, y);
            raw.delta = raw.globalDelta = Vec2(ev->mouse_dx * scale, ev->mouse_dy * scale);
            raw.shift = hasShift; raw.ctrl = hasCtrl; raw.alt = hasAlt; raw.super = hasSuper;
            if (ctx.mousePressed && ctx.mouseButton >= 0) {
                raw.button = ctx.mouseButton;
                MouseDragEventArgs e = internal::toDragArgs(raw);
                win->events().mouseDragged.notify(e);
                if (win->getApp()) win->getApp()->mouseDragged(e);
                raw.consumed = e.consumed;
            } else {
                MouseMoveEventArgs e = internal::toMoveArgs(raw);
                win->events().mouseMoved.notify(e);
                if (win->getApp()) win->getApp()->mouseMoved(e);
                raw.consumed = e.consumed;
            }
            // Node tree: drag goes to the grabbed node, move bubbles from the
            // hit node (same dispatchMouseMove path as the main window).
            if (!raw.consumed) win->dispatchMouseMoveToTree(raw);
            break;
        }
        case SAPP_EVENTTYPE_MOUSE_LEAVE: {
            // Park the cursor offscreen so the next tick's updateHoverState
            // clears this window's hover (hoveredNode lives in ITS context).
            ctx.pmouseX = ctx.mouseX; ctx.pmouseY = ctx.mouseY;
            ctx.mouseX = -1.0f; ctx.mouseY = -1.0f;
            break;
        }
        case SAPP_EVENTTYPE_MOUSE_SCROLL: {
            ScrollEventArgs e;
            e.pos = e.globalPos = Vec2(ctx.mouseX, ctx.mouseY);
            e.scroll = Vec2(ev->scroll_x, ev->scroll_y);
            e.shift = hasShift; e.ctrl = hasCtrl; e.alt = hasAlt; e.super = hasSuper;
            e.syncLegacy();
            win->events().mouseScrolled.notify(e);
            if (win->getApp()) win->getApp()->mouseScrolled(e);
            if (!e.consumed) win->dispatchMouseScrollToTree(e);
            break;
        }
        case SAPP_EVENTTYPE_KEY_DOWN: {
            KeyEventArgs e;
            e.key = ev->key_code;   // SAPP_KEYCODE_*, identical to the main window
            e.isRepeat = ev->key_repeat;
            e.shift = hasShift; e.ctrl = hasCtrl; e.alt = hasAlt; e.super = hasSuper;
            if (!ev->key_repeat) ctx.keysPressed.insert(e.key);
            win->events().keyPressed.notify(e);
            if (win->getApp()) win->getApp()->keyPressed(e);
            if (!e.consumed) win->dispatchKeyPressToTree(e);
            break;
        }
        case SAPP_EVENTTYPE_KEY_UP: {
            KeyEventArgs e;
            e.key = ev->key_code;
            e.isRepeat = false;
            e.shift = hasShift; e.ctrl = hasCtrl; e.alt = hasAlt; e.super = hasSuper;
            ctx.keysPressed.erase(e.key);
            win->events().keyReleased.notify(e);
            if (win->getApp()) win->getApp()->keyReleased(e);
            if (!e.consumed) win->dispatchKeyReleaseToTree(e);
            break;
        }
        case SAPP_EVENTTYPE_SUSPENDED:
            logNotice("Window") << "second window occluded - skipping frames (no stall)";
            break;
        case SAPP_EVENTTYPE_RESUMED:
            logNotice("Window") << "second window visible again - resuming ticks";
            break;
        default:
            // CHAR / RESIZED / FOCUSED: rawEvent carries them;
            // size sync happens per tick.
            break;
    }
}

void windowEvent(const sapp_event* ev, sapp_window swin, void* user) {
    trussc::Window* win = static_cast<trussc::Window*>(user);
    // A closed Window may outlive its native side. Never dispatch callbacks
    // or lazily recreate GPU resources after teardown.
    if (!win || !win->isOpen()) return;
    auto& ctx = win->context();
    auto* prev = internal::currentWindowCtx();
    internal::restoreWindowContext(&ctx);
    {
        internal::WindowDispatchScope scope(*win);   // frame boundary, as in windowTick
        dispatchWindowEvent(ev, swin, win, ctx);
    }
    internal::restoreWindowContext(prev);
}

void windowClosed(sapp_window swin, void* user) {
    (void)swin;
    trussc::Window* win = static_cast<trussc::Window*>(user);
    // The backend's safe point (close button, or a close() request landing):
    // never inside this window's tick or events.
    if (win) trussc::internal::WindowRequestAccess::teardown(*win);   // native + app state
}

} // namespace

// ---------------------------------------------------------------------------
// Window methods (Linux implementations)
// ---------------------------------------------------------------------------
namespace trussc {

// Internal headless-test seam. The caller makes win's context current; an
// invalid native handle uses the backend's unit DPI without creating a window.
namespace internal {
void dispatchSecondaryWindowEventForTests(const sapp_event* ev, Window& win) {
    dispatchWindowEvent(ev, sapp_window{0}, &win, win.context());
}
} // namespace internal

Window::~Window() { teardown(); }

// A request (see tcWindow.h): the backend calls close_cb -> teardown() at its
// next safe point, outside this window's tick and events.
void Window::close() {
    auto* st = static_cast<AdapterState*>(native_);
    if (!st || closeRequested_) return;
    closeRequested_ = true;
    sapp_window_request_close(st->win);
}

void Window::teardown() {
    // The window's exit() is an entry point (#349).
    internal::EntryStackGuard guard(internal::AppEntry::Exit);
    auto* st = static_cast<AdapterState*>(native_);
    if (!st) return;
    native_ = nullptr;             // re-entrancy guard (close_cb)
    closeRequested_ = false;
    ctx_.teardownTargets();
    ctx_.acquireSwapchain = nullptr;
    ctx_.acquireSwapchainUser = nullptr;
    sapp_destroy_window(st->win);
    // Frees this window's sgl pipelines and its sgl context (#317).
    ctx_.swapchainTarget.resources().release();
    delete st;
    // The window's own exit event: per-window resource holders (e.g. the
    // tcxImGui per-window manager) tear down here, while this window's
    // CoreEvents is still alive.
    events_.exit.notify();
    endApp();
}

void Window::setTitle(const std::string& title) {
    title_ = title;
    auto* st = static_cast<AdapterState*>(native_);
    if (st) sapp_window_set_title(st->win, title.c_str());
}

void Window::setSize(int width, int height) {
    auto* st = static_cast<AdapterState*>(native_);
    if (!st) return;
    Display* dpy = (Display*)(void*)sapp_x11_get_display();
    ::Window xwin = (::Window)(uintptr_t)sapp_window_x11_get_window(st->win);
    if (!dpy || !xwin) return;
    // Logical -> physical pixels via this window's DPI scale (X11 sizes are in
    // device pixels; sapp_window_width() reports the logical size).
    float s = ctx_.dpiScale > 0.0f ? ctx_.dpiScale : 1.0f;
    XResizeWindow(dpy, xwin, (unsigned)(width * s), (unsigned)(height * s));
    XFlush(dpy);
    st->requestedWidth = width;
    st->requestedHeight = height;
}

void Window::setPosition(int x, int y) {
    auto* st = static_cast<AdapterState*>(native_);
    if (!st) return;
    Display* dpy = (Display*)(void*)sapp_x11_get_display();
    ::Window xwin = (::Window)(uintptr_t)sapp_window_x11_get_window(st->win);
    if (!dpy || !xwin) return;
    // Root-window coordinates, unscaled (X11 screen space is device pixels and
    // spans every display, so a monitor left of the primary one has negative x).
    // Note this is a request: a reparenting window manager may adjust or ignore
    // it, and some WMs apply their own placement policy on first map. Call it
    // after the window is mapped for a predictable result.
    //
    // Fullscreen and maximized windows are pinned to their monitor: KWin (and
    // other EWMH WMs) ignore move requests for them, so the move silently did
    // nothing. Maximized is not always our doing -- KWin maximizes a new window
    // that does not fit the monitor it is placed on (e.g. 1920x1080 plus a
    // title bar on a 1920x1080 display, the one holding the focused window),
    // which pinned it there before setPosition() ran. Drop both states, move,
    // then restore fullscreen only (maximize was never requested -- Window has
    // no maximize API). The WM processes the requests in order, so no wait is
    // needed -- and none is possible: right after createWindow() the WM has not
    // written _NET_WM_STATE yet, so the un-maximize is sent unconditionally
    // rather than after checking the property. Un-maximizing restores the WM's
    // own guess of a size (1280x676 on KWin), so the last requested size
    // (createWindow / setSize) is re-applied with the move.
    //
    // x and y name the decorated frame's top-left, the corner Windows and macOS
    // use. On X11 that is win_gravity's job, so pin it before asking (see
    // pinMoveGravity): with sokol_app's CenterGravity the same request lands at
    // a different pixel on KWin than on Mutter.
    pinMoveGravity(dpy, xwin);
    if (fullscreenRequested_) sendWmState(dpy, xwin, false, "_NET_WM_STATE_FULLSCREEN");
    sendWmState(dpy, xwin, false, "_NET_WM_STATE_MAXIMIZED_VERT", "_NET_WM_STATE_MAXIMIZED_HORZ");
    if (st->requestedWidth > 0 && st->requestedHeight > 0) {
        float s = ctx_.dpiScale > 0.0f ? ctx_.dpiScale : 1.0f;
        XMoveResizeWindow(dpy, xwin, x, y, (unsigned)(st->requestedWidth * s),
                          (unsigned)(st->requestedHeight * s));
    } else {
        XMoveWindow(dpy, xwin, x, y);
    }
    if (fullscreenRequested_) sendWmState(dpy, xwin, true, "_NET_WM_STATE_FULLSCREEN");
    XFlush(dpy);
}

IVec2 Window::getPosition() const {
    auto* st = static_cast<AdapterState*>(native_);
    if (!st) return IVec2(-1, -1);
    Display* dpy = (Display*)(void*)sapp_x11_get_display();
    ::Window xwin = (::Window)(uintptr_t)sapp_window_x11_get_window(st->win);
    if (!dpy || !xwin) return IVec2(-1, -1);
    // XTranslateCoordinates gives the CLIENT area's origin in root coordinates,
    // which sits below the title bar. setPosition() names the decorated frame's
    // top-left, so subtract the WM's frame extents to report the same corner and
    // round-trip. A caller that reads back and re-applies -- an app nudging a
    // window -- would otherwise walk it down the screen by one title bar per step.
    ::Window root = DefaultRootWindow(dpy);
    int rx = 0, ry = 0;
    ::Window child = 0;
    if (!XTranslateCoordinates(dpy, xwin, root, 0, 0, &rx, &ry, &child)) {
        return IVec2(-1, -1);
    }
    long extents[4];
    frameExtents(dpy, xwin, extents);
    return IVec2(rx - (int)extents[0], ry - (int)extents[2]);
}

void Window::setFullscreen(bool full) {
    auto* st = static_cast<AdapterState*>(native_);
    if (!st) return;
    Display* dpy = (Display*)(void*)sapp_x11_get_display();
    ::Window xwin = (::Window)(uintptr_t)sapp_window_x11_get_window(st->win);
    if (!dpy || !xwin) return;

    // The WM resizes the window to the monitor; the per-window tick's
    // fbWidth/fbHeight sync + syncRootSize pick up the new size on the next tick.
    sendWmState(dpy, xwin, full, "_NET_WM_STATE_FULLSCREEN");
    XFlush(dpy);
    fullscreenRequested_ = full;
}

bool Window::isFullscreen() const {
    return fullscreenRequested_;
}

int Window::getWidth() const {
    auto* st = static_cast<AdapterState*>(native_);
    return st ? sapp_window_width(st->win) : 0;
}

int Window::getHeight() const {
    auto* st = static_cast<AdapterState*>(native_);
    return st ? sapp_window_height(st->win) : 0;
}

bool Window::isOccluded() const {
    if (auto hook = internal::windowOccludedHookForTests()) return hook(*this);
    auto* st = static_cast<AdapterState*>(native_);
    return st ? sapp_window_occluded(st->win) : false;
}

std::shared_ptr<Window> internal::createSecondaryWindow(const WindowSettings& settings) {
    if (headless::isActive()) {
        logError("Window") << "createWindow(): not available in headless mode";
        return nullptr;
    }
    auto win = std::shared_ptr<Window>(new Window());
    win->title_ = settings.title;
    win->ctx_.swapchainTarget.colorFormat = SG_PIXELFORMAT_RGBA8;
    auto* st = new AdapterState();

    sapp_window_desc d = {};
    d.x = -1;
    d.y = -1;
    d.width = settings.width;
    d.height = settings.height;
    d.title = settings.title.c_str();
    d.borderless = !settings.decorated;
    d.no_high_dpi = !settings.highDpi;
    d.sample_count = settings.sampleCount;  // NOTE: forced to the shared fbconfig's count on GLX
    d.tick_cb = &windowTick;
    d.event_cb = &windowEvent;
    d.close_cb = &windowClosed;
    d.user_data = win.get();
    st->win = sapp_create_window(&d);
    if (st->win.id == 0) {
        delete st;
        logError("Window") << "createWindow(): native window creation failed";
        return nullptr;
    }

    st->requestedWidth = settings.width;
    st->requestedHeight = settings.height;
    win->ctx_.swapchainTarget.sampleCount = sapp_window_sample_count(st->win);
    win->ctx_.swapchainTarget.width = sapp_window_framebuffer_width(st->win);
    win->ctx_.swapchainTarget.height = sapp_window_framebuffer_height(st->win);
    win->native_ = st;
    win->ctx_.acquireSwapchain = &acquireSecondarySwapchain;
    win->ctx_.acquireSwapchainUser = st;
    return win;
}

} // namespace trussc

#endif // __linux__ && SOKOL_GLCORE && !__ANDROID__
