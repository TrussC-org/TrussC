#pragma once

// =============================================================================
// tcxImGui.h - Dear ImGui addon for TrussC
// =============================================================================

#include <TrussC.h>
#include "imgui/imgui.h"
#include "sokol_imgui.h"
#include "tcImGuiHooks.h"
#include "tcImGuiTools.h"
#include <cstdlib>
#include <string>

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

        // Overlay capture queries — tell the framework when imgui owns the
        // pointer / keyboard so node-tree hover is suppressed under panels and
        // user code can guard raw input (isOverlayHovered/isOverlayFocused).
        tc::internal::overlayHoveredQuery = []() { return ImGui::GetIO().WantCaptureMouse; };
        tc::internal::overlayFocusedQuery = []() { return ImGui::GetIO().WantCaptureKeyboard; };

        // Consume input before it reaches the node tree (BeforeApp). Pointer
        // capture follows the press: a gesture imgui claims on press stays
        // imgui's until release, so a drag begun on the canvas is never stolen
        // mid-way, and a press over a panel owns the whole gesture. Move/scroll
        // and keys are stateless — consumed whenever imgui wants them.
        mousePressConsume_ = tc::events().mousePressed.listen([this](tc::MouseEventArgs& e) {
            if (ImGui::GetIO().WantCaptureMouse) { pointerCaptured_ = true; e.consumed = true; }
        }, tc::EventPriority::BeforeApp);
        mouseReleaseConsume_ = tc::events().mouseReleased.listen([this](tc::MouseEventArgs& e) {
            if (pointerCaptured_) { e.consumed = true; pointerCaptured_ = false; }
        }, tc::EventPriority::BeforeApp);
        mouseDragConsume_ = tc::events().mouseDragged.listen([this](tc::MouseDragEventArgs& e) {
            if (pointerCaptured_) e.consumed = true;
        }, tc::EventPriority::BeforeApp);
        mouseMoveConsume_ = tc::events().mouseMoved.listen([](tc::MouseMoveEventArgs& e) {
            if (ImGui::GetIO().WantCaptureMouse) e.consumed = true;
        }, tc::EventPriority::BeforeApp);
        mouseScrollConsume_ = tc::events().mouseScrolled.listen([](tc::ScrollEventArgs& e) {
            if (ImGui::GetIO().WantCaptureMouse) e.consumed = true;
        }, tc::EventPriority::BeforeApp);
        keyPressConsume_ = tc::events().keyPressed.listen([](tc::KeyEventArgs& e) {
            if (ImGui::GetIO().WantCaptureKeyboard) e.consumed = true;
        }, tc::EventPriority::BeforeApp);
        keyReleaseConsume_ = tc::events().keyReleased.listen([](tc::KeyEventArgs& e) {
            if (ImGui::GetIO().WantCaptureKeyboard) e.consumed = true;
        }, tc::EventPriority::BeforeApp);

        // Auto-teardown at the framework's exit event, while sokol_gfx and the
        // event system are still alive. Apps don't need to call imguiShutdown()
        // themselves (calling it stays harmless — shutdown() is idempotent).
        exitListener_ = tc::events().exit.listen([this]() { shutdown(); });

        tc::logVerbose() << "ImGui initialized";
    }

    // Shutdown
    void shutdown() {
        if (!initialized_) return;
        exitListener_ = {};
        renderListener_ = {};
        eventListener_ = {};
        mousePressConsume_ = {};
        mouseReleaseConsume_ = {};
        mouseDragConsume_ = {};
        mouseMoveConsume_ = {};
        mouseScrollConsume_ = {};
        keyPressConsume_ = {};
        keyReleaseConsume_ = {};
        tc::internal::overlayHoveredQuery = nullptr;
        tc::internal::overlayFocusedQuery = nullptr;
        pointerCaptured_ = false;
        // GPU teardown only while sokol_gfx is alive. If shutdown runs from the
        // static destructor during exit() (e.g. an abnormal teardown path where
        // the exit event never fired), sokol_gfx is already gone and its objects
        // with it — skipping is the correct cleanup, touching them would crash.
        if (sg_isvalid()) simgui_shutdown();
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
    tc::EventListener exitListener_;
    tc::EventListener renderListener_;
    tc::EventListener eventListener_;

    // Input arbitration: consume listeners (BeforeApp) + pointer-gesture capture.
    bool pointerCaptured_ = false;  // imgui owns the current press→release gesture
    tc::EventListener mousePressConsume_;
    tc::EventListener mouseReleaseConsume_;
    tc::EventListener mouseDragConsume_;
    tc::EventListener mouseMoveConsume_;
    tc::EventListener mouseScrollConsume_;
    tc::EventListener keyPressConsume_;
    tc::EventListener keyReleaseConsume_;
};

// ---------------------------------------------------------------------------
// Convenience functions
// ---------------------------------------------------------------------------

inline void imguiSetup() {
    ImGuiManager::instance().setup();
    // Auto-register the ImGui MCP tools when MCP is enabled, so ImGui-based
    // UIs are AI-drivable without an explicit registerImGuiTools() call.
    if (const char* m = std::getenv("TRUSSC_MCP"); m && std::string(m) == "1") {
        trussc::imgui_tools::registerImGuiTools();
    }
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
