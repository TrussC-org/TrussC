#pragma once
// =============================================================================
// RenderTarget owns the lightweight state of one drawable (swapchain or Fbo).
// Fbo targets borrow GPU resources from the (format, samples, depth) pool;
// window targets keep a dedicated context because their recorded commands coexist.
// Targets own matrices and pass state; all pipeline creation is host-owned.
// Included from TrussC.h after BlendMode, CameraContext and the sokol headers.
// =============================================================================
#include "impl/tcImportError.h"
#include <unordered_map>
#include <cstdint>
#include <climits>
#ifndef NDEBUG
#include <cassert>
#endif

namespace trussc { namespace internal {

// Always exported: Debug consumers can link against a Release core.
std::unordered_map<uint32_t, uint32_t>& pipelineOwnerCtx();

// Premultiplied-alpha sgl shader handle (defined in tcGlobal.cpp, built from
// core/shaders/sglPremult.glsl). Screen/Multiply pipelines bind it so their blend
// equations respect source alpha; every other role keeps sgl's straight shader.
// Returns {0} before sokol is up — sgl then falls back to its built-in shader.
sg_shader sglPremultShader();

// Coverage sgl shader handle (defined in tcGlobal.cpp, built from
// core/shaders/sglCoverage.glsl): samples a single-channel texture and uses R as
// alpha. Bound by pipeDescCoverage2D() for the R8 TrueType glyph atlas.
sg_shader sglCoverageShader();

// --- Role blend/depth specs (the blend tables formerly duplicated in tcGlobal.cpp).
// Pixel format / sample count / depth format are left at defaults on purpose: sgl
// fills them from the target's context, so the same desc is correct for swapchain
// AND any FBO format. Keep these in sync with tcGlobal.cpp until that is unified.
inline sg_pipeline_desc pipeDesc2D(BlendMode mode, bool depthTest = false) {
    sg_pipeline_desc d = {};
    d.colors[0].write_mask = SG_COLORMASK_RGBA;   // write alpha too (sgl defaults to RGB-only)
    auto& b = d.colors[0].blend;
    switch (mode) {
        case BlendMode::Alpha:    // normal alpha; alpha channel ACCUMULATES (unified)
            b.enabled = true;
            b.src_factor_rgb   = SG_BLENDFACTOR_SRC_ALPHA; b.dst_factor_rgb   = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            b.src_factor_alpha = SG_BLENDFACTOR_ONE;       b.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            break;
        case BlendMode::Add:
            b.enabled = true;
            b.src_factor_rgb   = SG_BLENDFACTOR_SRC_ALPHA; b.dst_factor_rgb   = SG_BLENDFACTOR_ONE;
            b.src_factor_alpha = SG_BLENDFACTOR_ONE;       b.dst_factor_alpha = SG_BLENDFACTOR_ONE;
            break;
        case BlendMode::Multiply:
            // Premultiplied source (see sglPremultShader): result = mix(dst, dst*src, a).
            // src*DST_COLOR = (a*Cs)*Cb, dst*(1-a) keeps the backdrop where the source
            // is transparent. α-correct over an OPAQUE backdrop (exact Photoshop there);
            // over a partially-transparent backdrop it approximates, and over a fully
            // transparent one it tends to black — an accepted limitation (fixed-function
            // can't express the exact Multiply for arbitrary backdrop alpha).
            b.enabled = true;
            d.shader = sglPremultShader();
            b.src_factor_rgb   = SG_BLENDFACTOR_DST_COLOR; b.dst_factor_rgb   = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            b.src_factor_alpha = SG_BLENDFACTOR_ONE;       b.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            break;
        case BlendMode::Screen:
            // Premultiplied source (see sglPremultShader): SRC_COLOR carries rgb*a, so
            // result = S + dst*(1-S) with S = rgb*a. This is exactly Photoshop's Screen
            // for ANY backdrop alpha (the premult algebra collapses the Adobe blend-in-
            // group formula to this fixed-function form). Alpha composites as over.
            b.enabled = true;
            d.shader = sglPremultShader();
            b.src_factor_rgb   = SG_BLENDFACTOR_ONE;       b.dst_factor_rgb   = SG_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
            b.src_factor_alpha = SG_BLENDFACTOR_ONE;       b.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            break;
        case BlendMode::Subtract:
            b.enabled = true;
            b.op_rgb = SG_BLENDOP_REVERSE_SUBTRACT;
            b.src_factor_rgb   = SG_BLENDFACTOR_SRC_ALPHA; b.dst_factor_rgb   = SG_BLENDFACTOR_ONE;
            b.op_alpha = SG_BLENDOP_ADD;  // alpha stays additive
            b.src_factor_alpha = SG_BLENDFACTOR_ONE;       b.dst_factor_alpha = SG_BLENDFACTOR_ONE;
            break;
        case BlendMode::Disabled:
        default:
            b.enabled = false;
            break;
    }
    // Depth-tested variant (tc::enableDepthTest()): same depth config as the 3D
    // pipeline (pipeDesc3D below), so blended draws can rejoin the scene's depth
    // buffer after a blend-mode change mid-frame.
    if (depthTest) {
        d.depth.write_enabled = true;
        d.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    }
    return d;
}

// Opaque fullscreen erase quad used by clear() (issue #190): blending disabled
// (dst = src, alpha included) and depth write with compare ALWAYS. The quad is
// drawn at the far plane, so it both erases prior color AND resets the depth
// buffer to far — 3D drawn after a mid-frame clear() is not occluded by
// pre-clear 3D geometry.
inline sg_pipeline_desc pipeDescClear() {
    sg_pipeline_desc d = pipeDesc2D(BlendMode::Disabled);
    d.depth.write_enabled = true;
    d.depth.compare = SG_COMPAREFUNC_ALWAYS;
    return d;
}

// Compositing a PREMULTIPLIED source (e.g. an FBO color texture). rgb is NOT
// re-multiplied by src alpha (it already is).
inline sg_pipeline_desc pipeDescPremult(bool depthTest = false) {
    sg_pipeline_desc d = {};
    d.colors[0].write_mask = SG_COLORMASK_RGBA;
    auto& b = d.colors[0].blend;
    b.enabled = true;
    b.src_factor_rgb   = SG_BLENDFACTOR_ONE; b.dst_factor_rgb   = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    b.src_factor_alpha = SG_BLENDFACTOR_ONE; b.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    if (depthTest) {   // see pipeDesc2D — keeps premultiplied texture draws consistent
        d.depth.write_enabled = true;
        d.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    }
    return d;
}

// Single-channel coverage texture (the R8 TrueType glyph atlas): the Alpha blend
// of pipeDesc2D with sglCoverageShader(), which turns the R channel into alpha.
inline sg_pipeline_desc pipeDescCoverage2D(bool depthTest = false) {
    sg_pipeline_desc d = pipeDesc2D(BlendMode::Alpha, depthTest);
    d.shader = sglCoverageShader();
    return d;
}

// Depth-tested 3D geometry (same accumulating-alpha blend as 2D Alpha).
inline sg_pipeline_desc pipeDesc3D() {
    sg_pipeline_desc d = pipeDesc2D(BlendMode::Alpha);
    d.cull_mode = SG_CULLMODE_NONE;
    d.depth.write_enabled = true;
    d.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    return d;
}

// Requests describe immutable raster state; attachment fields are always supplied
// by the target. Pipeline programs and caches live in the host (#249).
enum class PipelineFamily { Immediate, Raw };
enum class PipelineRole { Fill, Premult, Coverage, Clear, Geometry3D,
    Pbr, PointSplat, PointPixel, Shader, Shadow, Mip, Blit, Ibl, ImGui };
struct ImmediateSelection {
    PipelineRole role = PipelineRole::Fill;
    BlendMode blend = BlendMode::Alpha;
    bool depth = false;
    bool operator==(const ImmediateSelection& other) const {
        return role == other.role && blend == other.blend && depth == other.depth;
    }
};
inline ImmediateSelection fillSelection(BlendMode blend, bool depth = false) {
    return {PipelineRole::Fill, blend, depth};
}
struct PipelineKey {
    PipelineFamily family = PipelineFamily::Immediate;
    PipelineRole role = PipelineRole::Fill;
    sg_pipeline_desc desc = {};
    ImmediateSelection selection;
    static PipelineKey raw(PipelineRole role, const sg_pipeline_desc& desc) {
        PipelineKey key; key.family = PipelineFamily::Raw; key.role = role;
        key.desc = desc; return key;
    }
    static PipelineKey immediate(ImmediateSelection selection);
};
// Field-wise encoding, excluding padding, labels and canaries. Equality checks
// the entire encoding even on a hash collision.
struct PipelineCacheKey {
    std::vector<uint32_t> fields;
    bool operator==(const PipelineCacheKey& other) const { return fields == other.fields; }
};
struct PipelineCacheHash {
    size_t operator()(const PipelineCacheKey& key) const {
        size_t hash = 2166136261u;
        for (auto field : key.fields) hash = (hash ^ field) * 16777619u;
        return hash;
    }
};
struct PipelineGeneration {
    uint64_t serial = 0;
    std::unordered_map<PipelineCacheKey, sg_pipeline, PipelineCacheHash> cache;
    ~PipelineGeneration();
};
struct PipelineSelection {
    sgl_pipeline immediate = {};
    sg_pipeline raw = {};
    std::shared_ptr<PipelineGeneration> generation;
};
struct RenderTargetResources {
    sgl_context context = {};
    std::unordered_map<PipelineCacheKey, sgl_pipeline, PipelineCacheHash> cache;
    std::unordered_map<uint32_t, ImmediateSelection> selections;
    std::shared_ptr<PipelineGeneration> rawCache;
    uint64_t generation = 0;
    int width = 0, height = 0;
    sg_pixel_format color = _SG_PIXELFORMAT_DEFAULT, depth = _SG_PIXELFORMAT_DEFAULT;
    int samples = 0;
    bool leased = false;
    bool contextFailed = false;
    bool makeContext(const sgl_context_desc_t& desc, const char* what);
    void release();
};
void retirePipelineProgram(sg_shader shader);
void returnTargetResources(RenderTargetResources& lease);
void invalidateTargetBucket(sg_pixel_format format, int samples, sg_pixel_format depth);
bool rebuildTargetResources();
void prepareWindowTarget();
void refreshWindowDrawingSize();

// C++17 vectors support incomplete elements; these draw snapshots are defined
// later in TrussC.h. Targets are constructed where those definitions are complete.
struct DeferredPbrDraw;
struct DeferredPointDraw;
struct DeferredShaderDraw;

struct ScissorRect {
    float x, y, w, h;
    bool active;
};

struct TargetCameraState {
    std::shared_ptr<const CameraContext> currentCameraContext;
    Mat4 currentViewMatrix = Mat4::identity();
    Mat4 currentProjectionMatrix = Mat4::identity();
    float currentScreenFov = TAU / 8.0f;
    float currentViewW = 0.0f;
    float currentViewH = 0.0f;
    float currentCameraDist = 0.0f;
    Vec3 cameraPosition = {0, 0, 0};
};

// Cache role key plus selected depth/blend variant, independent of logical flags.
struct TargetCameraScope {
    TargetCameraState camera;
    ImmediateSelection selectedPipeline;
    const void* owner;
    bool automaticScreenProjection = false;
};

uint64_t nextTargetOwnerId();
struct WindowContext;
enum class TargetPassKind { Swapchain, PublicFbo, Fixed, Transfer };
enum class TargetPassPhase { Preparing, Rendering, Finalizing };
enum class TargetPassStart { Initial, Resume, Clear };

struct RenderTarget {
    uint64_t ownerId = nextTargetOwnerId();
    int width = 0;
    int height = 0;
    sg_pixel_format colorFormat = _SG_PIXELFORMAT_DEFAULT;
    int sampleCount = 0;
    bool hasDepth = true;
    bool isFbo = false; // attachment-pixel coordinates (public or fixed raster)
    sg_pass_action clearAction = [] {
        sg_pass_action action = {};
        action.colors[0].clear_value = {0, 0, 0, 1};
        return action;
    }();
    bool inPass = false;
    bool passStartedThisFrame = false;
    uint64_t frameEpoch = UINT64_MAX;
    uint64_t activation = 0;
    WindowContext* activeWindow = nullptr;
    TargetPassKind passKind = TargetPassKind::Swapchain;
    sg_pass binding = {};
    PipelineKey fixedPipeline; // resolved again on resume
    bool hasFixedPipeline = false;
    sg_image transferSource = {}; // resolved source retained by a native transfer
    sg_pixel_format depthFormat = SG_PIXELFORMAT_DEPTH_STENCIL;
    bool resumable = true;
    unsigned openShaderScopes = 0;
    // Owner finalization runs while the completed entry is still on the stack.
    // Mip scopes cannot reopen it; the parent resumes only after finalization.
    void* owner = nullptr;
    void (*finalize)(void*) = nullptr;
    void (*beginTransfer)(void*) = nullptr;
    void (*endTransfer)(void*) = nullptr;

    void beginPass(TargetPassStart reason);
    void endPass();
    void flushLayers();
    void advanceLayer();
    void restartClear(float r, float g, float b, float a);

    TargetCameraState camera;
    std::vector<TargetCameraScope> cameraStack;
    ImmediateSelection selectedPipeline;
    Mat4 currentMatrix = Mat4::identity();
    std::vector<Mat4> matrixStack;
    ScissorRect viewport = {0, 0, 0, 0, false};
    // Only beginFrame opts into resize-following screen state. Application
    // camera/viewport choices opt out independently until the next frame.
    bool automaticScreenProjection = false;
    bool automaticViewport = false;
    bool windowDrawingSizeStale = false;
    int automaticScreenWidth = 0, automaticScreenHeight = 0;
    ScissorRect currentScissor = {0, 0, 0, 0, false};
    std::vector<ScissorRect> scissorStack;
    BlendMode currentBlendMode = BlendMode::Alpha;
    bool depthTestEnabled = false;

    std::vector<DeferredPbrDraw> deferredPbrDraws;
    std::vector<DeferredPointDraw> deferredPointDraws;
    std::vector<DeferredShaderDraw> deferredShaderDraws;
    int sglLayerNext = 0;

    RenderTargetResources windowResources;
    RenderTargetResources* pooledResources = nullptr;
    RenderTargetResources& resources() {
        return pooledResources ? *pooledResources : windowResources;
    }
    const RenderTargetResources& resources() const {
        return pooledResources ? *pooledResources : windowResources;
    }
    int getWidth() const { return width; }
    int getHeight() const { return height; }
    PipelineSelection pipeline(const PipelineKey& key);

    // Only these logical flags retain continuity across targets in PR 2.
    void inheritLogicalDrawFlags(const RenderTarget& previous) {
        currentBlendMode = previous.currentBlendMode;
        depthTestEnabled = previous.depthTestEnabled;
    }
};
// A failed append skips this draw without discarding earlier recordings.
template<class Queue, class Draw>
bool appendTargetRecording(RenderTarget&, Queue& queue, Draw&& draw) {
    try {
        queue.push_back(std::forward<Draw>(draw));
        return true;
    } catch (const std::bad_alloc&) {
        reportImportError("RenderTarget", "could not allocate draw recording");
    } catch (const std::length_error&) {
        reportImportError("RenderTarget", "draw recording size cannot be represented");
    }
    return false;
}

// Stack entries use tokens, never addresses into the growable entry vector.
struct TargetStackEntry {
    RenderTarget* target;
    uint64_t token;
    TargetPassPhase phase;
    bool resumeParent;
    RenderTargetResources* lease;
};

uint64_t pushTarget(RenderTarget& target, bool prepareOnly = false);
bool startPreparedTarget(RenderTarget& target, uint64_t token);
bool popTarget(RenderTarget& target, uint64_t token);
void ensureCurrentPass();
void parkWindowPass();
void submitCapturePrefix();
// Window: a capture of the current window's pixels; even a never-opened pass
// starts so its clear and recorded prefix reach the source.
// Target: an Fbo readback; only an open pass is parked and resumed, as with
// Metal's transfer target, so the caller's swapchain pass state is unchanged.
enum class CaptureSource { Window, Target };
class CapturePrefixScope {
public:
    explicit CapturePrefixScope(CaptureSource source = CaptureSource::Window);
    ~CapturePrefixScope() noexcept;
    // A failed native capture must not retry a stalled GPU slot just to resume.
    void leaveParked() noexcept { resume_ = false; }
    CapturePrefixScope(const CapturePrefixScope&) = delete;
    CapturePrefixScope& operator=(const CapturePrefixScope&) = delete;
private:
    WindowContext* window_;
    RenderTarget* target_;
    uint64_t token_;
    bool resume_;
};
// Q2 plug-in: source selection/finalization stay behind this host interface.
// Part 1 uses the existing native swapchain source; it allocates no snapshot.
// Bound (abi): native capture/encoder row strides must fit int and allocation byte products must fit size_t. Overflow is reported by captureLayout returning false.
inline bool captureLayout(int width, int height, size_t bytesPerPixel,
                          size_t& rowBytes, size_t& totalBytes) {
    if (width <= 0 || height <= 0 || !bytesPerPixel ||
        static_cast<size_t>(width) > static_cast<size_t>(INT_MAX) / bytesPerPixel) return false;
    rowBytes = static_cast<size_t>(width) * bytesPerPixel;
    if (static_cast<size_t>(height) > SIZE_MAX / rowBytes) return false;
    totalBytes = rowBytes * static_cast<size_t>(height);
    return true;
}
struct WindowCaptureSource {
    sg_swapchain swapchain = {};
    const void* drawable = nullptr;
    int width = 0, height = 0;
};
WindowCaptureSource windowCaptureSource();
bool finishWindowCaptureSource(RenderTarget& target);
// One host-allocated generation per shadow-using window. Deferred PBR retains
// its generation; final release links it into host retirement without allocating.
struct ShadowStorage {
    ShadowStorage() = default;
    ShadowStorage(const ShadowStorage&) = delete;
    ShadowStorage& operator=(const ShadowStorage&) = delete;
    ~ShadowStorage() noexcept;
    sg_image colorImage = {}, depthImage = {};
    sg_view colorAttachments[4] = {}, colorTexture = {}, depthAttachment = {};
    sg_sampler sampler = {};
    int resolution = 0;
    size_t imageBytes = 0;
    ShadowStorage* retiredNext = nullptr;
};
bool shadowImageLayout(int resolution, size_t& colorBytes, size_t& depthBytes, size_t& totalBytes);
bool ensureShadowStorage(WindowContext& window, int resolution);
void consumeShadowSlot(int slot);
bool hasPublicFboActivation();
RenderTargetResources* leaseTargetResources(sg_pixel_format format, int samples,
    sg_pixel_format depth = SG_PIXELFORMAT_DEPTH_STENCIL);

// Internal scopes own fixed targets or borrow a stable owner target. All stack
// operations live in the host, including destruction during early returns.
class TargetPassScope {
public:
    explicit TargetPassScope(RenderTarget& target);
    TargetPassScope(const sg_pass& pass, int width, int height,
                    sg_pixel_format format, int samples = 1, bool depth = false);
    ~TargetPassScope() noexcept;
    TargetPassScope(const TargetPassScope&) = delete;
    TargetPassScope& operator=(const TargetPassScope&) = delete;
    bool active() const { return token_ != 0 && target_->activation == token_; }
private:
    RenderTarget owned_;
    RenderTarget* target_;
    uint64_t token_ = 0;
};

// Check for TrussC's internal lazily created shaders (PBR, shadow, points,
// IBL, Fbo mip/blit), called once right after sg_make_shader(): true when the
// shader is usable. Otherwise logs one warning; the caller remembers the
// result and skips the draws that need the shader, instead of making a
// pipeline from an invalid shader (#317).
inline bool internalShaderReady(sg_shader shd, const char* name) {
    if (sg_query_shader_state(shd) == SG_RESOURCESTATE_VALID) return true;
    logWarning("TrussC") << "could not create the internal " << name
        << " shader (GPU shader pool full?); draws that use it are skipped";
    return false;
}

// The swapchain target, the active-target pointer, and the active*() pipeline
// helpers moved to the per-window WindowContext — see tc/app/tcWindowContext.h
// (included right after this header from TrussC.h).

}} // namespace trussc::internal
