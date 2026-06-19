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

namespace trussc { namespace internal {

// --- Role blend/depth specs (the blend tables formerly duplicated in tcGlobal.cpp).
// Pixel format / sample count / depth format are left at defaults on purpose: sgl
// fills them from the target's context, so the same desc is correct for swapchain
// AND any FBO format. Keep these in sync with tcGlobal.cpp until that is unified.
inline sg_pipeline_desc pipeDesc2D(BlendMode mode) {
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
            b.enabled = true;
            b.src_factor_rgb   = SG_BLENDFACTOR_DST_COLOR; b.dst_factor_rgb   = SG_BLENDFACTOR_ZERO;
            b.src_factor_alpha = SG_BLENDFACTOR_ONE;       b.dst_factor_alpha = SG_BLENDFACTOR_ONE;
            break;
        case BlendMode::Screen:
            b.enabled = true;
            b.src_factor_rgb   = SG_BLENDFACTOR_ONE;       b.dst_factor_rgb   = SG_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
            b.src_factor_alpha = SG_BLENDFACTOR_ONE;       b.dst_factor_alpha = SG_BLENDFACTOR_ONE;
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
    return d;
}

// Compositing a PREMULTIPLIED source (e.g. an FBO color texture). rgb is NOT
// re-multiplied by src alpha (it already is).
inline sg_pipeline_desc pipeDescPremult() {
    sg_pipeline_desc d = {};
    d.colors[0].write_mask = SG_COLORMASK_RGBA;
    auto& b = d.colors[0].blend;
    b.enabled = true;
    b.src_factor_rgb   = SG_BLENDFACTOR_ONE; b.dst_factor_rgb   = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    b.src_factor_alpha = SG_BLENDFACTOR_ONE; b.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
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

    sgl_pipeline pipeline(uint32_t key, const sg_pipeline_desc& desc) {
        if (context.id == 0) return {};   // not set up yet (e.g. headless) -> no-op
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;
        sg_pipeline_desc d = desc;   // sgl fills pixel_format/sample_count/depth from the context
        sgl_pipeline p = sgl_context_make_pipeline(context, &d);
        cache.emplace(key, p);
        return p;
    }
};

inline RenderTarget  swapchainTarget;                  // .context set after sgl_setup()
inline RenderTarget* currentTarget = &swapchainTarget; // Fbo::begin/end retargets this

// Role keys: high nibble = role, low byte = blend mode (2D only).
inline sgl_pipeline active2D(BlendMode m) { return currentTarget->pipeline(0x000u | (uint32_t)m, pipeDesc2D(m)); }
inline sgl_pipeline activeFill2D()        { return active2D(BlendMode::Alpha); }
inline sgl_pipeline activePremult()       { return currentTarget->pipeline(0x100u, pipeDescPremult()); }
inline sgl_pipeline activeClear()         { return currentTarget->pipeline(0x200u, pipeDesc2D(BlendMode::Disabled)); }
inline sgl_pipeline active3D()            { return currentTarget->pipeline(0x300u, pipeDesc3D()); }

// Single chokepoint for loading an sgl pipeline by role. No-op if the target isn't
// ready (id 0). Future: assert here that the pipeline matches the active target's
// format (debug safety net against the "wrong pipeline for this target" bug class).
inline void loadPipeline(sgl_pipeline p) { if (p.id != 0) sgl_load_pipeline(p); }

}} // namespace trussc::internal
