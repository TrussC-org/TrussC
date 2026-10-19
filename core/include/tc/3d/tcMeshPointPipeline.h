#pragma once

// =============================================================================
// tcMeshPointPipeline.h - Internal GPU pipeline for point-cloud rendering
// =============================================================================
//
// Singleton sg_shader + sg_pipeline used by Mesh::drawGpuPoints() to draw a
// PrimitiveMode::Points mesh as GPU-resident billboarded splats. One unit quad
// is instanced once per point (positions + per-vertex colors live in the Mesh's
// point instance buffer), so a large cloud is one instanced draw call instead of
// streaming every vertex through sokol_gl each frame. Not part of the public API.
//
// Modeled on tcMeshPbrPipeline.h. Draws are deferred into the same per-layer
// flush as the PBR meshes, using the active target's deferredPointDraws,
// so points composite in submission order, share the target
// depth buffer, and 2D drawn afterwards lands on top.
//
// Included BEFORE tcMeshPbrPipeline.h so that header's RenderTarget::flushLayers() can
// replay point draws within the Fbo layer walk.
//
// =============================================================================

#include <cstring>
#include <map>
#include <vector>

#include "tc/gpu/shaders/pointCloud.glsl.h"

namespace trussc {
namespace internal {

// A fully-resolved point-cloud draw captured at submission time (pipeline +
// bindings + uniforms + instance count), replayed later so it composites with
// sokol_gl 2D and the PBR meshes in submission order. Mirrors PbrDrawCommand.
struct PointDrawCommand {
    sg_pipeline               pip;
    std::shared_ptr<PipelineGeneration> generation;
    sg_bindings               bind;
    tc_pointcloud_vs_params_t vsp;
    int                       numElements;    // splat: 4 (quad)    point: N
    int                       numInstances;   // splat: N (points)  point: 1
};
struct DeferredPointDraw { int layerId; PointDrawCommand cmd; };

// The active target owns the point queue and the layer counter shared with
// PBR, user shaders and sokol_gl. Both existing flush paths replay this queue.

// Submit a packaged point draw to the GPU. Used from both flush sites.
inline void executePointDraw(const PointDrawCommand& c) {
    sg_apply_pipeline(c.pip);
    sg_apply_bindings(&c.bind);
    sg_range vr{ &c.vsp, sizeof(c.vsp) };
    sg_apply_uniforms(UB_tc_pointcloud_vs_params, &vr);
    sg_draw(0, c.numElements, c.numInstances);
}

class PointPipeline {
public:
    // Lazily create the shader and the shared unit-quad buffer. Safe every frame.
    void ensureInit() {
        if (initialized_) return;
        shader_     = sg_make_shader(tc_pointcloud_point_shader_desc(sg_query_backend()));
        shaderPrim_ = sg_make_shader(tc_pointcloud_point_prim_shader_desc(sg_query_backend()));
        shaderReady_     = internal::internalShaderReady(shader_, "point");
        shaderPrimReady_ = internal::internalShaderReady(shaderPrim_, "point primitive");

        // Unit quad corners as a triangle strip, in [-0.5, 0.5].
        const float corners[8] = {
            -0.5f, -0.5f,
             0.5f, -0.5f,
            -0.5f,  0.5f,
             0.5f,  0.5f,
        };
        sg_buffer_desc qbd = {};
        qbd.data = SG_RANGE(corners);
        qbd.label = "tc_point_quad";
        quadBuf_ = sg_make_buffer(&qbd);

        initialized_ = true;
    }

    // Get or create a pipeline for the given color format and sample count.
    // alpha-to-coverage anti-aliases the round splats on a multisampled target
    // without alpha blending, so depth stays correct and order-independent.
    PipelineSelection getPipeline() {
        sg_pipeline_desc pd = {};
        pd.shader = shader_;
        pd.layout.buffers[0].stride = sizeof(float) * 2;                  // corner (per-vertex)
        pd.layout.buffers[1].stride = sizeof(float) * 7;                  // pos3 + color4 (per-instance)
        pd.layout.buffers[1].step_func = SG_VERTEXSTEP_PER_INSTANCE;
        pd.layout.attrs[ATTR_tc_pointcloud_point_corner]  = { 0, 0,                 SG_VERTEXFORMAT_FLOAT2 };
        pd.layout.attrs[ATTR_tc_pointcloud_point_inPos]   = { 1, 0,                 SG_VERTEXFORMAT_FLOAT3 };
        pd.layout.attrs[ATTR_tc_pointcloud_point_inColor] = { 1, sizeof(float) * 3, SG_VERTEXFORMAT_FLOAT4 };
        pd.primitive_type = SG_PRIMITIVETYPE_TRIANGLE_STRIP;
        pd.cull_mode = SG_CULLMODE_NONE;
        pd.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
        pd.depth.write_enabled = true;
        pd.depth.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;

        pd.alpha_to_coverage_enabled = true;

        pd.label = "tc_mesh_point_pipeline";

        return currentTarget().pipeline(PipelineKey::raw(PipelineRole::PointSplat, pd));
    }

    // Pipeline for PointStyle::Pixel: draws the per-point buffer directly as
    // POINTS (1 device pixel, no quad expansion, no instancing, no a2c).
    PipelineSelection getPrimPipeline() {
        sg_pipeline_desc pd = {};
        pd.shader = shaderPrim_;
        pd.layout.buffers[0].stride = sizeof(float) * 7;                  // pos3 + color4 (per-vertex)
        pd.layout.attrs[ATTR_tc_pointcloud_point_prim_inPos]   = { 0, 0,                 SG_VERTEXFORMAT_FLOAT3 };
        pd.layout.attrs[ATTR_tc_pointcloud_point_prim_inColor] = { 0, sizeof(float) * 3, SG_VERTEXFORMAT_FLOAT4 };
        pd.primitive_type = SG_PRIMITIVETYPE_POINTS;
        pd.cull_mode = SG_CULLMODE_NONE;
        pd.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
        pd.depth.write_enabled = true;
        pd.depth.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;


        pd.label = "tc_mesh_point_prim_pipeline";

        return currentTarget().pipeline(PipelineKey::raw(PipelineRole::PointPixel, pd));
    }

    // Draw a Points mesh. Assumes the mesh has uploaded its point instance buffer.
    void drawMesh(const Mesh& mesh) {
        ensureInit();

        const int n = mesh.getGpuPointCount();
        if (n == 0) return;

        auto& wctx = internal::currentWindowContext();
        PointDrawCommand cmd{};
        cmd.vsp = makeParams();
        // No shader for this style (warned once in ensureInit()): skip.
        if (!(getPointStyle() == PointStyle::Pixel ? shaderPrimReady_ : shaderReady_)) return;
        if (getPointStyle() == PointStyle::Pixel) {
            // True 1px point primitive: draw the point buffer's positions directly.
            const auto selection = getPrimPipeline();
            cmd.pip = selection.raw; cmd.generation = selection.generation;
            cmd.bind.vertex_buffers[0] = mesh.getGpuPointBuffer();
            cmd.numElements  = n;
            cmd.numInstances = 1;
        } else {
            // Square / Round: instanced billboard quad splat.
            const auto selection = getPipeline();
            cmd.pip = selection.raw; cmd.generation = selection.generation;
            cmd.bind.vertex_buffers[0] = quadBuf_;
            cmd.bind.vertex_buffers[1] = mesh.getGpuPointBuffer();
            cmd.numElements  = 4;
            cmd.numInstances = n;
        }

        if (!cmd.pip.id) return;

        // Defer on the active target, sharing the PBR/shader layer space.
        wctx.currentTarget->deferredPointDraws.push_back({ wctx.currentTarget->sglLayerNext, cmd });
        wctx.currentTarget->sglLayerNext++;
        sgl_layer(wctx.currentTarget->sglLayerNext);
    }

private:
    // Pack the shared vertex-stage uniforms. Size is a logical-pixel point size
    // converted to an NDC half-extent (matching strokeWeight's unit), tint is the
    // current draw color so uncolored point meshes take it.
    tc_pointcloud_vs_params_t makeParams() const {
        tc_pointcloud_vs_params_t vsp = {};

        // TrussC Mat4 is row-major; GLSL mat4 is column-major. Transpose before
        // upload so the shader can use the conventional `viewProj * v`.
        Mat4 viewProj = (internal::currentWindowContext().currentTarget->camera.currentProjectionMatrix * internal::currentWindowContext().currentTarget->camera.currentViewMatrix * internal::currentTarget().currentMatrix).transposed();
        std::memcpy(vsp.viewProj, viewProj.m, sizeof(vsp.viewProj));

        const auto& target = currentTarget();
        const float density = targetPixelScale();
        const float w = target.viewport.active ? target.viewport.w : float(target.width);
        const float h = target.viewport.active ? target.viewport.h : float(target.height);
        const float px = getPointSize() * density;   // logical pixels
        // NDC full extent = 2 * px / H gives a quad `px` logical pixels tall;
        // aspect (H/W) keeps it square in pixels (the shader scales x by it).
        vsp.params[0] = (h > 0) ? 2.0f * px / float(h) : 0.0f;
        vsp.params[1] = (getPointStyle() == PointStyle::Round) ? 1.0f : 0.0f;
        vsp.params[2] = (w > 0) ? float(h) / float(w) : 1.0f;
        vsp.params[3] = 0.0f;

        const Color& c = getColor();
        vsp.tint[0] = c.r; vsp.tint[1] = c.g; vsp.tint[2] = c.b; vsp.tint[3] = c.a;
        return vsp;
    }

    bool initialized_ = false;
    sg_shader shader_{};       // quad splat (Square/Round)
    sg_shader shaderPrim_{};   // point primitive (Pixel)
    bool shaderReady_ = false;
    bool shaderPrimReady_ = false;
    sg_buffer quadBuf_{};
};

// Singleton accessor, defined in tcGlobal.cpp: one point pipeline per process
// (see getPbrPipeline(); a Windows hot reload guest built its own, #249).
PointPipeline& getPointPipeline();

} // namespace internal

// Out-of-class definition of Mesh::drawGpuPoints(). Lives here (after the point
// pipeline is complete) rather than in tcMesh.h which sees only the declaration.
inline void Mesh::drawGpuPoints() const {
    uploadPointsToGpu();
    if (pbuf_.id == 0) return;  // upload failed or mesh empty
    internal::getPointPipeline().drawMesh(*this);
}

} // namespace trussc
