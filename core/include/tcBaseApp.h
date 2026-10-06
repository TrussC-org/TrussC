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
// Teardown steps 2 and 3 wherever the framework lets an App go (exit, hot
// reload, closing a secondary window; #256): after app.cleanup(), before the
// App is destroyed. Defined below the App class.
inline void detachAppAudio(App& app);
// True once the framework has run the App's cleanup() (#256): Window::setApp()
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
    // App whose window has been closed. Resolved from the App itself, not from
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
    // They stop being called after cleanup(): the framework detaches them
    // before it destroys the App (exit, hot reload, closing the App's
    // window), so the App adds nothing to the last few buffers before it
    // goes. It waits for a call already running, as long as it takes, so
    // don't wait on the main thread or on a lock the main thread may hold in
    // here: the teardown would hang (with an error in the log after one
    // second). An App runs once: setup() when first attached, exit() /
    // cleanup() when its window closes (or, with #318, when it is swapped
    // out). To show it again, create a new App.
    virtual void audioOut(AudioOutBuffer& buf) { (void)buf; }
    virtual void audioIn(const AudioInBuffer& buf) { (void)buf; }

private:
    EventListener audioOutListener_;
    EventListener audioInListener_;

    // Node's post-setup hook: subscribe audioOut() / audioIn() now that the
    // first setup() has returned (#426). final: apps override setup().
    void onSetupDone() final { internal::attachAppAudio(*this); }

    // Node's pre-setup hook: resolve the data path root before setup() runs,
    // not in _setup_cb (on iOS the executable path may not be available that
    // early). getDataPath() also resolves it on first use from any thread.
    void onSetupStart() final { internal::resolveDataPathRootOnce(); }

    // Framework lifecycle, next to Node's setupCalled_: true once the
    // framework has run cleanup() and let the App go
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

    // Each handler fires the user-facing App virtual unconditionally (so the
    // raw App::mouseXxx/keyXxx override stays an always-on escape hatch), then
    // dispatches to the node tree ONLY if the event was not already consumed.
    // A higher-priority consumer (e.g. tcxImGui, a BeforeApp listener) sets
    // `consumed` during events().xxx.notify() to claim the event before it
    // reaches the tree. See _event_cb in TrussC.h.
    void handleKeyPressed(const KeyEventArgs& e) {
        keyPressed(e);
        if (!e.consumed) dispatchKeyPress(e);
    }

    void handleKeyReleased(const KeyEventArgs& e) {
        keyReleased(e);
        if (!e.consumed) dispatchKeyRelease(e);
    }

    void handleMousePressed(const MouseEventArgs& e) {
        mousePressed(e);
        if (!e.consumed) dispatchMousePress(e);
    }

    void handleMouseReleased(const MouseEventArgs& e) {
        mouseReleased(e);
        if (!e.consumed) dispatchMouseRelease(e);
    }

    void handleMouseMoved(const internal::MouseEventRaw& e) {
        mouseMoved(internal::toMoveArgs(e));
        if (!e.consumed) dispatchMouseMove(e);
    }

    void handleMouseDragged(const internal::MouseEventRaw& e) {
        mouseDragged(internal::toDragArgs(e));
        if (!e.consumed) dispatchMouseMove(e);  // drag + hover share the node-tree move dispatch
    }

    void handleMouseScrolled(const ScrollEventArgs& e) {
        mouseScrolled(e);
        if (!e.consumed) dispatchMouseScroll(e);
    }

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
// destroyed. The wait has no time limit: a listener that never returns hangs
// the teardown (with an error in the log after one second) instead of
// letting the App be destroyed under it. Main thread; returns at once when no
// audio is running.
//
// Every framework path calls it right after cleanup() (a hot reload, which
// runs no cleanup(), destroys the App right after), so it also records that
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
