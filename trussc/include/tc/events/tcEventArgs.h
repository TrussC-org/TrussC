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
// Touch event arguments (for future use)
// ---------------------------------------------------------------------------
struct TouchEventArgs {
    int id = 0;               // Touch ID
    float x = 0.0f;
    float y = 0.0f;
    float pressure = 1.0f;
};

// ---------------------------------------------------------------------------
// Console input event arguments (commands from stdin)
// ---------------------------------------------------------------------------
struct ConsoleEventArgs {
    std::string raw;               // Raw input line (e.g., "tcdebug screenshot /tmp/a.png")
    std::vector<std::string> args; // Parsed by whitespace (e.g., ["tcdebug", "screenshot", "/tmp/a.png"])
};

} // namespace trussc
