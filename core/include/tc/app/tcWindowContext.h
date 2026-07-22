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
    // True once the swapchain pass has been started at least once this frame.
    // First start CLEARs with swapchainClearValue (frame background); any
    // later restart (resume after an FBO / shadow / reflection pass suspended
    // it) must LOAD color and depth so the content already rendered into the
    // drawable survives (issue #191). Reset in present() after sg_commit().
    bool swapchainPassStartedThisFrame = false;
    // Metal drawable actually rendered into this frame (see tcGlobal.cpp notes)
    const void* lastSwapchainDrawable = nullptr;
    std::vector<ScissorRect> scissorStack;
    ScissorRect currentScissor = {0, 0, 0, 0, false};
    // Current 2D blend mode (the "role"); actual sgl pipeline in currentTarget
    BlendMode currentBlendMode = BlendMode::Alpha;
    // Depth-tested 2D pipelines (tc::enableDepthTest()). When true, active2D()
    // resolves the depth-enabled variant of the current blend pipeline, so
    // blended draws keep participating in the scene's depth buffer. Persists
    // like currentBlendMode: across frames and into Fbo passes, until changed.
    bool depthTestEnabled = false;

    // --- window identity / swapchain source (Phase 1 seam) ---
    // Main window: isMain=true, swapchain comes from sglue_swapchain() and
    // dimensions from sapp. Secondary windows: the native layer sets isMain
    // false, keeps fbWidth/fbHeight/dpiScale up to date, and provides the
    // frame's swapchain through acquireSwapchain (called by
    // ensureSwapchainPass/resumeSwapchainPass; must return the SAME drawable
    // for the duration of one window frame).
    bool isMain = true;
    // Swapchain attachment formats of THIS window. Secondary windows: set by
    // the platform adapter (mac/win: BGRA8, linux: RGBA8 -- the main window
    // is the only RGB10A2 surface). Zero = environment defaults (main).
    // Consumers that build pipelines rendering into this window's swapchain
    // (e.g. tcxImGui) must use these instead of the environment defaults.
    sg_pixel_format swapchainColorFormat = _SG_PIXELFORMAT_DEFAULT;
    int swapchainSampleCount = 0;
    int fbWidth = 0;
    int fbHeight = 0;
    float dpiScale = 1.0f;
    sg_swapchain (*acquireSwapchain)(void* user) = nullptr;
    void* acquireSwapchainUser = nullptr;

    // --- frame timing (per window) ---
    // getDeltaTime()/getFrameRate() resolve through the current context, so a
    // window ticking at 60 Hz next to a 120 Hz main window sees its own real
    // per-tick delta (measured wall-clock between THIS window's update calls).
    double updateDeltaTime = 0.0;
    std::chrono::high_resolution_clock::time_point lastUpdateCallTime;
    bool lastUpdateCallTimeInitialized = false;
    // Frame rate measurement (10-frame moving average)
    double frameTimeBuffer[10] = {};
    int frameTimeIndex = 0;
    bool frameTimeBufferFilled = false;

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

// Role keys: high nibble = role, low byte = blend mode (2D only). Bit 12
// (0x1000u) marks the depth-tested variant of a 2D blend pipeline
// (tc::enableDepthTest()) — orthogonal to both role and blend mode.
inline sgl_pipeline active2D(BlendMode m) {
    bool depth = currentWindowContext().depthTestEnabled;
    return currentWindowContext().currentTarget->pipeline(
        (depth ? 0x1000u : 0x000u) | (uint32_t)m, pipeDesc2D(m, depth));
}
inline sgl_pipeline activeFill2D()        { return active2D(BlendMode::Alpha); }
inline sgl_pipeline activePremult() {
    bool depth = currentWindowContext().depthTestEnabled;
    return currentWindowContext().currentTarget->pipeline(
        (depth ? 0x1000u : 0x000u) | 0x100u, pipeDescPremult(depth));
}
inline sgl_pipeline activeClear()         { return currentWindowContext().currentTarget->pipeline(0x200u, pipeDescClear()); }
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
// Honors the current blend mode on the swapchain and inside Fbo passes alike:
// active2D() resolves per render target, so the pipeline always matches the
// active target's color format / sample count. (moved from TrussC.h)
inline void restoreCurrentPipeline() {
    loadPipeline(active2D(currentWindowContext().currentBlendMode));
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
