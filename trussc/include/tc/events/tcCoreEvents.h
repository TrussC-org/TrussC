#pragma once

// =============================================================================
// tcCoreEvents - Framework core events
// =============================================================================

#include "tcEvent.h"
#include "tcEventArgs.h"

namespace trussc {

// ---------------------------------------------------------------------------
// CoreEvents - Framework core events
// ---------------------------------------------------------------------------
class CoreEvents {
public:
    // App lifecycle
    Event<void> setup;            // After setup completes
    Event<void> update;           // Before update each frame
    Event<void> draw;             // Before draw each frame
    Event<void> exit;             // On app exit

    // Keyboard
    Event<KeyEventArgs> keyPressed;
    Event<KeyEventArgs> keyReleased;

    // Mouse
    Event<MouseEventArgs> mousePressed;
    Event<MouseEventArgs> mouseReleased;
    Event<MouseMoveEventArgs> mouseMoved;
    Event<MouseDragEventArgs> mouseDragged;
    Event<ScrollEventArgs> mouseScrolled;

    // Window
    Event<ResizeEventArgs> windowResized;

    // Drag & drop
    Event<DragDropEventArgs> filesDropped;

    // Console input (commands from stdin)
    Event<ConsoleEventArgs> console;

    // For future use
    // Event<TouchEventArgs> touchBegan;
    // Event<TouchEventArgs> touchMoved;
    // Event<TouchEventArgs> touchEnded;
};

// ---------------------------------------------------------------------------
// Global accessor
// ---------------------------------------------------------------------------
inline CoreEvents& events() {
    static CoreEvents instance;
    return instance;
}

} // namespace trussc
