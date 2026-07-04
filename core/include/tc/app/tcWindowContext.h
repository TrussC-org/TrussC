#pragma once

// =============================================================================
// tcWindowContext.h - per-window state container (internal)
// =============================================================================
//
// Phase 0 of multi-window support: every piece of state that is conceptually
// "per window" — input, hover/scene, camera/projection, render-pass — lives in
// one WindowContext instead of scattered process globals. The app is still
// single-window: currentWindowContext() == mainWindowContext() always. Phase 1
// creates additional contexts (one per native window) and switches
// internal::currentWindowCtx while dispatching each window's events and draw.
//
// This file is included from TrussC.h AFTER tcRenderTarget.h (RenderTarget by
// value) and tcCameraContext.h / tcCoreEvents.h; it is not self-contained.
//
// Hot-reload note: mainWindowContext() is NON-inline (defined in tcGlobal.cpp)
// so Host and Guest share one instance — same pattern as events() and
// getDefaultContext(). The RenderContext / CoreEvents pointers below are wired
// lazily by those accessors, preserving their existing construction timing.

namespace trussc {

class Node;
class CoreEvents;

namespace internal {

class RenderContext;   // the real class lives in internal:: (tcRenderContext.h)

// ---------------------------------------------------------------------------
// Scissor clipping stack (moved from TrussC.h)
// ---------------------------------------------------------------------------
struct ScissorRect {
    float x, y, w, h;
    bool active;  // Whether a valid range exists in the stack
};

struct WindowContext {
    WindowContext() = default;
    // currentTarget points into this object — copying/moving would dangle it.
    WindowContext(const WindowContext&) = delete;
    WindowContext& operator=(const WindowContext&) = delete;

    // --- input state (written by the framework event loop) ---
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    float pmouseX = 0.0f;   // Previous frame mouse position
    float pmouseY = 0.0f;
    int mouseButton = -1;   // Currently pressed button (-1 = none)
    bool mousePressed = false;
    std::unordered_set<int> keysPressed;

    // --- scene / hover state (per window's node tree) ---
    Node* rootNode = nullptr;         // The running App (set by the framework)
    Node* hoveredNode = nullptr;      // Currently hovered node
    Node* prevHoveredNode = nullptr;  // Previously hovered node
    Node* grabbedNode = nullptr;      // Node grabbed by mouse press
    int grabbedButton = -1;           // Mouse button that caused the grab
    Node* selectedNode = nullptr;     // Last node clicked (selection)

    // --- camera / projection state ---
    std::shared_ptr<const CameraContext> currentCameraContext;
    Mat4 currentViewMatrix = Mat4::identity();
    Mat4 currentProjectionMatrix = Mat4::identity();
    float currentScreenFov = 45.0f;
    float currentViewW = 0.0f;
    float currentViewH = 0.0f;
    float currentCameraDist = 0.0f;  // Distance from camera to Z=0 plane

    // --- render-pass state ---
    RenderTarget swapchainTarget;                     // .context set after sgl_setup()
    RenderTarget* currentTarget = &swapchainTarget;   // Fbo::begin/end retargets this
    sg_color swapchainClearValue = { 0.0f, 0.0f, 0.0f, 1.0f };
    bool inSwapchainPass = false;
    bool inFboPass = false;
    // Metal drawable actually rendered into this frame (see tcGlobal.cpp notes)
    const void* lastSwapchainDrawable = nullptr;
    std::vector<ScissorRect> scissorStack;
    ScissorRect currentScissor = {0, 0, 0, 0, false};
    // Current 2D blend mode (the "role"); actual sgl pipeline in currentTarget
    BlendMode currentBlendMode = BlendMode::Alpha;

    // --- window identity / swapchain source (Phase 1 seam) ---
    // Main window: isMain=true, swapchain comes from sglue_swapchain() and
    // dimensions from sapp. Secondary windows: the native layer sets isMain
    // false, keeps fbWidth/fbHeight/dpiScale up to date, and provides the
    // frame's swapchain through acquireSwapchain (called by
    // ensureSwapchainPass/resumeSwapchainPass; must return the SAME drawable
    // for the duration of one window frame).
    bool isMain = true;
    int fbWidth = 0;
    int fbHeight = 0;
    float dpiScale = 1.0f;
    sg_swapchain (*acquireSwapchain)(void* user) = nullptr;
    void* acquireSwapchainUser = nullptr;

    // --- misc per-window ---
    int clipboardSize = 65536;   // Clipboard buffer size (for overflow check)

    // Wired lazily by getDefaultContext() / events() for the main window
    // (preserves their pre-context construction timing); Phase 1 pre-wires
    // these when constructing secondary-window contexts.
    RenderContext* render = nullptr;
    CoreEvents* coreEvents = nullptr;
};

// Main window context. Non-inline (tcGlobal.cpp) — Host/Guest share one.
WindowContext& mainWindowContext();

// Active context while dispatching a window's events / draw. Null = main.
// (An inline variable: a hot-reload guest may hold its own copy, which stays
// null there and falls through to the shared mainWindowContext() — identical
// behavior while single-window.)
inline WindowContext* currentWindowCtx = nullptr;

inline WindowContext& currentWindowContext() {
    return currentWindowCtx ? *currentWindowCtx : mainWindowContext();
}

// ---------------------------------------------------------------------------
// Active render-target pipeline helpers (moved from tcRenderTarget.h — they
// resolve through the current window's target now)
// ---------------------------------------------------------------------------

// Role keys: high nibble = role, low byte = blend mode (2D only).
inline sgl_pipeline active2D(BlendMode m) { return currentWindowContext().currentTarget->pipeline(0x000u | (uint32_t)m, pipeDesc2D(m)); }
inline sgl_pipeline activeFill2D()        { return active2D(BlendMode::Alpha); }
inline sgl_pipeline activePremult()       { return currentWindowContext().currentTarget->pipeline(0x100u, pipeDescPremult()); }
inline sgl_pipeline activeClear()         { return currentWindowContext().currentTarget->pipeline(0x200u, pipeDesc2D(BlendMode::Disabled)); }
inline sgl_pipeline active3D()            { return currentWindowContext().currentTarget->pipeline(0x300u, pipeDesc3D()); }

// Single chokepoint for loading an sgl pipeline by role. No-op if the target isn't
// ready (id 0). In debug builds it asserts the pipeline was built for the active
// target's context — the runtime safety net against loading a pipeline meant for a
// different target (the bug class this refactor removes).
inline void loadPipeline(sgl_pipeline p) {
    if (p.id == 0) return;
#ifndef NDEBUG
    auto& owner = pipelineOwnerCtx();
    auto it = owner.find(p.id);
    // Only check pipelines WE built (others, e.g. sgl's built-in default, are unknown).
    assert((it == owner.end() || it->second == currentWindowContext().currentTarget->context.id)
           && "loadPipeline: sgl pipeline built for a different render target than the active one");
#endif
    sgl_load_pipeline(p);
}

// Restore the current blend pipeline after temporary pipeline changes.
// FBO uses its accumulating Fill2D; swapchain honors the current blend mode.
// (moved from TrussC.h)
inline void restoreCurrentPipeline() {
    auto& ctx = currentWindowContext();
    loadPipeline(ctx.inFboPass ? activeFill2D() : active2D(ctx.currentBlendMode));
}

// Register a new camera scope (declared in tcCameraContext.h, defined here
// where WindowContext is complete). Always allocates a fresh context — see
// the immutability note in tcCameraContext.h.
inline void registerCameraContext(const Mat4& view, const Mat4& projection,
                                  float viewW, float viewH, bool pickable) {
    auto ctx = std::make_shared<CameraContext>();
    ctx->view = view;
    ctx->projection = projection;
    ctx->viewW = viewW;
    ctx->viewH = viewH;
    ctx->pickable = pickable;
    currentWindowContext().currentCameraContext = std::move(ctx);
}

} // namespace internal
} // namespace trussc
