#pragma once

// =============================================================================
// tcMeshPbrPipeline.h - Internal GPU pipeline for PBR mesh rendering
// =============================================================================
//
// Singleton sg_shader + sg_pipeline used by Mesh::drawGpuPbr() to evaluate
// Cook-Torrance PBR in the fragment shader. Not part of the public API.
//
// Include order (enforced by TrussC.h):
//   1. tcLightingState.h          (LightingMode, currentPbrMaterial)
//   2. tcPbrMaterial.h            (PbrMaterial class)
//   3. tcLight.h                  (Light class)
//   4. tcMesh.h                   (Mesh class with drawGpuPbr() declaration)
//   5. tc/gpu/shaders/meshPbr.glsl.h (generated sokol-shdc output)
//   6. tcMeshPbrPipeline.h        (this file; defines Mesh::drawGpuPbr())
//
// v1 limitations:
//   - Swapchain target only. Drawing PBR mesh inside an Fbo pass will trigger
//     a sokol_gfx pipeline format mismatch. Phase 4 will add per-target cache.
//   - Immediate-mode sg_draw; ordering with sokol_gl is "PBR below 2D overlay".
//     See plan for the deferred-layer optimization (Phase later).
//
// =============================================================================

#include <cstring>

#include "tc/gpu/shaders/meshPbr.glsl.h"

namespace trussc {
namespace internal {

class PbrPipeline {
public:
    // Lazily create shader + pipeline on first use. Safe to call every frame.
    void ensureInit() {
        if (initialized_) return;

        shader_ = sg_make_shader(pbr_mesh_shader_desc(sg_query_backend()));

        sg_pipeline_desc pd = {};
        pd.shader = shader_;

        // Vertex layout: pos(3) + normal(3) + uv(2), interleaved, single buffer.
        pd.layout.attrs[ATTR_pbr_mesh_position].format = SG_VERTEXFORMAT_FLOAT3;
        pd.layout.attrs[ATTR_pbr_mesh_normal].format   = SG_VERTEXFORMAT_FLOAT3;
        pd.layout.attrs[ATTR_pbr_mesh_texcoord0].format = SG_VERTEXFORMAT_FLOAT2;

        // Depth test on, no culling. createSphere and friends use CW winding
        // while most PBR content assumes CCW, so disabling cull matches the
        // behaviour of the default sokol_gl 3D pipeline (tcGlobal.cpp).
        pd.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
        pd.depth.write_enabled = true;
        pd.depth.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
        pd.cull_mode = SG_CULLMODE_NONE;

        // Opaque draw. baseColor.a is preserved but not blended for v1.
        pd.colors[0].blend.enabled = false;

        pd.index_type = SG_INDEXTYPE_UINT32;
        pd.label = "tc_mesh_pbr_pipeline";

        pipeline_ = sg_make_pipeline(&pd);
        initialized_ = true;
    }

    // Draw a single Mesh with the current PBR state.
    // Assumes mesh has uploaded GPU buffers and currentPbrMaterial is set.
    void drawMesh(const Mesh& mesh) {
        ensureInit();

        // Ensure we are inside a render pass. Mirrors FullscreenShader::draw().
        ensureSwapchainPass();

        // Flush any pending sokol_gl commands so they are drawn *before* this
        // PBR mesh. This preserves "drawBox → mesh.draw() → drawBox" intent
        // where the first drawBox should appear beneath the PBR mesh.
        sgl_draw();

        sg_apply_pipeline(pipeline_);

        // --- Bindings -------------------------------------------------------
        sg_bindings bind = {};
        bind.vertex_buffers[0] = mesh.getGpuVertexBuffer();
        if (mesh.getGpuIndexCount() > 0) {
            bind.index_buffer = mesh.getGpuIndexBuffer();
        }
        sg_apply_bindings(&bind);

        // --- vs_params ------------------------------------------------------
        vs_params_t vsp = {};
        // TrussC Mat4 is row-major; GLSL mat4 is column-major. Transpose the
        // storage before upload so shader can use the conventional `model * v`.
        Mat4 modelT = getDefaultContext().getCurrentMatrix().transposed();
        Mat4 viewProj = internal::currentProjectionMatrix * internal::currentViewMatrix;
        Mat4 viewProjT = viewProj.transposed();
        // For now, normal matrix is just the model matrix. This is correct for
        // rotations and uniform scale; non-uniform scale would need
        // transpose(inverse(model3x3)). Revisit if/when mesh scale distorts lighting.
        Mat4 normalMatT = modelT;

        std::memcpy(vsp.model,     modelT.m,     sizeof(vsp.model));
        std::memcpy(vsp.viewProj,  viewProjT.m,  sizeof(vsp.viewProj));
        std::memcpy(vsp.normalMat, normalMatT.m, sizeof(vsp.normalMat));

        sg_range vsRange = { &vsp, sizeof(vsp) };
        sg_apply_uniforms(UB_vs_params, &vsRange);

        // --- fs_params ------------------------------------------------------
        fs_params_t fsp = {};
        const PbrMaterial& mat = *internal::currentPbrMaterial;
        const Color& bc = mat.getBaseColor();
        fsp.baseColor[0] = bc.r;
        fsp.baseColor[1] = bc.g;
        fsp.baseColor[2] = bc.b;
        fsp.baseColor[3] = bc.a;
        fsp.pbrParams[0] = mat.getMetallic();
        fsp.pbrParams[1] = mat.getRoughness();
        fsp.pbrParams[2] = mat.getAo();
        fsp.pbrParams[3] = mat.getEmissiveStrength();
        const Color& em = mat.getEmissive();
        fsp.emissive[0] = em.r;
        fsp.emissive[1] = em.g;
        fsp.emissive[2] = em.b;
        fsp.emissive[3] = 0.0f;

        const Vec3& cam = internal::cameraPosition;
        int numLights = static_cast<int>(internal::activeLights.size());
        if (numLights > internal::maxLights) numLights = internal::maxLights;
        fsp.cameraPos[0] = cam.x;
        fsp.cameraPos[1] = cam.y;
        fsp.cameraPos[2] = cam.z;
        fsp.cameraPos[3] = static_cast<float>(numLights);

        for (int i = 0; i < numLights; i++) {
            const Light& L = *internal::activeLights[i];
            if (L.getType() == LightType::Directional) {
                const Vec3& d = L.getDirection();
                fsp.lightPosType[i][0] = d.x;
                fsp.lightPosType[i][1] = d.y;
                fsp.lightPosType[i][2] = d.z;
                fsp.lightPosType[i][3] = 0.0f;  // 0 = directional
            } else {
                const Vec3& p = L.getPosition();
                fsp.lightPosType[i][0] = p.x;
                fsp.lightPosType[i][1] = p.y;
                fsp.lightPosType[i][2] = p.z;
                fsp.lightPosType[i][3] = 1.0f;  // 1 = point
            }
            // Light contributes its diffuse color as the PBR radiance. The
            // Phong ambient/specular split isn't meaningful here; we pick
            // diffuse as the representative color for backward compatibility
            // with existing Light presets.
            const Color& c = L.getDiffuse();
            fsp.lightColorIntensity[i][0] = c.r;
            fsp.lightColorIntensity[i][1] = c.g;
            fsp.lightColorIntensity[i][2] = c.b;
            fsp.lightColorIntensity[i][3] = L.getIntensity();
            fsp.lightAttenuation[i][0] = L.getConstantAttenuation();
            fsp.lightAttenuation[i][1] = L.getLinearAttenuation();
            fsp.lightAttenuation[i][2] = L.getQuadraticAttenuation();
            fsp.lightAttenuation[i][3] = 0.0f;
        }

        sg_range fsRange = { &fsp, sizeof(fsp) };
        sg_apply_uniforms(UB_fs_params, &fsRange);

        // --- Draw -----------------------------------------------------------
        if (mesh.getGpuIndexCount() > 0) {
            sg_draw(0, mesh.getGpuIndexCount(), 1);
        } else {
            sg_draw(0, mesh.getGpuVertexCount(), 1);
        }
    }

private:
    sg_shader shader_{};
    sg_pipeline pipeline_{};
    bool initialized_{false};
};

// Singleton accessor. The instance lives in the first TU that calls this.
inline PbrPipeline& getPbrPipeline() {
    static PbrPipeline instance;
    return instance;
}

} // namespace internal

// Out-of-class definition of Mesh::drawGpuPbr(). Must live here (after
// PbrPipeline is complete) rather than in tcMesh.h which sees only the
// forward declaration.
inline void Mesh::drawGpuPbr() const {
    uploadToGpu();
    if (vbuf_.id == 0) return;  // upload failed or mesh empty
    internal::getPbrPipeline().drawMesh(*this);
}

} // namespace trussc
