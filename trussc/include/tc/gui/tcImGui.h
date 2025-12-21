#pragma once

// =============================================================================
// tcImGui.h - Dear ImGui integration
// Wrapper for sokol_imgui
// =============================================================================

#include "imgui/imgui.h"
#include "sokol/sokol_imgui.h"
#include "tc/utils/tcLog.h"

namespace trussc {

// Access to imguiEnabled flag in internal namespace
namespace internal {
    extern bool imguiEnabled;
}

// ---------------------------------------------------------------------------
// ImGui manager class
// ---------------------------------------------------------------------------
class ImGuiManager {
public:
    // Initialize (call in setup)
    void setup() {
        if (initialized_) return;

        simgui_desc_t desc = {};
        desc.logger.func = slog_func;
        simgui_setup(&desc);

        initialized_ = true;
        internal::imguiEnabled = true;
        tcLogVerbose() << "ImGui initialized";
    }

    // Shutdown (called automatically)
    void shutdown() {
        if (!initialized_) return;
        simgui_shutdown();
        initialized_ = false;
        internal::imguiEnabled = false;
        tcLogVerbose() << "ImGui shutdown";
    }

    // Begin frame (call at start of draw)
    void begin(int width, int height, float deltaTime) {
        if (!initialized_) return;

        simgui_frame_desc_t desc = {};
        desc.width = width;
        desc.height = height;
        desc.delta_time = deltaTime;
        desc.dpi_scale = sapp_dpi_scale();
        simgui_new_frame(&desc);
    }

    // End frame (call at end of draw)
    void end() {
        if (!initialized_) return;
        simgui_render();
    }

    // Event handling (called automatically internally)
    bool handleEvent(const sapp_event* event) {
        if (!initialized_) return false;
        return simgui_handle_event(event);
    }

    // Is initialized
    bool isInitialized() const { return initialized_; }

    // Singleton access
    static ImGuiManager& instance() {
        static ImGuiManager mgr;
        return mgr;
    }

private:
    ImGuiManager() = default;
    ~ImGuiManager() { shutdown(); }

    bool initialized_ = false;
};

// ---------------------------------------------------------------------------
// Convenience functions
// ---------------------------------------------------------------------------

// ImGui initialization
inline void imguiSetup() {
    ImGuiManager::instance().setup();
}

// ImGui shutdown
inline void imguiShutdown() {
    ImGuiManager::instance().shutdown();
}

// Begin frame
inline void imguiBegin() {
    int w = sapp_width();
    int h = sapp_height();
    float dt = static_cast<float>(sapp_frame_duration());
    ImGuiManager::instance().begin(w, h, dt);
}

// End frame (render)
inline void imguiEnd() {
    ImGuiManager::instance().end();
}

// Event handling (internal use)
inline bool imguiHandleEvent(const sapp_event* event) {
    return ImGuiManager::instance().handleEvent(event);
}

// Is ImGui using mouse input
inline bool imguiWantsMouse() {
    return ImGui::GetIO().WantCaptureMouse;
}

// Is ImGui using keyboard input
inline bool imguiWantsKeyboard() {
    return ImGui::GetIO().WantCaptureKeyboard;
}

} // namespace trussc
