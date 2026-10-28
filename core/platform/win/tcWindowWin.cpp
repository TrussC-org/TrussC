// =============================================================================
// tcWindowWin.cpp - TrussC adapter for secondary windows (Windows)
// =============================================================================
// The native windowing layer (HWND + D3D11 flip swapchain + per-window frame
// latency waitable ticks, occlusion-tested presents, sapp_event conversion
// with the full sokol_app scancode table) lives in sokol/sokol_app_tc.h and
// knows nothing about TrussC. This file maps its C API onto TrussC:
//   tick_cb   -> WindowContext switch + per-window dt + update/draw the tree
//   event_cb  -> CoreEvents + App hooks + Node-tree dispatch (same keycode
//                semantics as the main window: key == SAPP_KEYCODE_*)
//   close_cb  -> Window::teardownSecondary() (close button, or a close() request)
// All rendering shares the one sokol_gfx context; each window gets its own
// sokol_gl context (same pattern as Fbo). Mirror of platform/mac/tcWindowMac.mm.

#include "TrussC.h"

#if defined(_WIN32)
#include <windows.h>        // HWND / SetWindowPos (per-window setSize)
#include "sokol_app_tc.h"   // declarations only (impl lives in sokol_impl.cpp)

using namespace trussc;

namespace {

struct AdapterState {
    sapp_window win{0};
    // Borderless-fullscreen save/restore (Window::setFullscreen).
    bool    savedValid = false;
    LONG_PTR savedStyle = 0;
    LONG_PTR savedExStyle = 0;
    RECT    savedRect{0, 0, 0, 0};   // window rect (screen coords) before fullscreen
};

bool bindAdapter(uint32_t id, void* user) noexcept {
    auto* window = static_cast<trussc::Window*>(user);
    if (!window || !window->native_) return false;
    static_cast<AdapterState*>(window->native_)->win = {id};
    return internal::bindNativeWindow(id, window);
}

// --- tick: drive this window's update/draw with its context active ---------
void windowTickImpl(sapp_window swin, void* user) {
    Window* win = static_cast<Window*>(user);
    // A closed Window may outlive its native side. Never dispatch callbacks
    // or lazily recreate GPU resources after teardown.
    if (!win || !win->isOpen()) return;
    // Frame boundary: a setApp() requested before or during this tick is
    // applied when the scope is entered and left, never inside the tick.
    auto& ctx = win->context();
    internal::refreshWindowMetadata(*win);

    // Native gates still forbid drawable acquisition/presentation. Deliver the
    // update decision to the same policy as visible ticks before size admission.
    if (win->isOccluded()) {
        auto* previous = internal::currentWindowCtx();
        internal::currentWindowCtx() = &ctx;
        struct Restore { internal::WindowContext* previous;
            ~Restore() { internal::currentWindowCtx() = previous; }
        } restore{previous};
        internal::WindowDispatchScope scope(*win);
        internal::syncWindowAppSize(*win);
        internal::runWindowTick(internal::windowClockNow(),
            sapp_window_frame_duration(swin), false, nullptr, true);
        return;
    }

    internal::WindowDispatchScope scope(*win);

    if (ctx.fbWidth <= 0 || ctx.fbHeight <= 0) return;
    auto* previous = internal::currentWindowCtx();
    internal::restoreWindowContext(&ctx);
    struct RestoreContext {
        internal::WindowContext* previous;
        ~RestoreContext() {
            internal::TeardownSteps steps;
            steps.run("window tick context restoration", [&] { internal::restoreWindowContext(previous); });
            internal::currentWindowCtx() = previous;
        }
    } restore{previous};
    internal::syncWindowAppSize(*win);
    if (!internal::runWindowTick(internal::windowClockNow(), sapp_window_frame_duration(swin)))
        sapp_tc_window_skip_present(swin);

}

void windowTick(sapp_window swin, void* user) noexcept {
    internal::windowCallback([&] { windowTickImpl(swin, user); });
}

// --- events: map sapp_event onto CoreEvents / App hooks / tree dispatch ----
// One event is an entry point (#349): the guard restores the stacks while
// this window's context is still current.
void dispatchWindowEvent(const sapp_event* ev, sapp_window swin, trussc::Window* win,
                         internal::WindowContext& ctx) {
    internal::EntryStackGuard guard(internal::AppEntry::Event,
        internal::eventEntryName(ev, ctx.mousePressed && ctx.mouseButton >= 0));

    internal::WindowCallbackTimeScope callbackTime;

    // Raw listeners can change pointer lock; snapshot this move's reliability.
    const bool recoverButtonsOnMove = internal::mouseMoveHasReliableButtonBits(ev);

    // Keep this event's metadata revision through raw-listener reentry.
    const float scale = internal::appPointerScale(ctx.dpiScale);
    // Raw event pass-through (same hook addons use on the main window)
    win->events().rawEvent.notify(*ev);

    // sapp pointer coordinates arrive in framebuffer pixels. Secondary coordinates
    // use main's unit scale: pixels in pixelPerfect mode, logical points otherwise.
    // Delta derivation and enter/leave handling retain their existing differences.
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
            } else {
                MouseMoveEventArgs e = internal::toMoveArgs(raw);
                win->events().mouseMoved.notify(e);
            }
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
            break;
        }
        case SAPP_EVENTTYPE_KEY_DOWN: {
            KeyEventArgs e;
            e.key = ev->key_code;   // SAPP_KEYCODE_*, identical to the main window
            e.isRepeat = ev->key_repeat;
            e.shift = hasShift; e.ctrl = hasCtrl; e.alt = hasAlt; e.super = hasSuper;
            if (!ev->key_repeat) ctx.keysPressed.insert(e.key);
            win->events().keyPressed.notify(e);
            break;
        }
        case SAPP_EVENTTYPE_KEY_UP: {
            KeyEventArgs e;
            e.key = ev->key_code;
            e.isRepeat = false;
            e.shift = hasShift; e.ctrl = hasCtrl; e.alt = hasAlt; e.super = hasSuper;
            ctx.keysPressed.erase(e.key);
            win->events().keyReleased.notify(e);
            break;
        }
        case SAPP_EVENTTYPE_RESIZED:
            internal::syncWindowAppSize(*win);
            break;
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

void windowEventImpl(const sapp_event* ev, sapp_window swin, void* user) {
    Window* win = static_cast<Window*>(user);
    // A closed Window may outlive its native side. Never dispatch callbacks
    // or lazily recreate GPU resources after teardown.
    if (!win || !win->isOpen()) return;
    auto& ctx = win->context();
    internal::refreshWindowMetadata(*win);

    auto* prev = internal::currentWindowCtx();
    internal::restoreWindowContext(&ctx);
    struct RestoreEventContext {
        internal::WindowContext* previous;
        ~RestoreEventContext() {
            internal::TeardownSteps steps;
            steps.run("window event context restoration", [&] { internal::restoreWindowContext(previous); });
            internal::currentWindowCtx() = previous;
        }
    } restore{prev};
    {
        internal::WindowDispatchScope scope(*win);   // frame boundary, as in windowTick
        internal::syncWindowAppSize(*win, true);
        dispatchWindowEvent(ev, swin, win, ctx);
    }
}

void windowEvent(const sapp_event* ev, sapp_window swin, void* user) noexcept {
    internal::windowCallback([&] { windowEventImpl(ev, swin, user); });
}

void windowClosed(sapp_window swin, void* user) noexcept {
    (void)swin;
    Window* win = static_cast<Window*>(user);
    // The backend's safe point (close button, or a close() request landing):
    // never inside this window's tick or events.
    if (win) trussc::internal::WindowRequestAccess::teardown(*win);   // native + app state
}

} // namespace

// ---------------------------------------------------------------------------
// Window methods (Windows implementations)
// ---------------------------------------------------------------------------
namespace trussc {

// Internal headless-test seam. The caller makes win's context current; an
// invalid native handle uses the backend's unit DPI without creating a window.
namespace internal {
void dispatchSecondaryWindowEventForTests(const sapp_event* ev, Window& win) {
    observeWindowVisibility(win.context(), ev->type);
    dispatchWindowEvent(ev, sapp_window{0}, &win, win.context());
}
// Display-test seam: use the native handle, DPI conversion and frame boundary.
void dispatchSecondaryDisplayEventForTests(const sapp_event* ev, Window& win) {
    auto* st = static_cast<AdapterState*>(win.native_);
    if (st) windowEvent(ev, st->win, &win);
}

} // namespace internal


// A request (see tcWindow.h): the backend calls close_cb -> teardown() at its
// next safe point, outside this window's tick and events.
void Window::closeSecondary() {
    auto* st = static_cast<AdapterState*>(native_);
    if (!st || closeRequested_) return;
    closeRequested_ = true;
    sapp_window_request_close(st->win);
}

void Window::teardownSecondary() noexcept {
    auto* st = static_cast<AdapterState*>(native_);
    if (!st) return;
    internal::TeardownSteps steps;
    internal::WindowRequestAccess::unbind(*this);
    native_ = nullptr;             // guard before entry diagnostics can reenter
    closeRequested_ = false;
    ctx_.targetsClosed = true;
    // Stack restoration must not prevent any native/App teardown step.
    std::optional<internal::EntryStackGuard> guard;
    steps.run("exit entry guard", [&] { guard.emplace(internal::AppEntry::Exit); });
    steps.run("native target cleanup", [&] { ctx_.teardownTargets(); });
    ctx_.acquireSwapchain = nullptr;
    ctx_.acquireSwapchainUser = nullptr;
    steps.run("native window destruction", [&] { sapp_destroy_window(st->win); });
    // Frees this window's sgl pipelines and its sgl context (#317).
    steps.run("native GPU resource release", [&] { ctx_.swapchainTarget.resources().release(); });
    delete st;
    // The window's own exit event: per-window resource holders (e.g. the
    // tcxImGui per-window manager) tear down here, while this window's
    // CoreEvents is still alive.
    endApp();
}

void Window::setSecondaryTitle(const std::string& title) {
    title_ = title;
    auto* st = static_cast<AdapterState*>(native_);
    if (st) sapp_window_set_title(st->win, title.c_str());
}

void Window::setSecondaryPosition(int x, int y) {
    auto* st = static_cast<AdapterState*>(native_);
    if (st) internal::setWindowPositionNative(sapp_window_win32_get_hwnd(st->win), x, y);
}
IVec2 Window::getSecondaryPosition() const {
    auto* st = static_cast<AdapterState*>(native_);
    return st ? internal::windowPositionNative(sapp_window_win32_get_hwnd(st->win)) : IVec2(-1, -1);
}

void Window::setSecondarySize(double width, double height, bool /*pixelSize*/) {
    auto* st = static_cast<AdapterState*>(native_);
    if (!st) return;
    internal::setWindowSizeWin32(sapp_window_win32_get_hwnd(st->win), width, height);
}

void Window::setSecondaryFullscreen(bool full) {
    auto* st = static_cast<AdapterState*>(native_);
    if (!st) return;
    HWND hwnd = (HWND)(void*)sapp_window_win32_get_hwnd(st->win);
    if (!hwnd) return;
    if (full == fullscreenRequested_) return;   // already in the requested state

    if (full) {
        // Save the current windowed style + rect, then switch to a borderless
        // popup covering the window's current monitor (physical pixels; the DXGI
        // swapchain resizes to the client area, and windowTick's fbWidth/fbHeight
        // sync + syncRootSize pick up the new size).
        st->savedStyle   = GetWindowLongPtrW(hwnd, GWL_STYLE);
        st->savedExStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        GetWindowRect(hwnd, &st->savedRect);
        st->savedValid = true;

        HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        if (!GetMonitorInfoW(mon, &mi)) return;
        const RECT& r = mi.rcMonitor;   // full monitor rect in physical pixels

        SetWindowLongPtrW(hwnd, GWL_STYLE,
            (st->savedStyle & ~(WS_OVERLAPPEDWINDOW)) | WS_POPUP);
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE,
            st->savedExStyle & ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE));
        SetWindowPos(hwnd, HWND_TOP, r.left, r.top,
                     r.right - r.left, r.bottom - r.top,
                     SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        fullscreenRequested_ = true;
    } else {
        // Restore the saved windowed style + rect.
        if (st->savedValid) {
            SetWindowLongPtrW(hwnd, GWL_STYLE, st->savedStyle);
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE, st->savedExStyle);
            const RECT& r = st->savedRect;
            SetWindowPos(hwnd, nullptr, r.left, r.top,
                         r.right - r.left, r.bottom - r.top,
                         SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOZORDER | SWP_SHOWWINDOW);
        }
        fullscreenRequested_ = false;
    }
}

bool Window::isSecondaryFullscreen() const {
    return fullscreenRequested_;
}

std::shared_ptr<Window> internal::createSecondaryWindow(const WindowSettings& settings) {
    if (headless::isActive()) {
        logError("Window") << "createWindow(): not available in headless mode";
        return nullptr;
    }
    auto win = std::shared_ptr<Window>(new Window());
    win->title_ = settings.title;
    win->ctx_.swapchainTarget.colorFormat = SG_PIXELFORMAT_BGRA8;
    auto* st = new AdapterState();

    sapp_window_desc d = {};
    d.x = -1;
    d.y = -1;
    d.width = settings.width;
    d.height = settings.height;
    if (pixelPerfectMode()) {
        d._tc_pixel_width = settings.width > 0 ? settings.width : 0;
        d._tc_pixel_height = settings.height > 0 ? settings.height : 0;
    }
    d.title = settings.title.c_str();
    d.borderless = !settings.decorated;
    d.no_high_dpi = !settings.highDpi;
    d.sample_count = settings.sampleCount;
    // (the shared D3D11 device is owned by sokol_app_tc.h; nothing to pass)
    d.tick_cb = &windowTick;
    d.event_cb = &windowEvent;
    d.close_cb = &windowClosed;
    d.user_data = win.get();
    d.bind_cb = bindAdapter;
    win->native_ = st;
    st->win = sapp_create_window(&d);
    if (st->win.id == 0) {
        win->native_ = nullptr;
        internal::WindowRequestAccess::unbind(*win);
        delete st;
        logError("Window") << "createWindow(): native window creation failed";
        return nullptr;
    }

    internal::refreshWindowMetadata(*win);
    return win;
}

} // namespace trussc

#endif // _WIN32
