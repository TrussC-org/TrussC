#pragma once

// =============================================================================
// tcEnvironment.h - Image-based lighting environment
// =============================================================================
//
// An Environment holds the three pre-computed textures that feed the meshPbr
// shader's indirect ("IBL") term:
//
//   - irradianceMap : cubemap, diffuse (cosine-weighted) hemisphere integral
//   - prefilterMap  : cubemap with mip chain, specular GGX-prefiltered
//   - brdfLut       : 2D texture, split-sum (scale, bias) LUT
//
// Baking is driven by four shader programs in iblBake.glsl:
//
//   1. equirect_to_cube  projects an input HDR equirectangular image into a
//      temporary environment cubemap.
//   2. irradiance        convolves that cubemap into irradianceMap.
//   3. prefilter         convolves it into prefilterMap (one roughness per mip).
//   4. brdf_lut          integrates the split-sum term into brdfLut.
//
// All passes use raw sokol_gfx (not sokol_gl) because they target cube faces
// at specific mip levels and do not participate in the 2D drawing flow. The
// passes run synchronously inside loadFromHDR().
//
// Usage:
//
//     Environment env;
//     env.loadFromHDR("assets/studio.hdr");
//     setEnvironment(env);  // exposes it to PbrPipeline
//
// =============================================================================

#include <cstring>
#include <string>

#include "tc/gpu/shaders/iblBake.glsl.h"

namespace trussc {

class Environment {
public:
    Environment() = default;

    ~Environment() {
        release();
    }

    Environment(const Environment&) = delete;
    Environment& operator=(const Environment&) = delete;

    // Load an equirectangular HDR image from disk and bake all IBL maps.
    // Returns false if the file cannot be loaded.
    bool loadFromHDR(const std::string& path) {
        Pixels src;
        if (!src.loadHDR(path)) {
            logError("Environment") << "loadHDR failed: " << path;
            return false;
        }
        return loadFromHDR(src);
    }

    // Bake IBL maps from an already-loaded HDR Pixels buffer (F32 RGBA).
    bool loadFromHDR(const Pixels& src) {
        if (!src.isFloat()) {
            logError("Environment") << "loadFromHDR expects F32 pixels";
            return false;
        }

        // 1. Upload equirect HDR as a 2D texture.
        Texture equirect;
        equirect.allocate(src, TextureUsage::Immutable);

        // 2. Allocate intermediate env cubemap and bake all three outputs.
        return bakeFromEquirectTexture(equirect);
    }

    // Generate a simple procedural HDR environment (blue sky + sun + ground)
    // and bake IBL maps from it. Useful for examples that don't ship HDR
    // assets. The sun direction and intensity are intentionally hardcoded to
    // produce a recognisable reflection on metallic spheres.
    bool loadProcedural() {
        const int W = 512;
        const int H = 256;
        Pixels src;
        src.allocate(W, H, 4, PixelFormat::F32);
        float* data = static_cast<float*>(src.getDataVoid());

        // Sun direction pointing FROM surface TO sun (normalized)
        Vec3 sun = Vec3(0.3f, 0.7f, 0.5f).normalized();

        auto mix = [](const Color& a, const Color& b, float t) -> Color {
            return Color(a.r + (b.r - a.r) * t,
                         a.g + (b.g - a.g) * t,
                         a.b + (b.b - a.b) * t,
                         1.0f);
        };

        const Color skyTop    (0.25f, 0.50f, 1.30f);
        const Color skyHoriz  (1.10f, 1.30f, 1.55f);
        const Color ground    (0.12f, 0.10f, 0.08f);
        const Color sunColor  (18.0f, 15.0f, 11.0f);

        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                float u = (x + 0.5f) / float(W);
                float v = (y + 0.5f) / float(H);
                // Equirect uv → direction (y up)
                float phi = (u - 0.5f) * TAU;            // [-pi, pi]
                float theta = (0.5f - v) * (TAU * 0.5f); // [-pi/2, pi/2]
                float cosT = std::cos(theta);
                Vec3 dir(cosT * std::cos(phi),
                         std::sin(theta),
                         cosT * std::sin(phi));

                Color base;
                if (dir.y >= 0.0f) {
                    float t = std::pow(dir.y, 0.5f);  // bias toward horizon
                    base = mix(skyHoriz, skyTop, t);
                } else {
                    float t = std::pow(-dir.y, 0.5f);
                    base = mix(skyHoriz, ground, t);
                }

                // Sun hot spot: tight cosine lobe, additive in HDR
                float sunDot = std::max(0.0f, dir.x * sun.x + dir.y * sun.y + dir.z * sun.z);
                float sunInt = std::pow(sunDot, 512.0f);
                Color rgb(
                    base.r + sunColor.r * sunInt,
                    base.g + sunColor.g * sunInt,
                    base.b + sunColor.b * sunInt);

                int idx = (y * W + x) * 4;
                data[idx + 0] = rgb.r;
                data[idx + 1] = rgb.g;
                data[idx + 2] = rgb.b;
                data[idx + 3] = 1.0f;
            }
        }

        return loadFromHDR(src);
    }

    void release() {
        irradiance_.clear();
        prefilter_.clear();
        brdfLut_.clear();
        loaded_ = false;
    }

    bool isLoaded() const { return loaded_; }

    // --- Accessors used by PbrPipeline ---------------------------------------
    const Texture& getIrradianceMap() const { return irradiance_; }
    const Texture& getPrefilterMap() const  { return prefilter_; }
    const Texture& getBrdfLut() const       { return brdfLut_; }

    int getPrefilterMipLevels() const { return kPrefilterMipLevels; }

private:
    // Output resolutions. Small values keep the bake under ~1s even on modest
    // hardware; raise if you want higher-frequency reflections.
    static constexpr int kEnvCubeSize       = 256;
    static constexpr int kIrradianceSize    = 32;
    static constexpr int kPrefilterSize     = 128;
    static constexpr int kPrefilterMipLevels = 5;
    static constexpr int kBrdfLutSize       = 256;

    Texture irradiance_;
    Texture prefilter_;
    Texture brdfLut_;
    bool loaded_ = false;

    // -------------------------------------------------------------------------
    // Shared baking resources (pipelines, quad buffer). Lazy-initialized on
    // the first bake call.
    // -------------------------------------------------------------------------
    struct BakeResources {
        sg_shader eqShader{};
        sg_shader irrShader{};
        sg_shader preShader{};
        sg_shader lutShader{};
        sg_pipeline eqPipe{};      // target: RGBA16F
        sg_pipeline irrPipe{};     // target: RGBA16F
        sg_pipeline prePipe{};     // target: RGBA16F
        sg_pipeline lutPipe{};     // target: RG16F
        sg_buffer quadVbuf{};      // 6 verts, 2 triangles
        sg_sampler linearSampler{};
        bool initialized = false;
    };

    static BakeResources& bake() {
        static BakeResources r;
        return r;
    }

    static void ensureBakeResources() {
        BakeResources& r = bake();
        if (r.initialized) return;

        r.eqShader  = sg_make_shader(tc_ibl_equirect_to_cube_shader_desc(sg_query_backend()));
        r.irrShader = sg_make_shader(tc_ibl_irradiance_shader_desc(sg_query_backend()));
        r.preShader = sg_make_shader(tc_ibl_prefilter_shader_desc(sg_query_backend()));
        r.lutShader = sg_make_shader(tc_ibl_brdf_lut_shader_desc(sg_query_backend()));

        auto makePipe = [](sg_shader sh, sg_pixel_format colorFmt) {
            sg_pipeline_desc pd = {};
            pd.shader = sh;
            pd.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;
            pd.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
            pd.cull_mode = SG_CULLMODE_NONE;
            pd.depth.pixel_format = SG_PIXELFORMAT_NONE;  // no depth attachment
            pd.colors[0].pixel_format = colorFmt;
            pd.colors[0].blend.enabled = false;
            pd.label = "tc_ibl_bake_pipeline";
            return sg_make_pipeline(&pd);
        };
        r.eqPipe  = makePipe(r.eqShader,  SG_PIXELFORMAT_RGBA16F);
        r.irrPipe = makePipe(r.irrShader, SG_PIXELFORMAT_RGBA16F);
        r.prePipe = makePipe(r.preShader, SG_PIXELFORMAT_RGBA16F);
        r.lutPipe = makePipe(r.lutShader, SG_PIXELFORMAT_RG16F);

        // Fullscreen quad in NDC, as two triangles.
        const float verts[] = {
            -1.0f, -1.0f,
             1.0f, -1.0f,
             1.0f,  1.0f,
            -1.0f, -1.0f,
             1.0f,  1.0f,
            -1.0f,  1.0f,
        };
        sg_buffer_desc bd = {};
        bd.data.ptr = verts;
        bd.data.size = sizeof(verts);
        bd.label = "tc_ibl_bake_quad";
        r.quadVbuf = sg_make_buffer(&bd);

        // Linear sampler with ClampToEdge (avoids seams at cube face borders).
        sg_sampler_desc sd = {};
        sd.min_filter = SG_FILTER_LINEAR;
        sd.mag_filter = SG_FILTER_LINEAR;
        sd.mipmap_filter = SG_FILTER_LINEAR;
        sd.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
        sd.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
        sd.wrap_w = SG_WRAP_CLAMP_TO_EDGE;
        sd.label = "tc_ibl_linear_sampler";
        r.linearSampler = sg_make_sampler(&sd);

        r.initialized = true;
    }

    // Run a fullscreen draw into the given attachment view. The caller
    // populates `bind` (texture/sampler slots) and uploads uniforms beforehand.
    static void runFullscreenPass(sg_view colorAttachment,
                                  int viewportW, int viewportH,
                                  sg_pipeline pipe,
                                  const sg_bindings& bindings,
                                  int uniformSlot,
                                  const void* uniformData, size_t uniformSize) {
        sg_pass pass = {};
        pass.attachments.colors[0] = colorAttachment;
        pass.action.colors[0].load_action = SG_LOADACTION_DONTCARE;
        sg_begin_pass(&pass);
        sg_apply_viewport(0, 0, viewportW, viewportH, true);
        sg_apply_pipeline(pipe);
        sg_apply_bindings(&bindings);
        if (uniformSize > 0) {
            sg_range r = { uniformData, uniformSize };
            sg_apply_uniforms(uniformSlot, &r);
        }
        sg_draw(0, 6, 1);
        sg_end_pass();
    }

    // -------------------------------------------------------------------------
    // Actual bake: equirect → cube → irradiance / prefilter / brdfLut
    // -------------------------------------------------------------------------
    bool bakeFromEquirectTexture(const Texture& equirect) {
        ensureBakeResources();
        BakeResources& r = bake();

        // IBL bakes run outside any user-facing pass. If a swapchain pass is
        // somehow active, suspend it so we can start fresh offscreen passes.
        bool wasInSwapchain = isInSwapchainPass();
        if (wasInSwapchain) {
            suspendSwapchainPass();
        }

        // Intermediate environment cubemap (RGBA16F, 1 mip).
        Texture envCube;
        envCube.allocateCubemap(kEnvCubeSize, TextureFormat::RGBA16F,
                                TextureUsage::RenderTarget, 1);

        // 1. equirect → envCube (6 faces)
        for (int face = 0; face < 6; ++face) {
            tc_ibl_equirect_params_t params = {};
            params.faceIdx[0] = static_cast<float>(face);

            sg_bindings bind = {};
            bind.vertex_buffers[0] = r.quadVbuf;
            bind.views[VIEW_tc_ibl_equirectTex] = equirect.getView();
            bind.samplers[SMP_tc_ibl_equirectSmp] = equirect.getSampler();

            runFullscreenPass(envCube.getCubemapFaceAttachmentView(face, 0),
                              kEnvCubeSize, kEnvCubeSize,
                              r.eqPipe, bind,
                              UB_tc_ibl_equirect_params, &params, sizeof(params));
        }

        // 2. envCube → irradianceMap (6 faces, 1 mip)
        irradiance_.allocateCubemap(kIrradianceSize, TextureFormat::RGBA16F,
                                    TextureUsage::RenderTarget, 1);
        for (int face = 0; face < 6; ++face) {
            tc_ibl_irradiance_params_t params = {};
            params.faceIdx[0] = static_cast<float>(face);

            sg_bindings bind = {};
            bind.vertex_buffers[0] = r.quadVbuf;
            bind.views[VIEW_tc_ibl_envTex] = envCube.getView();
            bind.samplers[SMP_tc_ibl_envSmp] = r.linearSampler;

            runFullscreenPass(irradiance_.getCubemapFaceAttachmentView(face, 0),
                              kIrradianceSize, kIrradianceSize,
                              r.irrPipe, bind,
                              UB_tc_ibl_irradiance_params, &params, sizeof(params));
        }

        // 3. envCube → prefilterMap (6 faces × kPrefilterMipLevels mips)
        prefilter_.allocateCubemap(kPrefilterSize, TextureFormat::RGBA16F,
                                   TextureUsage::RenderTarget, kPrefilterMipLevels);
        for (int mip = 0; mip < kPrefilterMipLevels; ++mip) {
            int mipSize = kPrefilterSize >> mip;
            if (mipSize < 1) mipSize = 1;
            float roughness = static_cast<float>(mip) / static_cast<float>(kPrefilterMipLevels - 1);

            for (int face = 0; face < 6; ++face) {
                tc_ibl_prefilter_params_t params = {};
                params.faceIdx[0] = static_cast<float>(face);
                params.params[0] = roughness;

                sg_bindings bind = {};
                bind.vertex_buffers[0] = r.quadVbuf;
                bind.views[VIEW_tc_ibl_envTex] = envCube.getView();
                bind.samplers[SMP_tc_ibl_envSmp] = r.linearSampler;

                runFullscreenPass(prefilter_.getCubemapFaceAttachmentView(face, mip),
                                  mipSize, mipSize,
                                  r.prePipe, bind,
                                  UB_tc_ibl_prefilter_params, &params, sizeof(params));
            }
        }

        // 4. BRDF LUT (2D, RG16F, no inputs)
        brdfLut_.allocate(kBrdfLutSize, kBrdfLutSize, TextureFormat::RG16F,
                          TextureUsage::RenderTarget, 1);
        {
            sg_bindings bind = {};
            bind.vertex_buffers[0] = r.quadVbuf;
            runFullscreenPass(brdfLut_.getAttachmentView(),
                              kBrdfLutSize, kBrdfLutSize,
                              r.lutPipe, bind,
                              0, nullptr, 0);
        }

        // envCube is a temporary; its destructor releases it.

        if (wasInSwapchain) {
            resumeSwapchainPass();
        }

        loaded_ = true;
        return true;
    }
};

// Forward-declared pointer to the active environment. Set via
// setEnvironment() and consumed by PbrPipeline::drawMesh().
namespace internal {
    inline Environment* currentEnvironment = nullptr;
}

inline void setEnvironment(Environment& env) {
    internal::currentEnvironment = &env;
}

inline void clearEnvironment() {
    internal::currentEnvironment = nullptr;
}

inline Environment* getEnvironment() {
    return internal::currentEnvironment;
}

} // namespace trussc
