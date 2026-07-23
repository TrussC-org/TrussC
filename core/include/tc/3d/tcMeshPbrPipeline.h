#pragma once

// =============================================================================
// tcMeshPbrPipeline.h - Internal GPU pipeline for PBR mesh rendering
// =============================================================================
//
// Singleton sg_shader + sg_pipeline used by Mesh::drawGpuPbr() to evaluate
// Cook-Torrance PBR in the fragment shader. Not part of the public API.
//
// Include order (enforced by TrussC.h):
//   1. tcLightingState.h          (currentMaterial)
//   2. tcMaterial.h               (Material class)
//   3. tcLight.h                  (Light class)
//   4. tcMesh.h                   (Mesh class with drawGpuPbr() declaration)
//   5. tc/gpu/shaders/meshPbr.glsl.h (generated sokol-shdc output)
//   6. tcMeshPbrPipeline.h        (this file; defines Mesh::drawGpuPbr())
//
// Draw submission:
//   - Pipelines are cached per (color format, sample count), so both the
//     swapchain and Fbo passes are supported render targets.
//   - ALL PBR draws are deferred: swapchain draws into the per-layer flush
//     (flushDeferredShaderDraws), FBO-pass draws into fboPbrDraws (flushed at
//     Fbo::end()), so they composite with sokol_gl 2D in submission order.
//     Captured sg_buffer handles stay valid because Mesh defers buffer
//     destruction to end of frame (internal::deferGpuDestroy,
//     tcGpuDestroyQueue.h).
//
// =============================================================================

#include <cstring>
#include <map>
#include <vector>

#include "tc/gpu/shaders/meshPbr.glsl.h"
#include "tc/gpu/shaders/shadowDepth.glsl.h"

namespace trussc {
namespace internal {

// A fully-resolved PBR mesh draw, captured at submission time (pipeline +
// bindings + packed uniforms) so it can be replayed later. Used to defer
// swapchain PBR draws into the same per-layer flush as sokol_gl 2D and deferred
// shaders, so everything composites in submission order. See PbrPipeline::drawMesh.
struct PbrDrawCommand {
    sg_pipeline        pip;
    sg_bindings        bind;
    tc_pbr_vs_params_t vsp;
    tc_pbr_fs_params_t fsp;
    int                indexCount;
    int                vertexCount;
};
struct DeferredPbrDraw { int layerId; PbrDrawCommand cmd; };
inline std::vector<DeferredPbrDraw> deferredPbrDraws;

// PBR draws deferred WITHIN an FBO pass. Flushed at Fbo::end()/clearColor() via
// flushFboDeferredPbr(), which interleaves them with the FBO context's sokol_gl
// layers using sgl_context_draw_layer() — each layer drawn once (O(N)). This
// replaces the old per-mesh sgl_draw() in the FBO path, which redrew the whole
// FBO context on every mesh (O(N^2)) and overflowed the per-frame uniform buffer
// once enough meshes were in flight.
inline std::vector<DeferredPbrDraw> fboPbrDraws;
// fboLayerNext (the shared FBO-pass layer counter) is declared in
// tcMeshPointPipeline.h, which is included before this file.

class PbrPipeline {
public:
    // Lazily create shader on first use. Safe to call every frame.
    void ensureInit() {
        if (initialized_) return;
        shader_ = sg_make_shader(tc_pbr_pbr_mesh_shader_desc(sg_query_backend()));
        initialized_ = true;
    }

    // Get or create a pipeline for the given color pixel format and sample count.
    sg_pipeline getPipeline(sg_pixel_format colorFormat, int sampleCount) {
        // キャッシュキー: colorFormat(下位16bit) + sampleCount(上位16bit)
        int key = static_cast<int>(colorFormat) | (sampleCount << 16);
        auto it = pipelineCache_.find(key);
        if (it != pipelineCache_.end()) return it->second;

        sg_pipeline_desc pd = {};
        pd.shader = shader_;

        pd.layout.attrs[ATTR_tc_pbr_pbr_mesh_position].format  = SG_VERTEXFORMAT_FLOAT3;
        pd.layout.attrs[ATTR_tc_pbr_pbr_mesh_normal].format    = SG_VERTEXFORMAT_FLOAT3;
        pd.layout.attrs[ATTR_tc_pbr_pbr_mesh_texcoord0].format = SG_VERTEXFORMAT_FLOAT2;
        pd.layout.attrs[ATTR_tc_pbr_pbr_mesh_tangent].format   = SG_VERTEXFORMAT_FLOAT4;

        pd.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
        pd.depth.write_enabled = true;
        pd.depth.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
        pd.cull_mode = SG_CULLMODE_NONE;

        pd.colors[0].pixel_format = colorFormat;
        pd.colors[0].blend.enabled = false;

        pd.sample_count = sampleCount;
        pd.index_type = SG_INDEXTYPE_UINT32;
        pd.label = "tc_mesh_pbr_pipeline";

        sg_pipeline pip = sg_make_pipeline(&pd);
        pipelineCache_[key] = pip;
        return pip;
    }

    // Draw a single Mesh with the current PBR state.
    // Assumes mesh has uploaded GPU buffers and currentMaterial is set.
    void drawMesh(const Mesh& mesh) {
        ensureInit();

        // 現在のレンダーターゲットのカラーフォーマットとサンプルカウントを取得
        // _SG_PIXELFORMAT_DEFAULT (0) = sokol環境デフォルト（スワップチェーン）
        // SG_PIXELFORMAT_NONE (1) は「カラーアタッチメントなし」なので使わない
        sg_pixel_format colorFmt;
        int sampleCount;
        if (internal::currentWindowContext().inFboPass) {
            colorFmt = internal::currentFboColorFormat;
            sampleCount = internal::currentFboSampleCount;
        } else {
            colorFmt = _SG_PIXELFORMAT_DEFAULT;
            sampleCount = sapp_sample_count();
        }

        // Resolve the pipeline now; GPU submission happens at the end of this
        // function — deferred for the swapchain (so it composites with sokol_gl
        // in submission order), immediate inside an FBO pass.
        sg_pipeline pip = getPipeline(colorFmt, sampleCount);

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
            bind.views[VIEW_tc_pbr_irradianceMap] = env->getIrradianceMap().getView();
            bind.views[VIEW_tc_pbr_prefilterMap]  = env->getPrefilterMap().getView();
            bind.views[VIEW_tc_pbr_brdfLut]       = env->getBrdfLut().getView();
            bind.samplers[SMP_tc_pbr_irradianceSmp] = env->getIrradianceMap().getSampler();
            bind.samplers[SMP_tc_pbr_prefilterSmp]  = env->getPrefilterMap().getSampler();
            bind.samplers[SMP_tc_pbr_brdfLutSmp]    = env->getBrdfLut().getSampler();
        } else {
            bind.views[VIEW_tc_pbr_irradianceMap] = fallbackCubeView_;
            bind.views[VIEW_tc_pbr_prefilterMap]  = fallbackCubeView_;
            bind.views[VIEW_tc_pbr_brdfLut]       = fallback2dView_;
            bind.samplers[SMP_tc_pbr_irradianceSmp] = fallbackSampler_;
            bind.samplers[SMP_tc_pbr_prefilterSmp]  = fallbackSampler_;
            bind.samplers[SMP_tc_pbr_brdfLutSmp]    = fallbackSampler_;
        }

        // Material reference (used for both normal map binding and uniform packing)
        const Material& pbrMat = *internal::currentMaterial;

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

        // Normal map from Material (or fallback flat normal)
        bool hasNormalMap = pbrMat.hasNormalMap();
        if (hasNormalMap) {
            bind.views[VIEW_tc_pbr_normalMap]      = pbrMat.getNormalMap()->getView();
            bind.samplers[SMP_tc_pbr_normalMapSmp] = pbrMat.getNormalMap()->getSampler();
        } else {
            bind.views[VIEW_tc_pbr_normalMap]      = fallbackNormalView_;
            bind.samplers[SMP_tc_pbr_normalMapSmp] = fallbackSampler_;
        }

        // Projector texture (or fallback)
        if (projectorLightIdx >= 0) {
            const Texture* pTex = internal::activeLights[projectorLightIdx]->getProjectionTexture();
            bind.views[VIEW_tc_pbr_projectorTex]       = pTex->getView();
            bind.samplers[SMP_tc_pbr_projectorTexSmp]  = pTex->getSampler();
        } else {
            bind.views[VIEW_tc_pbr_projectorTex]       = fallbackNormalView_;  // reuse 1x1 fallback
            bind.samplers[SMP_tc_pbr_projectorTexSmp]  = fallbackSampler_;
        }

        // IES profile texture (or fallback white = no angular modulation)
        if (iesLightIdx >= 0) {
            const IesProfile* ies = internal::activeLights[iesLightIdx]->getIesProfile();
            bind.views[VIEW_tc_pbr_iesProfileTex]       = ies->getView();
            bind.samplers[SMP_tc_pbr_iesProfileTexSmp]  = ies->getSampler();
        } else {
            bind.views[VIEW_tc_pbr_iesProfileTex]       = fallbackIesView_;
            bind.samplers[SMP_tc_pbr_iesProfileTexSmp]  = fallbackSampler_;
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
        bindMatTex(VIEW_tc_pbr_baseColorTex,          SMP_tc_pbr_baseColorTexSmp,          pbrMat.getBaseColorTexture());
        bindMatTex(VIEW_tc_pbr_metallicRoughnessTex,  SMP_tc_pbr_metallicRoughnessTexSmp,  pbrMat.getMetallicRoughnessTexture());
        bindMatTex(VIEW_tc_pbr_emissiveTex,           SMP_tc_pbr_emissiveTexSmp,           pbrMat.getEmissiveTexture());
        bindMatTex(VIEW_tc_pbr_occlusionTex,          SMP_tc_pbr_occlusionTexSmp,          pbrMat.getOcclusionTexture());

        // Shadow map texture array (or fallback array = never sampled because
        // the shader sees numShadowSlots == 0)
        if (shadowSlotCount_ > 0 && shadowColorTexView_.id != 0) {
            bind.views[VIEW_tc_pbr_shadowMap]      = shadowColorTexView_;
            bind.samplers[SMP_tc_pbr_shadowMapSmp] = shadowSampler_;
        } else {
            bind.views[VIEW_tc_pbr_shadowMap]      = fallbackShadowArrayView_;
            bind.samplers[SMP_tc_pbr_shadowMapSmp] = fallbackSampler_;
        }

        // (bindings applied later, in executePbrDraw)

        // --- vs_params ------------------------------------------------------
        tc_pbr_vs_params_t vsp = {};
        // TrussC Mat4 is row-major; GLSL mat4 is column-major. Transpose the
        // storage before upload so shader can use the conventional `model * v`.
        Mat4 modelT = getDefaultContext().getMatrix().transposed();
        Mat4 viewProj = internal::currentWindowContext().currentProjectionMatrix * internal::currentWindowContext().currentViewMatrix;
        Mat4 viewProjT = viewProj.transposed();
        // For now, normal matrix is just the model matrix. This is correct for
        // rotations and uniform scale; non-uniform scale would need
        // transpose(inverse(model3x3)). Revisit if/when mesh scale distorts lighting.
        Mat4 normalMatT = modelT;

        std::memcpy(vsp.model,     modelT.m,     sizeof(vsp.model));
        std::memcpy(vsp.viewProj,  viewProjT.m,  sizeof(vsp.viewProj));
        std::memcpy(vsp.normalMat, normalMatT.m, sizeof(vsp.normalMat));

        // --- fs_params ------------------------------------------------------
        tc_pbr_fs_params_t fsp = {};
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

        // Shadow slots: per-slot light VP matrices and params, packed by VALUE
        // so deferred draws replay with the shadow state that was current at
        // submission time (issue #192). The array texture VIEW is shared, but
        // each light's layer is rendered exactly once per frame before the
        // material draws that use it, so the layer content matches the matrix.
        // Slot data refreshes every frame that runs a shadow pass. Draws
        // submitted before the first shadow pass of a frame may still use the
        // previous frame's slots (intentional, one frame old at most). If the
        // slots are older than that, the app stopped calling beginShadowPass()
        // entirely -- expose zero slots instead of ghost shadows from a stale
        // map (the per-frame slot reset only runs inside beginShadowPass()).
        int liveSlotCount = (shadowSlotFrame_ + 1 >= getFrameCount()) ? shadowSlotCount_ : 0;
        fsp.shadowMapParams[0] = static_cast<float>(shadowTexResolution_);
        fsp.shadowMapParams[1] = static_cast<float>(liveSlotCount);
        // z flags a top-left-origin backend (Metal / D3D11 / WebGPU): the
        // shader must v-flip its shadow UV to sample the offscreen map
        // correctly (GL convention otherwise).
        fsp.shadowMapParams[2] = sg_query_features().origin_top_left ? 1.0f : 0.0f;
        for (int s = 0; s < liveSlotCount; s++) {
            Mat4 svpT = shadowSlotViewProj_[s].transposed();
            std::memcpy(fsp.shadowViewProj[s], svpT.m, sizeof(fsp.shadowViewProj[s]));
            fsp.shadowSlotParams[s][0] = static_cast<float>(shadowSlotLightIndex_[s]);
            fsp.shadowSlotParams[s][1] = shadowSlotBias_[s];
            fsp.shadowSlotParams[s][2] = 1.0f;    // shadow strength
            fsp.shadowSlotParams[s][3] = shadowSlotSoftness_[s];  // emitter size (world units, 0 = hard/PCSS off)
            // x = PCSS variable-PCF tap count (clamped [1,36] at the Light API).
            // y = depth-storage mode (0=perspective/spot, 1=ortho/directional).
            // z/w reserved (zero).
            fsp.shadowSlotParams2[s][0] = static_cast<float>(shadowSlotSamples_[s]);
            fsp.shadowSlotParams2[s][1] = shadowSlotMode_[s];
            fsp.shadowSlotParams2[s][2] = 0.0f;
            fsp.shadowSlotParams2[s][3] = 0.0f;
            // xyz = light direction (normalized), w = refDot (ortho depth ref).
            fsp.shadowSlotLightDir[s][0] = shadowSlotLightDirX_[s];
            fsp.shadowSlotLightDir[s][1] = shadowSlotLightDirY_[s];
            fsp.shadowSlotLightDir[s][2] = shadowSlotLightDirZ_[s];
            fsp.shadowSlotLightDir[s][3] = shadowSlotRefDot_[s];
        }

        // --- Submit ---------------------------------------------------------
        // Package the fully-resolved draw. Inside an FBO pass we submit it
        // immediately (that pass is self-contained). For the swapchain we DEFER
        // it: append to deferredPbrDraws and bump the sokol_gl layer so any 2D
        // drawn after this mesh composites on top of it (same trick the deferred
        // shaders use). flushDeferredShaderDraws() replays it per layer.
        PbrDrawCommand cmd{ pip, bind, vsp, fsp,
                            mesh.getGpuIndexCount(), mesh.getGpuVertexCount() };
        if (internal::currentWindowContext().inFboPass) {
            // Defer (like the swapchain path) into the per-FBO list; flushed
            // per-layer in Fbo::end(). Bump the FBO layer so 2D drawn after this
            // mesh composites on top of it, matching the swapchain ordering.
            internal::fboPbrDraws.push_back({ internal::fboLayerNext, cmd });
            internal::fboLayerNext++;
            sgl_layer(internal::fboLayerNext);
        } else {
            internal::deferredPbrDraws.push_back({ internal::sglLayerNext, cmd });
            internal::sglLayerNext++;
            sgl_layer(internal::sglLayerNext);
        }
    }

    // Submit a packaged PBR draw to the GPU. Used immediately for FBO passes and
    // replayed from flushDeferredShaderDraws() for the swapchain.
    void executePbrDraw(const PbrDrawCommand& c) {
        sg_apply_pipeline(c.pip);
        sg_apply_bindings(&c.bind);
        sg_range vr{ &c.vsp, sizeof(c.vsp) };
        sg_apply_uniforms(UB_tc_pbr_vs_params, &vr);
        sg_range fr{ &c.fsp, sizeof(c.fsp) };
        sg_apply_uniforms(UB_tc_pbr_fs_params, &fr);
        sg_draw(0, c.indexCount > 0 ? c.indexCount : c.vertexCount, 1);
    }

private:
    // Create dummy 1x1 cube and 2D textures used when no Environment is
    // bound. sokol-gfx requires the bound views/samplers to be valid; we
    // supply black for samples and 0 for iblParams.x so the shader branches
    // away from IBL evaluation.
    void ensureFallbacks() {
        if (fallbackInitialized_) return;

        // 1x1x6 RGBA8 cubemap, all zeros
        // D3D11はdynamic cubemapを作れない（ArraySize=1制限）のでimmutableで作成
        uint8_t zero[4 * 6] = {0};
        sg_image_desc cube_desc = {};
        cube_desc.type = SG_IMAGETYPE_CUBE;
        cube_desc.width = 1;
        cube_desc.height = 1;
        cube_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
        cube_desc.data.mip_levels[0].ptr = zero;
        cube_desc.data.mip_levels[0].size = sizeof(zero);
        cube_desc.label = "tc_pbr_fallback_cube";
        fallbackCube_ = sg_make_image(&cube_desc);
        sg_view_desc cube_view_desc = {};
        cube_view_desc.texture.image = fallbackCube_;
        fallbackCubeView_ = sg_make_view(&cube_view_desc);

        // 1x1 RG16F 2D texture
        uint16_t zero2d[2] = {0, 0};
        sg_image_desc tex_desc = {};
        tex_desc.type = SG_IMAGETYPE_2D;
        tex_desc.width = 1;
        tex_desc.height = 1;
        tex_desc.pixel_format = SG_PIXELFORMAT_RG16F;
        tex_desc.data.mip_levels[0].ptr = zero2d;
        tex_desc.data.mip_levels[0].size = sizeof(zero2d);
        tex_desc.label = "tc_pbr_fallback_2d";
        fallback2d_ = sg_make_image(&tex_desc);
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

        // 1x1x1 white ARRAY texture for the shadow map binding when no shadow
        // pass has run (the shader never samples it — numShadowSlots is 0 —
        // but sokol-gfx validates that the bound image type matches the
        // shader's texture2DArray).
        sg_image_desc sa_desc = {};
        sa_desc.type = SG_IMAGETYPE_ARRAY;
        sa_desc.num_slices = 1;
        sa_desc.width = 1;
        sa_desc.height = 1;
        sa_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
        sa_desc.data.mip_levels[0].ptr = white_pixel;
        sa_desc.data.mip_levels[0].size = sizeof(white_pixel);
        sa_desc.label = "tc_pbr_fallback_shadow_array";
        fallbackShadowArray_ = sg_make_image(&sa_desc);
        sg_view_desc sa_view_desc = {};
        sa_view_desc.texture.image = fallbackShadowArray_;
        fallbackShadowArrayView_ = sg_make_view(&sa_view_desc);

        fallbackInitialized_ = true;
    }

    // -------------------------------------------------------------------------
    // Shadow pass methods
    // -------------------------------------------------------------------------
public:
    void beginShadowPass(int lightIndex) {
        ensureShadowInit();
        const Light& light = *internal::activeLights[lightIndex];

        // Point lights are not supported yet: they need a cube/omni shadow map,
        // which this single-2D-layer pipeline can't express. Warn once and no-op
        // cleanly — do NOT claim a slot or begin a pass. inShadowPass_ stays false
        // so shadowDraw()/endShadowPass() no-op, and no stale slot is exposed.
        if (light.getType() == LightType::Point) {
            if (!shadowPointWarned_) {
                logWarning("TrussC") << "beginShadowPass: point light shadows not "
                    << "supported yet; this light casts no shadow";
                shadowPointWarned_ = true;
            }
            return;
        }

        // Per-frame slot assignment: the slot counter resets on the first
        // beginShadowPass() of each frame (keyed on getFrameCount()), and each
        // pass claims the next array layer in call order. Slot data persists
        // across frames so draws submitted before the first shadow pass of a
        // frame keep the previous frame's shadow state (same semantics as the
        // old single-map design).
        uint64_t frame = getFrameCount();
        if (frame != shadowSlotFrame_) {
            shadowSlotFrame_ = frame;
            shadowSlotCount_ = 0;
        }
        if (shadowSlotCount_ >= internal::maxShadowLights) {
            if (!shadowOverflowWarned_) {
                logWarning("TrussC") << "beginShadowPass: more than "
                    << internal::maxShadowLights << " shadow passes in one "
                    << "frame; lights beyond the limit cast no shadow";
                shadowOverflowWarned_ = true;
            }
            // inShadowPass_ stays false -> shadowDraw()/endShadowPass() no-op
            return;
        }
        ensureShadowTexture(light.getShadowResolution());

        // Suspend swapchain pass if active. Remember whether one was active so
        // endShadowPass() only resumes when there is something to resume
        // (mirrors Fbo::wasInSwapchainPass_ — issue #191: it used to start a
        // swapchain pass even when none was active before).
        shadowWasInSwapchainPass_ = internal::currentWindowContext().inSwapchainPass;
        if (shadowWasInSwapchainPass_) {
            sgl_draw();
            sg_end_pass();
            internal::currentWindowContext().inSwapchainPass = false;
        }

        // Claim the next shadow slot for this light. computeShadowViewProj()
        // returns the perspective projector VP for Spot lights and an ortho box
        // VP for Directional lights.
        int slot = shadowSlotCount_++;
        currentShadowSlot_ = slot;
        shadowSlotViewProj_[slot]   = light.computeShadowViewProj();
        shadowSlotLightIndex_[slot] = lightIndex;
        shadowSlotBias_[slot]       = light.getShadowBias();
        shadowSlotSoftness_[slot]   = light.getShadowSoftness();
        shadowSlotSamples_[slot]    = light.getShadowSamples();

        // Depth-storage mode + ortho depth reference. For Directional lights the
        // shadow depth is stored as dot(worldPos, lightDir) - refDot (linear world
        // units); refDot = dot(eye, lightDir) with eye = center - dir*radius, i.e.
        // dot(center, dir) - radius. Spot keeps clip.w (mode 0), refDot unused.
        if (light.getType() == LightType::Directional) {
            Vec3 dir = light.getDirection().normalized();
            const Vec3& center = light.getShadowAreaCenter();
            float radius = light.getShadowAreaRadius();
            shadowSlotMode_[slot]     = 1.0f;
            shadowSlotLightDirX_[slot] = dir.x;
            shadowSlotLightDirY_[slot] = dir.y;
            shadowSlotLightDirZ_[slot] = dir.z;
            shadowSlotRefDot_[slot]   = center.dot(dir) - radius;
        } else {
            shadowSlotMode_[slot]     = 0.0f;
            shadowSlotLightDirX_[slot] = 0.0f;
            shadowSlotLightDirY_[slot] = 0.0f;
            shadowSlotLightDirZ_[slot] = 0.0f;
            shadowSlotRefDot_[slot]   = 0.0f;
        }

        // Begin shadow depth pass into this slot's array layer
        sg_pass pass = {};
        pass.attachments.colors[0] = shadowColorAttView_[slot];
        pass.attachments.depth_stencil = shadowDepthAttView_;
        pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
        pass.action.colors[0].clear_value = { 10000.0f, 0.0f, 0.0f, 1.0f };  // R32F: far distance
        pass.action.depth.load_action = SG_LOADACTION_CLEAR;
        pass.action.depth.clear_value = 1.0f;
        pass.label = "tc_shadow_pass";
        sg_begin_pass(&pass);

        // Render at the shared array resolution (max of all requested sizes)
        sg_apply_viewport(0, 0, shadowTexResolution_, shadowTexResolution_, true);
        sg_apply_pipeline(shadowPipeline_);

        inShadowPass_ = true;
    }

    void shadowDrawMesh(const Mesh& mesh) {
        if (!inShadowPass_) return;
        mesh.uploadToGpu();
        if (mesh.getGpuVertexBuffer().id == 0) return;

        sg_bindings bind = {};
        bind.vertex_buffers[0] = mesh.getGpuVertexBuffer();
        if (mesh.getGpuIndexCount() > 0) {
            bind.index_buffer = mesh.getGpuIndexBuffer();
        }
        sg_apply_bindings(&bind);

        // Shadow VS uniforms: model + lightViewProj + depth-storage params.
        tc_shadow_shadow_vs_params_t svp = {};
        Mat4 modelT = getDefaultContext().getMatrix().transposed();
        Mat4 lightVPT = shadowSlotViewProj_[currentShadowSlot_].transposed();
        std::memcpy(svp.model, modelT.m, sizeof(svp.model));
        std::memcpy(svp.lightViewProj, lightVPT.m, sizeof(svp.lightViewProj));
        // depthParams: xyz = light direction, w = mode (0=persp, 1=ortho)
        int cs = currentShadowSlot_;
        svp.depthParams[0] = shadowSlotLightDirX_[cs];
        svp.depthParams[1] = shadowSlotLightDirY_[cs];
        svp.depthParams[2] = shadowSlotLightDirZ_[cs];
        svp.depthParams[3] = shadowSlotMode_[cs];
        // depthParams2.x = refDot (ortho depth reference)
        svp.depthParams2[0] = shadowSlotRefDot_[cs];
        svp.depthParams2[1] = 0.0f;
        svp.depthParams2[2] = 0.0f;
        svp.depthParams2[3] = 0.0f;
        sg_range r = { &svp, sizeof(svp) };
        sg_apply_uniforms(UB_tc_shadow_shadow_vs_params, &r);

        int count = mesh.getGpuIndexCount() > 0 ? mesh.getGpuIndexCount() : mesh.getGpuVertexCount();
        sg_draw(0, count, 1);
        shadowDrawCount_++;
    }

    void endShadowPass() {
        if (!inShadowPass_) return;
        sg_end_pass();
        inShadowPass_ = false;
        shadowDrawCount_ = 0;

        // Resume swapchain pass only if one was active before beginShadowPass()
        if (shadowWasInSwapchainPass_) {
            resumeSwapchainPass();
            shadowWasInSwapchainPass_ = false;
        }
    }

private:
    void ensureShadowInit() {
        if (shadowInitialized_) return;

        shadowShader_ = sg_make_shader(tc_shadow_shadow_depth_shader_desc(sg_query_backend()));

        sg_pipeline_desc pd = {};
        pd.shader = shadowShader_;

        // Match PBR vertex layout stride (48 bytes) — shadow VS only reads position
        pd.layout.attrs[ATTR_tc_shadow_shadow_depth_position].format = SG_VERTEXFORMAT_FLOAT3;
        pd.layout.buffers[0].stride = 48;

        pd.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
        pd.depth.write_enabled = true;
        pd.depth.pixel_format = SG_PIXELFORMAT_DEPTH;
        pd.depth.bias = 2;
        pd.depth.bias_slope_scale = 2.0f;
        pd.cull_mode = SG_CULLMODE_NONE;  // no cull: thin planes need front face in shadow map

        pd.colors[0].pixel_format = SG_PIXELFORMAT_R32F;
        pd.index_type = SG_INDEXTYPE_UINT32;
        // Shadow map is single-sampled; pin the pipeline to match. Leaving this
        // at 0 makes sokol inherit the swapchain MSAA count (e.g. 4), which GL/
        // Metal tolerate but WebGPU rejects ("attachment state not compatible"
        // → shadow pass fails → black screen).
        pd.sample_count = 1;
        pd.label = "tc_shadow_depth_pipeline";

        shadowPipeline_ = sg_make_pipeline(&pd);
        shadowInitialized_ = true;
    }

    // The shadow map array is shared by all shadow slots at ONE resolution:
    // the largest resolution requested by any shadow-enabled light so far.
    // It is only recreated when it needs to GROW; lights requesting a smaller
    // resolution render into (and sample from) the shared larger map.
    void ensureShadowTexture(int resolution) {
        if (resolution <= shadowTexResolution_) return;

        // Release old resources (deferred: a deferred PBR draw recorded this
        // frame may still bind the old shadow map view/sampler)
        internal::deferGpuDestroy(shadowColorImage_);
        for (int s = 0; s < internal::maxShadowLights; s++) {
            internal::deferGpuDestroy(shadowColorAttView_[s]);
        }
        internal::deferGpuDestroy(shadowColorTexView_);
        internal::deferGpuDestroy(shadowDepthImage_);
        internal::deferGpuDestroy(shadowDepthAttView_);
        internal::deferGpuDestroy(shadowSampler_);

        // R32F color target array (stores depth value for sampling), one
        // layer per shadow slot
        sg_image_desc cd = {};
        cd.type = SG_IMAGETYPE_ARRAY;
        cd.num_slices = internal::maxShadowLights;
        cd.usage.color_attachment = true;
        cd.width = resolution;
        cd.height = resolution;
        cd.pixel_format = SG_PIXELFORMAT_R32F;
        cd.sample_count = 1;
        cd.label = "tc_shadow_color_array";
        shadowColorImage_ = sg_make_image(&cd);

        // Per-layer attachment views for rendering...
        for (int s = 0; s < internal::maxShadowLights; s++) {
            sg_view_desc cav = {};
            cav.color_attachment.image = shadowColorImage_;
            cav.color_attachment.slice = s;
            shadowColorAttView_[s] = sg_make_view(&cav);
        }

        // ...and one full-array texture view for sampling
        sg_view_desc ctv = {};
        ctv.texture.image = shadowColorImage_;
        shadowColorTexView_ = sg_make_view(&ctv);

        // Depth buffer (for correct depth testing during shadow pass; a
        // single 2D image reused by every slot's pass)
        sg_image_desc dd = {};
        dd.usage.depth_stencil_attachment = true;
        dd.width = resolution;
        dd.height = resolution;
        dd.pixel_format = SG_PIXELFORMAT_DEPTH;
        dd.sample_count = 1;
        dd.label = "tc_shadow_depth";
        shadowDepthImage_ = sg_make_image(&dd);

        sg_view_desc dav = {};
        dav.depth_stencil_attachment.image = shadowDepthImage_;
        shadowDepthAttView_ = sg_make_view(&dav);

        // Standard sampler (nearest, manual comparison in shader)
        sg_sampler_desc sd = {};
        sd.min_filter = SG_FILTER_NEAREST;
        sd.mag_filter = SG_FILTER_NEAREST;
        sd.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
        sd.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
        sd.label = "tc_shadow_smp";
        shadowSampler_ = sg_make_sampler(&sd);

        shadowTexResolution_ = resolution;
    }

    // --- PBR pipeline state ---
    sg_shader shader_{};
    std::map<int, sg_pipeline> pipelineCache_;  // keyed by sg_pixel_format
    bool initialized_{false};

    // --- Shadow pipeline state ---
    sg_shader shadowShader_{};
    sg_pipeline shadowPipeline_{};
    bool shadowInitialized_{false};

    sg_image shadowColorImage_{};   // SG_IMAGETYPE_ARRAY, maxShadowLights layers
    sg_view shadowColorAttView_[internal::maxShadowLights]{};  // per-layer render views
    sg_view shadowColorTexView_{};  // full-array sampling view
    sg_image shadowDepthImage_{};   // shared 2D depth, reused per slot pass
    sg_view shadowDepthAttView_{};
    sg_sampler shadowSampler_{};
    int shadowTexResolution_{0};

    bool inShadowPass_{false};
    bool shadowWasInSwapchainPass_{false};  // swapchain pass active before beginShadowPass()
    int shadowDrawCount_{0};

    // Per-frame shadow slot state (lightIndex -> array layer, in
    // beginShadowPass() call order; counter resets each frame)
    uint64_t shadowSlotFrame_{static_cast<uint64_t>(-1)};
    int shadowSlotCount_{0};
    int currentShadowSlot_{-1};
    Mat4 shadowSlotViewProj_[internal::maxShadowLights]{};
    int shadowSlotLightIndex_[internal::maxShadowLights]{};
    float shadowSlotBias_[internal::maxShadowLights]{};
    float shadowSlotSoftness_[internal::maxShadowLights]{};
    int shadowSlotSamples_[internal::maxShadowLights]{};
    // Directional-ortho depth storage: per-slot mode (0=persp, 1=ortho), light
    // direction, and ortho depth reference (refDot = dot(eye, lightDir)).
    float shadowSlotMode_[internal::maxShadowLights]{};
    float shadowSlotLightDirX_[internal::maxShadowLights]{};
    float shadowSlotLightDirY_[internal::maxShadowLights]{};
    float shadowSlotLightDirZ_[internal::maxShadowLights]{};
    float shadowSlotRefDot_[internal::maxShadowLights]{};
    bool shadowOverflowWarned_{false};
    bool shadowPointWarned_{false};

    // --- Fallback resources ---
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
    sg_image fallbackShadowArray_{};
    sg_view fallbackShadowArrayView_{};
    sg_sampler fallbackSampler_{};
    bool fallbackInitialized_{false};
};

// Singleton accessor. The instance lives in the first TU that calls this.
inline PbrPipeline& getPbrPipeline() {
    static PbrPipeline instance;
    return instance;
}

// Flush the PBR draws deferred during an FBO pass, interleaved per-layer with the
// FBO context's sokol_gl 2D content (mirror of flushDeferredShaderDraws but for a
// single FBO context). Called by Fbo::end()/clearColor() while the FBO pass is
// still active. Each sgl layer is drawn exactly once (O(N)).
inline void flushFboDeferredPbr(sgl_context ctx) {
    for (int layer = 0; layer <= fboLayerNext; layer++) {
        sgl_context_draw_layer(ctx, layer);
        // Deferred shader draws share this FBO's layer space; replay them
        // first within the layer, matching the swapchain flush order
        // (sokol_gl -> shader -> PBR -> points) in flushDeferredShaderDraws.
        for (auto& d : fboShaderDraws) {
            if (d.layerId == layer) executeDeferredShaderDraw(d);
        }
        for (auto& d : fboPbrDraws) {
            if (d.layerId == layer) getPbrPipeline().executePbrDraw(d.cmd);
        }
        // Point splats share this FBO's layer space (fboLayerNext); replay them
        // in the same walk so they composite in submission order with PBR + 2D.
        for (auto& d : fboPointDraws) {
            if (d.layerId == layer) executePointDraw(d.cmd);
        }
    }
    fboShaderDraws.clear();
    fboPbrDraws.clear();
    fboPointDraws.clear();
    fboLayerNext = 0;
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
