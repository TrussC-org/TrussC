#pragma once

// This file is included from TrussC.h (early, right after the sokol_gfx
// wrappers), so every header that releases GPU resources can defer their
// destruction.
//
// TrussC records draws and replays them at end-of-frame flush: sokol_gl
// commands (textured quads, bitmap font glyphs) are drawn in present(),
// and deferred PBR / point mesh commands capture sg_bindings that are
// replayed in RenderTarget::flushLayers() (swapchain) or Fbo::end() (FBO
// passes). Destroying a GPU resource mid-frame therefore leaves dead
// handles inside already-recorded commands — sokol invalidates the next
// draw and silently drops it.
//
// Instead of destroying immediately, callers hand their handles to
// internal::deferGpuDestroy(). After sg_commit(), retirement enumerates every
// live target's unconsumed commands and keeps their full typed SG handles and
// dependency closure. Unrelated resources retire without waiting for an idle
// window. Native submitted work retains the backend's normal GPU lifetime.
//
// This is invariant 2 of the deferred draw model. The other three (commands
// capture inputs by value, mutable-content resources need per-frame snapshots,
// per-frame record state is per-window) are documented together in
// docs/ARCHITECTURE.md, section 5.D "Deferred Draw Model" — read that before
// adding a new deferred draw path.

// The queues are one per process, so the functions below are defined in
// tcGlobal.cpp rather than inline over header-inline vectors: a hot reload
// guest on Windows compiles its own copy of header-inline state, and every
// handle guest code released went into a queue present() never drained -- a
// GPU leak that grew each frame a guest released a temporary resource (#249).

#include <vector>

namespace trussc {
namespace internal {

// Texture owns this node before creating GPU handles. Cleanup transfers its
// existing view arrays and links the node without allocating, including OOM.
struct TextureRetirement {
    sg_image image{};
    sg_view view{}, attachment{};
    sg_sampler sampler{};
    std::vector<sg_view> cubeViews, mipAttachments, mipViews;
    TextureRetirement* next = nullptr;
    ~TextureRetirement() noexcept;
};
void retireTexture(TextureRetirement* storage) noexcept;

// Allocation failure retains the SG-owned handle until device shutdown; never
// destroy it while recordings may still reference it or throw from a destructor.
void deferGpuDestroy(sg_buffer buf) noexcept;
void deferGpuDestroy(sg_image img) noexcept;
void deferGpuDestroy(sg_view view) noexcept;
void deferGpuDestroy(sg_sampler smp) noexcept;
void deferGpuDestroy(sg_pipeline pip) noexcept;
void deferGpuDestroy(sg_shader shd) noexcept;

// Retire queued handles unreferenced by CPU recordings. Called after
// sg_commit(). Skips the sg_destroy calls entirely when sokol_gfx has
// already shut down (handles queued during teardown are reclaimed by
// sg_shutdown itself).
void drainPendingGpuDestroys();

} // namespace internal
} // namespace trussc
