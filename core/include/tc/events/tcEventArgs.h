#pragma once

// =============================================================================
// tcEventArgs - Event argument structures
// =============================================================================

#include <vector>
#include <string>
#include "tcMath.h"   // Vec2

namespace trussc {

// ---------------------------------------------------------------------------
// Mouse button identifier (values match SAPP_MOUSEBUTTON_*)
// ---------------------------------------------------------------------------
enum class MouseButton {
    Left   = 0,      // SAPP_MOUSEBUTTON_LEFT
    Right  = 1,      // SAPP_MOUSEBUTTON_RIGHT
    Middle = 2,      // SAPP_MOUSEBUTTON_MIDDLE
    None   = 0x100,  // SAPP_MOUSEBUTTON_INVALID (no button; e.g. during a plain move)
};

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
// Mouse event arguments
// ---------------------------------------------------------------------------
// There is one struct per event kind so that no field is ever meaningless
// (a move carries no button, a press carries no delta, etc.).
//
// Coordinate convention (the rich Vec2 fields):
//   - `pos`       : position in the receiving node's LOCAL space.
//   - `globalPos` : position in SCREEN space.
//   When handled at app level (events().mouseXxx or App::mouseXxx) there is no
//   node transform, so `pos == globalPos`. Inside a Node's onMouseXxx the two
//   differ by that node's transform.
//   - `delta`/`globalDelta` follow the same local/screen split (movement since
//   the previous event).
//
// Legacy scalar fields (`x`/`y`/`deltaX`/`deltaY`/`scrollX`/`scrollY` and the
// int `button`) mirror the Vec2 fields and are kept for source compatibility
// with pre-rich code. They are synced from the canonical Vec2 fields via
// syncLegacy(). These mirrors are scheduled to be removed at v1.0 (a migration
// guide will accompany the cut); new code should prefer the Vec2 forms.
// `button` stays an int (compare with MOUSE_BUTTON_*, consistent with KEY_*);
// the MouseButton enum is the type-safe source of those constants.

// Mouse pressed / released
struct MouseEventArgs {
    // Legacy mirrors (source-compat; removed at v1.0)
    float x = 0.0f;           // == pos.x
    float y = 0.0f;           // == pos.y
    int button = 0;           // MOUSE_BUTTON_* (left/right/middle)
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    bool super = false;
    // Rich (canonical)
    Vec2 pos;                 // Local position (== globalPos at app level)
    Vec2 globalPos;           // Screen position
    // No delta here: a press/release has no movement of its own (the cursor's
    // travel is delivered by the preceding mouseMoved/mouseDragged). Movement
    // lives on MouseMoveEventArgs / MouseDragEventArgs.

    // Sync legacy scalar mirrors from the canonical Vec2 fields.
    void syncLegacy() { x = pos.x; y = pos.y; }
};

// Mouse moved (no button)
struct MouseMoveEventArgs {
    // Legacy mirrors (source-compat; removed at v1.0)
    float x = 0.0f;           // == pos.x
    float y = 0.0f;           // == pos.y
    float deltaX = 0.0f;      // == delta.x
    float deltaY = 0.0f;      // == delta.y
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    bool super = false;
    // Rich (canonical)
    Vec2 pos;
    Vec2 globalPos;
    Vec2 delta;               // Movement since last event, local space
    Vec2 globalDelta;         // Movement since last event, screen space

    void syncLegacy() { x = pos.x; y = pos.y; deltaX = delta.x; deltaY = delta.y; }
};

// Mouse dragged (move with a button held)
struct MouseDragEventArgs {
    // Legacy mirrors (source-compat; removed at v1.0)
    float x = 0.0f;           // == pos.x
    float y = 0.0f;           // == pos.y
    float deltaX = 0.0f;      // == delta.x
    float deltaY = 0.0f;      // == delta.y
    int button = 0;           // MOUSE_BUTTON_* being dragged
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    bool super = false;
    // Rich (canonical)
    Vec2 pos;
    Vec2 globalPos;
    Vec2 delta;
    Vec2 globalDelta;

    void syncLegacy() { x = pos.x; y = pos.y; deltaX = delta.x; deltaY = delta.y; }
};

// ---------------------------------------------------------------------------
// Mouse scroll event arguments
// ---------------------------------------------------------------------------
struct ScrollEventArgs {
    // Legacy mirrors (source-compat; removed at v1.0)
    float scrollX = 0.0f;     // == scroll.x
    float scrollY = 0.0f;     // == scroll.y
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    bool super = false;
    // Rich (canonical)
    Vec2 pos;                 // Local position of the cursor (== globalPos at app level)
    Vec2 globalPos;           // Screen position of the cursor
    Vec2 scroll;              // Scroll amount (x: horizontal, y: vertical)

    void syncLegacy() { scrollX = scroll.x; scrollY = scroll.y; }
};

// ---------------------------------------------------------------------------
// Internal mouse-move carrier
// ---------------------------------------------------------------------------
// move/drag dispatch flows a single rich carrier through the plumbing (OS ->
// App -> node tree); the public move/drag boundary builds the specific type
// from it. This is internal: press/release never carry movement, so they use
// the lean public MouseEventArgs directly and never touch this type.
struct MouseEventRaw {
    Vec2 pos;                 // Local position (== globalPos at app level)
    Vec2 globalPos;           // Screen position
    Vec2 delta;               // Movement since last event, local space
    Vec2 globalDelta;         // Movement since last event, screen space
    int button = 0;           // MOUSE_BUTTON_* held during a drag (0 on a move)
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    bool super = false;
};

// ---------------------------------------------------------------------------
// Carrier -> per-event converters
// ---------------------------------------------------------------------------
inline MouseMoveEventArgs toMoveArgs(const MouseEventRaw& m) {
    MouseMoveEventArgs a;
    a.pos = m.pos; a.globalPos = m.globalPos;
    a.delta = m.delta; a.globalDelta = m.globalDelta;
    a.shift = m.shift; a.ctrl = m.ctrl; a.alt = m.alt; a.super = m.super;
    a.syncLegacy();
    return a;
}

inline MouseDragEventArgs toDragArgs(const MouseEventRaw& m) {
    MouseDragEventArgs a;
    a.pos = m.pos; a.globalPos = m.globalPos;
    a.delta = m.delta; a.globalDelta = m.globalDelta;
    a.button = m.button;
    a.shift = m.shift; a.ctrl = m.ctrl; a.alt = m.alt; a.super = m.super;
    a.syncLegacy();
    return a;
}

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
// Clipboard paste event arguments
// ---------------------------------------------------------------------------
// Fired on the platform paste gesture (macOS: Cmd+V, others: Ctrl+V, Web:
// browser 'paste' event). `text` is the pasted clipboard content, already
// read for you — no need to call getClipboardString() yourself. This is the
// only reliable way to read the clipboard on the Web platform, where arbitrary
// getClipboardString() calls are blocked by the browser.
struct ClipboardPastedEventArgs {
    std::string text;  // Pasted clipboard content
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
