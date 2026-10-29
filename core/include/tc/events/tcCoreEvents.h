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
    // Hot reload only: the host is about to unload the current guest
    // generation (its App is still alive). Fired on each reload, and when the
    // host unloads the last generation at exit (after `exit`). Code in the
    // guest with state that outlives the App (function-local statics,
    // singletons) drops its listeners on the host's events here: a guest
    // image stays loaded, so nothing else removes them. Never fired in a
    // normal (non hot reload) build.
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

    // Window
    Event<ResizeEventArgs> windowResized{EventPriority::BeforeApp};
    Event<OccludedEventArgs> occludedChanged; // Explicit listeners for effective-hidden changes

    // Drag & drop
    Event<DragDropEventArgs> filesDropped{EventPriority::BeforeApp};

    // Clipboard paste (Cmd+V / Ctrl+V / browser paste); args.text holds the content
    Event<ClipboardPastedEventArgs> clipboardPasted;

    // Console input (commands from stdin)
    Event<ConsoleEventArgs> console;

    // Touch (multi-touch, used on Android/iOS)
    Event<TouchEventArgs> touchPressed{EventPriority::BeforeApp};
    Event<TouchEventArgs> touchMoved{EventPriority::BeforeApp};
    Event<TouchEventArgs> touchReleased{EventPriority::BeforeApp};   // Also fired on cancellation — check args.cancelled

    // Raw sokol event (for addons that need the full sapp_event)
    Event<const sapp_event> rawEvent;
};

// ---------------------------------------------------------------------------
// Global accessor (non-inline: Host/Guest share the same instance on Windows)
// ---------------------------------------------------------------------------
CoreEvents& events();

} // namespace trussc
