#pragma once
#include <TrussC.h>
#include "tc/video/internal/tcVideoRotation.h"
#include "tc/gpu/shaders/videoNV12.glsl.h"


namespace trussc::internal {
// NV12 immediate-draw shader (immutable unit-quad, uniform-based positioning)
class NV12VideoShader : public Shader {
public:
    explicit NV12VideoShader(VideoRotation rotation = {}) : transformed_(rotation.active()) {
        // Same source horizontal reflection, then clockwise turn as the CPU path.
        if (rotation.degrees == 90)
            orientation_ = Mat4(0,-1,0,1, 1,0,0,0, 0,0,1,0, 0,0,0,1);
        else if (rotation.degrees == 180)
            orientation_ = Mat4(-1,0,0,1, 0,-1,0,1, 0,0,1,0, 0,0,0,1);
        else if (rotation.degrees == 270)
            orientation_ = Mat4(0,1,0,0, -1,0,0,1, 0,0,1,0, 0,0,0,1);
        if (rotation.mirrored)
            orientation_ = orientation_ * Mat4(-1,0,0,1, 0,1,0,0, 0,0,1,0, 0,0,0,1);
    }

    void loadShader() { load(tc_video_nv12_nv12_shader_desc); }

    void draw(float x, float y, float w, float h,
              const Texture& texY, const Texture& texUV) {
        if (!loaded) return;

        internal::ensureCurrentPass();
        internal::currentTarget().flushLayers();
        internal::applyTargetDrawingState();

        const auto selected = selectPipelineForCurrentTarget_();
        if (!selected.raw.id) return;
        sg_apply_pipeline(selected.raw);
        tc_video_nv12_vs_params_t params = {};
        const auto& target = internal::currentTarget();
        auto mvp = target.camera.currentProjectionMatrix * target.camera.currentViewMatrix * target.currentMatrix;
        if (transformed_) {
            // UVs remain encoded; map the unit quad into the display rectangle.
            mvp = mvp * Mat4::translate(x, y, 0) * Mat4::scale(w, h, 1) * orientation_;
            params.rect[2] = params.rect[3] = 1;
        } else {
            params.rect[0] = x; params.rect[1] = y; params.rect[2] = w; params.rect[3] = h;
        }
        mvp = mvp.transposed();
        std::memcpy(params.mvp, mvp.m, sizeof(params.mvp));
        sg_range range = {&params, sizeof(params)};
        sg_apply_uniforms(UB_tc_video_nv12_vs_params, &range);

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

private:
    Mat4 orientation_;
    bool transformed_ = false;

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
