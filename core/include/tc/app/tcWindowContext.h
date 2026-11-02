#pragma once

// =============================================================================
// tcWindowContext.h - per-window state container (internal)
// =============================================================================
//
// Multi-window support: every piece of state that is conceptually "per window"
// — input, hover/scene, render-target routing, per-window frame count,
// lighting/material/environment, and mutable shadow maps and slots —
// lives in one WindowContext instead of scattered process globals.
// App-facing APIs resolve the App token or the sole open Window; each native
// Window owns its own context and the native
// layer switches internal::currentWindowCtx() while dispatching that window's
// events and draw (see tcWindow.h / platform/*/tcWindow*).
//
// This file is included from TrussC.h AFTER tcRenderTarget.h (RenderTarget by
// value) and tcCameraContext.h / tcCoreEvents.h; it is not self-contained. It
// is included BEFORE the 3D headers (tcLight/tcMaterial/tcEnvironment), so the
// lighting members below are held by forward-declared pointer / pointer-vector.
//
// Hot-reload note: windowApiContext() is NON-inline (defined in tcGlobal.cpp)
// so Host and Guest share one instance — same pattern as events() and
// getDefaultContext(). Each Window prewires the RenderContext / CoreEvents
// pointers below to its owned state before exposing those references.

namespace trussc {

class Node;
class Window;
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
static_assert(std::extent_v<decltype(ShadowStorage::colorAttachments)> == maxShadowLights,
    "shadow storage must match the shader's slot count");

// ---------------------------------------------------------------------------
// Per-window shadow state, including lazy host-owned GPU generations. A nested
// foreign tick can park the pass without changing this owner's slots or maps.
// ---------------------------------------------------------------------------
struct ShadowSlotState {
    // Slot assignment: `frame` (the base-equivalent resource key at last reset)
    // resets the counter on the first beginShadowPass() after the key changes;
    // each pass claims the next layer in call order. Slot data persists across
    // frames so draws submitted before the first shadow pass keep the previous
    // frame's shadow state.
    uint64_t frame = static_cast<uint64_t>(-1);
    int count = 0;
    int current = -1;
    bool inPass = false;
    uint64_t allocationFailures = 0;
    std::shared_ptr<ShadowStorage> storage;
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

struct WindowContext;
// One admitted input frame. Nested native delivery installs its own frame;
// App/tree continuations reuse the admitting frame rather than recapturing it.
class MouseDispatchScope {
public:
    MouseDispatchScope(WindowContext&, int pressButton = -1, bool force = false) noexcept;
    ~MouseDispatchScope() noexcept;
    bool current(int button = -1) const noexcept;
    void trackMotionButtons(int dragButton = -1) noexcept;
    void beginRecovery(int button) noexcept;
    MouseDispatchScope(const MouseDispatchScope&) = delete;
private:
    WindowContext& ctx_;
    MouseDispatchScope* previous_;
    uint64_t epochs_[SAPP_MAX_MOUSEBUTTONS] = {};
    bool installed_;
    unsigned int motionButtons_ = 0;
};

struct WindowContext {
    WindowContext();
    ~WindowContext();
    // Recover owned scopes before native/GPU resources or any members die.
    // Safe to call again from the destructor after native window teardown.
    void teardownTargets() noexcept;
    // Terminal state for native close/destruction, distinct from recoverable
    // target-scope cleanup. Closed owners remain selectable for events only.
    bool targetsClosed = false;
    // currentTarget points into this object — copying/moving would dangle it.
    WindowContext(const WindowContext&) = delete;
    WindowContext& operator=(const WindowContext&) = delete;

    Window* hostWindow = nullptr;
    bool processSwapchainWarned = false;

    // --- input state (written by the framework event loop) ---
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    float pmouseX = 0.0f;   // Previous frame mouse position
    float pmouseY = 0.0f;
    int mouseButton = -1;   // Currently pressed button (-1 = none)
    bool mousePressed = false;
    // Outstanding presses are independent of the legacy current drag button.
    unsigned int pressedMouseButtons = 0;
    unsigned int snapshotMouseButtons = 0;
    // Scoped click ownership is consumed at converter entry, before reentrant callbacks.
    const void* pendingMousePressOwner = nullptr;
    const void* mousePressOwners[SAPP_MAX_MOUSEBUTTONS] = {};
    uint64_t mousePressGenerations[SAPP_MAX_MOUSEBUTTONS] = {};
    MouseDispatchScope* mouseDispatch = nullptr;
    void admitMousePress(int button) noexcept;
    bool pointerValid = false;
    float pointerBasis = 1;
    double pointerLogicalX = 0, pointerLogicalY = 0;
    TouchMouseMapper touchMapper;
    // MOUSE_LEAVE parks mouseX/Y offscreen for hover clearing.
    float lastPointerX = 0.0f;
    float lastPointerY = 0.0f;
    std::unordered_set<int> keysPressed;

    // --- scene / hover state (per window's node tree) ---
    // Weak references (#255): a node removed from the tree and freed while
    // one of these still names it just makes lock() return null, so the next
    // hover update, drag or selection read never touches freed memory. Use
    // them through lock() and keep the resulting shared_ptr for as long as
    // the node is used (e.g. across a handler that may remove it). A hot
    // reload host resets them before it unloads a guest
    // during its common ending: releasing the last weak reference to a
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
    // A capture must start/flush an unfinished frame, but must leave the
    // completed afterFrame source intact until the next beginFrame().
    bool framePending = true;
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
    // Host metadata is initialized before callbacks for every bound Window.
    // Headless uses a separate non-window context. Drawable acquisition is lazy.
    // Existing hidden-update behavior is constructor configuration until #997.
    bool pauseUpdatesWhenHidden = false;
    bool drawWhileHidden = true;
    const void* deferredResponseTarget = nullptr;
    // Published dimensions for bound windows; unbound main/headless reads native.
    int fbWidth = 0;
    int fbHeight = 0;
    float dpiScale = 1.0f;
    sg_swapchain (*acquireSwapchain)(void* user) = nullptr;
    void* acquireSwapchainUser = nullptr;
    // Restore the native drawable before parking/reading a window. The GL
    // adapter supplies this; other backends have a shared device context.
    void (*activateNative)(void*) = nullptr;

    // Scheduler and counters live with the host-owned window, across App swaps.
    MainLoopState loop;
    uint64_t updateCount = 0;
    uint64_t drawCount = 0;
    // Base secondary adapters advanced once after native admission, before update.
    // Independent callbacks retain that key separately from public update counts.
    uint64_t resourceTickCount = 0;
    double tickDuration = 0.0;
    bool tickActive = false;
    bool suspended = false;
    bool lifecycleSuspended = false;
    bool visibilityPaused = false;
    std::chrono::steady_clock::time_point tickTime{};

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

    // --- per-window shadow maps, slot assignment and active pass ---
    ShadowSlotState shadow;

    // --- frame timing (per window) ---
    // getDeltaTime()/getFrameRate()/getFrameElapsedTime() resolve through the
    // current context, so a window ticking at 60 Hz next to a 120 Hz main
    // window sees its own real per-tick delta (measured wall-clock between THIS
    // window's update calls; the nominal 1/updateFps in its
    // fixed-Hz update mode).
    double updateDeltaTime = 0.0;
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
    // Draw/event timing scopes are measured callbacks, not update calls.
    bool inCallbackClock = false;
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
    // --- misc per-window ---
    // Resolved absolute paths (download names on web) queued by saveScreenshot()
    // on THIS window. The common host tick drains after the complete afterFrame
    // notification while this context and lastSwapchainDrawable are current,
    // identically for main and secondaries. See TrussC.h drainPendingScreenshots().
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

    // Prewired by Window construction; the non-window fallback wires them
    // explicitly before exposing its context.
    RenderContext* render = nullptr;
    CoreEvents* coreEvents = nullptr;

    // Host-owned App bindings survive guest swaps and public Event::clear().
    std::vector<EventListener> appEventListeners;
};

// Pipeline format for an addressed Window swapchain.
struct SwapchainTargetFormat {
    sg_pixel_format colorFormat;
    int sampleCount;
};

inline SwapchainTargetFormat swapchainTargetFormat(const WindowContext& ctx) {
    return {ctx.swapchainTarget.colorFormat, ctx.swapchainTarget.sampleCount};
}

// Resolved API context. Non-inline (tcGlobal.cpp) so Host/Guest share it.
WindowContext& windowApiContext();
WindowContext* findWindowApiContext();
class WindowContextScope {
public:
    explicit WindowContextScope(WindowContext*);
    ~WindowContextScope() noexcept;
    WindowContextScope(const WindowContextScope&) = delete;
private:
    WindowContext* previous_;
    void* display_ = nullptr;
    void* context_ = nullptr;
    uintptr_t draw_ = 0, read_ = 0;
    uint32_t drawWindow_ = 0, readWindow_ = 0, sgl_ = 0;
};

// Active context while dispatching a window's events / draw. Null = no Window. The
// native layer selects the addressed Window around its tick. Non-inline
// storage ensures hot-reload guests observe the same physical selection.
WindowContext*& currentWindowCtx();

// The context drawing and input calls act on. Also non-inline: it reads
// currentWindowCtx(), and in a single-window app it costs the one out-of-line
// call its inline version always made (to windowApiContext()).
WindowContext& currentWindowContext();

// Restore both window dispatch and its current target's actual SGL context.
// nullptr restores process context, without selecting any Window or surface.
void restoreWindowContext(WindowContext* previous);

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

// Timers created from event/draw listeners use this window's measured clock,
// never a stale fixed-step mark. Nested dispatch restores its caller's scope.
class WindowCallbackTimeScope {
    WindowContext& ctx_;
    bool inUpdate_;
    bool fixed_;
    bool callback_;
    std::chrono::steady_clock::time_point time_;
public:
    WindowCallbackTimeScope();
    ~WindowCallbackTimeScope();
};

// One conversion for resize notifications and getters, preserving main's truncation.
using WindowClock = std::chrono::steady_clock::time_point (*)();
WindowClock& windowClockForTests();
std::chrono::steady_clock::time_point windowClockNow();
int framebufferToLogical(int pixels, float dpi);
ResizeEventArgs framebufferResizeArgs(int width, int height, float dpi);
float appSizeDpi(float dpi);
// Framebuffer pointer position/motion to App units, following the global mode.
float appPointerScale(float dpi);
uint64_t resourceFrameKey(); // Base getFrameCount key for internal resource consumers.
uint64_t deviceFrameCount(); // GPU upload guard; independent of window draw counts.

// A deterministic clock / GPU seam used by the shared tick tests. Production
// callers omit it and use the real frame and presentation path.
struct WindowTickPresentation {
    std::function<void()> begin;
    std::function<bool()> present; // false: rendering/presentation was not admitted
    std::function<void()> finish;
};
void observeWindowVisibility(WindowContext& ctx, sapp_event_type type);
bool runWindowTick(std::chrono::steady_clock::time_point now, double sourceDuration,
                   bool forceDraw = false, const WindowTickPresentation* presentation = nullptr, bool nativeDrawGated = false);

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
    currentTarget().automaticScreenProjection = false;
}

} // namespace internal
} // namespace trussc
