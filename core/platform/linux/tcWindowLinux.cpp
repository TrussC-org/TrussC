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
//   close_cb  -> Window::close()
// All rendering shares the one sokol_gfx context; each window gets its own
// sokol_gl context (same pattern as Fbo). Mirror of platform/win/tcWindowWin.cpp.

#include "TrussC.h"

#if defined(__linux__) && defined(SOKOL_GLCORE) && !defined(__ANDROID__)
#include "sokol_app_tc.h"   // declarations only (impl lives in sokol_impl.cpp)

using namespace trussc;

namespace {

struct AdapterState {
    sapp_window win{0};
};

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
    Window* win = static_cast<Window*>(user);
    if (!win) return;
    auto& ctx = win->context();

    const int fbw = sapp_window_framebuffer_width(swin);
    const int fbh = sapp_window_framebuffer_height(swin);
    if (fbw <= 0 || fbh <= 0) return;
    ctx.fbWidth = fbw;
    ctx.fbHeight = fbh;
    ctx.dpiScale = sapp_window_dpi_scale(swin);
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

    auto* prev = internal::currentWindowCtx;
    internal::currentWindowCtx = &ctx;

    // Own sokol_gl context, created lazily on the first tick (sgl is set up
    // by then). Same lifecycle caveats as Fbo contexts on sgl resize.
    if (ctx.swapchainTarget.context.id == SG_INVALID_ID) {
        sgl_context_desc_t cdesc = {};
        cdesc.max_vertices = 65536;
        cdesc.max_commands = 16384;
        cdesc.color_format = SG_PIXELFORMAT_RGBA8;
        cdesc.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
        cdesc.sample_count = sapp_window_sample_count(swin);
        ctx.swapchainTarget.context = sgl_make_context(&cdesc);
    }
    sgl_set_context(ctx.swapchainTarget.context);
    sgl_defaults();

    beginFrame();

    win->events().update.notify();
    win->tickTree();

    const Color& cc = win->clearColor_;
    clear(cc.r, cc.g, cc.b, cc.a);
    win->events().draw.notify();
    win->drawTreeNow();

    present();
    win->events().afterFrame.notify();

    sgl_set_context(sgl_default_context());
    internal::currentWindowCtx = prev;
}

// --- events: map sapp_event onto CoreEvents / App hooks / tree dispatch ----
void windowEvent(const sapp_event* ev, sapp_window swin, void* user) {
    Window* win = static_cast<Window*>(user);
    if (!win) return;
    auto& ctx = win->context();
    auto* prev = internal::currentWindowCtx;
    internal::currentWindowCtx = &ctx;

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

    switch (ev->type) {
        case SAPP_EVENTTYPE_MOUSE_DOWN: {
            const float x = ev->mouse_x * scale, y = ev->mouse_y * scale;
            ctx.mouseX = x; ctx.mouseY = y;
            ctx.mouseButton = (int)ev->mouse_button;
            ctx.mousePressed = true;
            MouseEventArgs e;
            e.pos = e.globalPos = Vec2(x, y);
            e.button = (int)ev->mouse_button;
            e.shift = hasShift; e.ctrl = hasCtrl; e.alt = hasAlt; e.super = hasSuper;
            e.syncLegacy();
            win->events().mousePressed.notify(e);
            if (win->getApp()) win->getApp()->mousePressed(e);
            win->dispatchMousePressToTree(e);
            break;
        }
        case SAPP_EVENTTYPE_MOUSE_UP: {
            const float x = ev->mouse_x * scale, y = ev->mouse_y * scale;
            ctx.mouseX = x; ctx.mouseY = y;
            ctx.mouseButton = -1;
            ctx.mousePressed = false;
            MouseEventArgs e;
            e.pos = e.globalPos = Vec2(x, y);
            e.button = (int)ev->mouse_button;
            e.shift = hasShift; e.ctrl = hasCtrl; e.alt = hasAlt; e.super = hasSuper;
            e.syncLegacy();
            win->events().mouseReleased.notify(e);
            if (win->getApp()) win->getApp()->mouseReleased(e);
            win->dispatchMouseReleaseToTree(e);
            break;
        }
        case SAPP_EVENTTYPE_MOUSE_ENTER:
        case SAPP_EVENTTYPE_MOUSE_MOVE: {
            const float x = ev->mouse_x * scale, y = ev->mouse_y * scale;
            ctx.pmouseX = ctx.mouseX; ctx.pmouseY = ctx.mouseY;
            ctx.mouseX = x; ctx.mouseY = y;
            internal::MouseEventRaw raw;
            raw.pos = raw.globalPos = Vec2(x, y);
            raw.delta = raw.globalDelta = Vec2(ev->mouse_dx * scale, ev->mouse_dy * scale);
            raw.shift = hasShift; raw.ctrl = hasCtrl; raw.alt = hasAlt; raw.super = hasSuper;
            if (ctx.mousePressed && ctx.mouseButton >= 0) {
                raw.button = ctx.mouseButton;
                MouseDragEventArgs e = internal::toDragArgs(raw);
                win->events().mouseDragged.notify(e);
                if (win->getApp()) win->getApp()->mouseDragged(e);
            } else {
                MouseMoveEventArgs e = internal::toMoveArgs(raw);
                win->events().mouseMoved.notify(e);
                if (win->getApp()) win->getApp()->mouseMoved(e);
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
            if (win->getApp()) win->getApp()->mouseScrolled(e);
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
            break;
        }
        case SAPP_EVENTTYPE_SUSPENDED:
            logNotice("Window") << "second window occluded - skipping frames (no stall)";
            break;
        case SAPP_EVENTTYPE_RESUMED:
            logNotice("Window") << "second window visible again - resuming ticks";
            break;
        default:
            // CHAR / RESIZED / FOCUSED / UNFOCUSED: rawEvent carries them;
            // size sync happens per tick.
            break;
    }

    internal::currentWindowCtx = prev;
}

void windowClosed(sapp_window swin, void* user) {
    (void)swin;
    Window* win = static_cast<Window*>(user);
    if (win) win->close();   // tears down native + app state
}

} // namespace

// ---------------------------------------------------------------------------
// Window methods (Linux implementations)
// ---------------------------------------------------------------------------
namespace trussc {

Window::~Window() { close(); }

void Window::close() {
    auto* st = static_cast<AdapterState*>(native_);
    if (!st) return;
    native_ = nullptr;             // re-entrancy guard (close_cb)
    ctx_.acquireSwapchain = nullptr;
    ctx_.acquireSwapchainUser = nullptr;
    sapp_destroy_window(st->win);
    if (ctx_.swapchainTarget.context.id != SG_INVALID_ID) {
        sgl_destroy_context(ctx_.swapchainTarget.context);
        ctx_.swapchainTarget.context.id = SG_INVALID_ID;
        ctx_.swapchainTarget.cache.clear();
    }
    delete st;
    if (app_) {
        app_->exit();
        app_->cleanup();
        internal::attachedApps.erase(app_.get());
        app_.reset();
        ctx_.rootNode = nullptr;
    }
}

void Window::setTitle(const std::string& title) {
    title_ = title;
    auto* st = static_cast<AdapterState*>(native_);
    if (st) sapp_window_set_title(st->win, title.c_str());
}

int Window::getWidth() const {
    auto* st = static_cast<AdapterState*>(native_);
    return st ? sapp_window_width(st->win) : 0;
}

int Window::getHeight() const {
    auto* st = static_cast<AdapterState*>(native_);
    return st ? sapp_window_height(st->win) : 0;
}

std::shared_ptr<Window> createWindow(const WindowSettings& settings) {
    if (headless::isActive()) {
        logError("Window") << "createWindow(): not available in headless mode";
        return nullptr;
    }
    auto win = std::shared_ptr<Window>(new Window());
    win->title_ = settings.title;
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

    win->native_ = st;
    win->ctx_.acquireSwapchain = &acquireSecondarySwapchain;
    win->ctx_.acquireSwapchainUser = st;
    return win;
}

} // namespace trussc

#endif // __linux__ && SOKOL_GLCORE && !__ANDROID__
