#pragma once

// INTERNAL header — do NOT include directly. Apps include <TrussC.h>, which
// pulls this in (in the correct order). Including tcBaseApp.h on its own will
// not compile (it depends on declarations the umbrella makes first).

#include "tcNode.h"
#include "tc/types/tcMod.h"
#include "tc/types/tcRectNode.h"
#include "tc/types/tcLayoutMod.h"
#include "tc/types/tcScrollContainer.h"
#include "tc/types/tcScrollBar.h"
#include "tc/types/tcTweenMod.h"
#include "tc/sound/tcSound.h"      // AudioEngine + AudioOutBuffer / AudioInBuffer
#include "tc/utils/tcUtils.h"      // internal::resolveDataPathRootOnce
#include "tc/events/tcEventListener.h"
#include "tc/utils/tcAnnotations.h"
#include <vector>
#include <string>

// =============================================================================
// trussc namespace
// =============================================================================
namespace trussc {

class Window;
class App;

namespace internal {
// Subscribes the App's audioOut() / audioIn() hooks, right after its first
// setup() has returned (App::onSetupDone(), #426). Defined below the App
// class.
inline void attachAppAudio(App& app);
// Detach and wait wherever the framework lets an App go (exit, hot reload,
// closing a secondary window): after exit(), before cleanup() (#698).
// Defined below the App class.
inline void detachAppAudio(App& app);
// True once the framework has begun the App's cleanup (#698): Window::setApp()
// refuses such an App. Defined below the App class.
inline bool appRanCleanup(const App& app);
// Priority of the App's audioOut / audioIn hooks: just before Generator (the
// default), so App::audioOut() runs before every default-priority listener,
// whenever that listener subscribed, also one subscribed in setup() before
// the hooks are (#426). For a listener subscribed after the App was
// constructed (setup() included) that is the order it had when the App
// constructor subscribed the hooks. A default-priority listener subscribed
// before the App existed (a static object, or main() before runApp()) used
// to run before App::audioOut() and now runs after it.
constexpr int appAudioPriority = audio::priority::Generator - 1;
}

// =============================================================================
// App - Application base class
// Inherits from tc::RectNode and functions as scene graph root node
// Size is synchronized with window size
// Create tcApp by inheriting this class
// =============================================================================

class App : public RectNode {
public:
    App() {
        // The audio hooks are not subscribed here: the audio thread could
        // then call audioOut() before setup() has prepared its state, even
        // before the derived constructor has finished. They are subscribed
        // right after the first setup() returns (onSetupDone()), so an App
        // that is never run gets no audio callbacks (#426).

        // Not registered as a window's scene-graph root here: the root is
        // held weakly (getRootNode()), and weak_from_this() is empty until
        // the constructor returns. Whoever creates the App through a
        // shared_ptr registers it: runApp(), runHeadlessApp() and the hot
        // reload host for the main window, Window::setApp() for a secondary
        // window.
    }

    // -------------------------------------------------------------------------
    // Size (synchronized with window)
    // -------------------------------------------------------------------------

    // Override setSize to resize the App's OWN window — the one it is attached
    // to via Window::setApp(), or the main window for the main App — no matter
    // which window's callback makes the call. Same units as setWindowSize().
    // An App attached to no window resizes no window: only its RectNode size
    // changes. The actual size update happens in the windowResized callback.
    // An App no shared_ptr owns yet (e.g. inside its constructor, or one made
    // on the stack) is run by no window: it resizes no window, only its own
    // size, and warns once per App (call it in setup()).
    // Resizing the main window does nothing on Linux (not implemented yet),
    // iOS and Android (fixed fullscreen); on Web it resizes the canvas.
    // Defined in tc/app/tcWindow.h (needs the complete Window).
    void setSize(float w, float h) override;

    // -------------------------------------------------------------------------
    // Exit request (for programmatic termination)
    // -------------------------------------------------------------------------

    /// Request application exit (works in both windowed and headless mode)
    void requestExit() { exitRequested_ = true; }

    /// Check if exit has been requested
    bool isExitRequested() const { return exitRequested_; }

    // -------------------------------------------------------------------------
    // Window
    // -------------------------------------------------------------------------

    // The Window this App is attached to via Window::setApp(), or nullptr if
    // it is not attached to one — which currently includes the main App
    // started by runApp() (the main window is not a Window object yet) and an
    // App whose window has been closed. close() only requests closure; the
    // window remains available until teardown begins. It is already nullptr
    // during teardown: inside this App's own exit() / cleanup() and in the window's
    // events().exit listeners, because the native window is destroyed first.
    // Read what you need (title, size, fullscreen) before calling close(), or
    // keep it up to date in update(). Resolved from the App itself, not from
    // the active window context, so subApp->getWindow() called from the main
    // window's callbacks still returns the second window. Defined in
    // tc/app/tcWindow.h (needs the complete Window).
    Window* getWindow() const;

private:
    bool exitRequested_ = false;
    bool unownedSetSizeWarned_ = false;   // setSize() warned once: no shared_ptr owns this App

public:

    // -------------------------------------------------------------------------
    // Lifecycle (inherited from Node, additional overrides possible)
    // -------------------------------------------------------------------------

    // setup(), update(), draw(), cleanup() are inherited from Node

    // -------------------------------------------------------------------------
    // Keyboard events (traditional style)
    // -------------------------------------------------------------------------

    // Rich form (canonical): carries modifiers + isRepeat. The simple int form
    // is a convenience that the default rich impl forwards to — override either.
    virtual void keyPressed(const KeyEventArgs& e) { keyPressed(e.key); }
    virtual void keyPressed(int key) { (void)key; }

    virtual void keyReleased(const KeyEventArgs& e) { keyReleased(e.key); }
    virtual void keyReleased(int key) { (void)key; }

    // -------------------------------------------------------------------------
    // Mouse events
    // -------------------------------------------------------------------------
    // Args carry pos (== screen at app level), globalPos, delta, button and
    // modifiers. See MouseEventArgs / ScrollEventArgs for the coordinate rules.

    // Rich form (canonical) + simple form (convenience, oF-style). The default
    // rich impl forwards to the simple one — override either. The simple form
    // gets screen-space pos (== e.pos at app level) and an int button.
    virtual void mousePressed(const MouseEventArgs& e) { mousePressed(e.pos, e.button); }
    virtual void mousePressed(Vec2 pos, int button) { (void)pos; (void)button; }

    virtual void mouseReleased(const MouseEventArgs& e) { mouseReleased(e.pos, e.button); }
    virtual void mouseReleased(Vec2 pos, int button) { (void)pos; (void)button; }

    virtual void mouseMoved(const MouseMoveEventArgs& e) { mouseMoved(e.pos); }
    virtual void mouseMoved(Vec2 pos) { (void)pos; }

    virtual void mouseDragged(const MouseDragEventArgs& e) { mouseDragged(e.pos, e.button); }
    virtual void mouseDragged(Vec2 pos, int button) { (void)pos; (void)button; }

    virtual void mouseScrolled(const ScrollEventArgs& e) { mouseScrolled(e.scroll); }
    virtual void mouseScrolled(Vec2 delta) { (void)delta; }

    // -------------------------------------------------------------------------
    // Touch events (multi-touch, used on Android/iOS)
    // First touch is also delivered as mouse events by default (see setTouchAsMouse)
    // -------------------------------------------------------------------------

    virtual void touchPressed(const TouchEventArgs& touch) { (void)touch; }
    virtual void touchMoved(const TouchEventArgs& touch) { (void)touch; }
    /// Also called on system cancellation (incoming call, gesture override, etc.).
    /// Check touch.cancelled to distinguish from normal release.
    virtual void touchReleased(const TouchEventArgs& touch) { (void)touch; }

    // -------------------------------------------------------------------------
    // Window events
    // -------------------------------------------------------------------------

    virtual void windowResized(int width, int height) {
        (void)width;
        (void)height;
    }

    // -------------------------------------------------------------------------
    // Drag & drop events
    // -------------------------------------------------------------------------

    virtual void filesDropped(const std::vector<std::string>& files) {
        (void)files;
    }

    // -------------------------------------------------------------------------
    // Exit event
    // -------------------------------------------------------------------------

    /// Called on app exit (before cleanup)
    /// Use for resource release or settings save
    virtual void exit() {}

    // -------------------------------------------------------------------------
    // Audio callbacks (oF-style)
    // -------------------------------------------------------------------------
    //
    // Override these to synthesize / process audio per device callback.
    // Runs on the audio thread — keep work RT-safe (no allocations, no
    // engine API calls, no heavy locks). Add to `buf.data`; zeroing it
    // silences other Sound voices that have already been mixed.
    //
    // For multiple independent listeners (e.g. a Node-based synth tree),
    // use `AudioEngine::getInstance().audioOut.listen(...)` directly
    // alongside the App override. The App's hooks run before every listener
    // at the default priority (audio::priority::Generator), whenever that
    // listener subscribed, setup() included; pass Effect / Monitor for one
    // that must see what audioOut() wrote. A value below
    // audio::priority::Generator - 1 runs before the App's hooks; exactly
    // Generator - 1 shares their priority and runs in subscription order
    // relative to them (after them if subscribed once setup() returned).
    // They are first called right after setup() returns (the framework
    // subscribes them then, not when the App is constructed), so state that
    // setup() prepares is ready in here. An App that is never run gets no
    // audio callbacks.
    // They stop being called before cleanup(): the framework detaches them
    // before cleanup() may free audio state (exit, hot reload, closing the App's
    // window), so the App adds nothing to the last few buffers before it
    // goes. It waits for a call already running, as long as it takes, so
    // don't wait on the main thread or on a lock the main thread may hold in
    // here: the teardown would hang (with an error in the log after one
    // second). An App runs once: setup() when first attached, exit() /
    // cleanup() when it leaves its window (the window closes, or setApp()
    // swaps it out or removes it). To show it again, create a new App.
    virtual void audioOut(AudioOutBuffer& buf) { (void)buf; }
    virtual void audioIn(const AudioInBuffer& buf) { (void)buf; }

private:
    EventListener audioOutListener_;
    EventListener audioInListener_;

    // Node's post-setup hook: subscribe audioOut() / audioIn() now that the
    // first setup() has returned (#426). final: apps override setup().
    void onSetupDone() final {
        internal::attachAppAudio(*this);
        // The main App's setup() has now made its code grant (windowed or
        // headless: both runners register this root before setup). Secondary apps
        // and hot reload guests must not repeat process startup.
        if (internal::mainWindowContext().rootNode.lock().get() == this)
            mcp::internal::initializeMcp();
    }

    // Node's pre-cleanup hook: an App added as a child ends with its
    // subtree's cleanupTree(), so its audio hooks are detached there too.
    void onCleanupStart() final {
        Node::onCleanupStart();
        internal::detachAppAudio(*this);
    }

    // Node's pre-setup hook: resolve the data path root before setup() runs,
    // not in _setup_cb (on iOS the executable path may not be available that
    // early). getDataPath() also resolves it on first use from any thread.
    void onSetupStart() final { internal::resolveDataPathRootOnce(); }

    // Framework lifecycle, next to Node's setupCalled_: true once the
    // framework has begun teardown, before running cleanup()
    // (internal::detachAppAudio()). An App runs once, so Window::setApp()
    // refuses it from then on (internal::appRanCleanup()).
    bool cleanupCalled_ = false;

    friend void internal::attachAppAudio(App& app);
    friend void internal::detachAppAudio(App& app);
    friend bool internal::appRanCleanup(const App& app);
public:

    // -------------------------------------------------------------------------
    // Event handlers (called by TrussC.h, dispatches to scene graph)
    // -------------------------------------------------------------------------

    // The App-priority listener calls the virtual first, then the tree, and
    // publishes the tree's explicit consumption to later listeners.
    bool handleKeyPressed(const KeyEventArgs& e) {
        keyPressed(e);
        return !e.consumed && dispatchKeyPress(e);
    }

    bool handleKeyReleased(const KeyEventArgs& e) {
        keyReleased(e);
        return !e.consumed && dispatchKeyRelease(e);
    }

    bool handleMousePressed(const MouseEventArgs& e) {
        mousePressed(e);
        return !e.consumed && bool(dispatchMousePress(e));
    }

    bool handleMouseReleased(const MouseEventArgs& e) {
        mouseReleased(e);
        return !e.consumed && bool(dispatchMouseRelease(e));
    }

    bool handleMouseMoved(const MouseMoveEventArgs& e) {
        mouseMoved(e);
        return !e.consumed && bool(dispatchMouseMove(internal::toRaw(e)));
    }

    bool handleMouseDragged(const MouseDragEventArgs& e) {
        mouseDragged(e);
        return !e.consumed && bool(dispatchMouseMove(internal::toRaw(e)));
    }

    bool handleMouseMoved(const internal::MouseEventRaw& e) {
        return handleMouseMoved(internal::toMoveArgs(e));
    }

    bool handleMouseDragged(const internal::MouseEventRaw& e) {
        return handleMouseDragged(internal::toDragArgs(e));
    }

    bool handleMouseScrolled(const ScrollEventArgs& e) {
        mouseScrolled(e);
        return !e.consumed && bool(dispatchMouseScroll(e));
    }

protected:
    // An enabled App root only consumes explicitly. Keep node events and the
    // legacy hooks, without RectNode's automatic background consumption.
    using RectNode::onMousePress;
    using RectNode::onMouseRelease;
    using RectNode::onMouseDrag;
    bool onMousePress(const MouseEventArgs& e) override {
        MouseEventArgs args = e;
        RectNode::mousePressed.notify(args);
        return onMousePress(e.pos, e.button) || args.consumed;
    }
    bool onMouseRelease(const MouseEventArgs& e) override {
        MouseEventArgs args = e;
        RectNode::mouseReleased.notify(args);
        return onMouseRelease(e.pos, e.button) || args.consumed;
    }
    bool onMouseDrag(const MouseDragEventArgs& e) override {
        MouseDragEventArgs args = e;
        RectNode::mouseDragged.notify(args);
        return onMouseDrag(e.pos, e.button) || args.consumed;
    }

public:
    void handleWindowResized(int width, int height) {
        // Update internal size (call RectNode::setSize directly, not our override)
        RectNode::setSize(static_cast<float>(width), static_cast<float>(height));
        // Call user callback
        windowResized(width, height);
    }

    // The App's setup() runs on its first update or draw, as its own entry
    // point (#349), before the tree walk.
    void handleUpdate(int mouseX, int mouseY) {
        internal::setupNodeOnce(*this);
        updateTree();
        updateHoverState((float)mouseX, (float)mouseY);
    }

    void handleDraw() {
        internal::setupNodeOnce(*this);
        drawTree();
    }
};

namespace internal {
// Subscribe the App's audioOut / audioIn hooks to the AudioEngine. Runs once,
// from App::onSetupDone(), right after the App's first setup() has returned:
// Node::setupOnce() runs both on the first updateTree() / drawTree() (the
// main App, a secondary window's App, every hot reload generation), and
// runHeadlessApp() through internal::setupNodeOnce(). So the audio thread
// never runs them before or during setup(). They run at appAudioPriority,
// ahead of the default-priority listeners setup() may have subscribed first,
// as when the App constructor subscribed them. Idempotent (a hook already
// subscribed is kept as it is, not re-subscribed), and a no-op once the App's
// lifecycle has ended (detachAppAudio()): an App runs once, its hooks are
// never subscribed again. Main thread.
inline void attachAppAudio(App& app) {
    if (app.cleanupCalled_) return;
    if (!app.audioOutListener_.isConnected()) {
        app.audioOutListener_ = AudioEngine::getInstance().audioOut.listen(
            [&app](AudioOutBuffer& b) { app.audioOut(b); }, appAudioPriority);
    }
    if (!app.audioInListener_.isConnected()) {
        app.audioInListener_ = AudioEngine::getInstance().audioIn.listen(
            [&app](AudioInBuffer& b) { app.audioIn(b); }, appAudioPriority);
    }
}

// Detach the App's audioOut / audioIn hooks, then wait for a callback that is
// already running on the audio thread (Event does not wait on disconnect).
// Afterwards nothing on the audio thread reaches the App, so it can be
// cleaned up and destroyed. The wait has no time limit: a listener that never returns hangs
// the teardown (with an error in the log after one second) instead of
// letting the App be destroyed under it. Main thread; returns at once when no
// audio is running.
//
// Every framework path calls it before cleanup(), so it also records that
// the App's lifecycle ended: Window::setApp() refuses it from then on, and
// its hooks are never subscribed again.
inline void detachAppAudio(App& app) {
    app.audioOutListener_.disconnect();
    app.audioInListener_.disconnect();
    app.cleanupCalled_ = true;
    waitForAudioCallbacksNoTimeout();
}

inline bool appRanCleanup(const App& app) {
    return app.cleanupCalled_;
}
}

} // namespace trussc

namespace trussc::internal {
// Install once per main-window run, before setup/guest loading. Lookup happens
// inside each callback; swapping a hot-reload guest never reorders the host slot.
template<class GetApp>
void registerAppEventListeners(GetApp getApp) {
    auto& listeners = mainWindowContext().appEventListeners;
    listeners.clear();
    auto warned = std::make_shared<std::atomic<bool>>(false);
    auto bind = [&](auto& event, auto callback) {
        listeners.push_back(FrameworkEventAccess::listen(event,
            [getApp, callback, warned](auto&&... args) {
                if (!isMainThread()) {
                    if (!warned->exchange(true))
                        logWarning("App") << "App events must be delivered on the main thread";
                    return;
                }
                if (auto app = getApp()) {
                    // Keep an outgoing guest alive through callback-induced changes.
                    auto owner = app->shared_from_this();
                    callback(*app, args...);
                }
            }, EventPriority::App));
    };
    auto& source = events();
    bind(source.update, [](auto& app) {
        auto& ctx = currentWindowContext();
        app.handleUpdate(ctx.mouseX, ctx.mouseY);
    });
    bind(source.draw, [](auto& app) { app.handleDraw(); });
    bind(source.exit, [](auto& app) { app.exit(); });
    bind(source.keyPressed, [](auto& app, auto& e) { e.consumed = app.handleKeyPressed(e) || e.consumed; });
    bind(source.keyReleased, [](auto& app, auto& e) { e.consumed = app.handleKeyReleased(e) || e.consumed; });
    bind(source.mousePressed, [](auto& app, auto& e) { e.consumed = app.handleMousePressed(e) || e.consumed; });
    bind(source.mouseReleased, [](auto& app, auto& e) { e.consumed = app.handleMouseReleased(e) || e.consumed; });
    bind(source.mouseMoved, [](auto& app, auto& e) { e.consumed = app.handleMouseMoved(e) || e.consumed; });
    bind(source.mouseDragged, [](auto& app, auto& e) { e.consumed = app.handleMouseDragged(e) || e.consumed; });
    bind(source.mouseScrolled, [](auto& app, auto& e) { e.consumed = app.handleMouseScrolled(e) || e.consumed; });
    bind(source.touchPressed, [](auto& app, auto& e) { app.touchPressed(e); });
    bind(source.touchMoved, [](auto& app, auto& e) { app.touchMoved(e); });
    bind(source.touchReleased, [](auto& app, auto& e) { app.touchReleased(e); });
    bind(source.windowResized, [](auto& app, auto& e) { app.handleWindowResized(e.width, e.height); });
    bind(source.filesDropped, [](auto& app, auto& e) { app.filesDropped(e.files); });
}
} // namespace trussc::internal
