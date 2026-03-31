#pragma once

// =============================================================================
// tcEventArgs - Event argument structures
// =============================================================================

#include <vector>
#include <string>

namespace trussc {

// ---------------------------------------------------------------------------
// Key event arguments
// ---------------------------------------------------------------------------
struct KeyEventArgs {
    int key = 0;              // Key code (SAPP_KEYCODE_*)
    bool isRepeat = false;    // Whether this is a repeat input
    bool shift = false;       // Shift key
    bool ctrl = false;        // Ctrl key
    bool alt = false;         // Alt key
    bool super = false;       // Super/Command key
};

// ---------------------------------------------------------------------------
// Mouse button event arguments
// ---------------------------------------------------------------------------
struct MouseEventArgs {
    float x = 0.0f;           // Mouse X coordinate
    float y = 0.0f;           // Mouse Y coordinate
    int button = 0;           // Button number
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    bool super = false;
};

// ---------------------------------------------------------------------------
// Mouse move event arguments
// ---------------------------------------------------------------------------
struct MouseMoveEventArgs {
    float x = 0.0f;           // Current X coordinate
    float y = 0.0f;           // Current Y coordinate
    float deltaX = 0.0f;      // Delta X
    float deltaY = 0.0f;      // Delta Y
};

// ---------------------------------------------------------------------------
// Mouse drag event arguments
// ---------------------------------------------------------------------------
struct MouseDragEventArgs {
    float x = 0.0f;
    float y = 0.0f;
    float deltaX = 0.0f;
    float deltaY = 0.0f;
    int button = 0;           // Button being dragged
};

// ---------------------------------------------------------------------------
// Mouse scroll event arguments
// ---------------------------------------------------------------------------
struct ScrollEventArgs {
    float scrollX = 0.0f;     // Horizontal scroll amount
    float scrollY = 0.0f;     // Vertical scroll amount
};

// ---------------------------------------------------------------------------
// Window resize event arguments
// ---------------------------------------------------------------------------
struct ResizeEventArgs {
    int width = 0;
    int height = 0;
};

// ---------------------------------------------------------------------------
// Drag & drop event arguments (for future use)
// ---------------------------------------------------------------------------
struct DragDropEventArgs {
    std::vector<std::string> files;  // Dropped file paths
    float x = 0.0f;
    float y = 0.0f;
};

// ---------------------------------------------------------------------------
// Touch point (single finger)
// ---------------------------------------------------------------------------
struct TouchPoint {
    int id = 0;               // Touch ID (persistent across move)
    float x = 0.0f;
    float y = 0.0f;
    float pressure = 1.0f;    // Touch pressure (0.0-1.0, default 1.0; not yet reported by sokol)
    bool changed = false;     // Whether this touch was part of the current action
};

// ---------------------------------------------------------------------------
// Touch event arguments (multi-touch)
// ---------------------------------------------------------------------------
struct TouchEventArgs {
    static constexpr int MAX_TOUCHES = 8;  // Matches SAPP_MAX_TOUCHPOINTS
    TouchPoint touches[MAX_TOUCHES];
    int numTouches = 0;
    bool cancelled = false;   // true when touchReleased is due to system cancellation
                              // (e.g. incoming call, system gesture). Released is still
                              // fired — check this flag for cancellation-specific handling.

    // Convenience: first touch
    float x() const { return numTouches > 0 ? touches[0].x : 0.0f; }
    float y() const { return numTouches > 0 ? touches[0].y : 0.0f; }
    int id() const { return numTouches > 0 ? touches[0].id : 0; }
};

// ---------------------------------------------------------------------------
// Console input event arguments (commands from stdin)
// ---------------------------------------------------------------------------
struct ConsoleEventArgs {
    std::string raw;               // Raw input line (e.g., "tcdebug screenshot /tmp/a.png")
    std::vector<std::string> args; // Parsed by whitespace (e.g., ["tcdebug", "screenshot", "/tmp/a.png"])
};

// ---------------------------------------------------------------------------
// Exit request event arguments
// ---------------------------------------------------------------------------
struct ExitRequestEventArgs {
    bool cancel = false;           // Set to true to cancel the exit
};

} // namespace trussc
