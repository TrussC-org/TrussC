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

namespace trussc {
namespace internal {

void deferGpuDestroy(sg_buffer buf);
void deferGpuDestroy(sg_image img);
void deferGpuDestroy(sg_view view);
void deferGpuDestroy(sg_sampler smp);
void deferGpuDestroy(sg_pipeline pip);
void deferGpuDestroy(sg_shader shd);

// Destroy everything queued this frame. Called from present() after
// sg_commit(). Skips the sg_destroy calls entirely when sokol_gfx has
// already shut down (handles queued during teardown are reclaimed by
// sg_shutdown itself).
void drainPendingGpuDestroys();

} // namespace internal
} // namespace trussc
