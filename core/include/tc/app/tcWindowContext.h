#pragma once

// =============================================================================
// tcWindowContext.h - per-window state container (internal)
// =============================================================================
//
// Multi-window support: every piece of state that is conceptually "per window"
// — input, hover/scene, render-target routing, per-window frame count,
// lighting/material/environment, and the per-frame shadow-slot assignment —
// lives in one WindowContext instead of scattered process globals.
// currentWindowContext() == mainWindowContext() while only the main window is
// running; each secondary native window owns its own context and the native
// layer switches internal::currentWindowCtx() while dispatching that window's
// events and draw (see tcWindow.h / platform/*/tcWindow*).
//
// This file is included from TrussC.h AFTER tcRenderTarget.h (RenderTarget by
// value) and tcCameraContext.h / tcCoreEvents.h; it is not self-contained. It
// is included BEFORE the 3D headers (tcLight/tcMaterial/tcEnvironment), so the
// lighting members below are held by forward-declared pointer / pointer-vector.
//
// Hot-reload note: mainWindowContext() is NON-inline (defined in tcGlobal.cpp)
// so Host and Guest share one instance — same pattern as events() and
// getDefaultContext(). The RenderContext / CoreEvents pointers below are wired
// lazily by those accessors, preserving their existing construction timing.

namespace trussc {

class Node;
class CoreEvents;
class Shader;          // shader stack holds Shader* (defined in tc/gpu/tcShader.h)
// 3D types whose state lives per-window (defined later, in the tc/3d headers).
// Held here by pointer / pointer-vector so tcWindowContext.h stays includable
// before them.
class Light;
class Material;
class Environment;
// Stroke cap of the first beginStroke() vertex (per-window shape accumulator).
// Opaque-enum-declaration: a scoped enum is a COMPLETE type once declared this
// way (underlying type int, matching the real definition in tcRenderContext.h,
// included after this file), so it can be a value member with a value-init
// default (StrokeCap{} == Butt == 0, byte-identical to the old global default).
enum class StrokeCap;

namespace internal {

class RenderContext;   // the real class lives in internal:: (tcRenderContext.h)

// ---------------------------------------------------------------------------
// Per-window shape accumulators; deferred draw queues live on RenderTarget.
// These incomplete elements are defined later in TrussC.h.
struct StrokeVertex;
struct LinesVertex;

// ---------------------------------------------------------------------------
// Lighting limits (were in tcLightingState.h; hoisted here because WindowContext
// now owns the lighting state and the shadow-slot arrays need maxShadowLights at
// definition time). tcLightingState.h documents the lighting state and points
// back here. maxShadowLights must match MAX_SHADOW_LIGHTS in
// core/shaders/meshPbr.glsl.
// ---------------------------------------------------------------------------
inline constexpr int maxLights = 8;
inline constexpr int maxShadowLights = 4;

// ---------------------------------------------------------------------------
// Per-window shadow-slot state (was in the PbrPipeline singleton). The GPU
// shadow-map array/views/sampler/pipeline STAY shared in the singleton; only
// the per-frame slot ASSIGNMENT moves here. Window ticks run strictly
// serialized on the main thread and each window's shadow passes + PBR flush
// complete within its own tick, so the shared array layers can be reused
// serially across windows with zero extra GPU memory.
// ---------------------------------------------------------------------------
struct ShadowSlotState {
    // Slot assignment: `frame` (the per-window updateCount at the last reset)
    // resets the counter on the first beginShadowPass() of each window tick;
    // each pass claims the next layer in call order. Slot data persists across
    // frames so draws submitted before the first shadow pass keep the previous
    // frame's shadow state.
    uint64_t frame = static_cast<uint64_t>(-1);
    int count = 0;
    int current = -1;
    bool inPass = false;
    RenderTarget target;
    std::unique_ptr<TargetPassScope> scope;
    Mat4  viewProj[maxShadowLights]{};
    int   lightIndex[maxShadowLights]{};
    float bias[maxShadowLights]{};
    float softness[maxShadowLights]{};
    int   samples[maxShadowLights]{};
    // Directional-ortho depth storage: per-slot mode (0=persp, 1=ortho), light
    // direction, and ortho depth reference (refDot = dot(eye, lightDir)).
    float mode[maxShadowLights]{};
    float lightDirX[maxShadowLights]{};
    float lightDirY[maxShadowLights]{};
    float lightDirZ[maxShadowLights]{};
    float refDot[maxShadowLights]{};
};

struct WindowContext {
    WindowContext();
    ~WindowContext();
    // Recover owned scopes before native/GPU resources or any members die.
    // Safe to call again from the destructor after native window teardown.
    void teardownTargets() noexcept;
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
    // Weak references (#255): a node removed from the tree and freed while
    // one of these still names it just makes lock() return null, so the next
    // hover update, drag or selection read never touches freed memory. Use
    // them through lock() and keep the resulting shared_ptr for as long as
    // the node is used (e.g. across a handler that may remove it). A hot
    // reload host resets them before it unloads a guest
    // (resetNodeRefsForUnload): releasing the last weak reference to a
    // make_shared node runs code of the module that created it.
    std::weak_ptr<Node> rootNode;         // The running App (set by the framework)
    std::weak_ptr<Node> hoveredNode;      // Currently hovered node
    std::weak_ptr<Node> prevHoveredNode;  // Previously hovered node
    std::weak_ptr<Node> grabbedNode;      // Node grabbed by mouse press
    int grabbedButton = -1;               // Mouse button that caused the grab
    std::weak_ptr<Node> selectedNode;     // Last node clicked (selection)

    // --- render target routing (one swapchain target per window) ---
    RenderTarget swapchainTarget;
    // Compatibility view: only the host stack coordinator changes this pointer.
    RenderTarget* currentTarget = &swapchainTarget;
    std::vector<TargetStackEntry> targetStack;
    uint64_t nextTargetToken = 1;
    uint64_t renderEpoch = 0;
    // Borrowed Metal drawable rendered into this frame. Valid only until the
    // native frame callback returns; clear after afterFrame and capture drains.
    const void* lastSwapchainDrawable = nullptr;
    // Full swapchain description this frame actually rendered into (recorded by
    // RenderTarget::beginPass, same timing as lastSwapchainDrawable). Used by
    // the per-window capture backends on Windows (d3d11.render_view) and Linux
    // (gl.framebuffer / width / height) so a secondary window's screenshot /
    // recording reads back ITS surface instead of the main swapchain. On macOS
    // capture reads lastSwapchainDrawable directly and ignores this.
    sg_swapchain lastSwapchain{};
    void clearFrameDrawable() {
        lastSwapchainDrawable = nullptr;
        lastSwapchain.metal.current_drawable = nullptr;
    }
    // --- window identity / swapchain source (Phase 1 seam) ---
    // Main window: isMain=true, swapchain comes from sglue_swapchain() and
    // dimensions from sapp. Secondary windows: the native layer sets isMain
    // false, keeps fbWidth/fbHeight/dpiScale up to date, and provides the
    // frame's swapchain through acquireSwapchain (called by
    // the target coordinator; must return the SAME drawable
    // for the duration of one window frame).
    bool isMain = true;
    int fbWidth = 0;
    int fbHeight = 0;
    float dpiScale = 1.0f;
    sg_swapchain (*acquireSwapchain)(void* user) = nullptr;
    void* acquireSwapchainUser = nullptr;

    // --- per-window frame count ---
    // getFrameCount()/getUpdateCount() resolve through the current context.
    // The MAIN window keeps using internal::updateFrameCount (bit-identical to
    // the old global, and the value the hot-reload host mirrors); secondary
    // windows advance THIS counter once per tick. See tcGlobal.cpp.
    uint64_t updateCount = 0;

    // --- lighting / material / environment (per window) ---
    // Moved out of the tcLightingState.h process globals so each window has its
    // own active light set, current material,
    // exposure and IBL environment. Registration in activeLights is by address:
    // a Light registered with addLight() must outlive its registration (prefer
    // stable storage — members, not vector<Light> elements). See tcLight.h.
    std::vector<Light*> activeLights;
    Material* currentMaterial = nullptr;
    float pbrExposure = 1.0f;              // global exposure scalar (pre-ACES tonemap)
    Environment* currentEnvironment = nullptr;

    // --- per-window shadow-slot assignment (GPU resources stay shared) ---
    ShadowSlotState shadow;

    // --- frame timing (per window) ---
    // getDeltaTime()/getFrameRate()/getFrameElapsedTime() resolve through the
    // current context, so a window ticking at 60 Hz next to a 120 Hz main
    // window sees its own real per-tick delta (measured wall-clock between THIS
    // window's update calls; the nominal 1/updateFps in the main window's
    // fixed-Hz update mode).
    double updateDeltaTime = 0.0;
    // Secondary windows: their platform tick (windowTick) measures the delta
    // with these, and records none of the frame time, update time or rate
    // samples below. Left as is until the multi-window work that follows #219.
    std::chrono::high_resolution_clock::time_point lastUpdateCallTime;
    bool lastUpdateCallTimeInitialized = false;
    // Main window: the main loop (beginMainUpdateCall in TrussC.h) measures
    // the delta on the steady clock instead, so a wall-clock step can't skew
    // the dt that Node timers count down with.
    std::chrono::steady_clock::time_point mainUpdateCallTime;
    bool mainUpdateCallTimeInitialized = false;
    // Uptime sampled once at the start of this window's frame (getFrameElapsedTime).
    std::chrono::steady_clock::duration frameUptime{};
    bool frameUptimeSampled = false;
    // Node timers (Node::processTimers): the time of the update being run
    // (the wall time of the update call; a fixed-Hz step's nominal time on the
    // loop's timeline), whether an update is running right now, and whether it
    // is a fixed-Hz step. A timer is charged only the time after its creation,
    // except one created during a fixed step, which counts whole steps from
    // the next one.
    std::chrono::steady_clock::time_point updateTime{};
    bool inUpdate = false;
    bool fixedStepUpdate = false;
    // Measured update rate (getFrameRate): per frame, the wall time it covered
    // and the update steps it ran. Recorded by the loops, read-only in the getter.
    // Fixed-step loops record fractional steps (the time they consumed divided
    // by the step interval), so the rate doesn't flicker between whole-step
    // counts when the step rate isn't a multiple of the frame rate.
    static constexpr int rateSampleCount = 10;
    double rateDurations[rateSampleCount] = {};
    double rateSteps[rateSampleCount] = {};
    int rateIndex = 0;
    int rateCount = 0;
    // Frame rate measurement of a secondary window, which records no rate
    // samples: getFrameRate() averages the last 10 deltas it read (10-frame
    // moving average), as before.
    double frameTimeBuffer[10] = {};
    int frameTimeIndex = 0;
    bool frameTimeBufferFilled = false;

    // --- per-window frame-rate throttle (Window::setFps / global setFps) ---
    // A secondary window's native display link always fires at the display's
    // vsync; we cannot portably retune it, so a lower target rate is realised
    // by SKIPPING display ticks via a time accumulator (windowThrottleShouldTick
    // in the native windowTick, before beginFrame). throttleFps <= 0 = free-run
    // at vsync, the default and the pre-Phase-2 behavior; a target at or above
    // the display rate also runs every tick (half-tick tolerance). The MAIN
    // window ignores this field entirely (its loop is driven by
    // updateTargetFps/drawTargetFps in _frame_cb).
    float throttleFps = 0.0f;
    double throttleAccumulator = 0.0;
    std::chrono::steady_clock::time_point throttleLastTime;
    bool throttleLastTimeInitialized = false;

    // --- misc per-window ---
    // Bound (security): OS clipboard text needs finite storage in the window backend. Overflow truncates or rejects oversized text, depending on the backend.
    int clipboardSize = 65536;   // Clipboard buffer size (for overflow check)
    // Resolved absolute paths (download names on web) queued by saveScreenshot()
    // on THIS window, drained
    // right after present() while this context is current (so the capture reads
    // back this window's lastSwapchainDrawable, not the main window's). The main
    // window drains from the afterFrame listener in _setup_cb; each secondary
    // window drains in its own windowTick. See TrussC.h drainPendingScreenshots().
    std::vector<std::filesystem::path> pendingScreenshotPaths;
    ScreenshotQueue asyncScreenshots;

    // --- per-window shader stack (pushShader / popShader) ------------------
    // Made per-window so an unbalanced push in window A can't leak into B.
    std::vector<Shader*> shaderStack;
    struct ShaderScopeTarget { RenderTarget* target; uint64_t token; };
    std::vector<ShaderScopeTarget> shaderScopeTargets;

    // --- per-window shape / stroke accumulators (beginShape/endShape etc.) -
    // Made per-window so a shape left open at end-of-tick can't bleed into the
    // next window. strokeStartCap defaults to Butt (StrokeCap{} == 0).
    std::vector<Vec3>         shapeVertices;
    bool                      shapeStarted = false;
    std::vector<LinesVertex>  linesVertices;
    bool                      linesStarted = false;
    std::vector<StrokeVertex> strokeVertices;
    bool                      strokeStarted = false;
    StrokeCap                 strokeStartCap{};   // Butt; real enum in tcRenderContext.h

    // Wired lazily by getDefaultContext() / events() for the main window
    // (preserves their pre-context construction timing); Phase 1 pre-wires
    // these when constructing secondary-window contexts.
    RenderContext* render = nullptr;
    CoreEvents* coreEvents = nullptr;
};

// Pipeline target for swapchain draws. The main window keeps sokol's defaults.
struct SwapchainTargetFormat {
    sg_pixel_format colorFormat;
    int sampleCount;
};

inline SwapchainTargetFormat swapchainTargetFormat(const WindowContext& ctx) {
    return {ctx.swapchainTarget.colorFormat, ctx.swapchainTarget.sampleCount};
}

// Main window context. Non-inline (tcGlobal.cpp) — Host/Guest share one.
WindowContext& mainWindowContext();

// Active context while dispatching a window's events / draw. Null = main. The
// native window layer points it at a secondary window's context around that
// window's tick. Non-inline (tcGlobal.cpp) like mainWindowContext(): a Windows
// hot reload guest DLL would get its own copy of an inline variable, which
// would stay null there and send the guest's drawing during a secondary
// window's tick to the main window.
WindowContext*& currentWindowCtx();

// The context drawing and input calls act on. Also non-inline: it reads
// currentWindowCtx(), and in a single-window app it costs the one out-of-line
// call its inline version always made (to mainWindowContext()).
WindowContext& currentWindowContext();

// Restore both window dispatch and its current target's actual SGL context.
// A resize can replace the main context; nullptr still denotes the main window.
void restoreWindowContext(WindowContext* previous);

// Drop the node references of every window context: hover, grab and
// selection in the main window's context and each secondary window's, and
// the main window's root. The hot reload host calls it before it unloads a
// guest (tcHotReloadHost.h), so no weak reference to a node the guest created
// outlives the guest. A secondary window's root stays: it names the App the
// window itself holds (Window::setApp). Non-inline (tcGlobal.cpp): it walks
// the window registry.
void resetNodeRefsForUnload();

// ---------------------------------------------------------------------------
// Per-window frame timing. Non-inline (tcGlobal.cpp).
// ---------------------------------------------------------------------------
// Sample the uptime getFrameElapsedTime() reports for this window's frame.
void sampleFrameTime(WindowContext& ctx);
// Record one frame for getFrameRate(): the wall time it covered and the number
// of update steps it ran (1 per update in VSYNC / synced modes; in fixed-step
// loops the time the steps consumed divided by the step interval, which may be
// fractional).
void recordUpdateRateSample(WindowContext& ctx, double duration, double steps);

// ---------------------------------------------------------------------------
// Per-window frame-rate throttle (T1). Shared by every platform's windowTick.
// Called once per native display tick BEFORE beginFrame: returns true to run
// this window's update/draw, false to skip it cheaply (the display link keeps
// firing at vsync — this only decides whether we do the frame's work).
// Free-run (throttleFps <= 0) always ticks. Otherwise accumulate real elapsed
// time and let one tick through per 1/fps interval, with a half-tick
// tolerance so a target at or above the display rate runs every tick. Same
// decision as the main loop's fixed-FPS draw skip (frameSkipShouldTick,
// tcFrameTiming.h).
// ---------------------------------------------------------------------------
inline bool windowThrottleShouldTick(WindowContext& ctx) {
    if (ctx.throttleFps <= 0.0f) return true;
    auto now = std::chrono::steady_clock::now();
    if (!ctx.throttleLastTimeInitialized) {
        ctx.throttleLastTimeInitialized = true;
        ctx.throttleLastTime = now;
        return true;  // always run the first tick
    }
    double elapsed = std::chrono::duration<double>(now - ctx.throttleLastTime).count();
    ctx.throttleLastTime = now;
    return frameSkipShouldTick(ctx.throttleAccumulator, elapsed, 1.0 / ctx.throttleFps);
}

// ---------------------------------------------------------------------------
// Active render-target pipeline helpers (moved from tcRenderTarget.h — they
// resolve through the current window's target now)
// ---------------------------------------------------------------------------

inline RenderTarget& currentTarget() { return *currentWindowContext().currentTarget; }
inline sgl_pipeline active2D(BlendMode mode) {
    return currentTarget().pipeline(PipelineKey::immediate(fillSelection(mode, currentTarget().depthTestEnabled))).immediate;
}
inline sgl_pipeline activeFill2D() { return active2D(BlendMode::Alpha); }
inline sgl_pipeline activePremult() {
    return currentTarget().pipeline(PipelineKey::immediate({PipelineRole::Premult, BlendMode::Alpha, currentTarget().depthTestEnabled})).immediate;
}
inline sgl_pipeline activeCoverage2D() {
    return currentTarget().pipeline(PipelineKey::immediate({PipelineRole::Coverage, BlendMode::Alpha, currentTarget().depthTestEnabled})).immediate;
}
inline sgl_pipeline activeClear() {
    return currentTarget().pipeline(PipelineKey::immediate({PipelineRole::Clear})).immediate;
}
inline sgl_pipeline active3D() {
    return currentTarget().pipeline(PipelineKey::immediate({PipelineRole::Geometry3D, BlendMode::Alpha, true})).immediate;
}
inline void loadTemporaryPipeline(sgl_pipeline pipeline) {
    if (!sg_isvalid() || !currentTarget().resources().context.id) return;
    // An invalid selection must never keep a previous incompatible pipeline.
    // sokol_gl rejects an invalid pipeline and drops its affected commands.
#ifndef NDEBUG
    auto it = pipelineOwnerCtx().find(pipeline.id);
    assert(it == pipelineOwnerCtx().end() || it->second == currentTarget().resources().context.id);
#endif
    sgl_load_pipeline(pipeline);
}
inline void loadPipeline(sgl_pipeline pipeline) {
    const auto& selections = currentTarget().resources().selections;
    auto it = selections.find(pipeline.id);
    if (it != selections.end()) currentTarget().selectedPipeline = it->second;
    loadTemporaryPipeline(pipeline);
}
inline void restoreCurrentPipeline() {
    auto& target = currentTarget();
    loadTemporaryPipeline(target.pipeline(PipelineKey::immediate(target.selectedPipeline)).immediate);
}

inline void applyTargetModelView() {
    const auto& target = currentTarget();
    const Mat4 composed = (target.camera.currentViewMatrix * target.currentMatrix).transposed();
    sgl_matrix_mode_modelview();
    sgl_load_matrix(composed.m);
}

// Reapply CPU-authoritative state after a target switch, clear, or direct draw.
inline void applyTargetDrawingState() {
    const auto& target = currentTarget();
    const Mat4 projection = target.camera.currentProjectionMatrix.transposed();
    sgl_matrix_mode_projection();
    sgl_load_matrix(projection.m);
    applyTargetModelView();
    const auto& vp = target.viewport;
    sgl_viewportf(vp.active ? vp.x : 0, vp.active ? vp.y : 0,
                  vp.active ? vp.w : float(target.width), vp.active ? vp.h : float(target.height), true);
    const auto& clip = target.currentScissor;
    sgl_scissor_rectf(clip.active ? clip.x : 0, clip.active ? clip.y : 0,
                      clip.active ? clip.w : float(target.width), clip.active ? clip.h : float(target.height), true);
    restoreCurrentPipeline();
    if (target.inPass) {
        sg_apply_viewportf(vp.active ? vp.x : 0, vp.active ? vp.y : 0,
                          vp.active ? vp.w : float(target.width), vp.active ? vp.h : float(target.height), true);
        sg_apply_scissor_rectf(clip.active ? clip.x : 0, clip.active ? clip.y : 0,
                              clip.active ? clip.w : float(target.width), clip.active ? clip.h : float(target.height), true);
    }
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
    currentWindowContext().currentTarget->camera.currentCameraContext = std::move(ctx);
}

} // namespace internal
} // namespace trussc
