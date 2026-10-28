#pragma once
#include "tcWindowGeometryState.h"

namespace trussc {
class Window;
namespace internal {
// Per-window production operations are replaceable without creating a native
// window. No query may acquire a drawable, allocate GPU resources or call App code.
struct WindowNativeOperations {
    bool (*readMetadata)(sapp_window, void*, sapp_tc_window_metadata&);
    sg_swapchain (*acquireSwapchain)(sapp_window, void*);
    void (*activate)(sapp_window, void*);
};
struct WindowBinding {
    sapp_window handle{};
    const WindowNativeOperations* operations = nullptr;
    void* user = nullptr;
    sapp_tc_window_metadata metadata{};
    bool bound = false;
    WindowGeometryState geometry;
    bool geometryInitialized = false;
};
Window* mainWindow();
WindowContext& nonWindowContext();
WindowContext* nonWindowContextIfInitialized() noexcept;
const WindowNativeOperations& productionWindowOperations();
bool bindWindow(Window& window, sapp_window handle, const WindowNativeOperations& operations, void* user);
bool bindNativeWindow(uint32_t id, void* window) noexcept;
void refreshWindowMetadata(Window& window);
void publishWindowMetadata(Window& window, const sapp_tc_window_metadata& metadata);
void syncWindowAppSize(Window& window, bool initial = false);
void refreshMainWindowMetadata();
// Single-window ports retain their native bootstrap and lifecycle boundary.
void bindSingleWindowMain();
}
}
