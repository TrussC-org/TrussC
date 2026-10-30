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
// On events the App receives, listen() defaults to BeforeApp; other events keep 100.
// ---------------------------------------------------------------------------
class CoreEvents {
public:
    // App lifecycle
    Event<void> setup;            // After setup completes
    Event<void> update{EventPriority::BeforeApp};           // Before update each frame
    Event<void> draw{EventPriority::BeforeApp};             // Before draw each frame
    Event<void> onRender;          // After sokol_gl flush, render pass still active
    Event<void> afterFrame;        // After present() (swapchain committed, outside any pass)
    Event<void> exit{EventPriority::BeforeApp};             // On app exit
    // Runtime lifecycle fan-out to the attached App, before its replacement.
    Event<void> hotReloadUnload;

    // Exit request (can be cancelled)
    Event<ExitRequestEventArgs> exitRequested;  // Set args.cancel = true to cancel

    // Shared D3D11 device lost (once); cancel suppresses the default failure exit.
    Event<DeviceLostEventArgs> deviceLost;

    // Keyboard
    Event<KeyEventArgs> keyPressed{EventPriority::BeforeApp};
    Event<KeyEventArgs> keyReleased{EventPriority::BeforeApp};

    // Mouse (one struct per kind so no field is ever meaningless)
    Event<MouseEventArgs> mousePressed{EventPriority::BeforeApp};
    Event<MouseEventArgs> mouseReleased{EventPriority::BeforeApp};
    Event<MouseMoveEventArgs> mouseMoved{EventPriority::BeforeApp};
    Event<MouseDragEventArgs> mouseDragged{EventPriority::BeforeApp};
    Event<ScrollEventArgs> mouseScrolled{EventPriority::BeforeApp};

    // OS close only; code close and process shutdown bypass this query.
    Event<CloseRequestEventArgs> closeRequested;

    // Window
    Event<ResizeEventArgs> windowResized{EventPriority::BeforeApp};
    Event<OccludedEventArgs> occludedChanged; // Explicit listeners for effective-hidden changes

    // Drag & drop
    Event<DragDropEventArgs> filesDropped{EventPriority::BeforeApp};

    // Clipboard paste (Cmd+V / Ctrl+V / browser paste); args.text holds the content
    Event<ClipboardPastedEventArgs> clipboardPasted;

    // Touch (multi-touch, used on Android/iOS)
    Event<TouchEventArgs> touchPressed{EventPriority::BeforeApp};
    Event<TouchEventArgs> touchMoved{EventPriority::BeforeApp};
    Event<TouchEventArgs> touchReleased{EventPriority::BeforeApp};   // Also fired on cancellation — check args.cancelled

    // Raw sokol event (for addons that need the full sapp_event)
    Event<const sapp_event> rawEvent;
};

// Process events are independent of drawing and Window count. Lifecycle events
// notify Runtime first, then the one reverse-attachment App snapshot.
class RuntimeEvents {
public:
    Event<ExitRequestEventArgs> exitRequested;
    Event<DeviceLostEventArgs> deviceLost;
    Event<void> hotReloadUnload;
    Event<ConsoleEventArgs> console;
};

class Runtime {
public:
    RuntimeEvents& events();
private:
    Runtime() = default;
    friend Runtime& runtime();
};
Runtime& runtime();

// ---------------------------------------------------------------------------
// Global accessor (non-inline: Host/Guest share the same instance on Windows)
// ---------------------------------------------------------------------------
CoreEvents& events();

} // namespace trussc
