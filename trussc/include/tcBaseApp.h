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
};

} // namespace trussc
