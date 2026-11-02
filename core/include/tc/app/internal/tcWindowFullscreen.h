#pragma once

#include <cstdint>
#include "../../../sokol/util/sokol_window_fullscreen.h"

namespace trussc::internal {
enum class WindowGeometryCall {
    GlobalSize, GlobalLogicalSize, GlobalPosition, WindowSize, WindowPosition, AppSize
};
using WindowFullscreenPhase = _sapp_tc_fullscreen_phase;
using WindowFullscreenState = _sapp_tc_fullscreen_state_t;

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

}
