#pragma once

// =============================================================================
// tcMouseGlobal.h - global (window-space) mouse state + getters
// =============================================================================
// The raw mouse position state and its window-space getters. Extracted from
// TrussC.h so lower-level headers (e.g. tcNode.h, which converts global mouse
// coords to node-local) can depend on them directly, in dependency order,
// instead of pulling the whole umbrella. Apps include <TrussC.h>; the framework
// event loop writes internal::currentWindowContext().mouseX/Y each frame. This is an internal piece.
// =============================================================================

#include "tcMath.h"   // Vec2

namespace trussc {

// Mouse position state (mouseX/Y, pmouseX/Y) lives in WindowContext
// (tc/app/tcWindowContext.h, included from TrussC.h before this header);
// the framework event loop writes the current window's members each frame.

// ---------------------------------------------------------------------------
// Mouse state (global / window coordinates)
// ---------------------------------------------------------------------------

// Current mouse X coordinate (window coordinates)
inline float getGlobalMouseX() {
    auto* ctx = internal::findWindowApiContext();
    return ctx ? ctx->mouseX : 0;
}

// Current mouse Y coordinate (window coordinates)
inline float getGlobalMouseY() {
    auto* ctx = internal::findWindowApiContext();
    return ctx ? ctx->mouseY : 0;
}

// Previous frame mouse X coordinate (window coordinates)
inline float getGlobalPMouseX() {
    auto* ctx = internal::findWindowApiContext();
    return ctx ? ctx->pmouseX : 0;
}

// Previous frame mouse Y coordinate (window coordinates)
inline float getGlobalPMouseY() {
    auto* ctx = internal::findWindowApiContext();
    return ctx ? ctx->pmouseY : 0;
}

// Alias for getGlobalMouseX/Y (for tcDebugInput)
inline float getMouseX() { return getGlobalMouseX(); }
inline float getMouseY() { return getGlobalMouseY(); }
inline Vec2 getMousePos() { return Vec2(getGlobalMouseX(), getGlobalMouseY()); }
inline Vec2 getGlobalMousePos() { return Vec2(getGlobalMouseX(), getGlobalMouseY()); }

} // namespace trussc
