#pragma once

// =============================================================================
// tcWindow.h - Application windows
// =============================================================================
//
// Runtime strongly owns every open Window. createWindow returns an operation
// handle; dropping it does not close the window. close is explicit. Each Window
// owns one App attachment with stable event slots and an independent clock.
//
// GPU resources (Texture / Fbo / Mesh / Font) live in the single shared
// sokol_gfx context and can be used freely from any window.
//
// Platform support: macOS, Windows and Linux (X11). On other platforms
// createWindow() logs an error and returns nullptr.
//
// This file is included from TrussC.h (after Node / CoreEvents / WindowSettings).

#include "internal/tcWindowRuntime.h"
#include "tcExit.h"

namespace trussc {

class Window;

namespace internal {
class WindowRequestAccess;   // friend of Window, defined after it
inline constexpr bool supportsSecondaryWindows();
// One control entry for explicit handles and context-aware globals.
enum class WindowControl { Title, Size, PixelSize, Position, Fullscreen, ToggleFullscreen, PositionQuery };
using WindowControlHook = bool (*)(Window&, WindowControl, int, int, const std::string*, IVec2&);
WindowControlHook& windowControlHookForTests();
using WindowFullscreenHook = WindowFullscreenPhase (*)(const Window&);
WindowFullscreenHook& windowFullscreenHookForTests();

// Native implementation, reached through createWindow's platform check.
std::shared_ptr<Window> createSecondaryWindow(const WindowSettings& settings);

// Open-window registry in creation order. Non-inline storage
// in tcGlobal.cpp so the hot-reload
// host and guest see ONE list. Used by the MCP window-targeting tools.
void registerWindow(Window* w);
void unregisterWindow(Window* w);
std::vector<Window*> openWindows();   // only windows whose native side is alive

// RAII registrar: every Window destruction unregisters.
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

    // Construct a fresh App and request attachment at the next safe boundary.
    // Constructor failure leaves the current App intact. The last request wins;
    // close cancels unpublished candidates without setup, exit or cleanup.
    template<class T, class... Args> void setApp(Args&&... args) {
        static_assert(std::is_base_of_v<App, T>, "T must derive from App");
        if (!isOpen() || closeRequested_ || internal::frameworkShutdown() || internal::runtimeEnding()) {
            logError("Window") << "setApp(): this window is closed or closing";
            return;
        }
        requestApp(internal::makeConstructedAppAt<T>(&ctx_, std::forward<Args>(args)...));
    }
    // Remove the App at the next boundary, keeping the window and settings.
    void destroyApp() { requestApp({}); }
    void setQuitOnClose(bool enabled) { if (!exitDelivered_) quitOnClose_ = enabled; }
    std::shared_ptr<App> getApp() const { return app_; }

    // This window's event stream (mousePressed / keyPressed / draw / ...).
    CoreEvents& events() { return events_; }

    // Request closure at the next safe boundary. Pending replacement is
    // cancelled. App ending and owner retirement precede native/GPU disposal.
    // Last-window/quitOnClose asks exitRequested first; any veto keeps all windows.
    // quitOnClose defaults to false. Closed handles stay closed across runs.
    void close();
    bool isOpen() const { return binding_.bound; }

    void setTitle(const std::string& title);
    // Last title set via WindowSettings/setTitle (for window listing/tooling).
    const std::string& getTitle() const { return title_; }
    // Always logical points for every Window, regardless of pixelPerfect.
    // getWindowWidth()/getWindowHeight() and the attached App follow pixelPerfect.
    int getWidth() const;
    int getHeight() const;

    // OS visibility snapshot used by the scheduler. Desktop backends expose
    // occlusion/minimization; unsupported visibility queries return false.
    bool isOccluded() const;

    // Resize this window's content area to the given LOGICAL size (points),
    // matching getWidth()/getHeight() units. Desktop controls address this
    // Window's native binding (macOS NSWindow, Windows HWND or X11).
    // No-op if the native window is gone.
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

    // Desktop fullscreen uses this Window's native state: animated phases on
    // macOS, borderless fullscreen on Windows, and EWMH requests on X11.
    // Framebuffer changes follow the common size-publication path. No-op if
    // the native window is gone.
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

private:
    TC_LUA_SKIP internal::WindowContext& context() { return ctx_; }

    // --- tree driving (called by the platform glue; friend access to Node) ---
    // The root is locked once per call, so it stays alive for the whole call
    // even if a handler detaches the window's App (destroyApp()).
    TC_LUA_SKIP void dispatchMousePressToTree(const MouseEventArgs& e) {
        internal::MouseDispatchScope dispatch(ctx_, e.button, true);
        if (auto root = ctx_.rootNode.lock()) root->dispatchMousePress(e);
    }
    TC_LUA_SKIP void dispatchMouseReleaseToTree(const MouseEventArgs& e) {
        if (auto root = ctx_.rootNode.lock()) root->dispatchMouseRelease(e);
    }
    TC_LUA_SKIP void dispatchMouseMoveToTree(const internal::MouseEventRaw& e) {
        if (auto root = ctx_.rootNode.lock()) root->dispatchMouseMove(e);
    }
    TC_LUA_SKIP void dispatchMouseScrollToTree(const ScrollEventArgs& e) {
        if (auto root = ctx_.rootNode.lock()) root->dispatchMouseScroll(e);
    }
    TC_LUA_SKIP void dispatchKeyPressToTree(const KeyEventArgs& e) {
        if (auto root = ctx_.rootNode.lock()) root->dispatchKeyPress(e);
    }
    TC_LUA_SKIP void dispatchKeyReleaseToTree(const KeyEventArgs& e) {
        if (auto root = ctx_.rootNode.lock()) root->dispatchKeyRelease(e);
    }
    TC_LUA_SKIP void tickTree() {
        auto root = ctx_.rootNode.lock();
        if (!root) return;
        internal::AppCallbackScope owner(app_ ? internal::appOwner(*app_) : nullptr);
        internal::setupNodeOnce(*root);   // the setup() entry point (#349)
        if (ctx_.rootNode.lock() != root) return;
        root->updateTree();
        root->updateHoverState(ctx_.mouseX, ctx_.mouseY);
    }
    TC_LUA_SKIP void drawTreeNow() {
        if (auto root = ctx_.rootNode.lock()) {
            internal::AppCallbackScope owner(app_ ? internal::appOwner(*app_) : nullptr);
            internal::setupNodeOnce(*root);   // the setup() entry point (#349)
            if (ctx_.rootNode.lock() != root) return;
            root->drawTree();
        }
    }
    // The App root is resized to the
    // App's coordinate size (pixels in pixelPerfect mode, otherwise logical)
    // before its first setup and on every resize/display change.
    // A plain Node root is left untouched.
    TC_LUA_SKIP void syncRootSize(float wPts, float hPts) {
        if (!app_) return;
        // handleWindowResized() = RectNode::setSize (non-virtual; App::setSize
        // is overridden to resize the OS window) + the windowResized() hook —
        // the shared SAPP_EVENTTYPE_RESIZED path.
        // The attachment's observed dimensions own deduplication. A listener
        // may have edited the App to this size on an earlier native revision.
        ResizeEventArgs args; args.width = (int)wPts; args.height = (int)hPts;
        internal::WindowCallbackTimeScope callbackTime;
        events_.windowResized.notify(args);
    }

public:
    // --- internal (used by the platform glue; not user API) ---
    Window();
    std::shared_ptr<App> app_;               // set via setApp (may be null)
    void* native_ = nullptr;                 // platform-side state
private:
    struct OwnedState {
        OwnedState();
        internal::WindowContext context;
        CoreEvents events;
        internal::RenderContext render;
        std::string title;
    };
    std::unique_ptr<OwnedState> owned_;
    std::weak_ptr<Window> hostOwner_;
    int entryDepth_ = 0;
    internal::WindowBinding binding_;
public:
    internal::WindowContext& ctx_;
    CoreEvents& events_;
    internal::RenderContext& render_;
    bool clearColorSet_ = false; // construction installs the Window clear colour
    Color clearColor_ = Color(0.05f, 0.05f, 0.08f, 1.0f);
    std::string& title_;
    // Request state (setApp() / close()). The pending App is owned here until
    // the boundary, so a request never refers to a freed App, and it goes
    // away with the window.
    std::shared_ptr<App> pendingApp_;
    bool appRequested_ = false;      // separate flag: destroyApp() is a request too
    bool quitOnClose_ = false;
    uint64_t epoch_ = internal::runtimeEpoch();
    bool closeRequested_ = false;
    int dispatchDepth_ = 0;          // ticks / events of this window running
    internal::WindowRegistryEntry registryEntry_{this};

private:
    IVec2 control(internal::WindowControl op, int x = 0, int y = 0,
                  const std::string* title = nullptr,
                  internal::WindowGeometryCall call = internal::WindowGeometryCall::WindowSize);
    void disposeNativeBacking() noexcept;
    internal::WindowFullscreenPhase fullscreenPhase() const;
    // Rebuilt only at a successful setApp boundary. Exit resolves the held
    // outgoing App after getApp() has been cleared for window teardown.
    struct AppEventState {
        std::atomic<bool> active{false};
        std::atomic<bool> warned{false};
    };
    void prepareAppEvents(std::vector<EventListener>& listeners,
                          const std::shared_ptr<AppEventState>& state);
    std::shared_ptr<AppEventState> appEventState_;
    // Only successful size deliveries participate in deduplication. Reentrant
    // observations are drained by the outer delivery from the current context.
    bool appSizeDelivering_ = false;
    bool appSizeSeeded_ = false;
    int appSizeWidth_ = 0, appSizeHeight_ = 0;
    bool appStarted() const { return app_ && app_->setupCalled_; }
    bool applyingApp_ = false; // Nested boundaries leave later requests pending.
    std::weak_ptr<App> exitingApp_;
    bool exitDelivered_ = false;
    bool windowExitNotified_ = false;
    bool closingApp_ = false;

    // setApp()'s checks; logs the reason and returns false when refused.
    bool canAttach(const std::shared_ptr<App>& app) const;
    void requestApp(std::shared_ptr<App>);
    void cancelCandidate(std::shared_ptr<App>) noexcept;

    // --- requests (framework only, at the frame boundary; not user API) ---
    // Reached through internal::WindowRequestAccess, so they stay out of the
    // public API (and the script bindings generated from it).
    // Applies a pending setApp() at a frame boundary: the platform glue calls
    // it through internal::WindowEntryScope, at the start and end of each
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
    friend class internal::WindowEntryScope;
    friend class internal::WindowLifecycleScope;
    friend void internal::syncWindowAppSize(Window&, bool);
};

namespace internal {
// Framework access to Window's request handling (platform glue, the frame
// boundary, shutdown, tests), the way VideoPlayerPlatformAccess opens
// VideoPlayer to its platform code.
class WindowRequestAccess {
public:
    // Internal tree/context access for framework plumbing and in-repo fixtures.
    static WindowContext& context(Window& w) { return w.context(); }
    static void dispatchMousePressToTree(Window& w, const MouseEventArgs& e) { w.dispatchMousePressToTree(e); }
    static void dispatchMouseReleaseToTree(Window& w, const MouseEventArgs& e) { w.dispatchMouseReleaseToTree(e); }
    static void dispatchMouseMoveToTree(Window& w, const MouseEventRaw& e) { w.dispatchMouseMoveToTree(e); }
    static void dispatchMouseScrollToTree(Window& w, const ScrollEventArgs& e) { w.dispatchMouseScrollToTree(e); }
    static void dispatchKeyPressToTree(Window& w, const KeyEventArgs& e) { w.dispatchKeyPressToTree(e); }
    static void dispatchKeyReleaseToTree(Window& w, const KeyEventArgs& e) { w.dispatchKeyReleaseToTree(e); }
    static void tickTree(Window& w) { w.tickTree(); }
    static void drawTreeNow(Window& w) { w.drawTreeNow(); }
    static void syncRootSize(Window& w, float width, float height) { w.syncRootSize(width, height); }
    static void attachCandidate(Window& w, std::shared_ptr<App> app) { w.requestApp(std::move(app)); }
    static bool acceptsBinding(const Window& w) { return !w.exitDelivered_ && w.epoch_ == runtimeEpoch() && !runtimeEnding(); }
    static const AppConstructionTicket& constructionTicket(const App& app) { return app.constructorTicket_; }
    using HostState = Window::OwnedState;
    static bool appStarted(const Window& w) { return w.appStarted(); }
    // Explicit virtual binding for deterministic host tests; no native operation is called.
    static void bindForTests(Window& w, uint32_t id) { w.binding_ = {}; w.binding_.handle = {id}; w.binding_.bound = id != 0; w.exitDelivered_ = false; w.closeRequested_ = false; w.ctx_.targetsClosed = false; }
    static void setPixelSize(Window& w, int width, int height, WindowGeometryCall call = WindowGeometryCall::GlobalSize) { w.control(WindowControl::PixelSize, width, height, nullptr, call); }
    static void setSize(Window& w, int width, int height, WindowGeometryCall call) { w.control(WindowControl::Size, width, height, nullptr, call); }
    static void setPosition(Window& w, int x, int y, WindowGeometryCall call) { w.control(WindowControl::Position, x, y, nullptr, call); }
    static bool ignoreGeometry(Window& w, WindowGeometryCall call) {
        return ignoreFullscreenGeometry(w.fullscreenPhase(), call, !internal::supportsSecondaryWindows() ? nullptr : &w.title_);
    }
    // Creation identity stays internal; IDs are never reused.
    static uint64_t creationId(const Window& w) { return w.binding_.handle.id; }
    static WindowBinding& binding(Window& w) { return w.binding_; }
    static const WindowBinding& binding(const Window& w) { return w.binding_; }
    static void unbind(Window& w) noexcept { w.binding_.bound = false; w.binding_.handle = {}; w.ctx_.suspended = false; }
    static void resetForLaunch(Window& w) {
        w.binding_.lastDeliveredHidden = false; w.binding_.visibilityReady = false;
        w.exitDelivered_ = false; w.closeRequested_ = false; w.ctx_.targetsClosed = false;
        w.ctx_.appEventListeners.clear(); w.appEventState_.reset(); w.appSizeSeeded_ = false;
        prepareInitialBindings(w); w.epoch_ = runtimeEpoch();
    }
    static bool dispatchActive(const Window& w) { return w.entryDepth_ > 0 || w.dispatchDepth_ > 0 || w.applyingApp_; }
    static void setHostOwner(Window& w, const std::shared_ptr<Window>& owner) { w.hostOwner_ = owner; }
    static std::shared_ptr<Window> retainHostOwner(Window& w) noexcept { return w.hostOwner_.lock(); }
    static void prepareInitialBindings(Window& w) {
        auto state = std::make_shared<Window::AppEventState>();
        std::vector<EventListener> listeners;
        w.prepareAppEvents(listeners, state);
        w.ctx_.appEventListeners.swap(listeners);
        w.appEventState_ = std::move(state);
    }
    static void cancelClose(Window& w) noexcept { w.closeRequested_ = false; }
    static void acceptCodeQuit(Window& w) noexcept { w.closeRequested_ = true; }
    static void applyPendingApp(Window& w) { w.applyPendingApp(); }
    static void endApp(Window& w) noexcept { w.endApp(); }
    static void endForShutdown(Window& w) noexcept { w.closingApp_ = true; w.closeRequested_ = true; w.endApp(); }
    static void teardown(Window& w) noexcept { w.teardown(); }
    static bool quitOnClose(const Window& w) { return w.quitOnClose_; }
    static bool closeRequested(const Window& w) { return w.closeRequested(); }
};

// Test hook: a headless test has no platform glue, so it applies a pending
// setApp() itself, standing in for the frame boundary.
inline void applyPendingAppForTests(Window& w) { WindowRequestAccess::applyPendingApp(w); }
}

namespace internal {
// App callbacks select their token's Window; unowned callers need a sole open
// Window. Backends address that handle, including single-surface platforms.
inline Window* currentWindow() { return resolveWindow(true); }

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
// Fullscreen routing addresses the resolved Window through its native binding.
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
// Runtime retains the Window when external handles are released. Explicit close
// disposes its native backing; a retained handle then reports isOpen() == false.
TC_PLATFORMS("macos,windows,linux") std::shared_ptr<Window> createWindow(const WindowSettings& settings = {});

// Construction and native binding initialization are host compiled.

namespace internal {
// Read-only attachment inspection. Runtime's ledger is the sole owner.
std::unordered_set<const App*> attachedApps();
}

inline bool Window::canAttach(const std::shared_ptr<App>& app) const {
    const auto owner = internal::appOwner(*app);
    return !internal::runtimeEnding() && isOpen() && !closeRequested_ && owner && !owner->retiring && !owner->ended &&
        owner->epoch == epoch_ && !owner->window;
}
inline void Window::cancelCandidate(std::shared_ptr<App> app) noexcept {
    if (app) {
        auto owner = internal::appOwner(*app);
        if (owner && !owner->window && !owner->ended) internal::retireAppOwner(owner);
    }
}
inline void Window::requestApp(std::shared_ptr<App> app) {
    if (!isOpen() || closeRequested_ || (app && !canAttach(app))) {
        cancelCandidate(std::move(app));
        return;
    }
    auto superseded = std::move(pendingApp_);
    pendingApp_ = std::move(app);
    appRequested_ = true;
    cancelCandidate(std::move(superseded));
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
// is still unpublished until this boundary).
inline void Window::applyPendingApp() {
    if (!appRequested_ || dispatchDepth_ || applyingApp_ || exitDelivered_) return;
    applyingApp_ = true;
    struct Reset { bool& flag; ~Reset() { flag = false; } } reset{applyingApp_};
    auto next = std::move(pendingApp_);
    appRequested_ = false;
    if (closeRequested_ || (next && !canAttach(next))) { cancelCandidate(std::move(next)); return; }
    // Stable App-priority slots are installed before any constructor, for every
    // window. Preparation failure never detaches the old attachment.
    try {
        if (!appEventState_) internal::WindowRequestAccess::prepareInitialBindings(*this);
        internal::refreshWindowMetadata(*this);
        if (next) {
            internal::recordAppAttachment(next, *this);
        }
    } catch (...) {
        cancelCandidate(std::move(next));
        internal::reportWindowAppAttachmentFailure();
        return;
    }
    auto outgoing = std::move(app_);
    ctx_.rootNode.reset();
    internal::endAppAttachment(outgoing, ctx_);
    // Outgoing callbacks can close the window: close still beats replacement.
    if (closeRequested_) {
        cancelCandidate(std::move(next));
        return;
    }
    app_ = std::move(next);
    ctx_.rootNode = app_;
    if (app_) {
        auto owner = internal::appOwner(*app_);
        std::lock_guard lock(owner->cancellationsMutex);
        owner->window = this;
        owner->context = &ctx_;

    }
    appSizeSeeded_ = false;
    if (appEventState_) appEventState_->active.store(true);
    internal::TeardownSteps steps;
    steps.run("App initial sizing", [&] { internal::syncWindowAppSize(*this, true); });
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
                if (app) {
                    internal::AppCallbackScope owner(internal::appOwner(*app));
                    callback(*app, args...);
                }
            }, EventPriority::App));
    };
    // Keep the float hover coordinates: App::handleUpdate takes integers.
    bind(events_.update, [this](App&) { tickTree(); });
    bind(events_.draw, [this](App&) { drawTreeNow(); });
    bind(events_.exit, [](App& app) { internal::deliverAppExit(app); }, true);

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
    internal::WindowLifecycleScope lifecycle(*this);
    cancelCandidate(std::move(pendingApp_));
    appRequested_ = false;
    auto outgoing = std::move(app_);
    ctx_.rootNode.reset();
    if (outgoing) {
        if (closingApp_ && !windowExitNotified_) {
            internal::appOwner(*outgoing)->notifyWindowExit = true;
            windowExitNotified_ = true;
        }
    }
    exitingApp_ = outgoing;
    internal::endAppAttachment(outgoing, ctx_);
    exitingApp_.reset();
}

inline Window* App::getWindow() const {
    auto owner = internal::appOwner(*this);
    return owner && !owner->ended && !owner->retiring ? owner->window : nullptr;
}

// Resize this App's attached Window using its own framebuffer-to-logical scale,
// including when called from a different App's callback.
inline void App::setSize(float w, float h) {
    if constexpr (internal::windowSizeIsScreenControlled()) {
        // Reach the Notice endpoint before validation or float-to-int conversion.
        if (getWindow()) { internal::setSingleWindowSizeLogical(0, 0); return; }
    }
    // During construction the candidate is not published or shared yet.
    // Only its local size changes; native resizing belongs in setup().
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
inline void Window::disposeNativeBacking() noexcept {}

#else
namespace internal {
inline constexpr bool supportsSecondaryWindows() { return true; }
}
#endif

// Desktop controls use the addressed binding. Single-window platforms retain
// their canvas/surface control endpoints.
inline Window::~Window() {
    if (!internal::frameworkShutdown() && epoch_ == internal::runtimeEpoch()) teardown();
}
inline void Window::close() {
    if (internal::frameworkShutdown() || epoch_ != internal::runtimeEpoch() || !isOpen() || closeRequested_) return;
    if (internal::codeCloseExitsRuntime(*this)) return;
    closeRequested_ = true;
    if (binding_.handle.id) sapp_window_request_close(binding_.handle);
    // Surface-only hosts drain this request at the outer host boundary.
}
inline void Window::teardown() noexcept {
    if (internal::frameworkShutdown() || epoch_ != internal::runtimeEpoch() || exitDelivered_) return;
    auto keepAlive = hostOwner_.lock();
    exitDelivered_ = true;
    closeRequested_ = true;
    internal::WindowLifecycleScope lifecycle(*this);
    internal::TeardownSteps steps;
    auto* previous = internal::currentWindowCtx();
    internal::currentWindowCtx() = &ctx_;
    // The App loses its Window and root before any ending callback. The native
    // and GPU resources remain usable until all App and owner teardown finishes.
    binding_.bound = false;
    closingApp_ = true;
    endApp();
    if (!windowExitNotified_) {
        windowExitNotified_ = true;
        internal::FrameworkEventAccess::notifyShutdown(events_.exit, steps);
    }
    internal::closeScreenshots(ctx_.asyncScreenshots);
    mcp::internal::retireDeferredTarget(&ctx_);
    if (appEventState_) appEventState_->active.store(false);
    ctx_.appEventListeners.clear();
    steps.run("window target cleanup", [&] { ctx_.teardownTargets(); });
    steps.run("window GPU release", [&] { ctx_.swapchainTarget.resources().release(); });
    ctx_.targetsClosed = true;
    sapp_tc_gl_activate_idle();
    if (internal::sessionEndCommitted()) {
        // Windows can end a logging-off process at the last top-level window's
        // destruction; process exit reclaims the windows.
    } else if (native_) disposeNativeBacking();
    else if (binding_.handle.id) steps.run("window native disposal", [&] { sapp_destroy_window(binding_.handle); });
    internal::WindowRequestAccess::unbind(*this);
    ctx_.acquireSwapchain = nullptr;
    ctx_.acquireSwapchainUser = nullptr;
    ctx_.activateNative = nullptr;
    steps.run("window caller restoration", [&] { internal::restoreWindowContext(previous == &ctx_ ? nullptr : previous); });
    internal::currentWindowCtx() = previous == &ctx_ ? nullptr : previous;
    internal::releaseRuntimeWindow(*this);
}
inline IVec2 Window::control(internal::WindowControl op, int x, int y, const std::string* title, internal::WindowGeometryCall call) {
    // Native controls take this Window's explicit binding. Keep the rendering
    // owner current: a synchronous
    // resize/fullscreen event must park, switch and restore the real caller.
    if constexpr (internal::windowSizeIsScreenControlled()) {
        if (op == internal::WindowControl::Size || op == internal::WindowControl::PixelSize) {
            internal::setSingleWindowSizeLogical(x, y);
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
        title_ = mcp::internal::markedWindowTitle(*title);
    }
    if (!isOpen()) return result;
    if (binding_.operations) {
        const auto& ops = *binding_.operations;
        switch (op) {
            case internal::WindowControl::Title:
                if (ops.setTitle) ops.setTitle(*this, title_.c_str());
                break;
            case internal::WindowControl::PixelSize: {
                if (x <= 0 || y <= 0) break;
                const float dpi = std::isfinite(ctx_.dpiScale) && ctx_.dpiScale > 0 ? ctx_.dpiScale : 1.0f;
                const double width = _sapp_tc_window_points(0, x, dpi);
                const double height = _sapp_tc_window_points(0, y, dpi);
                // Bound (size representation): reject logical extents outside the int getter range.
                if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0 ||
                    width > double((std::numeric_limits<int>::max)()) || height > double((std::numeric_limits<int>::max)())) break;
                if (ops.setSize) ops.setSize(*this, width, height, true);
                break;
            }
            case internal::WindowControl::Size:
                if (x > 0 && y > 0 && ops.setSize) ops.setSize(*this, x, y, false);
                break;
            case internal::WindowControl::Position:
                if (ops.setPosition) ops.setPosition(*this, x, y);
                break;
            case internal::WindowControl::PositionQuery:
                if (ops.position) result = ops.position(*this);
                break;
            case internal::WindowControl::Fullscreen:
                if (ops.setFullscreen) ops.setFullscreen(*this, bool(x));
                break;
            case internal::WindowControl::ToggleFullscreen:
                if (ops.toggleFullscreen) ops.toggleFullscreen(*this);
                break;
        }
        return result;
    }
    switch (op) {
        case internal::WindowControl::Title:
            if (!internal::supportsSecondaryWindows()) {
                if (sapp_isvalid()) sapp_set_window_title(title_.c_str());
            }
            break;
        case internal::WindowControl::PixelSize: {
            if (x <= 0 || y <= 0) return result;
            const float scale = ctx_.dpiScale;
            const float factor = std::isfinite(scale) && scale > 0 ? scale : 1.0f;
#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
            bool useFractionalSize = internal::supportsSecondaryWindows();
#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && !TARGET_OS_IPHONE)
            // Desktop sole-surface sizing is independent of secondary-window support.
            useFractionalSize = useFractionalSize || !internal::supportsSecondaryWindows();
#endif
            if (useFractionalSize) {
                const double width = _sapp_tc_window_points(0, x, factor);
                const double height = _sapp_tc_window_points(0, y, factor);
                // Bound (size API): logical extents must fit the int logical-size getters.
                if (!std::isfinite(width) || !std::isfinite(height) ||
                    width <= 0 || height <= 0 || width > double((std::numeric_limits<int>::max)()) ||
                    height > double((std::numeric_limits<int>::max)())) return result;
#if defined(_WIN32)
                internal::setWindowSizeWin32(sapp_win32_get_hwnd(), width, height); break;
#elif defined(__linux__) && !defined(__ANDROID__)
                if (sapp_isvalid()) internal::setSingleWindowSizeFractionalX11(width, height);
                break;
#elif defined(__APPLE__) && !TARGET_OS_IPHONE
                if (sapp_isvalid()) internal::setSingleWindowSizeFractionalMac(width, height);
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
            if (!internal::supportsSecondaryWindows()) { if (sapp_isvalid()) internal::setSingleWindowSizeLogical(x, y); }
            break;
        case internal::WindowControl::Position:
            if (!internal::supportsSecondaryWindows()) internal::setSingleWindowPosition(x, y);
            break;
        case internal::WindowControl::PositionQuery:
            result = internal::singleWindowPosition();
            break;
        case internal::WindowControl::Fullscreen:
            if (!internal::supportsSecondaryWindows()) internal::setSingleWindowFullscreen(bool(x));
            break;
        case internal::WindowControl::ToggleFullscreen:
            if (!internal::supportsSecondaryWindows()) sapp_toggle_fullscreen();
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
    if (!isOpen() || epoch_ != internal::runtimeEpoch()) return internal::WindowFullscreenPhase::Windowed;
    if (auto hook = internal::windowFullscreenHookForTests()) return hook(*this);
    if (binding_.operations && binding_.operations->fullscreenPhase)
        return static_cast<internal::WindowFullscreenPhase>(binding_.operations->fullscreenPhase(*this));
    if (!internal::supportsSecondaryWindows()) return isOpen() ? internal::singleWindowFullscreenPhase() : internal::WindowFullscreenPhase::Windowed;
    return internal::WindowFullscreenPhase::Windowed;
}
inline bool Window::isFullscreen() const {
    return fullscreenPhase() != internal::WindowFullscreenPhase::Windowed;
}
namespace internal {
inline bool usesNativeWindowDimensions(const WindowContext& ctx) noexcept {
    return headless::isActive() && !ctx.hostWindow;
}
inline WindowDimensions windowDimensions(const WindowContext& ctx) noexcept {
    if (usesNativeWindowDimensions(ctx))
        return {sapp_dpi_scale(), sapp_width(), sapp_height()};
    return {ctx.dpiScale, ctx.fbWidth, ctx.fbHeight};
}
} // namespace internal

inline int Window::getWidth() const {
    return isOpen() ? internal::framebufferToLogical(ctx_.fbWidth, ctx_.dpiScale) : 0;
}
inline int Window::getHeight() const {
    return isOpen() ? internal::framebufferToLogical(ctx_.fbHeight, ctx_.dpiScale) : 0;
}
inline void Window::setFps(float fps) { internal::setFpsForContext(ctx_, fps); }
inline void Window::setIndependentFps(float updateFps, float drawFps) {
    internal::setIndependentFpsForContext(ctx_, updateFps, drawFps);
}
inline bool Window::isOccluded() const {
    const auto& m = binding_.metadata;
    return isOpen() && m.trust_hidden_signals && (m.iconified || m.occluded);
}

inline std::shared_ptr<Window> createWindow(const WindowSettings& settings) {
    if (internal::runtimeEnding()) return nullptr;
    if constexpr (!internal::supportsSecondaryWindows()) {
        logError("Window") << "createWindow(): secondary windows are supported on "
            "macOS, Windows and Linux X11 (GLX or GLES3/EGL); this platform is "
            "single-window";
        return nullptr;
    }
    return internal::createSecondaryWindow(settings);
}

} // namespace trussc
