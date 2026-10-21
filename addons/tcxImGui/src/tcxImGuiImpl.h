#pragma once

// Included only by the platform TU that compiles Dear ImGui.
#include "tcxImGui.h"
#include "tcImGuiToolsImpl.h"

namespace tcx::imgui {

void ImGuiManager::setup() {
    if (initialized_) return;

    offscreenOwner_ = false;
    initializeContext_();

    // Listen to beforePresent for rendering. events() resolves against the
    // CURRENT window context, so these listeners land on this window's
    // event streams (main or secondary alike).
    renderListener_ = tc::events().onRender.listen([this]() {
        if (renderPending_) {
            simgui_tc_set_context(simguiCtx_);
            detail::renderTargetFrame(simguiCtx_);
            renderPending_ = false;
        } else {
            // No imgui frame in this window frame: the values the MCP
            // tools queued for this context are settled now (swapFrames()
            // settles them when imgui runs).
            detail::settleWithoutImGuiFrame(imguiCtx_);
        }
    }, 1000);

    installDeadlineListeners_();

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
    tc::internal::overlayHoveredQuery() = []() {
        ImGuiManager* m = ImGuiManager::findForCurrentWindow();
        if (!m || !m->isInitialized()) return false;
        simgui_tc_set_context(m->simguiCtx_);
        return ImGui::GetIO().WantCaptureMouse;
    };
    tc::internal::overlayFocusedQuery() = []() {
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

void ImGuiManager::shutdown() {
    if (!initialized_) return;
    exitListener_ = {};
    hotReloadUnloadListener_ = {};
    renderListener_ = {};
    overdueListener_ = {};
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
    // shutdown() runs only while the runtime registries are alive.
    // Keep this guard for an explicit imguiShutdown() after sokol_gfx is gone:
    // its GPU objects are no longer valid to destroy.
    if (sg_isvalid()) {
        forgetContext(imguiCtx_);   // drop its widget registry before the context goes
        detail::destroyTargetContext(simguiCtx_);
    }
    imguiCtx_ = nullptr;
    simguiCtx_ = {};
    initialized_ = false;
    renderPending_ = false;
    offscreenFrameCompleted_ = false;
    attempts_.clear();
    tc::logVerbose() << "ImGui shutdown";
}

void ImGuiManager::setupOffscreen() {
    if (initialized_) return;
    offscreenOwner_ = true;
    const auto previous = simgui_tc_get_context();
    auto* previousImGui = ImGui::GetCurrentContext();
    initializeContext_();
    // Independent UI state must not share the window manager's ini file.
    ImGui::GetIO().IniFilename = nullptr;
    simgui_tc_set_context(previous);
    ImGui::SetCurrentContext(previousImGui);
    installDeadlineListeners_();
    // Cached Fbos may skip UI for any number of owning-window frames.
    renderListener_ = tc::events().onRender.listen([this]() {
        if (!offscreenFrameCompleted_) detail::settleWithoutImGuiFrame(imguiCtx_);
        offscreenFrameCompleted_ = false;
    }, 1000);
    exitListener_ = tc::events().exit.listen([this]() { shutdown(); });
}

bool ImGuiManager::begin() {
    if (!initialized_) { attempts_.push_back(false); return false; }
    // Expire queued input before NewFrame can consume it after a long idle.
    detail::settleOverdueValues(imguiCtx_);
    auto& target = tc::internal::currentTarget();
    if (offscreenOwner_ && !target.isFbo) {
        tc::logError("ImGui") << "offscreen UI requires an active Fbo";
        attempts_.push_back(false);
        return false;
    }
    simgui_frame_desc_t desc = {};
    desc.width = target.isFbo ? target.width : tc::getFramebufferWidth();
    desc.height = target.isFbo ? target.height : tc::getFramebufferHeight();
    desc.delta_time = static_cast<float>(tc::getDeltaTime());
    desc.dpi_scale = target.isFbo ? tc::internal::targetPixelScale() : tc::getDpiScale();
    const bool accepted = detail::beginTargetFrame(simguiCtx_, desc);
    attempts_.push_back(accepted);
    if (accepted) {
        synchronous_ = target.isFbo;
        beginFrame();
    }
    return accepted;
}

void ImGuiManager::end() {
    if (attempts_.empty()) return;
    const bool accepted = attempts_.back();
    attempts_.pop_back();
    if (!accepted) return;
    simgui_tc_set_context(simguiCtx_);
    if (!detail::validateTargetFrame(simguiCtx_)) return;
    swapFrames();
    if (offscreenOwner_) offscreenFrameCompleted_ = true;
    if (synchronous_) detail::renderTargetFrame(simguiCtx_);
    else renderPending_ = true;
}

ImGuiManager::Scope ImGuiManager::scoped() { return Scope(*this); }

ImGuiManager& ImGuiManager::instance() {
    auto& slot = registry()[&tc::internal::currentWindowContext()];
    if (!slot) {
        slot.reset(new ImGuiManager());
        slot->registryOwned_ = true;
    }
    return *slot;
}

ImGuiManager* ImGuiManager::findForCurrentWindow() {
    auto& reg = registry();
    auto it = reg.find(&tc::internal::currentWindowContext());
    return (it != reg.end()) ? it->second.get() : nullptr;
}

ImGuiManager::~ImGuiManager() { if (!registryOwned_ && detail::hookRegistriesAlive) shutdown(); }

void ImGuiManager::installDeadlineListeners_() {
    // A value the MCP tools wrote into this context's widgets whose check
    // frame does not come in time (a cached Fbo or a stopped/slow window)
    // is settled from the main window's afterFrame, before
    // tc::mcp's timeout drain there (BeforeApp), whichever window this is.
    {
        auto& wctx = tc::internal::currentWindowContext();
        tc::CoreEvents* mainEvents = wctx.isMain ? &tc::events()
                                                 : tc::internal::mainWindowContext().coreEvents;
        if (mainEvents) {
            overdueListener_ = mainEvents->afterFrame.listen([this]() {
                detail::settleOverdueValues(imguiCtx_);
            }, tc::EventPriority::BeforeApp);
            // Hot reload: the manager registry is a static of the guest
            // image, which outlives a reload. Tear down this generation's
            // manager (the same cleanup as at exit) before the host
            // unloads it, so no stale listener stays on the host's events
            // (#416). The host fires it on the main window's events.
            hotReloadUnloadListener_ = mainEvents->hotReloadUnload.listen([this]() { shutdown(); });
        }
    }
}

void ImGuiManager::initializeContext_() {
    simgui_desc_t desc = {};
    desc.logger.func = tc::internal::sokolLog;
    desc.disable_set_mouse_cursor = offscreenOwner_;
    simguiCtx_ = simgui_tc_make_context(&desc);
    simgui_tc_set_context(simguiCtx_);
    imguiCtx_ = ImGui::GetCurrentContext();
    setContextOwner(imguiCtx_, &tc::internal::currentWindowContext());
    initialized_ = true;
}

std::unordered_map<tc::internal::WindowContext*,
                   std::unique_ptr<ImGuiManager>>& ImGuiManager::registry() {
    static std::unordered_map<tc::internal::WindowContext*,
                              std::unique_ptr<ImGuiManager>> reg;
    return reg;
}

ImGuiManager::Scope::Scope(ImGuiManager& manager) : manager_(manager),
        previous_(simgui_tc_get_context()), previousImGui_(ImGui::GetCurrentContext()),
        accepted_(manager.begin()) {}

ImGuiManager::Scope::~Scope() {
    manager_.end();
    simgui_tc_set_context(previous_);
    ImGui::SetCurrentContext(previousImGui_);
}

void imguiSetup() {
    ImGuiManager::instance().setup();
    // Registration always happens; the host's tool level controls exposure.
    registerImGuiTools();
}

void imguiShutdown() {
    ImGuiManager::instance().shutdown();
}

bool imguiBegin() {
    return ImGuiManager::instance().begin();
}

void imguiEnd() {
    ImGuiManager::instance().end();
}

bool imguiWantsMouse() {
    return tc::isOverlayHovered();
}

bool imguiWantsKeyboard() {
    return tc::isOverlayFocused();
}

} // namespace tcx::imgui
