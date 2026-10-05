#pragma once

// =============================================================================
// tcWindow.h - Secondary application windows (multi-window, Phase 1)
// =============================================================================
//
// One App, many windows: the main window keeps the classic implicit lifecycle
// (runApp / update / draw), a secondary Window owns its own Node tree, events
// and render state (WindowContext) and is driven by its display's own vsync
// (macOS: one CADisplayLink per window, delivered on the main run loop;
// Windows: one DXGI frame latency waitable object per swapchain, serviced by
// the message loop; Linux: timer-paced ticks at the measured refresh rate,
// serviced by the X11 event loop). Everything runs synchronously on the main
// thread; a fully occluded window simply skips rendering (fps 0), so it can
// never stall the others.
//
// GPU resources (Texture / Fbo / Mesh / Font) live in the single shared
// sokol_gfx context and can be used freely from any window.
//
// Platform support: macOS, Windows and Linux (X11). On other platforms
// createWindow() logs an error and returns nullptr.
//
// This file is included from TrussC.h (after Node / CoreEvents / WindowSettings).

namespace trussc {

class Window;

namespace internal {
// Open-window registry (creation order, secondaries only — the main window is
// not a Window object). Non-inline storage in tcGlobal.cpp so the hot-reload
// host and guest see ONE list. Used by the MCP window-targeting tools.
void registerWindow(Window* w);
void unregisterWindow(Window* w);
std::vector<Window*> openWindows();   // only windows whose native side is alive

// Test hook, not a user setting: a headless test has no native window the OS
// could cover or minimize. While a hook is set, Window::isOccluded() returns
// hook(window) instead of the native flag (core/tests/mcpOccludedWindow).
// nullptr, the default, turns it off. Defined in tcGlobal.cpp (one per
// process), next to the window registry.
using WindowOccludedHook = bool (*)(const Window&);
WindowOccludedHook& windowOccludedHookForTests();

// RAII registrar: a Window member, so every ~Window() unregisters no matter
// which platform adapter defines the destructor.
struct WindowRegistryEntry {
    Window* w;
    explicit WindowRegistryEntry(Window* win) : w(win) { registerWindow(win); }
    ~WindowRegistryEntry() { unregisterWindow(w); }
    WindowRegistryEntry(const WindowRegistryEntry&) = delete;
    WindowRegistryEntry& operator=(const WindowRegistryEntry&) = delete;
};
}

class TC_PLATFORMS("macos,windows,linux") Window {
public:
    ~Window();

    // Attach an App to this window — the ONLY way to give a window content.
    // The App gets its familiar lifecycle wired to THIS window: setup() once
    // on the first tick, update()/draw() on the window's own tick,
    // keyPressed/mousePressed/... from this window's event stream, and
    // windowResized() on resize (whose base impl keeps the App's RectNode
    // size in sync, exactly like the main window). App IS a Node — attach
    // children as usual for scene content.
    // One App per window; attaching an App that is already driving another
    // window (or the main one) is an error.
    // App::setSize() resizes this window from anywhere. The global
    // window-control functions do not: they act on the window being processed
    // (setWindowTitle/setWindowSize, fullscreen) or on the main window only
    // (position, decoration). To control this window from elsewhere, use this
    // Window handle (from inside the App, App::getWindow() returns it).
    // Note: the App's setup() runs once on the window's first tree update
    // (standard Node lifecycle), i.e. on the window's first tick; its
    // audioOut() / audioIn() are subscribed right after that setup() returns,
    // not at setApp(). An App runs
    // once: setup() when first attached, exit() / cleanup() when its window
    // closes (or, with #318, when it is swapped out); closing the window also
    // detaches its audioOut() / audioIn() for good. To show it again, create
    // a new App. setApp() refuses an App whose cleanup() already ran, and any
    // App on a window that is not open (both log an error and leave the
    // window as it is); setApp(nullptr) always releases.
    void setApp(std::shared_ptr<App> app);
    std::shared_ptr<App> getApp() const { return app_; }

    // This window's event stream (mousePressed / keyPressed / draw / ...).
    CoreEvents& events() { return events_; }

    // Close the native window. The main window and other windows keep running.
    void close();
    bool isOpen() const { return native_ != nullptr; }

    void setTitle(const std::string& title);
    // Last title set via WindowSettings/setTitle (for window listing/tooling).
    const std::string& getTitle() const { return title_; }
    int getWidth() const;    // logical size (matches the window's coordinates)
    int getHeight() const;

    // True while the OS reports this window as not visible, so it renders no
    // frames (its update/draw are paused until it is visible again). The
    // signals are the ones that pause the window's tick: macOS: minimized,
    // fully covered or on another Space (NSWindow occlusionState); Windows:
    // minimized, or DXGI reports the window occluded; Linux (X11): minimized,
    // or fully obscured (without a compositing manager). False for a closed
    // window. A window can turn hidden or visible at any time, so this is a
    // snapshot. The MCP screenshot tools use it to fail fast (#347).
    bool isOccluded() const;

    // Resize this window's content area to the given LOGICAL size (points),
    // matching getWidth()/getHeight() units. Implemented natively per platform
    // (macOS NSWindow, Windows HWND, X11) because sokol_app has no per-window
    // resize entry point. No-op if the native window is gone.
    void setSize(int width, int height);

    // Per-window fullscreen. Implemented natively per platform because sokol_app's
    // fullscreen only targets the main window:
    //   macOS  -> NSWindow toggleFullScreen: (native, animated; isFullscreen()
    //             reads the live styleMask, so it reflects the true state once
    //             the async transition finishes).
    //   Windows-> borderless-fullscreen: save style + rect, switch to a popup
    //             sized to the window's monitor, restore on exit.
    //   Linux  -> EWMH _NET_WM_STATE_FULLSCREEN ClientMessage to the root window.
    // The framebuffer-size change is picked up by the normal per-window resize
    // path (windowTick keeps fbWidth/fbHeight in sync and syncRootSize fires
    // windowResized). No-op if the native window is gone.
    void setFullscreen(bool full);
    bool isFullscreen() const;
    void toggleFullscreen() { setFullscreen(!isFullscreen()); }

    void setClearColor(const Color& c) { clearColor_ = c; }

    // Per-window target frame rate (throttle).
    //   fps <= 0            -> free-run at the display's vsync (default)
    //   0 < fps < display   -> update/draw run at ~fps
    //   fps >= display rate -> effectively free-run (nothing is skipped)
    // The native display link always fires at vsync and cannot be portably
    // retuned, so a lower rate is realised by skipping display ticks with a
    // time accumulator (the skip bails before the frame's work, so it is cheap).
    // Unlike the main loop this is a SINGLE rate: independent update/draw rates
    // (setIndependentFps) and the event-driven redraw() loop are main-window
    // only. getFps() returns the last target set here (0 = free-run), not a
    // measured rate — use getFrameRate() from inside this window's tick for the
    // measured value.
    void setFps(float fps) { ctx_.throttleFps = fps; }
    float getFps() const { return ctx_.throttleFps; }

    internal::WindowContext& context() { return ctx_; }

    // --- tree driving (called by the platform glue; friend access to Node) ---
    // The root is locked once per call, so it stays alive for the whole call
    // even if a handler detaches the window's App (setApp(nullptr)).
    void dispatchMousePressToTree(const MouseEventArgs& e) {
        if (auto root = ctx_.rootNode.lock()) root->dispatchMousePress(e);
    }
    void dispatchMouseReleaseToTree(const MouseEventArgs& e) {
        if (auto root = ctx_.rootNode.lock()) root->dispatchMouseRelease(e);
    }
    void dispatchMouseMoveToTree(const internal::MouseEventRaw& e) {
        if (auto root = ctx_.rootNode.lock()) root->dispatchMouseMove(e);
    }
    void dispatchMouseScrollToTree(const ScrollEventArgs& e) {
        if (auto root = ctx_.rootNode.lock()) root->dispatchMouseScroll(e);
    }
    void dispatchKeyPressToTree(const KeyEventArgs& e) {
        if (auto root = ctx_.rootNode.lock()) root->dispatchKeyPress(e);
    }
    void dispatchKeyReleaseToTree(const KeyEventArgs& e) {
        if (auto root = ctx_.rootNode.lock()) root->dispatchKeyRelease(e);
    }
    void tickTree() {
        auto root = ctx_.rootNode.lock();
        if (!root) return;
        internal::setupNodeOnce(*root);   // the setup() entry point (#349)
        root->updateTree();
        root->updateHoverState(ctx_.mouseX, ctx_.mouseY);
    }
    void drawTreeNow() {
        if (auto root = ctx_.rootNode.lock()) {
            internal::setupNodeOnce(*root);   // the setup() entry point (#349)
            root->drawTree();
        }
    }
    // Size-sync convention (mirrors the main App, which is a RectNode kept in
    // sync with the window): if the root IS a RectNode it is resized to the
    // window's logical size at attach time and on every resize/display change.
    // A plain Node root is left untouched.
    void syncRootSize(float wPts, float hPts) {
        if (!app_) return;
        // handleWindowResized() = RectNode::setSize (non-virtual; App::setSize
        // is overridden to resize the OS window) + the windowResized() hook —
        // the same path the main window uses on SAPP_EVENTTYPE_RESIZED.
        if (app_->getWidth() != wPts || app_->getHeight() != hPts) {
            app_->handleWindowResized((int)wPts, (int)hPts);
            ResizeEventArgs args; args.width = (int)wPts; args.height = (int)hPts;
            events_.windowResized.notify(args);
        }
    }

    // --- internal (used by the platform glue; not user API) ---
    Window();
    std::shared_ptr<App> app_;               // set via setApp (may be null)
    void* native_ = nullptr;                 // platform-side state
    internal::WindowContext ctx_;
    CoreEvents events_;
    internal::RenderContext render_;
    Color clearColor_ = Color(0.05f, 0.05f, 0.08f, 1.0f);
    // Fullscreen intent. Windows/Linux report isFullscreen() from this flag
    // (there is no cheap live query); macOS ignores it and reads the live
    // NSWindow styleMask instead (the transition is async).
    bool fullscreenRequested_ = false;
    std::string title_;
    internal::WindowRegistryEntry registryEntry_{this};
};

namespace internal {
// The secondary Window whose context is currently active (during its
// windowTick / event dispatch), or nullptr on the main context. Linear scan
// over the (tiny) open-window registry — used by the context-aware global
// window-control functions to route to the right window. Defined here (after
// Window is complete); the globals in TrussC.h call the forward-declared
// route* helpers below.
inline Window* currentWindow() {
    auto& ctx = currentWindowContext();
    if (ctx.isMain) return nullptr;
    for (Window* w : openWindows()) {
        if (&w->context() == &ctx) return w;
    }
    return nullptr;
}

// Route helpers for the global window-control functions (forward-declared near
// the top of TrussC.h, defined here). Each returns true if a secondary window
// consumed the call; false on the main context (caller keeps main behavior).
inline bool routeSetWindowTitleToWindow(const std::string& title) {
    Window* w = currentWindow();
    if (!w) return false;
    w->setTitle(title);
    return true;
}
inline bool routeSetWindowSizeToWindow(int width, int height) {
    Window* w = currentWindow();
    if (!w) return false;
    w->setSize(width, height);
    return true;
}
// Fullscreen routing: from a secondary window's context the global
// setFullscreen()/toggleFullscreen()/isFullscreen() drive THAT window instead of
// warning + no-op'ing (the pre-Phase-2 behavior). Return true when a secondary
// window consumed the call; the read variant reports the window's state via out.
inline bool routeSetFullscreenToWindow(bool full) {
    Window* w = currentWindow();
    if (!w) return false;
    w->setFullscreen(full);
    return true;
}
inline bool routeToggleFullscreenToWindow() {
    Window* w = currentWindow();
    if (!w) return false;
    w->toggleFullscreen();
    return true;
}
inline bool routeIsFullscreenFromWindow(bool& out) {
    Window* w = currentWindow();
    if (!w) return false;
    out = w->isFullscreen();
    return true;
}
}

// Create a secondary window. macOS, Windows and Linux (nullptr elsewhere).
// The returned shared_ptr keeps the window alive; closing the window (user or
// close()) destroys the native side while the handle stays valid (isOpen()).
TC_PLATFORMS("macos,windows,linux") std::shared_ptr<Window> createWindow(const WindowSettings& settings = {});

inline Window::Window() {
    ctx_.isMain = false;
    ctx_.render = &render_;
    ctx_.coreEvents = &events_;
}

namespace internal {
// Apps currently driving a window (double-attach guard). One set per process,
// defined in tcGlobal.cpp: setApp() below adds to it from app code, and the
// platform close() (TrussC.lib) removes from it, so under hot reload the guest
// adds and the host removes. With a copy per module a Windows guest never saw
// the removal: the released App stayed "attached" in the guest's view, so a
// new App that got a released App's address was refused. (A closed App is
// never attached again: setApp() refuses an App whose cleanup() ran; attach
// a new App instead.)
// Main thread only. (runApp unification — "runApp = create main window +
// setApp" — is a future refactor; the main App is guarded via rootNode.)
std::unordered_set<const App*>& attachedApps();
}

inline void Window::setApp(std::shared_ptr<App> app) {
    auto& attached = internal::attachedApps();
    if (app) {
        // A closed window never runs close() again (~Window() returns early),
        // so nothing would end an App attached to it (#256).
        if (!isOpen()) {
            logError("Window") << "setApp(): this window is closed; create a new window";
            return;
        }
        if (app == internal::mainWindowContext().rootNode.lock()) {
            logError("Window") << "setApp(): this App is the running main App";
            return;
        }
        if (attached.count(app.get())) {
            logError("Window") << "setApp(): this App already drives another window";
            return;
        }
        // An App runs once (#256): its window's close() ran its cleanup() and
        // detached its audio hooks for good.
        if (internal::appRanCleanup(*app)) {
            logError("Window") << "setApp(): this App already ran cleanup(); create a new App";
            return;
        }
    }
    if (app_) attached.erase(app_.get());
    if (app) attached.insert(app.get());
    app_ = std::move(app);
    ctx_.rootNode = app_;
}

// Looked up in the open-window registry rather than cached on the App: every
// platform's close() drops app_, so a closed window stops matching here and
// no back-pointer can dangle. The registry holds only a handful of windows.
inline Window* App::getWindow() const {
    for (Window* w : internal::openWindows()) {
        if (w->getApp().get() == this) return w;
    }
    return nullptr;
}

// Picks the window from the App, not from the active context: the global
// setWindowSize() follows the context, so from another window's callback it
// resized THAT window instead. Width/height get the same framebuffer ->
// logical conversion as setWindowSize(), with the target window's own scale.
inline void App::setSize(float w, float h) {
    // Not owned by a shared_ptr (yet): inside the App's constructor, or an
    // App made on the stack or in a unique_ptr. No window runs such an App
    // (it becomes getRootNode() / a window's App only through a shared_ptr),
    // so there is no window to resize: only the App's own size is set, and
    // the first such call on this App logs a warning.
    if (weak_from_this().expired()) {
        if (!unownedSetSizeWarned_) {
            unownedSetSizeWarned_ = true;
            logWarning("App") << "setSize(): this App isn't owned by a shared_ptr yet "
                "(e.g. inside its constructor), so no window runs it: setSize() only "
                "changes the App's own size. Call it in setup().";
        }
        RectNode::setSize(w, h);
        return;
    }
    int width = static_cast<int>(w), height = static_cast<int>(h);
    if (Window* win = getWindow()) {
        if (internal::pixelPerfectMode()) {
            float s = win->context().dpiScale > 0.0f ? win->context().dpiScale : 1.0f;
            width = static_cast<int>(width / s);
            height = static_cast<int>(height / s);
        }
        win->setSize(width, height);
        return;
    }
    if (this != internal::mainWindowContext().rootNode.lock().get()) {
        RectNode::setSize(w, h);   // attached to no window: nothing to resize
        return;
    }
    if (internal::pixelPerfectMode()) {
        float scale = sapp_dpi_scale();
        width = static_cast<int>(width / scale);
        height = static_cast<int>(height / scale);
    }
    setWindowSizeLogical(width, height);   // main window only; never routed
}

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if !(defined(__APPLE__) && TARGET_OS_OSX) && !defined(_WIN32) && !(defined(__linux__) && defined(SOKOL_GLCORE))
// Stubs for platforms without window glue (web / iOS / Android / Raspberry
// Pi). The real implementations live in platform/mac/tcWindowMac.mm,
// platform/win/tcWindowWin.cpp and platform/linux/tcWindowLinux.cpp (desktop
// Linux is SOKOL_GLCORE; Raspberry Pi builds the same sources with GLES3/EGL
// and keeps these stubs).
inline Window::~Window() {}
inline void Window::close() {}
inline void Window::setTitle(const std::string&) {}
inline int Window::getWidth() const { return 0; }
inline int Window::getHeight() const { return 0; }
inline bool Window::isOccluded() const {
    if (auto hook = internal::windowOccludedHookForTests()) return hook(*this);
    return false;
}
inline void Window::setSize(int, int) {}
inline void Window::setFullscreen(bool) {}
inline bool Window::isFullscreen() const { return false; }
inline std::shared_ptr<Window> createWindow(const WindowSettings&) {
    logError("Window") << "createWindow(): secondary windows are supported on "
        "macOS, Windows and desktop Linux (OpenGL); this platform is "
        "single-window";
    return nullptr;
}
#endif

} // namespace trussc
