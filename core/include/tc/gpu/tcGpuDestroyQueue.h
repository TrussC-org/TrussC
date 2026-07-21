#pragma once

// This file is included from TrussC.h (early, right after the sokol_gfx
// wrappers), so every header that releases GPU resources can defer their
// destruction.
//
// TrussC records draws and replays them at end-of-frame flush: sokol_gl
// commands (textured quads, bitmap font glyphs) are drawn in present(),
// and deferred PBR / point mesh commands capture sg_bindings that are
// replayed in flushDeferredShaderDraws() (swapchain) or Fbo::end() (FBO
// passes). Destroying a GPU resource mid-frame therefore leaves dead
// handles inside already-recorded commands — sokol invalidates the next
// draw and silently drops it.
//
// Instead of destroying immediately, callers hand their handles to
// internal::deferGpuDestroy(). The queues are drained once per frame in
// present(), after sg_commit(), when every recorded command referencing
// the old handles has been submitted.

#include <vector>

namespace trussc {
namespace internal {

inline std::vector<sg_buffer>  pendingGpuBufferDestroys;
inline std::vector<sg_image>   pendingGpuImageDestroys;
inline std::vector<sg_view>    pendingGpuViewDestroys;
inline std::vector<sg_sampler> pendingGpuSamplerDestroys;

inline void deferGpuDestroy(sg_buffer buf) {
    if (buf.id != 0) pendingGpuBufferDestroys.push_back(buf);
}

inline void deferGpuDestroy(sg_image img) {
    if (img.id != 0) pendingGpuImageDestroys.push_back(img);
}

inline void deferGpuDestroy(sg_view view) {
    if (view.id != 0) pendingGpuViewDestroys.push_back(view);
}

inline void deferGpuDestroy(sg_sampler smp) {
    if (smp.id != 0) pendingGpuSamplerDestroys.push_back(smp);
}

// Destroy everything queued this frame. Called from present() after
// sg_commit(). Skips the sg_destroy calls entirely when sokol_gfx has
// already shut down (handles queued during teardown are reclaimed by
// sg_shutdown itself).
inline void drainPendingGpuDestroys() {
    if (sg_isvalid()) {
        for (sg_buffer buf : pendingGpuBufferDestroys)   sg_destroy_buffer(buf);
        for (sg_view view : pendingGpuViewDestroys)      sg_destroy_view(view);
        for (sg_image img : pendingGpuImageDestroys)     sg_destroy_image(img);
        for (sg_sampler smp : pendingGpuSamplerDestroys) sg_destroy_sampler(smp);
    }
    pendingGpuBufferDestroys.clear();
    pendingGpuImageDestroys.clear();
    pendingGpuViewDestroys.clear();
    pendingGpuSamplerDestroys.clear();
}

} // namespace internal
} // namespace trussc
