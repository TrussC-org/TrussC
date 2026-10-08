#pragma once
// =============================================================================
// RenderTarget — single source of truth for "which sgl pipeline for the current
// target". Replaces the scattered `inFboPass ? fboPipeline : swapchainPipeline`
// branches (every recent FBO rendering bug was a missing branch).
//
// Two orthogonal axes:
//   - target : swapchain / FBO-A / FBO-B  -> sgl context (carries pixel format,
//              sample count, depth format)
//   - role   : depth + rgb-blend + alpha-blend  (intrinsic to the draw)
// A pipeline is f(target, role): each RenderTarget lazily builds+caches the
// pipeline for a requested role in ITS context, so one desc serves any target
// (sgl fills format/sample/depth from the context).
//
// Migration is incremental: this is added in parallel with the old globals; call
// sites switch to active2D()/activePremult()/activeClear()/active3D() per slice.
// Included from TrussC.h after BlendMode + the sokol headers.
// =============================================================================
#include <unordered_map>
#include <cstdint>
#ifndef NDEBUG
#include <cassert>
#endif

namespace trussc { namespace internal {

#ifndef NDEBUG
// Debug safety net: remember which sgl context each pipeline we build belongs to,
// so loadPipeline() can assert the pipeline matches the active target. This is the
// runtime guard against the "wrong pipeline for this target" bug class that this
// whole RenderTarget refactor exists to prevent (every recent FBO glitch was one).
inline std::unordered_map<uint32_t, uint32_t>& pipelineOwnerCtx() {
    // Debug bookkeeping, harmless per module: a module only checks the
    // pipelines it built itself.
    static std::unordered_map<uint32_t, uint32_t> m;
    return m;
}
#endif

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

// A render target: an sgl context + a lazy pipeline cache (one entry per role key).
struct RenderTarget {
    sgl_context context = {};
    bool        isFbo   = false;
    std::unordered_map<uint32_t, sgl_pipeline> cache;
    // A creation failed (out of memory, or a GPU pool full): warned once and
    // not retried. The target stays degraded for the rest of the run (#317).
    bool contextFailed  = false;
    bool pipelineWarned = false;

    // Makes this target's own sgl context once. On failure logs one warning
    // and does not try again (no retry on every tick / begin()).
    bool makeContext(const sgl_context_desc_t& desc, const char* what) {
        if (context.id != SG_INVALID_ID) return true;
        if (contextFailed) return false;
        context = sgl_make_context(&desc);
        if (context.id == SG_INVALID_ID) {
            contextFailed = true;
            logWarning("TrussC") << "could not create the sokol_gl context for "
                << what << "; its shapes are not drawn for the rest of the run";
            return false;
        }
        return true;
    }

    sgl_pipeline pipeline(uint32_t key, const sg_pipeline_desc& desc) {
        if (context.id == 0) return {};   // not set up yet (e.g. headless) -> no-op
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;
        sg_pipeline_desc d = desc;   // sgl fills pixel_format/sample_count/depth from the context
        sgl_pipeline p = sgl_context_make_pipeline(context, &d);
        // Id 0 is cached too, so the creation is not retried on every call;
        // loadPipeline() then keeps the previously loaded pipeline.
        cache.emplace(key, p);
        if (p.id == SG_INVALID_ID) {
            if (!pipelineWarned) {
                pipelineWarned = true;
                logWarning("TrussC") << "could not create a sokol_gl pipeline (GPU "
                    << "pipeline pool full); draws that need it keep the previous "
                    << "blend/depth state";
            }
            return p;
        }
#ifndef NDEBUG
        pipelineOwnerCtx()[p.id] = context.id;
#endif
        return p;
    }

    // Frees this target's GPU objects: every cached sgl pipeline (sgl
    // pipelines live in one global pool, and sgl_destroy_context() frees only
    // the context's own default pipeline), then the context. For a target
    // that owns its context, e.g. a secondary window on close().
    void release() {
        for (auto& entry : cache) {
            if (entry.second.id == SG_INVALID_ID) continue;
            sgl_destroy_pipeline(entry.second);
#ifndef NDEBUG
            pipelineOwnerCtx().erase(entry.second.id);
#endif
        }
        cache.clear();
        if (context.id != SG_INVALID_ID) {
            sgl_destroy_context(context);
            context = {};
        }
        contextFailed = false;
        pipelineWarned = false;
    }
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
