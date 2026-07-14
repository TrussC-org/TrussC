#pragma once

// =============================================================================
// tcCoreEvents - Framework core events
// =============================================================================

#include "tcEvent.h"
#include "tcEventArgs.h"
#include "sokol/sokol_app_tc.h"

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
    Event<void> afterFrame;        // After present() (swapchain committed, outside any pass)
    Event<void> exit;             // On app exit

    // Exit request (can be cancelled)
    Event<ExitRequestEventArgs> exitRequested;  // Set args.cancel = true to cancel

    // Keyboard
    Event<KeyEventArgs> keyPressed;
    Event<KeyEventArgs> keyReleased;

    // Mouse (one struct per kind so no field is ever meaningless)
    Event<MouseEventArgs> mousePressed;
    Event<MouseEventArgs> mouseReleased;
    Event<MouseMoveEventArgs> mouseMoved;
    Event<MouseDragEventArgs> mouseDragged;
    Event<ScrollEventArgs> mouseScrolled;

    // Window
    Event<ResizeEventArgs> windowResized;

    // Drag & drop
    Event<DragDropEventArgs> filesDropped;

    // Clipboard paste (Cmd+V / Ctrl+V / browser paste); args.text holds the content
    Event<ClipboardPastedEventArgs> clipboardPasted;

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
// Global accessor (non-inline: Host/Guest share the same instance on Windows)
// ---------------------------------------------------------------------------
CoreEvents& events();

} // namespace trussc
