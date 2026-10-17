#pragma once
#include <TrussC.h>
#include "tc/gpu/shaders/videoNV12.glsl.h"


namespace trussc::internal {
// NV12 immediate-draw shader (immutable unit-quad, uniform-based positioning)
class NV12VideoShader : public Shader {
public:
    void loadShader() { load(tc_video_nv12_nv12_shader_desc); }

    void draw(float x, float y, float w, float h,
              const Texture& texY, const Texture& texUV) {
        if (!loaded) return;

        internal::ensureCurrentPass();
        internal::currentTarget().flushLayers();

        sg_apply_pipeline(pipeline);

        // x/y/w/h come in logical coordinates (same space tcApp draws in),
        // but sapp_width()/sapp_height() are physical framebuffer pixels.
        // Divide by DPI scale so the shader's viewport matches its inputs.
        // Unrounded logical size: fractional DPI must not shift the video.
        const Vec2 logical = internal::targetLogicalSize();
        const float vpW = logical.x;
        const float vpH = logical.y;

        struct VsParams { float v[8]; } p = {{
            vpW, vpH, x, y,
            w,   h,   0.0f, 0.0f
        }};
        sg_range range = { &p, sizeof(p) };
        sg_apply_uniforms(0, &range);

        sg_bindings bind = {};
        bind.vertex_buffers[0] = vertexBuffer;
        bind.index_buffer      = indexBuffer;
        bind.views[0]    = texY.getView();
        bind.samplers[0] = texY.getSampler();
        bind.views[1]    = texUV.getView();
        bind.samplers[1] = texUV.getSampler();
        sg_apply_bindings(&bind);

        sg_draw(0, 6, 1);

        sg_reset_state_cache();
        internal::applyTargetDrawingState();
    }

protected:
    sg_pipeline_desc createPipelineDesc() override {
        sg_pipeline_desc desc = {};
        desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;
        desc.colors[0].blend.enabled = false;
        desc.index_type = SG_INDEXTYPE_UINT16;
        desc.label = "tc_nv12_pipeline";
        return desc;
    }

    void createVertexBuffer() override {
        float verts[] = { 0.f,0.f, 1.f,0.f, 1.f,1.f, 0.f,1.f };
        sg_buffer_desc vd = {};
        vd.data  = SG_RANGE(verts);
        vd.label = "tc_nv12_verts";
        vertexBuffer = sg_make_buffer(&vd);

        uint16_t idx[] = { 0, 1, 2, 0, 2, 3 };
        sg_buffer_desc id = {};
        id.usage.index_buffer = true;
        id.data  = SG_RANGE(idx);
        id.label = "tc_nv12_idx";
        indexBuffer = sg_make_buffer(&id);
    }
};

} // namespace trussc::internal
