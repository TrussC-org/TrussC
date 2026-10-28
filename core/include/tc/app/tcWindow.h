#pragma once

// =============================================================================
// tcWindow.h - Application windows
// =============================================================================
//
// Each Window owns its App attachment. The runner owns the main Window;
// secondary windows are kept alive by the handles returned by createWindow().
// Native tick sources follow the OS; every window uses the shared scheduler.
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
class WindowRequestAccess;   // friend of Window, defined after it
void syncSecondaryAppSize(Window& window, int width, int height);
// One control entry for explicit handles and context-aware globals.
enum class WindowControl { Title, Size, PixelSize, Position, Fullscreen, ToggleFullscreen, PositionQuery };
using WindowControlHook = bool (*)(Window&, WindowControl, int, int, const std::string*, IVec2&);
WindowControlHook& windowControlHookForTests();
using WindowFullscreenHook = WindowFullscreenPhase (*)(const Window&);
WindowFullscreenHook& windowFullscreenHookForTests();

// Native implementation, reached through createWindow's platform check.
std::shared_ptr<Window> createSecondaryWindow(const WindowSettings& settings);

// Open-window registry (creation order, including main). Non-inline storage
// in tcGlobal.cpp so the hot-reload
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

// RAII registrar: every Window destruction unregisters, including main.
struct WindowRegistryEntry {
    Window* w;
    explicit WindowRegistryEntry(Window* win) : w(win) { registerWindow(win); }
    ~WindowRegistryEntry() { unregisterWindow(w); }
    WindowRegistryEntry(const WindowRegistryEntry&) = delete;
    WindowRegistryEntry& operator=(const WindowRegistryEntry&) = delete;
};
}

class Window {
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
    // window is an error.
    // App::setSize() resizes this window from anywhere. The global
    // window-control functions do not: they act on the window being processed
    // (title, size, position and fullscreen). To control it from elsewhere, use this
    // Window handle (from inside the App, App::getWindow() returns it).
    // Note: the App's setup() runs once on the window's first tree update
    // (standard Node lifecycle), i.e. on the window's first tick; its
    // audioOut() / audioIn() are subscribed right after that setup() returns,
    // not at setApp(). An App runs
    // once: setup() when first attached, exit() / cleanup() when it leaves
    // its window (close(), setApp(other), or setApp(nullptr)); leaving also
    // detaches its audioOut() / audioIn() for good. To show it again, create
    // a new App. setApp() refuses an App whose cleanup() already ran, and any
    // App on a window that is not open or is closing (both log an error and
    // leave the window as it is); setApp(nullptr) releases.
    // setApp() is a request: it records the App and returns at once. The
    // window applies it at its next frame boundary (before or after one of
    // its ticks or events, never inside them), wherever setApp() was called
    // from, including the App's own update() / draw() / keyPressed(). Until
    // then getApp() returns the current App. The last setApp() before the
    // boundary wins; a close() requested before the boundary wins over it.
    // The checks above run again when the request is applied. Replacing or
    // removing an App directly calls its exit(), without events().exit; only
    // closing the window notifies that event.
    void setApp(std::shared_ptr<App> app);
    std::shared_ptr<App> getApp() const { return app_; }

    // This window's event stream (mousePressed / keyPressed / draw / ...).
    CoreEvents& events() { return events_; }

    // Close this window. Closing the main window requests application quit;
    // closing a secondary window leaves the other windows running.
    // close() is a request, like requestExitApp() for the main window: it returns at
    // once, and the window closes at its backend's next safe point (after the
    // current run-loop pass on Linux / Windows, after the next main-window
    // tick on macOS), through the same path as the window's close button.
    // Until then isOpen() is true and getApp() / App::getWindow() still
    // return the App and the window. Safe to call from the window's own
    // App (update() / draw() / keyPressed() ...). Destroying the Window
    // (the last shared_ptr) still closes it immediately.
    // Secondary teardown destroys the native window; main teardown is owned
    // by the runner. In either case isOpen() turns false,
    // then events().exit delivers App::exit() at App priority, followed by
    // AfterApp teardown hooks; cleanup() runs after the notification.
    // App::getWindow() returns nullptr during those teardown callbacks.
    // The borrowed main Window pointer expires when the runner shuts down.
    void close();
    bool isOpen() const { return ctx_.isMain ? mainOpen_ : native_ != nullptr; }

    void setTitle(const std::string& title);
    // Last title set via WindowSettings/setTitle (for window listing/tooling).
    const std::string& getTitle() const { return title_; }
    // Always logical points for main and secondary, regardless of pixelPerfect.
    // getWindowWidth()/getWindowHeight() and the attached App follow pixelPerfect.
    int getWidth() const;
    int getHeight() const;

    // OS visibility snapshot used by the scheduler. Desktop backends expose
    // occlusion/minimization; unsupported visibility queries return false.
    bool isOccluded() const;

    // Resize this window's content area to the given LOGICAL size (points),
    // matching getWidth()/getHeight() units. Main uses the existing sapp path
    // and its platform limitations. Secondary sizing is implemented natively
    // (macOS NSWindow, Windows HWND, X11) because sokol_app has no per-window
    // resize entry point. No-op if the native window is gone.
    // Ignored with one Notice until isFullscreen() returns false (including
    // macOS entering/exiting). No geometry is queued; exit restores entry geometry.
    void setSize(int width, int height);

    // Move this window on the desktop. Coordinates are SCREEN coordinates with
    // the primary display's top-left at (0, 0) — the same space the global
    // getWindowPosition()/setWindowPosition() use, and what the OS display
    // settings show. A display placed left of or above the primary one has
    // negative coordinates.
    //
    // Implemented natively per platform (macOS NSWindow, Windows HWND, X11)
    // because sokol_app has no per-window move entry point. The global
    // setWindowPosition() targets the current window; this handle always targets
    // this window. No-op if the native window is gone.
    //
    // Ordering matters for multi-display setups: setFullscreen() covers the
    // display the window is CURRENTLY on (Windows MonitorFromWindow, macOS the
    // window's screen, Linux the WM's choice), so move the window onto the
    // target display FIRST, then go fullscreen. This is the same order
    // openFrameworks uses in its GLFW backend.
    // Ignored with one Notice until isFullscreen() returns false.
    TC_PLATFORMS("macos,windows,linux") void setPosition(int x, int y);

    // Current position in the same screen-coordinate space as setPosition().
    // Returns (-1, -1) if the native window is gone.
    TC_PLATFORMS("macos,windows,linux") IVec2 getPosition() const;

    // Main fullscreen uses sapp. Secondary fullscreen is native because sapp's
    // fullscreen only targets the main window:
    //   macOS  -> NSWindow toggleFullScreen: (native, animated; isFullscreen()
    //             stays true through entering/fullscreen/exiting).
    //   Windows-> borderless-fullscreen: save style + rect, switch to a popup
    //             sized to the window's monitor, restore on exit.
    //   Linux  -> EWMH _NET_WM_STATE_FULLSCREEN ClientMessage to the root window.
    // The framebuffer-size change is picked up by the normal per-window resize
    // path (windowTick keeps fbWidth/fbHeight in sync and syncRootSize fires
    // windowResized). No-op if the native window is gone.
    void setFullscreen(bool full);
    bool isFullscreen() const;
    // Flip the most recently requested state, including during macOS animation.
    void toggleFullscreen() { control(internal::WindowControl::ToggleFullscreen); }

    void setClearColor(const Color& c) { clearColor_ = c; clearColorSet_ = true; }

    // Target update/draw rate of this window: VSYNC (-1), EVENT_DRIVEN (0),
    // or positive fps. OS tick sources cap the achievable draw rate.
    void setFps(float fps);
    // Set separate update/draw rates for this window from anywhere, including
    // another window's callbacks. Rate values follow trussc::setIndependentFps.
    void setIndependentFps(float updateFps, float drawFps);
    float getFps() const { return ctx_.loop.drawTargetFps; }

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
    // App's coordinate size (pixels in pixelPerfect mode, otherwise logical)
    // before its first setup and on every resize/display change.
    // A plain Node root is left untouched.
    void syncRootSize(float wPts, float hPts) {
        if (!app_) return;
        // handleWindowResized() = RectNode::setSize (non-virtual; App::setSize
        // is overridden to resize the OS window) + the windowResized() hook —
        // the same path the main window uses on SAPP_EVENTTYPE_RESIZED.
        if (app_->getWidth() != wPts || app_->getHeight() != hPts) {
            ResizeEventArgs args; args.width = (int)wPts; args.height = (int)hPts;
            internal::WindowCallbackTimeScope callbackTime;
            events_.windowResized.notify(args);
        }
    }

    // --- internal (used by the platform glue; not user API) ---
    Window();
    std::shared_ptr<App> app_;               // set via setApp (may be null)
    void* native_ = nullptr;                 // platform-side state
private:
    struct OwnedState {
        internal::WindowContext context;
        CoreEvents events;
        internal::RenderContext render;
        std::string title;
    };
    // Main borrows the existing host state. Do not construct a spare context:
    // its destructor would introduce target teardown into main shutdown.
    std::unique_ptr<OwnedState> owned_;
public:
    internal::WindowContext& ctx_;
    CoreEvents& events_;
    internal::RenderContext& render_;
    bool clearColorSet_ = false; // main keeps the existing clear() default until explicitly set
    Color clearColor_ = Color(0.05f, 0.05f, 0.08f, 1.0f);
    // Fullscreen intent. Windows/Linux report isFullscreen() from this flag
    // (there is no cheap live query); macOS uses implementation-owned phases.
    bool fullscreenRequested_ = false;
    std::string& title_;
    // Request state (setApp() / close()). The pending App is owned here until
    // the boundary, so a request never refers to a freed App, and it goes
    // away with the window.
    std::shared_ptr<App> pendingApp_;
    bool appRequested_ = false;      // separate flag: setApp(nullptr) is a request too
    bool closeRequested_ = false;
    int dispatchDepth_ = 0;          // ticks / events of this window running
    internal::WindowRegistryEntry registryEntry_{this};

private:
    IVec2 control(internal::WindowControl op, int x = 0, int y = 0,
                  const std::string* title = nullptr,
                  internal::WindowGeometryCall call = internal::WindowGeometryCall::WindowSize);
    void closeSecondary();
    void teardownSecondary() noexcept;
    void setSecondaryTitle(const std::string& title);
    void setSecondarySize(double width, double height, bool pixelSize = false);
    void setSecondaryPosition(int x, int y);
    IVec2 getSecondaryPosition() const;
    void setSecondaryFullscreen(bool full);
    bool isSecondaryFullscreen() const;
#if defined(__APPLE__) && TARGET_OS_OSX
    internal::WindowFullscreenPhase secondaryFullscreenPhase() const;
    void toggleSecondaryFullscreen();
#endif
    internal::WindowFullscreenPhase fullscreenPhase() const;
    int getSecondaryWidth() const;
    int getSecondaryHeight() const;
    bool isSecondaryOccluded() const;
    void seedMainAppSize();
    explicit Window(bool mainWindow);
    bool mainOpen_ = false;
    uint64_t id_;
    // Rebuilt only at a successful setApp boundary. Exit resolves the held
    // outgoing App after getApp() has been cleared for window teardown.
    struct AppEventState {
        std::atomic<bool> active{false};
        std::atomic<bool> warned{false};
    };
    void prepareAppEvents(std::vector<EventListener>& listeners,
                          const std::shared_ptr<AppEventState>& state);
    std::shared_ptr<AppEventState> appEventState_;
    bool initialAppBindings_ = false; // runner reserves the initial App slot before its constructor
    bool applyingApp_ = false; // Nested boundaries leave later requests pending.
    std::weak_ptr<App> exitingApp_;
    bool exitDelivered_ = false;

    // setApp()'s checks; logs the reason and returns false when refused.
    bool canAttach(const std::shared_ptr<App>& app) const;

    // --- requests (framework only, at the frame boundary; not user API) ---
    // Reached through internal::WindowRequestAccess, so they stay out of the
    // public API (and the script bindings generated from it).
    // Applies a pending setApp() at a frame boundary: the platform glue calls
    // it through internal::WindowDispatchScope, at the start and end of each
    // of this window's ticks and events. Does nothing while one of them is
    // running.
    void applyPendingApp();
    // Ends the window's App: drops a pending setApp() (logged when it held an
    // App), detaches the App from the window, notifies exit once, detaches
    // its audio hooks (waiting for a call in flight) and runs its cleanup().
    // Part of the platform teardown.
    void endApp() noexcept;
    // The platform teardown: destroys the native window right away, then calls
    // endApp() (including events().exit). Runs when the backend closes the window
    // (close_cb) and in ~Window().
    void teardown() noexcept;
    bool closeRequested() const { return closeRequested_; }

    friend class internal::WindowRequestAccess;
};

namespace internal {
// Framework access to Window's request handling (platform glue, the frame
// boundary, shutdown, tests), the way VideoPlayerPlatformAccess opens
// VideoPlayer to its platform code.
class WindowRequestAccess {
public:
    static void setPixelSize(Window& w, int width, int height, WindowGeometryCall call = WindowGeometryCall::GlobalSize) { w.control(WindowControl::PixelSize, width, height, nullptr, call); }
    static void setSize(Window& w, int width, int height, WindowGeometryCall call) { w.control(WindowControl::Size, width, height, nullptr, call); }
    static void setPosition(Window& w, int x, int y, WindowGeometryCall call) { w.control(WindowControl::Position, x, y, nullptr, call); }
    static bool ignoreGeometry(Window& w, WindowGeometryCall call) {
        return ignoreFullscreenGeometry(w.fullscreenPhase(), call, w.ctx_.isMain ? nullptr : &w.title_);
    }
    // Creation identity stays internal; main and secondary IDs are never reused.
    static uint64_t creationId(const Window& w) { return w.id_; }
    static std::unique_ptr<Window> makeMain() { return std::unique_ptr<Window>(new Window(true)); }
    static void seedMainAppSize(Window& w) { w.seedMainAppSize(); }
    static bool dispatchActive(const Window& w) { return w.dispatchDepth_ > 0 || w.applyingApp_; }
    static void prepareInitialBindings(Window& w) {
        auto state = std::make_shared<Window::AppEventState>();
        std::vector<EventListener> listeners;
        w.prepareAppEvents(listeners, state);
        w.ctx_.appEventListeners.swap(listeners);
        w.appEventState_ = std::move(state);
        w.initialAppBindings_ = true;
    }
    static void openMain(Window& w) { w.mainOpen_ = true; }
    static void cancelMainClose(Window& w) noexcept { w.closeRequested_ = false; }
    static void endMain(Window& w) noexcept {
        if (!w.mainOpen_) { w.ctx_.appEventListeners.clear(); return; }
        w.mainOpen_ = false;
        w.closeRequested_ = false;
        w.endApp();
    }
    static void applyPendingApp(Window& w) { w.applyPendingApp(); }
    static void endApp(Window& w) noexcept { w.endApp(); }
    static void teardown(Window& w) noexcept { w.teardown(); }
    static bool closeRequested(const Window& w) { return w.closeRequested(); }
};

// Test hook: a headless test has no platform glue, so it applies a pending
// setApp() itself, standing in for the frame boundary.
inline void applyPendingAppForTests(Window& w) { WindowRequestAccess::applyPendingApp(w); }
}

namespace internal {
// The Window whose context is active, including main (nullptr without an
// open window, e.g. headless). Linear scan
// over the (tiny) open-window registry — used by the context-aware global
// window-control functions to route to the right window. Defined here (after
// Window is complete); the globals in TrussC.h call the forward-declared
// route* helpers below.
inline Window* currentWindow() {
    auto& ctx = currentWindowContext();
    for (Window* w : openWindows()) {
        if (&w->context() == &ctx) return w;
    }
    return nullptr;
}

// Route helpers consume main and secondary handles alike. Window operations
// route a main handle to the existing sapp path, including single-window OSes.
inline bool routeSetWindowTitleToWindow(const std::string& title) {
    Window* w = currentWindow();
    if (!w) return false;
    w->setTitle(title);
    return true;
}
inline bool routeSetWindowSizeToWindow(int width, int height, WindowGeometryCall call) {
    Window* w = currentWindow();
    if (!w) return false;
    WindowRequestAccess::setSize(*w, width, height, call);
    return true;
}
inline bool routeSetWindowPixelSizeToWindow(int width, int height, WindowGeometryCall call) {
    Window* w = currentWindow();
    if (!w) return false;
    WindowRequestAccess::setPixelSize(*w, width, height, call);
    return true;
}
// Fullscreen routing follows the active Window; each backend keeps its
// existing main/secondary native implementation.
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
inline void closeRequestedWindowsAtShutdown() {
    // exit() / cleanup() can destroy another Window or request another close.
    // Re-read the registry after each teardown; never retain a snapshot of
    // raw Window pointers across user code, and drain requests added by it.
    for (;;) {
        Window* requested = nullptr;
        for (Window* w : openWindows()) {
            if (!w->context().isMain && WindowRequestAccess::closeRequested(*w)) {
                requested = w;
                break;
            }
        }
        if (!requested) return;
        WindowRequestAccess::teardown(*requested);
    }
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

namespace internal {
uint64_t nextWindowId(); // host-owned, no header-inline mutable state
CoreEvents& mainCoreEvents();
Window* mainWindow();
inline void cancelMainWindowClose() noexcept {
    if (auto* window = mainWindow()) WindowRequestAccess::cancelMainClose(*window);
}
void createMainWindow(const WindowSettings& settings);
void destroyMainWindow() noexcept;
// Runner and hot reload use the same transactional Window attachment.
bool attachMainApp(std::shared_ptr<App> app);
void detachMainApp();
}

inline Window::Window() : Window(false) {}
inline Window::Window(bool mainWindow)
    : owned_(mainWindow ? nullptr : std::make_unique<OwnedState>()),
      ctx_(mainWindow ? internal::mainWindowContext() : owned_->context),
      events_(mainWindow ? internal::mainCoreEvents() : owned_->events),
      render_(mainWindow ? internal::mainRenderContext() : owned_->render),
      title_(mainWindow ? mcp::internal::mainWindowTitleStorage() : owned_->title),
      id_(internal::nextWindowId()) {
    ctx_.isMain = mainWindow;
    ctx_.render = &render_;
    ctx_.coreEvents = &events_;
}

namespace internal {
// Apps currently driving a window (double-attach guard). One set per process,
// defined in tcGlobal.cpp: applyPendingApp() below adds to it and the
// platform teardown (TrussC.lib) removes from it; setApp() reads it from app
// code, so under hot reload the guest reads what the host changed. With a
// copy per module a Windows guest never saw
// the removal: the released App stayed "attached" in the guest's view, so a
// new App that got a released App's address was refused. (A closed App is
// never attached again: setApp() refuses an App whose cleanup() ran; attach
// a new App instead.)
// Main thread only; all window attachments use this contract.
std::unordered_set<const App*>& attachedApps();
}

inline bool Window::canAttach(const std::shared_ptr<App>& app) const {
    // A closed window never runs its teardown again (~Window() returns
    // early), so nothing would end an App attached to it (#256).
    if (!isOpen()) {
        logError("Window") << "setApp(): this window is closed; create a new window";
        return false;
    }
    auto owner = app->weak_from_this().lock();
    if (!owner || owner.owner_before(app) || app.owner_before(owner)) {
        logError("Window") << "setApp(): this App has a different shared owner";
        return false;
    }
    // This window's own App may be requested again (it cancels a pending
    // swap); any other attached App drives another window.
    if (app != app_ && internal::attachedApps().count(app.get())) {
        logError("Window") << "setApp(): this App already drives another window";
        return false;
    }
    // An App runs once (#256): its window's teardown ran its cleanup() and
    // detached its audio hooks for good.
    if (internal::appRanCleanup(*app)) {
        logError("Window") << "setApp(): this App already ran cleanup(); create a new App";
        return false;
    }
    return true;
}

// Request time: the checks log at the call site. A pending close() wins, so
// a setApp() made while the window is closing is refused here.
inline void Window::setApp(std::shared_ptr<App> app) {
    if (app) {
        if (closeRequested_) {
            logError("Window") << "setApp(): this window is closing; create a new window";
            return;
        }
        if (!canAttach(app)) return;
    } else if (!isOpen() || closeRequested_) {
        return;   // nothing to release: the teardown ends the App
    }
    // Publish the complete request before releasing superseded user ownership.
    // Its destructor may setApp() again; that later request must survive.
    auto superseded = std::move(pendingApp_);
    pendingApp_ = std::move(app);
    appRequested_ = true;
}

namespace internal {
inline void reportWindowAppAttachmentFailure() noexcept {
    try { logError("Window") << "setApp(): failed to prepare App attachment"; }
    catch (...) {
        std::fputs("[ERROR] [Window] setApp(): failed to prepare App attachment\n", stderr); // log-check: allow (allocation-free fallback)
    }
}
}

// Apply time: the checks run again and are authoritative here (a pending App
// is not in attachedApps() yet, so the same App requested on two windows
// passes both request-time checks; the second window to apply refuses it).
inline void Window::applyPendingApp() {
    if (!appRequested_ || dispatchDepth_ > 0 || applyingApp_ || exitDelivered_) return;
    // A busy session end freezes App attachment until the process ends.
    if (internal::sessionEndExit()) return;
    applyingApp_ = true;
    struct ApplyingReset {
        bool& flag;
        ~ApplyingReset() { flag = false; }
    } applyingReset{applyingApp_};
    std::shared_ptr<App> next = std::move(pendingApp_);
    pendingApp_.reset();
    appRequested_ = false;
    if (closeRequested_) {
        if (next) logWarning("Window") << "setApp(): dropped, close() was requested on this window";
        return;
    }
    if (next == app_) return;
    if (next && !canAttach(next)) return;
    // Consume this request even on preparation failure. A later request (for
    // example from failure reporting or outgoing callbacks) remains pending.
    // Keep the old attachment/root/audio/bindings until all preparation succeeds.
    auto* prev = internal::currentWindowCtx();
    // Even an empty vector may allocate a Debug iterator proxy on MSVC.
    std::optional<std::vector<EventListener>> prepared;
    std::shared_ptr<AppEventState> state;
    auto& attached = internal::attachedApps();
    internal::TeardownSteps steps;
    auto rollback = [&] {
        // Provisional callbacks are inert, including during capture reclamation.
        // Token invalidation cannot throw even if rebuilding a snapshot fails.
        if (prepared) prepared->clear();
        steps.run("failed attachment context restoration", [&] { internal::restoreWindowContext(prev); });
        internal::currentWindowCtx() = prev;
    };
    try {
        // Target restoration can allocate when rebuilding a resized target.
        internal::restoreWindowContext(&ctx_);
        if (initialAppBindings_) {
            // Preserve the launcher's initial equal-priority ordering. These
            // reserved callbacks have been inert since descriptor construction.
            prepared.emplace(std::move(ctx_.appEventListeners));
            state = std::move(appEventState_);
            initialAppBindings_ = false;
        } else {
            // The count constructor lets a refused Debug iterator proxy reach
            // this guard; vector's default constructor is noexcept on MSVC.
            prepared.emplace(size_t(0));
            if (next) {
                state = std::make_shared<AppEventState>();
                prepareAppEvents(*prepared, state);
            }
        }
        // Preparation can release user captures or invoke diagnostic sinks.
        // Recheck after all such work; a nested window may have accepted next.
        if (!isOpen() || closeRequested_ || (next && !canAttach(next))) {
            rollback();
            return;
        }
        // Final throwing operation. A failed insertion belongs to another
        // committed attachment: never publish next or erase that registration.
        if (next && !attached.insert(next.get()).second) {
            rollback();
            return;
        }
    } catch (...) {
        rollback();
        internal::reportWindowAppAttachmentFailure();
        return;
    }
    // No user code in publication: retain every replaced owner until all
    // App/root/registry/binding state agrees. Retired callbacks become inert
    // before disconnect can reclaim user captures from old Event snapshots.
    // Main launcher slots keep their position across hot reload (and swaps).
    // Preparation still exercises the same transactional validation; its
    // provisional bindings stay inert and are retired instead of the slot.
    const bool keepMainBindings = ctx_.isMain && bool(appEventState_);
    auto oldState = keepMainBindings ? std::shared_ptr<AppEventState>{} : std::move(appEventState_);
    if (oldState) oldState->active.store(false);
    std::shared_ptr<App> outgoing = std::move(app_);
    const bool endOutgoing = outgoing && !internal::appRanCleanup(*outgoing);
    if (outgoing) outgoing->cleanupCalled_ = true;
    app_ = std::move(next);
    ctx_.rootNode = app_;
    if (!keepMainBindings) {
        ctx_.appEventListeners.swap(*prepared);
        appEventState_ = std::move(state);
    }
    if (outgoing) attached.erase(outgoing.get());
    if (appEventState_) appEventState_->active.store(true);
    prepared->clear();
    // setApp is a direct-call exception: no window exit event on a swap.
    // Same shape as endApp() (close): the window drops the App first, then
    // its exit() / cleanup() run, so getApp() there is already the next App.
    // An App runs once (#318): end it at the boundary, outside its own
    // callbacks, with this window's context still active. Keep it alive until
    // its audio hooks are detached and any callback in flight has returned.
    if (endOutgoing) {
        steps.run("outgoing App exit", [&] {
            internal::EntryStackGuard guard(internal::AppEntry::Exit);
            outgoing->exit();
        });
        steps.run("outgoing App audio detach", [&] { internal::detachAppAudio(*outgoing); });
        steps.run("outgoing App cleanup", [&] { outgoing->cleanup(); });
    }
    outgoing.reset();
    steps.run("swap context restoration", [&] { internal::restoreWindowContext(prev); });
    internal::currentWindowCtx() = prev;
}

inline void Window::prepareAppEvents(std::vector<EventListener>& listeners,
                                     const std::shared_ptr<AppEventState>& state) {
    listeners.reserve(15);
    auto bind = [&](auto& event, auto callback, bool exiting = false) {
        listeners.push_back(internal::FrameworkEventAccess::listen(event,
            [this, callback, state, exiting](auto&&... args) {
                if (!state->active.load()) return;
                if (!isMainThread()) {
                    if (!state->warned.exchange(true))
                        logWarning("App") << "App events must be delivered on the main thread";
                    return;
                }
                // Resolve at delivery time, retaining the target through user code.
                auto app = exiting ? exitingApp_.lock() : app_;
                if (!app && exiting) app = app_;
                if (app) callback(*app, args...);
            }, EventPriority::App));
    };
    // Keep the float hover coordinates: App::handleUpdate takes integers.
    bind(events_.update, [this](App& app) {
        if (ctx_.isMain) app.handleUpdate(ctx_.mouseX, ctx_.mouseY);
        else tickTree();
    });
    bind(events_.draw, [this](App& app) {
        if (ctx_.isMain) app.handleDraw();
        else drawTreeNow();
    });
    bind(events_.exit, [](App& app) { app.exit(); }, true);
    bind(events_.keyPressed, [](App& app, auto& e) { e.consumed = app.handleKeyPressed(e) || e.consumed; });
    bind(events_.keyReleased, [](App& app, auto& e) { e.consumed = app.handleKeyReleased(e) || e.consumed; });
    bind(events_.mousePressed, [](App& app, auto& e) { e.consumed = app.handleMousePressed(e) || e.consumed; });
    bind(events_.mouseReleased, [](App& app, auto& e) { e.consumed = app.handleMouseReleased(e) || e.consumed; });
    bind(events_.mouseMoved, [](App& app, auto& e) { e.consumed = app.handleMouseMoved(e) || e.consumed; });
    bind(events_.mouseDragged, [](App& app, auto& e) { e.consumed = app.handleMouseDragged(e) || e.consumed; });
    bind(events_.mouseScrolled, [](App& app, auto& e) { e.consumed = app.handleMouseScrolled(e) || e.consumed; });
    bind(events_.touchPressed, [](App& app, auto& e) { app.touchPressed(e); });
    bind(events_.touchMoved, [](App& app, auto& e) { app.touchMoved(e); });
    bind(events_.touchReleased, [](App& app, auto& e) { app.touchReleased(e); });
    bind(events_.windowResized, [](App& app, auto& e) { app.handleWindowResized(e.width, e.height); });
    bind(events_.filesDropped, [](App& app, auto& e) { app.filesDropped(e.files); });
}

inline void Window::endApp() noexcept {
    if (exitDelivered_) return;
    exitDelivered_ = true;
    if (!ctx_.isMain) ctx_.targetsClosed = true;
    internal::TeardownSteps steps;
    std::shared_ptr<App> dropped = std::move(pendingApp_);
    pendingApp_.reset();
    if (appRequested_ && dropped) {
        steps.run("pending App warning", [&] {
            logWarning("Window") << "setApp(): dropped, this window closed before the App was attached";
        });
    }
    appRequested_ = false;
    dropped.reset();
    // Detach first, then deliver exit with the outgoing App held for the
    // entire notification, including BeforeApp and mandatory AfterApp hooks.
    auto* prev = internal::currentWindowCtx();
    // The native target has been released. Select only the event/state owner;
    // nested restoration also honors WindowContext's terminal target state.
    internal::currentWindowCtx() = &ctx_;
    std::shared_ptr<App> app = std::move(app_);
    // Keep the main runner's legacy weak root through shutdown/unload hooks.
    // Ownership/getWindow are already detached; secondary teardown clears root.
    if (!ctx_.isMain) ctx_.rootNode.reset();
    // End admission before any exit hook, logger or disconnect destructor can
    // request this outgoing App on another window. Audio stays live until exit.
    if (app) app->cleanupCalled_ = true;
    if (app) steps.run("attachment registry removal", [&] { internal::attachedApps().erase(app.get()); });
    exitingApp_ = app;
    internal::FrameworkEventAccess::notifyShutdown(events_.exit, steps);
    if (ctx_.isMain) steps.run("requested secondary closes", [] { internal::closeRequestedWindowsAtShutdown(); });
    exitingApp_.reset();
    if (appEventState_) appEventState_->active.store(false);
    // Keep each backend's existing shutdown order. Main screenshots are
    // cancelled by the runner's resource-stop step before App shutdown;
    // secondary cancellation and binding retirement follow the exit hooks.
    if (!ctx_.isMain) {
        internal::closeScreenshots(ctx_.asyncScreenshots);
        ctx_.appEventListeners.clear();
    }
    if (app) {
        // Other windows keep their audio callbacks; only detach this App.
        steps.run("App audio detach", [&] { internal::detachAppAudio(*app); });
        steps.run("App cleanup", [&] { app->cleanup(); });
    }
    app.reset();
    if (ctx_.isMain) ctx_.appEventListeners.clear();
    if (prev != &ctx_) {
        steps.run("App context restoration", [&] { internal::restoreWindowContext(prev); });
    }
    internal::currentWindowCtx() = prev; // also restore ownership if target preparation failed
}

namespace internal {
// Platform glue: brackets one tick or one event of a window. Pending
// requests are applied when the outermost scope is entered and left, so a
// setApp() made inside it lands right after it (frame boundary).
struct WindowDispatchScope {
    Window& w;
    explicit WindowDispatchScope(Window& win) : w(win) {
        WindowRequestAccess::applyPendingApp(w);
        ++w.dispatchDepth_;
    }
    ~WindowDispatchScope() {
        --w.dispatchDepth_;
        TeardownSteps steps;
        steps.run("pending App at dispatch end", [&] { WindowRequestAccess::applyPendingApp(w); });
    }
    WindowDispatchScope(const WindowDispatchScope&) = delete;
    WindowDispatchScope& operator=(const WindowDispatchScope&) = delete;
};

// Every secondary native tick/input callback uses this boundary. Restoration
// is a separate mandatory step even when dispatch or target preparation fails.
template<class F> void windowCallback(F&& work) noexcept {
    auto* previous = currentWindowCtx();
    TeardownSteps steps;
    steps.run("native window callback", std::forward<F>(work));
    if (currentWindowCtx() != previous) {
        steps.run("native callback context restoration", [&] { restoreWindowContext(previous); });
        currentWindowCtx() = previous;
    }
}
}

// Looked up in the open-window registry rather than cached on the App: every
// platform's teardown drops app_, so a closed window stops matching here and
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
    if constexpr (internal::windowSizeIsScreenControlled()) {
        // Reach the Notice endpoint before validation or float-to-int conversion.
        if (getWindow()) { internal::setMainWindowSizeLogical(0, 0); return; }
    }
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
    if (Window* win = getWindow()) {
        if (internal::WindowRequestAccess::ignoreGeometry(*win, internal::WindowGeometryCall::AppSize)) return;
        // Only the window path converts to int; unattached Apps keep any float size.
        if (!std::isfinite(w) || !std::isfinite(h) || w <= 0 || h <= 0 ||
            double(w) > (std::numeric_limits<int>::max)() || double(h) > (std::numeric_limits<int>::max)()) {
            logWarning("App") << "setSize(): invalid size";
            return;
        }
        if (internal::pixelPerfectMode()) internal::WindowRequestAccess::setPixelSize(*win, int(w), int(h), internal::WindowGeometryCall::AppSize);
        else internal::WindowRequestAccess::setSize(*win, int(w), int(h), internal::WindowGeometryCall::AppSize);
        return;
    }
    RectNode::setSize(w, h);   // unattached: only this App's own Node size
}

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if !(defined(__APPLE__) && TARGET_OS_OSX) && !defined(_WIN32) && !(defined(__linux__) && !defined(__ANDROID__) && (defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)))
namespace internal {
// Keep the capability check and the single-window stubs under one platform rule.
inline constexpr bool supportsSecondaryWindows() { return false; }
inline std::shared_ptr<Window> createSecondaryWindow(const WindowSettings&) { return nullptr; }
}
// Stubs for platforms without window glue (web / iOS / Android). The real implementations live in platform/mac/tcWindowMac.mm,
// platform/win/tcWindowWin.cpp and platform/linux/tcWindowLinux.cpp (desktop
// Linux uses the same adapter for GLX and GLES3/EGL).
inline void Window::closeSecondary() {}
inline void Window::teardownSecondary() noexcept {}
inline void Window::setSecondaryTitle(const std::string&) {}
inline int Window::getSecondaryWidth() const { return 0; }
inline int Window::getSecondaryHeight() const { return 0; }
inline bool Window::isSecondaryOccluded() const {
    if (auto hook = internal::windowOccludedHookForTests()) return hook(*this);
    return false;
}
inline void Window::setSecondarySize(double, double, bool) {}
inline void Window::setSecondaryPosition(int, int) {}
inline IVec2 Window::getSecondaryPosition() const { return IVec2(-1, -1); }
inline void Window::setSecondaryFullscreen(bool) {}
inline bool Window::isSecondaryFullscreen() const { return false; }
#else
namespace internal {
inline constexpr bool supportsSecondaryWindows() { return true; }
}
#endif

// Main routing is compiled identically on desktop and single-window platforms.
// Only the secondary branch reaches the native adapters (or unsupported stubs).
inline Window::~Window() { teardown(); }
inline void Window::close() {
    if (!ctx_.isMain) { closeSecondary(); return; }
    if (isOpen() && !closeRequested_) { closeRequested_ = true; requestExitApp(); }
}
inline void Window::teardown() noexcept {
    if (!ctx_.isMain) { teardownSecondary(); return; }
    if (mainOpen_) internal::WindowRequestAccess::endMain(*this);
}
inline IVec2 Window::control(internal::WindowControl op, int x, int y, const std::string* title, internal::WindowGeometryCall call) {
    // Native controls take an explicit target (the main endpoint or this
    // Window's native handle). Keep the rendering owner current: a synchronous
    // resize/fullscreen event must park, switch and restore the real caller.
    if constexpr (internal::windowSizeIsScreenControlled()) {
        if (op == internal::WindowControl::Size || op == internal::WindowControl::PixelSize) {
            internal::setMainWindowSizeLogical(x, y);
            return {-1, -1};
        }
    }
    IVec2 result{-1, -1};
    if constexpr (!internal::windowSizeIsScreenControlled()) {
        if ((op == internal::WindowControl::Size || op == internal::WindowControl::PixelSize ||
             op == internal::WindowControl::Position) && internal::WindowRequestAccess::ignoreGeometry(*this, call)) return result;
    }
    if (auto hook = internal::windowControlHookForTests()) {
        if (hook(*this, op, x, y, title, result)) return result;
    }
    if (op == internal::WindowControl::Title) {
        if (ctx_.isMain) mcp::internal::mainWindowTitle(*title);
        else title_ = *title;
    }
    if (!isOpen()) return result;
    switch (op) {
        case internal::WindowControl::Title:
            if (ctx_.isMain) {
                if (sapp_isvalid()) sapp_set_window_title(title_.c_str());
            } else { title_ = *title; setSecondaryTitle(*title); }
            break;
        case internal::WindowControl::PixelSize: {
            if (x <= 0 || y <= 0) return result;
            const float scale = ctx_.isMain ? sapp_dpi_scale() : ctx_.dpiScale;
            const float factor = std::isfinite(scale) && scale > 0 ? scale : 1.0f;
#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
            bool useFractionalSize = internal::supportsSecondaryWindows();
#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && !TARGET_OS_IPHONE)
            // Main desktop sizing is independent of secondary-window support.
            useFractionalSize = useFractionalSize || ctx_.isMain;
#endif
            if (useFractionalSize) {
                const double width = _sapp_tc_window_points(0, x, factor);
                const double height = _sapp_tc_window_points(0, y, factor);
                // Bound (size API): logical extents must fit the int logical-size getters.
                if (!std::isfinite(width) || !std::isfinite(height) ||
                    width <= 0 || height <= 0 || width > double((std::numeric_limits<int>::max)()) ||
                    height > double((std::numeric_limits<int>::max)())) return result;
                if constexpr (internal::supportsSecondaryWindows()) {
                    if (!ctx_.isMain) { setSecondarySize(width, height, true); break; }
                }
#if defined(_WIN32)
                internal::setWindowSizeWin32(sapp_win32_get_hwnd(), width, height); break;
#elif defined(__linux__) && !defined(__ANDROID__)
                if (sapp_isvalid()) internal::setMainWindowSizeFractionalX11(width, height);
                break;
#elif defined(__APPLE__) && !TARGET_OS_IPHONE
                if (sapp_isvalid()) internal::setMainWindowSizeFractionalMac(width, height);
                break;
#endif
            }
#endif
            // Preserve the public API's float division before truncation.
            const float width = x / factor, height = y / factor;
            // Bound: float(INT_MAX) rounds up to 2^31, outside int range.
            if (!std::isfinite(width) || !std::isfinite(height) ||
                width >= 0x1p31f || height >= 0x1p31f) return result;
            x = int(width); y = int(height);
            [[fallthrough]];
        }
        case internal::WindowControl::Size:
            if (x <= 0 || y <= 0) return result;
            if (ctx_.isMain) { if (sapp_isvalid()) internal::setMainWindowSizeLogical(x, y); }
            else setSecondarySize(x, y);
            break;
        case internal::WindowControl::Position:
            if (ctx_.isMain) internal::setMainWindowPosition(x, y);
            else setSecondaryPosition(x, y);
            break;
        case internal::WindowControl::PositionQuery:
            result = ctx_.isMain ? internal::mainWindowPosition() : getSecondaryPosition();
            break;
        case internal::WindowControl::Fullscreen:
            if (ctx_.isMain) internal::setMainFullscreen(bool(x));
            else setSecondaryFullscreen(bool(x));
            break;
        case internal::WindowControl::ToggleFullscreen:
            if (ctx_.isMain) sapp_toggle_fullscreen();
#if defined(__APPLE__) && TARGET_OS_OSX
            else toggleSecondaryFullscreen();
#else
            else setSecondaryFullscreen(!isSecondaryFullscreen());
#endif
            break;
    }
    return result;
}
inline void Window::setTitle(const std::string& title) {
    control(internal::WindowControl::Title, 0, 0, &title);
}
inline void Window::setSize(int width, int height) {
    control(internal::WindowControl::Size, width, height);
}
inline void Window::setPosition(int x, int y) {
    control(internal::WindowControl::Position, x, y, nullptr, internal::WindowGeometryCall::WindowPosition);
}
inline IVec2 Window::getPosition() const {
    return const_cast<Window*>(this)->control(internal::WindowControl::PositionQuery);
}
inline void Window::setFullscreen(bool full) {
    control(internal::WindowControl::Fullscreen, int(full));
}
inline internal::WindowFullscreenPhase Window::fullscreenPhase() const {
    if (auto hook = internal::windowFullscreenHookForTests()) return hook(*this);
    if (ctx_.isMain) return isOpen() ? internal::mainWindowFullscreenPhase() : internal::WindowFullscreenPhase::Windowed;
#if defined(__APPLE__) && TARGET_OS_OSX
    return secondaryFullscreenPhase();
#else
    return isSecondaryFullscreen() ? internal::WindowFullscreenPhase::Fullscreen : internal::WindowFullscreenPhase::Windowed;
#endif
}
inline bool Window::isFullscreen() const {
    return fullscreenPhase() != internal::WindowFullscreenPhase::Windowed;
}
inline int Window::getWidth() const {
    return ctx_.isMain ? (isOpen() ? internal::framebufferToLogical(sapp_width(), sapp_dpi_scale()) : 0) : getSecondaryWidth();
}
inline int Window::getHeight() const {
    return ctx_.isMain ? (isOpen() ? internal::framebufferToLogical(sapp_height(), sapp_dpi_scale()) : 0) : getSecondaryHeight();
}
inline void Window::setFps(float fps) {
    // Select only the API context; changing a target needs no GPU switch.
    auto* previous = internal::currentWindowCtx();
    internal::currentWindowCtx() = &ctx_;
    struct Restore { internal::WindowContext* previous; ~Restore() { internal::currentWindowCtx() = previous; } } restore{previous};
    trussc::setFps(fps);
}
inline void Window::setIndependentFps(float updateFps, float drawFps) {
    // Select only the API context; changing a target needs no GPU switch.
    auto* previous = internal::currentWindowCtx();
    internal::currentWindowCtx() = &ctx_;
    struct Restore { internal::WindowContext* previous; ~Restore() { internal::currentWindowCtx() = previous; } } restore{previous};
    trussc::setIndependentFps(updateFps, drawFps);
}
inline bool Window::isOccluded() const {
    if (!isOpen()) return false;
    if (auto hook = internal::windowOccludedHookForTests()) return hook(*this);
    return ctx_.isMain ? sapp_window_occluded({0}) : isSecondaryOccluded();
}

inline std::shared_ptr<Window> createWindow(const WindowSettings& settings) {
    if constexpr (!internal::supportsSecondaryWindows()) {
        logError("Window") << "createWindow(): secondary windows are supported on "
            "macOS, Windows and Linux X11 (GLX or GLES3/EGL); this platform is "
            "single-window";
        return nullptr;
    }
    return internal::createSecondaryWindow(settings);
}

} // namespace trussc
