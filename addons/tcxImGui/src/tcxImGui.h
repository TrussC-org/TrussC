#pragma once

// =============================================================================
// tcxImGui.h - Dear ImGui addon for TrussC
// =============================================================================

#include <TrussC.h>
#include "imgui/imgui.h"
#include "sokol_imgui.h"
#include "tcImGuiHooks.h"
#include "tcImGuiTools.h"

namespace tcx {

// ---------------------------------------------------------------------------
// ImGui manager
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

        // Listen to beforePresent for rendering
        renderListener_ = tc::events().onRender.listen([this]() {
            if (renderPending_) {
                simgui_render();
                renderPending_ = false;
            }
        }, 1000);

        // Listen to rawEvent for input handling
        eventListener_ = tc::events().rawEvent.listen([this](const sapp_event& ev) {
            simgui_handle_event(&ev);
        }, tc::EventPriority::BeforeApp);

        tc::logVerbose() << "ImGui initialized";
    }

    // Shutdown
    void shutdown() {
        if (!initialized_) return;
        renderListener_ = {};
        eventListener_ = {};
        simgui_shutdown();
        initialized_ = false;
        tc::logVerbose() << "ImGui shutdown";
    }

    // Begin frame (call at start of draw)
    void begin() {
        if (!initialized_) return;

        simgui_frame_desc_t desc = {};
        desc.width = sapp_width();
        desc.height = sapp_height();
        desc.delta_time = static_cast<float>(sapp_frame_duration());
        desc.dpi_scale = sapp_dpi_scale();
        simgui_new_frame(&desc);
    }

    // End frame (defers render to beforePresent)
    void end() {
        if (!initialized_) return;
        renderPending_ = true;
    }

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
    bool renderPending_ = false;
    tc::EventListener renderListener_;
    tc::EventListener eventListener_;
};

// ---------------------------------------------------------------------------
// Convenience functions
// ---------------------------------------------------------------------------

inline void imguiSetup() {
    ImGuiManager::instance().setup();
}

inline void imguiShutdown() {
    ImGuiManager::instance().shutdown();
}

inline void imguiBegin() {
    ImGuiManager::instance().begin();
    trussc::imgui_tools::beginFrame();
}

inline void imguiEnd() {
    ImGuiManager::instance().end();
    trussc::imgui_tools::swapFrames();
}

inline bool imguiWantsMouse() {
    return ImGui::GetIO().WantCaptureMouse;
}

inline bool imguiWantsKeyboard() {
    return ImGui::GetIO().WantCaptureKeyboard;
}

} // namespace tcx
