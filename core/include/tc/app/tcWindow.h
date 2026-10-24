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
class WindowRequestAccess;   // friend of Window, defined after it

// Native implementation, reached through createWindow's platform check.
std::shared_ptr<Window> createSecondaryWindow(const WindowSettings& settings);

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

    // Close the native window. The main window and other windows keep running.
    // close() is a request, like exitApp() for the main window: it returns at
    // once, and the window closes at its backend's next safe point (after the
    // current run-loop pass on Linux / Windows, after the next main-window
    // tick on macOS), through the same path as the window's close button.
    // Until then isOpen() is true and getApp() / App::getWindow() still
    // return the App and the window. Safe to call from the window's own
    // App (update() / draw() / keyPressed() ...). Destroying the Window
    // (the last shared_ptr) still closes it immediately.
    // At teardown: the native window is destroyed and isOpen() turns false,
    // then events().exit delivers App::exit() at App priority, followed by
    // AfterApp teardown hooks; cleanup() runs after the notification.
    // App::getWindow() returns nullptr during those teardown callbacks.
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
    // Request state (setApp() / close()). The pending App is owned here until
    // the boundary, so a request never refers to a freed App, and it goes
    // away with the window.
    std::shared_ptr<App> pendingApp_;
    bool appRequested_ = false;      // separate flag: setApp(nullptr) is a request too
    bool closeRequested_ = false;
    int dispatchDepth_ = 0;          // ticks / events of this window running
    internal::WindowRegistryEntry registryEntry_{this};

private:
    // Rebuilt only at a successful setApp boundary. Exit resolves the held
    // outgoing App after getApp() has been cleared for window teardown.
    struct AppEventState {
        std::atomic<bool> active{false};
        std::atomic<bool> warned{false};
    };
    void prepareAppEvents(std::vector<EventListener>& listeners,
                          const std::shared_ptr<AppEventState>& state);
    std::shared_ptr<AppEventState> appEventState_;
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
inline void closeRequestedWindowsAtShutdown() {
    // exit() / cleanup() can destroy another Window or request another close.
    // Re-read the registry after each teardown; never retain a snapshot of
    // raw Window pointers across user code, and drain requests added by it.
    for (;;) {
        Window* requested = nullptr;
        for (Window* w : openWindows()) {
            if (WindowRequestAccess::closeRequested(*w)) {
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

inline Window::Window() {
    ctx_.isMain = false;
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
// Main thread only. (runApp unification — "runApp = create main window +
// setApp" — is a future refactor; the main App is guarded via rootNode.)
std::unordered_set<const App*>& attachedApps();
}

inline bool Window::canAttach(const std::shared_ptr<App>& app) const {
    // A closed window never runs its teardown again (~Window() returns
    // early), so nothing would end an App attached to it (#256).
    if (!isOpen()) {
        logError("Window") << "setApp(): this window is closed; create a new window";
        return false;
    }
    if (app == internal::mainWindowContext().rootNode.lock()) {
        logError("Window") << "setApp(): this App is the running main App";
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
    std::shared_ptr<Node> mainRoot; // Retain validation ownership through publication.
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
        prepared.emplace();
        if (next) {
            state = std::make_shared<AppEventState>();
            prepareAppEvents(*prepared, state);
        }
        // Preparation can release user captures or invoke diagnostic sinks.
        // Recheck after all such work; a nested window may have accepted next.
        mainRoot = internal::mainWindowContext().rootNode.lock();
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
    auto oldState = std::move(appEventState_);
    if (oldState) oldState->active.store(false);
    std::shared_ptr<App> outgoing = std::move(app_);
    const bool endOutgoing = outgoing && !internal::appRanCleanup(*outgoing);
    if (outgoing) outgoing->cleanupCalled_ = true;
    app_ = std::move(next);
    ctx_.rootNode = app_;
    ctx_.appEventListeners.swap(*prepared);
    appEventState_ = std::move(state);
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
    bind(events_.update, [this](App&) { tickTree(); });
    bind(events_.draw, [this](App&) { drawTreeNow(); });
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
    ctx_.targetsClosed = true;
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
    ctx_.rootNode.reset();
    // End admission before any exit hook, logger or disconnect destructor can
    // request this outgoing App on another window. Audio stays live until exit.
    if (app) app->cleanupCalled_ = true;
    if (app) steps.run("attachment registry removal", [&] { internal::attachedApps().erase(app.get()); });
    exitingApp_ = app;
    internal::FrameworkEventAccess::notifyShutdown(events_.exit, steps);
    exitingApp_.reset();
    if (appEventState_) appEventState_->active.store(false);
    // Preserve the native lifecycle order: exit and AfterApp hooks precede
    // screenshot cancellation. Each cancellation is independently contained.
    internal::closeScreenshots(ctx_.asyncScreenshots);
    ctx_.appEventListeners.clear();
    if (app) {
        // Other windows keep their audio callbacks; only detach this App.
        steps.run("App audio detach", [&] { internal::detachAppAudio(*app); });
        steps.run("App cleanup", [&] { app->cleanup(); });
    }
    app.reset();
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
#if defined(_WIN32)
        internal::setWindowSizeWin32(sapp_win32_get_hwnd(), double(width) / scale, double(height) / scale);
        return;
#else
        width = static_cast<int>(width / scale);
        height = static_cast<int>(height / scale);
#endif
    }
    setWindowSizeLogical(width, height);   // main window only; never routed
}

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if !(defined(__APPLE__) && TARGET_OS_OSX) && !defined(_WIN32) && !(defined(__linux__) && defined(SOKOL_GLCORE))
namespace internal {
// Keep the capability check and the single-window stubs under one platform rule.
inline constexpr bool supportsSecondaryWindows() { return false; }
inline std::shared_ptr<Window> createSecondaryWindow(const WindowSettings&) { return nullptr; }
}
// Stubs for platforms without window glue (web / iOS / Android / Raspberry
// Pi). The real implementations live in platform/mac/tcWindowMac.mm,
// platform/win/tcWindowWin.cpp and platform/linux/tcWindowLinux.cpp (desktop
// Linux is SOKOL_GLCORE; Raspberry Pi builds the same sources with GLES3/EGL
// and keeps these stubs).
inline Window::~Window() {}
inline void Window::close() {}
inline void Window::teardown() noexcept {}
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
#else
namespace internal {
inline constexpr bool supportsSecondaryWindows() { return true; }
}
#endif

inline std::shared_ptr<Window> createWindow(const WindowSettings& settings) {
    if constexpr (!internal::supportsSecondaryWindows()) {
        logError("Window") << "createWindow(): secondary windows are supported on "
            "macOS, Windows and desktop Linux (OpenGL); this platform is "
            "single-window";
        return nullptr;
    }
    return internal::createSecondaryWindow(settings);
}

} // namespace trussc
