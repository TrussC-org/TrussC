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
    // Caveat (Phase 2): App::setSize / window-control helpers still target
    // the MAIN window; use this Window handle to control this one.
    // Note: the App's setup() runs once on the window's first tree update
    // (standard Node lifecycle), i.e. on the window's first tick.
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

    // Resize this window's content area to the given LOGICAL size (points),
    // matching getWidth()/getHeight() units. Implemented natively per platform
    // (macOS NSWindow, Windows HWND, X11) because sokol_app has no per-window
    // resize entry point. No-op if the native window is gone.
    void setSize(int width, int height);

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
    void dispatchMousePressToTree(const MouseEventArgs& e) {
        if (ctx_.rootNode) ctx_.rootNode->dispatchMousePress(e);
    }
    void dispatchMouseReleaseToTree(const MouseEventArgs& e) {
        if (ctx_.rootNode) ctx_.rootNode->dispatchMouseRelease(e);
    }
    void dispatchMouseMoveToTree(const internal::MouseEventRaw& e) {
        if (ctx_.rootNode) ctx_.rootNode->dispatchMouseMove(e);
    }
    void dispatchMouseScrollToTree(const ScrollEventArgs& e) {
        if (ctx_.rootNode) ctx_.rootNode->dispatchMouseScroll(e);
    }
    void dispatchKeyPressToTree(const KeyEventArgs& e) {
        if (ctx_.rootNode) ctx_.rootNode->dispatchKeyPress(e);
    }
    void dispatchKeyReleaseToTree(const KeyEventArgs& e) {
        if (ctx_.rootNode) ctx_.rootNode->dispatchKeyRelease(e);
    }
    void tickTree() {
        if (!ctx_.rootNode) return;
        ctx_.rootNode->updateTree();
        ctx_.rootNode->updateHoverState(ctx_.mouseX, ctx_.mouseY);
    }
    void drawTreeNow() {
        if (ctx_.rootNode) ctx_.rootNode->drawTree();
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
// Apps currently driving a window (double-attach guard). An inline variable:
// a hot-reload guest gets its own copy, so the guard is per-binary — fine for
// a misuse check. (runApp unification — "runApp = create main window +
// setApp" — is a future refactor; the main App is guarded via rootNode.)
inline std::unordered_set<const App*> attachedApps;
}

inline void Window::setApp(std::shared_ptr<App> app) {
    if (app) {
        if (app.get() == internal::mainWindowContext().rootNode) {
            logError("Window") << "setApp(): this App is the running main App";
            return;
        }
        if (internal::attachedApps.count(app.get())) {
            logError("Window") << "setApp(): this App already drives another window";
            return;
        }
    }
    if (app_) internal::attachedApps.erase(app_.get());
    if (app) internal::attachedApps.insert(app.get());
    app_ = std::move(app);
    ctx_.rootNode = app_.get();
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
inline void Window::setSize(int, int) {}
inline std::shared_ptr<Window> createWindow(const WindowSettings&) {
    logError("Window") << "createWindow(): secondary windows are supported on "
        "macOS, Windows and desktop Linux (OpenGL); this platform is "
        "single-window";
    return nullptr;
}
#endif

} // namespace trussc
