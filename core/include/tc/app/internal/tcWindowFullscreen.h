#pragma once

#include <cstdint>

namespace trussc::internal {
enum class WindowGeometryCall {
    GlobalSize, GlobalLogicalSize, GlobalPosition, WindowSize, WindowPosition, AppSize
};
enum class WindowFullscreenPhase { Windowed, Entering, Fullscreen, Exiting };

// Fullscreen notifications only update this plain state. Native callers toggle
// when request()/reconcile() returns true; reconciliation runs on a normal tick.
struct WindowFullscreenState {
    WindowFullscreenPhase phase = WindowFullscreenPhase::Windowed;
    bool desired = false;
    bool reconcilePending = false;
    bool transitioning() const noexcept {
        return phase == WindowFullscreenPhase::Entering || phase == WindowFullscreenPhase::Exiting;
    }
    bool blocksGeometry() const noexcept { return phase != WindowFullscreenPhase::Windowed; }
    bool request(bool full) noexcept {
        desired = full;
        reconcilePending = false;
        if (transitioning() || full == (phase == WindowFullscreenPhase::Fullscreen)) return false;
        phase = full ? WindowFullscreenPhase::Entering : WindowFullscreenPhase::Exiting;
        return true;
    }
    void will(bool full) noexcept {
        // A pre-toggle request already owns the desired state, including a
        // reversal. A user/menu transition supplies its own intent instead.
        if (!transitioning()) desired = full;
        phase = full ? WindowFullscreenPhase::Entering : WindowFullscreenPhase::Exiting;
    }
    void terminal(bool observedFullscreen) noexcept {
        const bool destination = phase == WindowFullscreenPhase::Entering;
        const bool reversed = transitioning() && desired != destination;
        phase = observedFullscreen ? WindowFullscreenPhase::Fullscreen : WindowFullscreenPhase::Windowed;
        // Failures settle without retrying the failed direction. Only a
        // reversal recorded during the animation can need one reconciliation.
        reconcilePending = reversed && desired != observedFullscreen;
        if (!reconcilePending) desired = observedFullscreen;
    }
    bool reconcile() noexcept {
        if (!reconcilePending) return false;
        reconcilePending = false;
        return request(desired);
    }
};

inline const char* windowGeometryCallName(WindowGeometryCall call) noexcept {
    switch (call) {
        case WindowGeometryCall::GlobalSize: return "setWindowSize()";
        case WindowGeometryCall::GlobalLogicalSize: return "setWindowSizeLogical()";
        case WindowGeometryCall::GlobalPosition: return "setWindowPosition()";
        case WindowGeometryCall::WindowSize: return "Window::setSize()";
        case WindowGeometryCall::WindowPosition: return "Window::setPosition()";
        case WindowGeometryCall::AppSize: return "App::setSize()";
    }
    return "";
}
inline const char* windowGeometryIgnoredReason(WindowFullscreenPhase phase) noexcept {
    switch (phase) {
        case WindowFullscreenPhase::Entering: return " ignored: the window is entering fullscreen";
        case WindowFullscreenPhase::Exiting: return " ignored: the window is still leaving fullscreen; call it once isFullscreen() returns false";
        case WindowFullscreenPhase::Fullscreen: return " ignored while fullscreen; call it once isFullscreen() returns false";
        case WindowFullscreenPhase::Windowed: return nullptr;
    }
    return nullptr;
}

#if defined(__APPLE__)
// Implemented beside the implementation-owned sokol main/secondary state.
WindowFullscreenPhase macWindowFullscreenPhase(uint32_t id = 0) noexcept;
void setMacWindowFullscreen(uint32_t id, bool full) noexcept;
#endif
}
