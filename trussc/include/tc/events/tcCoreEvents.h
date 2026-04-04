#pragma once

// =============================================================================
// tcCoreEvents - Framework core events
// =============================================================================

#include "tcEvent.h"
#include "tcEventArgs.h"
#include "sokol/sokol_app.h"

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
    Event<void> onRender;          // After sokol_gl flush, render pass still active
    Event<void> exit;             // On app exit

    // Exit request (can be cancelled)
    Event<ExitRequestEventArgs> exitRequested;  // Set args.cancel = true to cancel

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

    // Touch (multi-touch, used on Android/iOS)
    Event<TouchEventArgs> touchPressed;
    Event<TouchEventArgs> touchMoved;
    Event<TouchEventArgs> touchReleased;   // Also fired on cancellation — check args.cancelled

    // Raw sokol event (for addons that need the full sapp_event)
    Event<const sapp_event> rawEvent;
};

// ---------------------------------------------------------------------------
// Global accessor
// ---------------------------------------------------------------------------
inline CoreEvents& events() {
    static CoreEvents instance;
    return instance;
}

} // namespace trussc
