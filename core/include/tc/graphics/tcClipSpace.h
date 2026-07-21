#pragma once

// =============================================================================
// tcClipSpace.h - single source of truth for the GPU clip-space Z convention
//
// GL backends (GLCORE / GLES3) use clip-space z in [-1, 1]; Metal / D3D11 /
// WebGPU use [0, 1]. TrussC keeps every stored projection matrix
// (WindowContext::currentProjectionMatrix, CameraContext::projection) in the
// ACTIVE BACKEND'S convention, so GPU pipelines (sgl, PBR, points) can upload
// them as-is. Anything that builds a projection with Mat4::ortho / frustum /
// perspective (which emit the GL convention) must pass it through
// toBackendClip() before storing, and anything that reads depth back out of
// NDC must go through depthFromNdcZ() / ndcNearZ() / ndcMidZ() instead of
// assuming [-1, 1]. See issue #134.
//
// NOTE: sokol-gl's own matrix helpers (sgl_ortho / sgl_frustum /
// sgl_perspective) always emit the GL convention and do NOT adapt. Core never
// calls them for projection setup — every projection is built as a Mat4,
// remapped with toBackendClip(), and loaded via sglLoadProjection(), so the
// sgl pipelines, the mesh pipelines (PBR / points) and the readback helpers
// all agree on one convention per backend.
// =============================================================================

#include "tcMath.h"
#include "sokol/sokol_gfx.h"
#include "sokol/util/sokol_gl_tc.h"

namespace trussc {
namespace internal {

// Test hook: forces the convention without a live GPU (-1 = auto, 0 = GL
// [-1,1], 1 = zero-to-one). Lets headless tests exercise both readback paths.
inline int& clipZOverrideForTests() {
    static int v = -1;
    return v;
}

// True when the active backend expects clip-space z in [0, 1]
// (Metal / D3D11 / WebGPU). Safe to call without a live GPU (headless
// tests, pre-setup): defaults to the GL convention then, matching the
// un-remapped matrices such code produces.
inline bool clipZeroToOne() {
    const int forced = clipZOverrideForTests();
    if (forced >= 0) return forced != 0;
    if (!sg_isvalid()) return false;
    switch (sg_query_backend()) {
        case SG_BACKEND_D3D11:
        case SG_BACKEND_METAL_IOS:
        case SG_BACKEND_METAL_MACOS:
        case SG_BACKEND_METAL_SIMULATOR:
        case SG_BACKEND_WGPU:
            return true;
        default:  // GLCORE / GLES3
            return false;
    }
}

// Remap a GL-convention projection matrix (clip z in [-1, 1]) to the active
// backend's convention. Identity on GL backends. Apply exactly once, at the
// point where the matrix is stored for rendering.
inline Mat4 toBackendClip(const Mat4& glProjection) {
    if (!clipZeroToOne()) return glProjection;
    // z' = 0.5*z + 0.5*w  (maps [-1, 1] -> [0, 1])
    static const Mat4 zeroToOne(
        1, 0, 0,    0,
        0, 1, 0,    0,
        0, 0, 0.5f, 0.5f,
        0, 0, 0,    1
    );
    return zeroToOne * glProjection;
}

// NDC z of the near plane under the active convention.
inline float ndcNearZ() { return clipZeroToOne() ? 0.0f : -1.0f; }

// NDC z halfway between near and far under the active convention. Unproject
// helpers sample here instead of the far plane to dodge precision loss with
// huge far clips.
inline float ndcMidZ() { return clipZeroToOne() ? 0.5f : 0.0f; }

// Normalized [0, 1] depth from an NDC z under the active convention.
inline float depthFromNdcZ(float ndcZ) {
    return clipZeroToOne() ? ndcZ : (ndcZ + 1.0f) * 0.5f;
}

// Load a (backend-native) projection matrix into sokol-gl's projection
// stack. Mat4 is row-major, sgl wants column-major, hence the transpose.
inline void sglLoadProjection(const Mat4& proj) {
    sgl_matrix_mode_projection();
    sgl_load_identity();
    Mat4 t = proj.transposed();
    sgl_mult_matrix(t.m);
}

// The standard TrussC 2D screen projection (pixel coordinates, y-down),
// backend-native. One definition so every 2D scope (beginFrame, Fbo 2D,
// Shader begin, RenderContext restores) shares the exact same matrix.
inline Mat4 screen2DProjection(float viewW, float viewH) {
    return toBackendClip(Mat4::ortho(0.0f, viewW, viewH, 0.0f, -10000.0f, 10000.0f));
}

} // namespace internal
} // namespace trussc
