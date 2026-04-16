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

        // Vertex layout: pos(3) + normal(3) + uv(2) + tangent(4), interleaved.
        pd.layout.attrs[ATTR_pbr_mesh_position].format  = SG_VERTEXFORMAT_FLOAT3;
        pd.layout.attrs[ATTR_pbr_mesh_normal].format    = SG_VERTEXFORMAT_FLOAT3;
        pd.layout.attrs[ATTR_pbr_mesh_texcoord0].format = SG_VERTEXFORMAT_FLOAT2;
        pd.layout.attrs[ATTR_pbr_mesh_tangent].format   = SG_VERTEXFORMAT_FLOAT4;

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

        // IBL resources. When no environment is bound we still have to supply
        // valid view/sampler handles because sokol-gfx validates bindings;
        // fall back to whatever the environment-less defaults are on the GPU
        // (a 1x1 black cubemap / 2D texture held by the pipeline singleton).
        Environment* env = internal::currentEnvironment;
        bool hasIbl = (env != nullptr && env->isLoaded());
        ensureFallbacks();
        if (hasIbl) {
            bind.views[VIEW_irradianceMap] = env->getIrradianceMap().getView();
            bind.views[VIEW_prefilterMap]  = env->getPrefilterMap().getView();
            bind.views[VIEW_brdfLut]       = env->getBrdfLut().getView();
            bind.samplers[SMP_irradianceSmp] = env->getIrradianceMap().getSampler();
            bind.samplers[SMP_prefilterSmp]  = env->getPrefilterMap().getSampler();
            bind.samplers[SMP_brdfLutSmp]    = env->getBrdfLut().getSampler();
        } else {
            bind.views[VIEW_irradianceMap] = fallbackCubeView_;
            bind.views[VIEW_prefilterMap]  = fallbackCubeView_;
            bind.views[VIEW_brdfLut]       = fallback2dView_;
            bind.samplers[SMP_irradianceSmp] = fallbackSampler_;
            bind.samplers[SMP_prefilterSmp]  = fallbackSampler_;
            bind.samplers[SMP_brdfLutSmp]    = fallbackSampler_;
        }

        // PbrMaterial reference (used for both normal map binding and uniform packing)
        const PbrMaterial& pbrMat = *internal::currentPbrMaterial;

        // Find the first projector-type light and the first IES-profiled light
        int nActiveLights = static_cast<int>(internal::activeLights.size());
        if (nActiveLights > internal::maxLights) nActiveLights = internal::maxLights;
        int projectorLightIdx = -1;
        int iesLightIdx = -1;
        for (int i = 0; i < nActiveLights; ++i) {
            const Light& L = *internal::activeLights[i];
            if (projectorLightIdx < 0 &&
                L.getType() == LightType::Spot && L.hasProjectionTexture()) {
                projectorLightIdx = i;
            }
            if (iesLightIdx < 0 && L.hasIesProfile()) {
                iesLightIdx = i;
            }
        }

        // Normal map from PbrMaterial (or fallback flat normal)
        bool hasNormalMap = pbrMat.hasNormalMap();
        if (hasNormalMap) {
            bind.views[VIEW_normalMap]      = pbrMat.getNormalMap()->getView();
            bind.samplers[SMP_normalMapSmp] = pbrMat.getNormalMap()->getSampler();
        } else {
            bind.views[VIEW_normalMap]      = fallbackNormalView_;
            bind.samplers[SMP_normalMapSmp] = fallbackSampler_;
        }

        // Projector texture (or fallback)
        if (projectorLightIdx >= 0) {
            const Texture* pTex = internal::activeLights[projectorLightIdx]->getProjectionTexture();
            bind.views[VIEW_projectorTex]       = pTex->getView();
            bind.samplers[SMP_projectorTexSmp]  = pTex->getSampler();
        } else {
            bind.views[VIEW_projectorTex]       = fallbackNormalView_;  // reuse 1x1 fallback
            bind.samplers[SMP_projectorTexSmp]  = fallbackSampler_;
        }

        // IES profile texture (or fallback white = no angular modulation)
        if (iesLightIdx >= 0) {
            const IesProfile* ies = internal::activeLights[iesLightIdx]->getIesProfile();
            bind.views[VIEW_iesProfileTex]       = ies->getView();
            bind.samplers[SMP_iesProfileTexSmp]  = ies->getSampler();
        } else {
            bind.views[VIEW_iesProfileTex]       = fallbackIesView_;
            bind.samplers[SMP_iesProfileTexSmp]  = fallbackSampler_;
        }

        // PBR material texture maps (or fallback white = identity multiplication)
        auto bindMatTex = [&](int viewSlot, int smpSlot, const Texture* tex) {
            if (tex) {
                bind.views[viewSlot]    = tex->getView();
                bind.samplers[smpSlot]  = tex->getSampler();
            } else {
                bind.views[viewSlot]    = fallbackWhiteView_;
                bind.samplers[smpSlot]  = fallbackSampler_;
            }
        };
        bindMatTex(VIEW_baseColorTex,          SMP_baseColorTexSmp,          pbrMat.getBaseColorTexture());
        bindMatTex(VIEW_metallicRoughnessTex,  SMP_metallicRoughnessTexSmp,  pbrMat.getMetallicRoughnessTexture());
        bindMatTex(VIEW_emissiveTex,           SMP_emissiveTexSmp,           pbrMat.getEmissiveTexture());
        bindMatTex(VIEW_occlusionTex,          SMP_occlusionTexSmp,          pbrMat.getOcclusionTexture());

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
        const Color& bc = pbrMat.getBaseColor();
        fsp.baseColor[0] = bc.r;
        fsp.baseColor[1] = bc.g;
        fsp.baseColor[2] = bc.b;
        fsp.baseColor[3] = bc.a;
        fsp.pbrParams[0] = pbrMat.getMetallic();
        fsp.pbrParams[1] = pbrMat.getRoughness();
        fsp.pbrParams[2] = pbrMat.getAo();
        fsp.pbrParams[3] = pbrMat.getEmissiveStrength();
        const Color& em = pbrMat.getEmissive();
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

        // iblParams: x=hasIbl, y=prefilterMaxLod, z=exposure, w=hasNormalMap
        fsp.iblParams[0] = hasIbl ? 1.0f : 0.0f;
        fsp.iblParams[1] = hasIbl ? static_cast<float>(env->getPrefilterMipLevels() - 1) : 0.0f;
        fsp.iblParams[2] = internal::pbrExposure;
        fsp.iblParams[3] = hasNormalMap ? 1.0f : 0.0f;

        for (int i = 0; i < numLights; i++) {
            const Light& L = *internal::activeLights[i];
            if (L.getType() == LightType::Directional) {
                const Vec3& d = L.getDirection();
                fsp.lightPosType[i][0] = d.x;
                fsp.lightPosType[i][1] = d.y;
                fsp.lightPosType[i][2] = d.z;
                fsp.lightPosType[i][3] = 0.0f;
            } else {
                // Point and Spot both use position
                const Vec3& p = L.getPosition();
                fsp.lightPosType[i][0] = p.x;
                fsp.lightPosType[i][1] = p.y;
                fsp.lightPosType[i][2] = p.z;
                fsp.lightPosType[i][3] = (L.getType() == LightType::Spot) ? 2.0f : 1.0f;
            }

            // Light color and attenuation (common to all types)
            const Color& c = L.getDiffuse();
            fsp.lightColorIntensity[i][0] = c.r;
            fsp.lightColorIntensity[i][1] = c.g;
            fsp.lightColorIntensity[i][2] = c.b;
            fsp.lightColorIntensity[i][3] = L.getIntensity();
            fsp.lightAttenuation[i][0] = L.getConstantAttenuation();
            fsp.lightAttenuation[i][1] = L.getLinearAttenuation();
            fsp.lightAttenuation[i][2] = L.getQuadraticAttenuation();
            fsp.lightAttenuation[i][3] = 0.0f;

            // Spot direction + cone angles
            if (L.getType() == LightType::Spot) {
                const Vec3& sd = L.getDirection();
                fsp.lightSpotDir[i][0] = sd.x;
                fsp.lightSpotDir[i][1] = sd.y;
                fsp.lightSpotDir[i][2] = sd.z;
                fsp.lightSpotDir[i][3] = L.getSpotInnerCos();
                fsp.lightAttenuation[i][3] = L.getSpotOuterCos();
            }
            // For point lights with IES, pack direction into spotDir so the
            // shader can compute the vertical angle. Default direction (0,-1,0)
            // corresponds to a downlight mounting.
            else if (L.getType() == LightType::Point && L.hasIesProfile()) {
                const Vec3& sd = L.getDirection();
                fsp.lightSpotDir[i][0] = sd.x;
                fsp.lightSpotDir[i][1] = sd.y;
                fsp.lightSpotDir[i][2] = sd.z;
            }
        }

        // Projector VP matrix (single projector, first spot with texture)
        fsp.projectorParams[0] = static_cast<float>(projectorLightIdx);
        if (projectorLightIdx >= 0) {
            Mat4 pvp = internal::activeLights[projectorLightIdx]->computeProjectorViewProj();
            Mat4 pvpT = pvp.transposed();
            std::memcpy(fsp.projectorViewProj, pvpT.m, sizeof(fsp.projectorViewProj));
        }

        // IES profile params
        fsp.iesParams[0] = static_cast<float>(iesLightIdx);
        if (iesLightIdx >= 0) {
            fsp.iesParams[1] = internal::activeLights[iesLightIdx]->getIesProfile()->getMaxVerticalAngle();
            fsp.iesParams[2] = 0.0f;
            fsp.iesParams[3] = 0.0f;
        }

        // PBR texture map flags
        fsp.texFlags[0] = pbrMat.hasBaseColorTexture()          ? 1.0f : 0.0f;
        fsp.texFlags[1] = pbrMat.hasMetallicRoughnessTexture()  ? 1.0f : 0.0f;
        fsp.texFlags[2] = pbrMat.hasEmissiveTexture()           ? 1.0f : 0.0f;
        fsp.texFlags[3] = pbrMat.hasOcclusionTexture()          ? 1.0f : 0.0f;

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
    // Create dummy 1x1 cube and 2D textures used when no Environment is
    // bound. sokol-gfx requires the bound views/samplers to be valid; we
    // supply black for samples and 0 for iblParams.x so the shader branches
    // away from IBL evaluation.
    void ensureFallbacks() {
        if (fallbackInitialized_) return;

        // 1x1x6 RGBA8 cubemap, all zeros
        sg_image_desc cube_desc = {};
        cube_desc.type = SG_IMAGETYPE_CUBE;
        cube_desc.width = 1;
        cube_desc.height = 1;
        cube_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
        cube_desc.usage.dynamic_update = true;
        cube_desc.label = "tc_pbr_fallback_cube";
        fallbackCube_ = sg_make_image(&cube_desc);
        // Zero-init all 6 faces
        uint8_t zero[4 * 6] = {0};
        sg_image_data cube_data = {};
        cube_data.mip_levels[0].ptr = zero;
        cube_data.mip_levels[0].size = sizeof(zero);
        sg_update_image(fallbackCube_, &cube_data);
        sg_view_desc cube_view_desc = {};
        cube_view_desc.texture.image = fallbackCube_;
        fallbackCubeView_ = sg_make_view(&cube_view_desc);

        // 1x1 RG16F 2D texture
        sg_image_desc tex_desc = {};
        tex_desc.type = SG_IMAGETYPE_2D;
        tex_desc.width = 1;
        tex_desc.height = 1;
        tex_desc.pixel_format = SG_PIXELFORMAT_RG16F;
        tex_desc.usage.dynamic_update = true;
        tex_desc.label = "tc_pbr_fallback_2d";
        fallback2d_ = sg_make_image(&tex_desc);
        uint16_t zero2d[2] = {0, 0};
        sg_image_data tex_data = {};
        tex_data.mip_levels[0].ptr = zero2d;
        tex_data.mip_levels[0].size = sizeof(zero2d);
        sg_update_image(fallback2d_, &tex_data);
        sg_view_desc tex_view_desc = {};
        tex_view_desc.texture.image = fallback2d_;
        fallback2dView_ = sg_make_view(&tex_view_desc);

        sg_sampler_desc smp_desc = {};
        smp_desc.min_filter = SG_FILTER_LINEAR;
        smp_desc.mag_filter = SG_FILTER_LINEAR;
        smp_desc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
        smp_desc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
        smp_desc.wrap_w = SG_WRAP_CLAMP_TO_EDGE;
        smp_desc.label = "tc_pbr_fallback_smp";
        fallbackSampler_ = sg_make_sampler(&smp_desc);

        // 1x1 flat normal map: (0.5, 0.5, 1.0, 1.0) = tangent-space "no bump"
        sg_image_desc nm_desc = {};
        nm_desc.type = SG_IMAGETYPE_2D;
        nm_desc.width = 1;
        nm_desc.height = 1;
        nm_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
        uint8_t flat_normal[4] = { 128, 128, 255, 255 };
        nm_desc.data.mip_levels[0].ptr = flat_normal;
        nm_desc.data.mip_levels[0].size = sizeof(flat_normal);
        nm_desc.label = "tc_pbr_fallback_normal";
        fallbackNormal_ = sg_make_image(&nm_desc);
        sg_view_desc nm_view_desc = {};
        nm_view_desc.texture.image = fallbackNormal_;
        fallbackNormalView_ = sg_make_view(&nm_view_desc);

        // 1x1 white texture (shared by IES fallback and material texture fallbacks)
        sg_image_desc white_desc = {};
        white_desc.type = SG_IMAGETYPE_2D;
        white_desc.width = 1;
        white_desc.height = 1;
        white_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
        uint8_t white_pixel[4] = { 255, 255, 255, 255 };
        white_desc.data.mip_levels[0].ptr = white_pixel;
        white_desc.data.mip_levels[0].size = sizeof(white_pixel);
        white_desc.label = "tc_pbr_fallback_white";
        fallbackWhite_ = sg_make_image(&white_desc);
        sg_view_desc white_view_desc = {};
        white_view_desc.texture.image = fallbackWhite_;
        fallbackWhiteView_ = sg_make_view(&white_view_desc);

        // IES uses the same white fallback
        fallbackIes_ = fallbackWhite_;
        fallbackIesView_ = fallbackWhiteView_;

        fallbackInitialized_ = true;
    }

    sg_shader shader_{};
    sg_pipeline pipeline_{};
    bool initialized_{false};

    sg_image fallbackCube_{};
    sg_view fallbackCubeView_{};
    sg_image fallback2d_{};
    sg_view fallback2dView_{};
    sg_image fallbackNormal_{};
    sg_view fallbackNormalView_{};
    sg_image fallbackWhite_{};
    sg_view fallbackWhiteView_{};
    sg_image fallbackIes_{};       // alias of fallbackWhite_
    sg_view fallbackIesView_{};
    sg_sampler fallbackSampler_{};
    bool fallbackInitialized_{false};
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
