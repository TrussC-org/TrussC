#pragma once

// =============================================================================
// tcxImGui.h - Dear ImGui addon for TrussC
// Definitions live in tcxImGuiImpl.h, compiled only by the platform TU.
// =============================================================================

#include <TrussC.h>
#include "imgui/imgui.h"
#include "sokol_imgui.h"
#include "tcImGuiHooks.h"
#include "tcImGuiTools.h"
#include "tcImGuiTarget.h"
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
    void setup();

    // Shutdown
    void shutdown();

    // Independent offscreen contexts install settlement/lifetime listeners only.
    // Initialize while the owning window is current, before opening a UI scope.
    ImGuiManager() = default;
    ImGuiManager(const ImGuiManager&) = delete;
    ImGuiManager& operator=(const ImGuiManager&) = delete;
    void setupOffscreen();

    // Returns false on a refused attempt. Pair every begin with end, and only
    // submit widgets when begin succeeds. Prefer scoped() for offscreen UI.
    bool begin();

    void end();

    class Scope {
    public:
        explicit Scope(ImGuiManager& manager);
        ~Scope();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
        explicit operator bool() const { return accepted_; }
    private:
        ImGuiManager& manager_;
        simgui_tc_context previous_;
        ImGuiContext* previousImGui_;
        bool accepted_;
    };
    Scope scoped();

    bool isInitialized() const { return initialized_; }

    // Per-window access, keyed by the current WindowContext: the main App and
    // each secondary window's App get their own manager (own ImGui context).
    static ImGuiManager& instance();

    // Lookup without creating (overlay queries, wants-helpers). May be null.
    static ImGuiManager* findForCurrentWindow();

public:
    // Explicitly owned managers have RAII teardown. Registry managers use the
    // exit/unload hooks; other process statics may already be gone at destruction.
    ~ImGuiManager();
private:
    std::shared_ptr<int> retirementLifetime_ = std::make_shared<int>(0);
    void installDeadlineListeners_();

    void initializeContext_();

    static std::unordered_map<tc::internal::WindowContext*,
                              std::unique_ptr<ImGuiManager>>& registry();

    simgui_tc_context simguiCtx_ {};
    ImGuiContext* imguiCtx_ = nullptr;   // the ImGui context inside simguiCtx_

    bool initialized_ = false;
    bool renderPending_ = false;
    bool offscreenOwner_ = false;
    bool registryOwned_ = false;
    bool synchronous_ = false;
    bool offscreenFrameCompleted_ = false;
    std::vector<bool> attempts_;
    tc::EventListener exitListener_;
    tc::EventListener hotReloadUnloadListener_;   // on Runtime hotReloadUnload
    tc::EventListener renderListener_;
    tc::EventListener overdueListener_;   // on Runtime maintenance before MCP expiry
    tc::EventListener eventListener_;

    // Input arbitration: consume listeners (-20) + pointer-gesture capture.
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

void imguiSetup();

void imguiShutdown();

bool imguiBegin();

void imguiEnd();

bool imguiWantsMouse();

bool imguiWantsKeyboard();

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
