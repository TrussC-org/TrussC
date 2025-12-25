#pragma once

#include "tcNode.h"
#include "tc/types/tcRectNode.h"
#include <vector>
#include <string>

// =============================================================================
// trussc namespace
// =============================================================================
namespace trussc {

// =============================================================================
// App - Application base class
// Inherits from tc::Node and functions as scene graph root node
// Create tcApp by inheriting this class
// =============================================================================

class App : public Node {
public:
    virtual ~App() = default;

    // -------------------------------------------------------------------------
    // Lifecycle (inherited from Node, additional overrides possible)
    // -------------------------------------------------------------------------

    // setup(), update(), draw(), cleanup() are inherited from Node

    // -------------------------------------------------------------------------
    // Keyboard events (traditional style)
    // -------------------------------------------------------------------------

    virtual void keyPressed(int key) {
        (void)key;
    }

    virtual void keyReleased(int key) {
        (void)key;
    }

    // -------------------------------------------------------------------------
    // Mouse events (delivered in screen coordinates)
    // -------------------------------------------------------------------------

    virtual void mousePressed(Vec2 pos, int button) {
        (void)pos;
        (void)button;
    }

    virtual void mouseReleased(Vec2 pos, int button) {
        (void)pos;
        (void)button;
    }

    virtual void mouseMoved(Vec2 pos) {
        (void)pos;
    }

    virtual void mouseDragged(Vec2 pos, int button) {
        (void)pos;
        (void)button;
    }

    virtual void mouseScrolled(Vec2 delta) {
        (void)delta;
    }

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
    // Event handlers (called by TrussC.h, dispatches to scene graph)
    // -------------------------------------------------------------------------

    void handleKeyPressed(int key) {
        keyPressed(key);
        dispatchKeyPress(key);
    }

    void handleKeyReleased(int key) {
        keyReleased(key);
        dispatchKeyRelease(key);
    }

    void handleMousePressed(int x, int y, int button) {
        mousePressed(Vec2(x, y), button);
        dispatchMousePress((float)x, (float)y, button);
    }

    void handleMouseReleased(int x, int y, int button) {
        mouseReleased(Vec2(x, y), button);
        dispatchMouseRelease((float)x, (float)y, button);
    }

    void handleMouseMoved(int x, int y) {
        mouseMoved(Vec2(x, y));
        dispatchMouseMove((float)x, (float)y);
    }

    void handleMouseDragged(int x, int y, int button) {
        mouseDragged(Vec2(x, y), button);
        dispatchMouseMove((float)x, (float)y);
    }

    void handleMouseScrolled(float dx, float dy, int mouseX, int mouseY) {
        mouseScrolled(Vec2(dx, dy));
        dispatchMouseScroll((float)mouseX, (float)mouseY, Vec2(dx, dy));
    }

    void handleUpdate(int mouseX, int mouseY) {
        updateTree();
        updateHoverState((float)mouseX, (float)mouseY);
    }
};

} // namespace trussc
