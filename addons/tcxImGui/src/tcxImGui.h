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
#include <memory>
#include <unordered_map>

namespace tcx::imgui {

// ---------------------------------------------------------------------------
// ImGui manager
// ---------------------------------------------------------------------------
class ImGuiManager {
public:
    // Initialize (call in setup — from the App that drives the target window;
    // the manager binds to whichever window's context is current, so an
    // imguiSetup() inside a secondary window's App wires imgui to THAT window)
    void setup() {
        if (initialized_) return;

        // Every manager owns a full sokol_imgui instance (own ImGui context,
        // font atlas, buffers). The classic single-window app just has one.
        simgui_desc_t desc = {};
        desc.logger.func = slog_func;
        // Secondary windows have their own swapchain formats (BGRA8/RGBA8,
        // not the main window's RGB10A2) -- the imgui pipeline must match
        // the pass it renders into or sg validation fails.
        {
            auto& wctx = tc::internal::currentWindowContext();
            if (!wctx.isMain) {
                desc.color_format = wctx.swapchainColorFormat;
                desc.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
                desc.sample_count = wctx.swapchainSampleCount;
            }
        }
        simguiCtx_ = simgui_tc_make_context(&desc);
        simgui_tc_set_context(simguiCtx_);

        initialized_ = true;

        // Listen to beforePresent for rendering. events() resolves against the
        // CURRENT window context, so these listeners land on this window's
        // event streams (main or secondary alike).
        renderListener_ = tc::events().onRender.listen([this]() {
            if (renderPending_) {
                simgui_tc_set_context(simguiCtx_);
                simgui_render();
                renderPending_ = false;
            }
        }, 1000);

        // Listen to rawEvent for input handling
        eventListener_ = tc::events().rawEvent.listen([this](const sapp_event& ev) {
            simgui_tc_set_context(simguiCtx_);
            simgui_handle_event(&ev);
        }, tc::EventPriority::BeforeApp);

        // Overlay capture queries — tell the framework when imgui owns the
        // pointer / keyboard so node-tree hover is suppressed under panels and
        // user code can guard raw input (isOverlayHovered/isOverlayFocused).
        // Installed once, shared by all windows: the query resolves the
        // manager of the window whose context is current when it is asked.
        tc::internal::overlayHoveredQuery = []() {
            ImGuiManager* m = ImGuiManager::findForCurrentWindow();
            if (!m || !m->isInitialized()) return false;
            simgui_tc_set_context(m->simguiCtx_);
            return ImGui::GetIO().WantCaptureMouse;
        };
        tc::internal::overlayFocusedQuery = []() {
            ImGuiManager* m = ImGuiManager::findForCurrentWindow();
            if (!m || !m->isInitialized()) return false;
            simgui_tc_set_context(m->simguiCtx_);
            return ImGui::GetIO().WantCaptureKeyboard;
        };

        // Consume input before it reaches the node tree (BeforeApp). Pointer
        // capture follows the press: a gesture imgui claims on press stays
        // imgui's until release, so a drag begun on the canvas is never stolen
        // mid-way, and a press over a panel owns the whole gesture. Move/scroll
        // and keys are stateless — consumed whenever imgui wants them.
        mousePressConsume_ = tc::events().mousePressed.listen([this](tc::MouseEventArgs& e) {
            simgui_tc_set_context(simguiCtx_);
            if (ImGui::GetIO().WantCaptureMouse) { pointerCaptured_ = true; e.consumed = true; }
        }, tc::EventPriority::BeforeApp);
        mouseReleaseConsume_ = tc::events().mouseReleased.listen([this](tc::MouseEventArgs& e) {
            if (pointerCaptured_) { e.consumed = true; pointerCaptured_ = false; }
        }, tc::EventPriority::BeforeApp);
        mouseDragConsume_ = tc::events().mouseDragged.listen([this](tc::MouseDragEventArgs& e) {
            if (pointerCaptured_) e.consumed = true;
        }, tc::EventPriority::BeforeApp);
        mouseMoveConsume_ = tc::events().mouseMoved.listen([this](tc::MouseMoveEventArgs& e) {
            simgui_tc_set_context(simguiCtx_);
            if (ImGui::GetIO().WantCaptureMouse) e.consumed = true;
        }, tc::EventPriority::BeforeApp);
        mouseScrollConsume_ = tc::events().mouseScrolled.listen([this](tc::ScrollEventArgs& e) {
            simgui_tc_set_context(simguiCtx_);
            if (ImGui::GetIO().WantCaptureMouse) e.consumed = true;
        }, tc::EventPriority::BeforeApp);
        keyPressConsume_ = tc::events().keyPressed.listen([this](tc::KeyEventArgs& e) {
            simgui_tc_set_context(simguiCtx_);
            if (ImGui::GetIO().WantCaptureKeyboard) e.consumed = true;
        }, tc::EventPriority::BeforeApp);
        keyReleaseConsume_ = tc::events().keyReleased.listen([this](tc::KeyEventArgs& e) {
            simgui_tc_set_context(simguiCtx_);
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
        // The overlay queries are shared across windows and resolve per window
        // through the manager registry — they stay installed (no-op for
        // windows without an initialized manager).
        pointerCaptured_ = false;
        // GPU teardown only while sokol_gfx is alive. If shutdown runs from the
        // static destructor during exit() (e.g. an abnormal teardown path where
        // the exit event never fired), sokol_gfx is already gone and its objects
        // with it — skipping is the correct cleanup, touching them would crash.
        if (sg_isvalid()) simgui_tc_destroy_context(simguiCtx_);
        simguiCtx_ = {};
        initialized_ = false;
        tc::logVerbose() << "ImGui shutdown";
    }

    // Begin frame (call at start of draw). Sizes/dpi/delta are the CURRENT
    // window's (context-routed), so imgui lays out for the window it draws in.
    void begin() {
        if (!initialized_) return;

        simgui_tc_set_context(simguiCtx_);
        simgui_frame_desc_t desc = {};
        desc.width = tc::getFramebufferWidth();
        desc.height = tc::getFramebufferHeight();
        desc.delta_time = static_cast<float>(tc::getDeltaTime());
        desc.dpi_scale = tc::getDpiScale();
        simgui_new_frame(&desc);
    }

    // End frame (defers render to beforePresent)
    void end() {
        if (!initialized_) return;
        renderPending_ = true;
    }

    bool isInitialized() const { return initialized_; }

    // Per-window access, keyed by the current WindowContext: the main App and
    // each secondary window's App get their own manager (own ImGui context).
    static ImGuiManager& instance() {
        auto& slot = registry()[&tc::internal::currentWindowContext()];
        if (!slot) slot.reset(new ImGuiManager());
        return *slot;
    }

    // Lookup without creating (overlay queries, wants-helpers). May be null.
    static ImGuiManager* findForCurrentWindow() {
        auto& reg = registry();
        auto it = reg.find(&tc::internal::currentWindowContext());
        return (it != reg.end()) ? it->second.get() : nullptr;
    }

public:
    ~ImGuiManager() { shutdown(); }   // public: owned by unique_ptr in the registry
private:
    ImGuiManager() = default;

    static std::unordered_map<tc::internal::WindowContext*,
                              std::unique_ptr<ImGuiManager>>& registry() {
        static std::unordered_map<tc::internal::WindowContext*,
                                  std::unique_ptr<ImGuiManager>> reg;
        return reg;
    }

    simgui_tc_context simguiCtx_ {};

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
        registerImGuiTools();
    }
}

inline void imguiShutdown() {
    ImGuiManager::instance().shutdown();
}

inline void imguiBegin() {
    ImGuiManager::instance().begin();
    beginFrame();
}

inline void imguiEnd() {
    ImGuiManager::instance().end();
    swapFrames();
}

inline bool imguiWantsMouse() {
    return tc::internal::overlayHoveredQuery ? tc::internal::overlayHoveredQuery() : false;
}

inline bool imguiWantsKeyboard() {
    return tc::internal::overlayFocusedQuery ? tc::internal::overlayFocusedQuery() : false;
}

} // namespace tcx::imgui

// -----------------------------------------------------------------------------
// Backward compatibility: tcxImGui's public entry points historically lived
// directly in `tcx` (e.g. `using namespace tcx; imguiSetup();`). Canonical is
// now `tcx::imgui`. Flat aliases keep existing user code compiling.
// DEPRECATED — removed in v1.0.0.
// -----------------------------------------------------------------------------
namespace tcx { // deprecated: remove at v1.0.0
using imgui::ImGuiManager;
using imgui::imguiSetup;
using imgui::imguiShutdown;
using imgui::imguiBegin;
using imgui::imguiEnd;
using imgui::imguiWantsMouse;
using imgui::imguiWantsKeyboard;
} // namespace tcx
