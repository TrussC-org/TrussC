#pragma once

#include "tcNode.h"
#include "tc/types/tcMod.h"
#include "tc/types/tcRectNode.h"
#include "tc/types/tcLayoutMod.h"
#include "tc/types/tcScrollContainer.h"
#include "tc/types/tcScrollBar.h"
#include "tc/types/tcTweenMod.h"
#include "tc/sound/tcSound.h"      // AudioEngine + AudioOutBuffer / AudioInBuffer
#include "tc/events/tcEventListener.h"
#include <vector>
#include <string>

// =============================================================================
// trussc namespace
// =============================================================================
namespace trussc {

// =============================================================================
// App - Application base class
// Inherits from tc::RectNode and functions as scene graph root node
// Size is synchronized with window size
// Create tcApp by inheriting this class
// =============================================================================

class App : public RectNode {
public:
    App() {
        // Auto-subscribe the virtual audio hooks. Subclasses just override
        // audioOut() / audioIn() — no need to write the listener boilerplate.
        // EventListener members RAII out when App is destroyed.
        audioOutListener_ = AudioEngine::getInstance().audioOut.listen(
            [this](AudioOutBuffer& b) { audioOut(b); });
        audioInListener_  = AudioEngine::getInstance().audioIn.listen(
            [this](AudioInBuffer& b) { audioIn(b); });
    }

    virtual ~App() = default;

    // -------------------------------------------------------------------------
    // Size (synchronized with window)
    // -------------------------------------------------------------------------

    // Override setSize to resize window
    void setSize(float w, float h) override {
        setWindowSize(static_cast<int>(w), static_cast<int>(h));
        // Actual size update happens in windowResized callback
    }

    // -------------------------------------------------------------------------
    // Exit request (for programmatic termination)
    // -------------------------------------------------------------------------

    /// Request application exit (works in both windowed and headless mode)
    void requestExit() { exitRequested_ = true; }

    /// Check if exit has been requested
    bool isExitRequested() const { return exitRequested_; }

private:
    bool exitRequested_ = false;

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
    // alongside the App override.
    virtual void audioOut(AudioOutBuffer& buf) { (void)buf; }
    virtual void audioIn(const AudioInBuffer& buf) { (void)buf; }

private:
    EventListener audioOutListener_;
    EventListener audioInListener_;
public:

    // -------------------------------------------------------------------------
    // Event handlers (called by TrussC.h, dispatches to scene graph)
    // -------------------------------------------------------------------------

    void handleKeyPressed(const KeyEventArgs& e) {
        keyPressed(e);
        dispatchKeyPress(e);
    }

    void handleKeyReleased(const KeyEventArgs& e) {
        keyReleased(e);
        dispatchKeyRelease(e);
    }

    void handleMousePressed(const MouseEventArgs& e) {
        mousePressed(e);
        dispatchMousePress(e);
    }

    void handleMouseReleased(const MouseEventArgs& e) {
        mouseReleased(e);
        dispatchMouseRelease(e);
    }

    void handleMouseMoved(const MouseEventArgs& e) {
        mouseMoved(toMoveArgs(e));
        dispatchMouseMove(e);
    }

    void handleMouseDragged(const MouseEventArgs& e) {
        mouseDragged(toDragArgs(e));
        dispatchMouseMove(e);  // drag + hover share the node-tree move dispatch
    }

    void handleMouseScrolled(const ScrollEventArgs& e) {
        mouseScrolled(e);
        dispatchMouseScroll(e);
    }

    void handleWindowResized(int width, int height) {
        // Update internal size (call RectNode::setSize directly, not our override)
        RectNode::setSize(static_cast<float>(width), static_cast<float>(height));
        // Call user callback
        windowResized(width, height);
    }

    void handleUpdate(int mouseX, int mouseY) {
        updateTree();
        updateHoverState((float)mouseX, (float)mouseY);
    }

    void handleDraw() {
        drawTree();
    }
};

} // namespace trussc
