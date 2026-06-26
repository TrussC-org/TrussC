#pragma once

// =============================================================================
// tcPointCloudRenderer.h - GPU instanced fat-point renderer (tc::PointCloudRenderer)
// =============================================================================
//
// Draws a Mesh of points (positions + optional per-vertex colors) as GPU-resident
// billboarded splats: one unit quad instanced once per point, screen-aligned and
// sized in the vertex shader. The win over Mesh::draw() for point clouds, which
// streams every vertex through sokol_gl immediate mode each frame: the points live
// in a GPU instance buffer and are drawn with a single instanced draw call instead
// of re-submitting every vertex on the CPU. (Mesh::draw() only has a retained-VBO
// path for lit PBR triangles; points/unlit always go through immediate mode.)
//
// Compositing: the draw is deferred into the same per-layer flush as the PBR mesh
// pipeline (deferredPointDraws, replayed by flushDeferredShaderDraws()), so it runs
// inside the target pass and shares its depth buffer - points occlude and are
// occluded correctly by other geometry, and 2D drawn after composites on top.
//
// Usage - the camera matrices are read from the active view, so draw() runs inside
// view.begin()/view.end():
//
//   tc::PointCloudRenderer pc;
//   ...
//   view.begin();
//       pc.draw(cloud, {.pointSize = 0.02f});
//   view.end();
//
// pointSize is a screen fraction (NDC), aspect-corrected to stay square in pixels.
// One renderer draws one cloud per frame (it reuses a single instance buffer); use
// separate renderers for multiple clouds.
// =============================================================================

#include "tc/gpu/shaders/pointCloud.glsl.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

namespace trussc {

namespace internal {

// A fully-resolved point-cloud draw, captured at submission time (pipeline +
// bindings + uniforms + instance count) so it can be replayed later by
// flushDeferredShaderDraws(), composited in submission order with sokol_gl 2D and
// the PBR meshes. Mirrors PbrDrawCommand.
struct PointDrawCommand {
    sg_pipeline pip;
    sg_bindings bind;
    vs_params_t vsp;
    int         instanceCount;
};
struct DeferredPointDraw { int layerId; PointDrawCommand cmd; };
inline std::vector<DeferredPointDraw> deferredPointDraws;

// Replay one packaged point draw. Used immediately inside an FBO pass and from
// flushDeferredShaderDraws() for the swapchain.
inline void executePointDraw(const PointDrawCommand& c) {
    sg_apply_pipeline(c.pip);
    sg_apply_bindings(&c.bind);
    sg_range vr{ &c.vsp, sizeof(c.vsp) };
    sg_apply_uniforms(UB_vs_params, &vr);
    sg_draw(0, 4, c.instanceCount);   // triangle strip (4 verts) x N instances
}

} // namespace internal

class PointCloudRenderer {
public:
    struct Options {
        float pointSize = 0.02f;  // splat size as a screen fraction (NDC)
        bool  round     = false;  // square (false) or disc (true) splats
    };

    ~PointCloudRenderer() { release(); }

    // Draw the cloud. Call INSIDE view.begin()/view.end().
    void draw(const Mesh& cloud, const Options& opt = {}) {
        const std::vector<Vec3>&  verts = cloud.getVertices();
        const std::vector<Color>& cols  = cloud.getColors();
        const int n = static_cast<int>(verts.size());
        if (n == 0) return;

        ensureInit();
        ensureInstanceCapacity(n);
        packAndUpload(verts, cols);

        internal::PointDrawCommand cmd{};
        cmd.pip = getPipeline();
        cmd.bind.vertex_buffers[0] = quadBuf_;
        cmd.bind.vertex_buffers[1] = instBuf_;
        cmd.vsp = makeParams(opt);
        cmd.instanceCount = n;

        if (internal::inFboPass) {
            // The FBO pass is active; submit immediately.
            internal::executePointDraw(cmd);
        } else {
            // Defer into the per-layer swapchain flush (same as PBR meshes), so the
            // points composite in submission order and share the swapchain depth.
            internal::deferredPointDraws.push_back({ internal::sglLayerNext, cmd });
            internal::sglLayerNext++;
            sgl_layer(internal::sglLayerNext);
        }
    }

private:
    // ------------------------------------------------------------------ params
    vs_params_t makeParams(const Options& opt) const {
        vs_params_t vsp = {};
        const Mat4 viewProj = (internal::currentProjectionMatrix * internal::currentViewMatrix).transposed();
        std::memcpy(vsp.viewProj, viewProj.m, sizeof(vsp.viewProj));

        // Screen-aligned splat: size is an NDC fraction, aspect-corrected so the
        // splat stays square in pixels.
        const int w = getWindowWidth();
        const int h = getWindowHeight();
        const float aspect = (w > 0) ? float(h) / float(w) : 1.0f;
        vsp.params[0] = opt.pointSize;
        vsp.params[1] = opt.round ? 1.0f : 0.0f;
        vsp.params[2] = aspect;
        return vsp;
    }

    // ------------------------------------------------------------- gpu objects
    void ensureInit() {
        if (initialized_) return;

        shader_ = sg_make_shader(point_cloud_shader_desc(sg_query_backend()));

        // Unit quad corners as a triangle strip, in [-0.5, 0.5].
        const float corners[8] = {
            -0.5f, -0.5f,
             0.5f, -0.5f,
            -0.5f,  0.5f,
             0.5f,  0.5f,
        };
        sg_buffer_desc qbd = {};
        qbd.data = SG_RANGE(corners);
        qbd.label = "tc_pc_quad";
        quadBuf_ = sg_make_buffer(&qbd);

        initialized_ = true;
    }

    // Pipeline matching the current render target's color format + sample count
    // (swapchain or the active FBO), cached per key - mirrors PbrPipeline.
    sg_pipeline getPipeline() {
        sg_pixel_format colorFmt;
        int sampleCount;
        if (internal::inFboPass) {
            colorFmt = internal::currentFboColorFormat;
            sampleCount = internal::currentFboSampleCount;
        } else {
            colorFmt = _SG_PIXELFORMAT_DEFAULT;
            sampleCount = sapp_sample_count();
        }
        const int key = static_cast<int>(colorFmt) | (sampleCount << 16);
        auto it = pipelineCache_.find(key);
        if (it != pipelineCache_.end()) return it->second;

        sg_pipeline_desc pd = {};
        pd.shader = shader_;
        pd.layout.buffers[0].stride = sizeof(float) * 2;                    // corner
        pd.layout.buffers[1].stride = sizeof(float) * 7;                    // pos3 + color4
        pd.layout.buffers[1].step_func = SG_VERTEXSTEP_PER_INSTANCE;
        pd.layout.attrs[ATTR_point_cloud_corner]  = { 0, 0,                 SG_VERTEXFORMAT_FLOAT2 };
        pd.layout.attrs[ATTR_point_cloud_inPos]   = { 1, 0,                 SG_VERTEXFORMAT_FLOAT3 };
        pd.layout.attrs[ATTR_point_cloud_inColor] = { 1, sizeof(float) * 3, SG_VERTEXFORMAT_FLOAT4 };
        pd.primitive_type = SG_PRIMITIVETYPE_TRIANGLE_STRIP;
        pd.cull_mode = SG_CULLMODE_NONE;
        pd.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
        pd.depth.write_enabled = true;
        pd.depth.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
        pd.colors[0].pixel_format = colorFmt;
        pd.sample_count = sampleCount;
        pd.label = "tc_pc_pipeline";

        sg_pipeline pip = sg_make_pipeline(&pd);
        pipelineCache_[key] = pip;
        return pip;
    }

    void ensureInstanceCapacity(int n) {
        if (n <= instCap_) return;
        if (instBuf_.id != SG_INVALID_ID) sg_destroy_buffer(instBuf_);
        instCap_ = n + n / 2;             // grow with headroom
        sg_buffer_desc ibd = {};
        ibd.size = static_cast<size_t>(instCap_) * sizeof(float) * 7;
        ibd.usage.vertex_buffer = true;
        ibd.usage.stream_update = true;
        ibd.label = "tc_pc_instances";
        instBuf_ = sg_make_buffer(&ibd);
    }

    void packAndUpload(const std::vector<Vec3>& verts, const std::vector<Color>& cols) {
        const int n = static_cast<int>(verts.size());
        packed_.resize(static_cast<size_t>(n) * 7);
        const bool haveColor = (static_cast<int>(cols.size()) == n);
        for (int i = 0; i < n; ++i) {
            float* o = &packed_[static_cast<size_t>(i) * 7];
            o[0] = verts[i].x; o[1] = verts[i].y; o[2] = verts[i].z;
            if (haveColor) { o[3] = cols[i].r; o[4] = cols[i].g; o[5] = cols[i].b; o[6] = cols[i].a; }
            else           { o[3] = 0.7f; o[4] = 0.85f; o[5] = 1.0f; o[6] = 1.0f; }
        }
        sg_range r = { packed_.data(), packed_.size() * sizeof(float) };
        sg_update_buffer(instBuf_, &r);
    }

    void release() {
        for (auto& kv : pipelineCache_) if (kv.second.id != SG_INVALID_ID) sg_destroy_pipeline(kv.second);
        pipelineCache_.clear();
        if (instBuf_.id != SG_INVALID_ID) sg_destroy_buffer(instBuf_);
        if (quadBuf_.id != SG_INVALID_ID) sg_destroy_buffer(quadBuf_);
        if (shader_.id  != SG_INVALID_ID) sg_destroy_shader(shader_);
        instBuf_ = {}; quadBuf_ = {}; shader_ = {};
        initialized_ = false;
    }

    // --------------------------------------------------------------- state
    bool        initialized_ = false;
    sg_shader   shader_   = {};
    sg_buffer   quadBuf_  = {};
    sg_buffer   instBuf_  = {};
    int         instCap_  = 0;
    std::map<int, sg_pipeline> pipelineCache_;

    std::vector<float> packed_;   // reused scratch for instance upload
};

} // namespace trussc
