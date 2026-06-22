#pragma once

// =============================================================================
// tcFbo.h - Framebuffer Object (off-screen rendering)
// =============================================================================

// This file is included from TrussC.h
// Requires access to sokol and internal namespace variables
// Texture and HasTexture must be included first

#include "tc/gpu/shaders/fboMipDownsample.glsl.h"

namespace trussc {

// Forward declaration
class Fbo;

// Static helper function for calling FBO's clearColor
inline void _fboClearColorHelper(float r, float g, float b, float a);

// ---------------------------------------------------------------------------
// Fbo Class - inherits from HasTexture
// ---------------------------------------------------------------------------
class Fbo : public HasTexture {
public:
    Fbo() { internal::fboCount++; }
    ~Fbo() { clear(); internal::fboCount--; }

    // Non-copyable
    Fbo(const Fbo&) = delete;
    Fbo& operator=(const Fbo&) = delete;

    // Move-enabled
    Fbo(Fbo&& other) noexcept {
        moveFrom(std::move(other));
    }

    Fbo& operator=(Fbo&& other) noexcept {
        if (this != &other) {
            clear();
            moveFrom(std::move(other));
        }
        return *this;
    }

    // Allocate FBO (MSAA supported, custom pixel format).
    //
    // - `sampleCount`: 1, 2, 4, 8, etc. (1 = no MSAA)
    // - `format`: pixel format (default RGBA8 for backward compatibility)
    // - `mipmaps`: when true, the color texture is allocated with a full mip
    //   chain (floor(log2(max(w,h))) + 1 levels) and `end()` automatically
    //   downsamples mip 0 into the remaining levels with a 2x2 box filter.
    //   Use this when the FBO will be sampled at varying scales (e.g. mapped
    //   onto a 3D surface that moves toward/away from the camera) to avoid
    //   aliasing/moiré. When false the FBO behaves exactly as before.
    void allocate(int w, int h, int sampleCount = 1,
                  TextureFormat format = TextureFormat::RGBA8,
                  bool mipmaps = false) {
        // Skip in headless mode (no graphics context)
        if (headless::isActive()) return;

        clear();

        width_ = w;
        height_ = h;
        sampleCount_ = sampleCount;
        format_ = format;
        mipmaps_ = mipmaps;
        // Compute the full mip chain length when requested. floor(log2(max)) + 1
        // matches the standard convention (e.g. 512 → 10 levels, 1×1 the last).
        numMipLevels_ = mipmaps_
            ? 1 + (int)std::floor(std::log2((float)std::max(w, h)))
            : 1;
        sg_pixel_format sgFormat = toSokolFormat(format);

        // MSAA case
        if (sampleCount_ > 1) {
            // MSAA color texture (render target)
            sg_image_desc msaa_desc = {};
            msaa_desc.usage.color_attachment = true;
            msaa_desc.width = w;
            msaa_desc.height = h;
            msaa_desc.pixel_format = sgFormat;
            msaa_desc.sample_count = sampleCount_;
            msaaColorImage_ = sg_make_image(&msaa_desc);

            // MSAA color attachment view
            sg_view_desc msaa_att_desc = {};
            msaa_att_desc.color_attachment.image = msaaColorImage_;
            msaaColorAttView_ = sg_make_view(&msaa_att_desc);

            // Resolve color texture (non-MSAA, for reading/display)
            colorTexture_.allocate(w, h, format, TextureUsage::RenderTarget, 1, numMipLevels_);

            // Resolve view (must be created as resolve_attachment)
            sg_view_desc resolve_view_desc = {};
            resolve_view_desc.resolve_attachment.image = colorTexture_.getImage();
            resolveAttView_ = sg_make_view(&resolve_view_desc);

            // MSAA depth buffer
            sg_image_desc depth_desc = {};
            depth_desc.usage.depth_stencil_attachment = true;
            depth_desc.width = w;
            depth_desc.height = h;
            depth_desc.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
            depth_desc.sample_count = sampleCount_;
            depthImage_ = sg_make_image(&depth_desc);
        } else {
            // Non-MSAA case
            colorTexture_.allocate(w, h, format, TextureUsage::RenderTarget, 1, numMipLevels_);

            // Scratch texture for mipmap generation (same specs as color)
            if (mipmaps_) {
                mipScratchTexture_.allocate(w, h, format, TextureUsage::RenderTarget, 1, numMipLevels_);
            }

            // Depth buffer
            sg_image_desc depth_desc = {};
            depth_desc.usage.depth_stencil_attachment = true;
            depth_desc.width = w;
            depth_desc.height = h;
            depth_desc.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
            depth_desc.sample_count = 1;
            depthImage_ = sg_make_image(&depth_desc);
        }

        // Depth attachment view
        sg_view_desc depth_att_desc = {};
        depth_att_desc.depth_stencil_attachment.image = depthImage_;
        depthAttView_ = sg_make_view(&depth_att_desc);

        // Ensure shared rendering resources exist for this (sampleCount, format) combo
        ensureShared(sampleCount_, format_);

        // FBO color texture stores premultiplied alpha (due to alpha blending + MSAA resolve)
        colorTexture_.setPremultipliedAlpha(true);

        allocated_ = true;
    }

    // Release resources
    void clear() {
        if (allocated_) {
            // Shared context/pipelines are NOT destroyed here (shared across FBOs)
            sg_destroy_view(depthAttView_);
            sg_destroy_image(depthImage_);

            if (sampleCount_ > 1) {
                sg_destroy_view(resolveAttView_);
                sg_destroy_view(msaaColorAttView_);
                sg_destroy_image(msaaColorImage_);
            }

            colorTexture_.clear();
            mipScratchTexture_.clear();
            allocated_ = false;
        }
        width_ = 0;
        height_ = 0;
        sampleCount_ = 1;
        format_ = TextureFormat::RGBA8;
        active_ = false;
        mipmaps_ = false;
        numMipLevels_ = 1;
        msaaColorImage_ = {};
        msaaColorAttView_ = {};
        resolveAttView_ = {};
        depthImage_ = {};
        depthAttView_ = {};
    }

    // Begin rendering to FBO (preserves previous content)
    void begin() {
        // Skip in headless mode (no graphics context)
        if (headless::isActive()) return;
        beginInternal(0.0f, 0.0f, 0.0f, 0.0f, false);  // LOAD: keep previous content
    }

    // Begin with specified background color (clears FBO)
    void begin(float r, float g, float b, float a = 1.0f) {
        // Skip in headless mode (no graphics context)
        if (headless::isActive()) return;
        beginInternal(r, g, b, a, true);  // CLEAR: fill with specified color
    }

    // Change background color during FBO rendering (restart pass)
    // Called from tc::clear()
    void clearColor(float r, float g, float b, float a) {
        if (!active_) return;

        auto& shared = getShared(sampleCount_, format_);

        // End current pass (flush deferred PBR + 2D for what was drawn so far).
        internal::flushFboDeferredPbr(shared.context);
        sg_end_pass();

        // Restart pass with new clear color
        sg_pass pass = {};
        if (sampleCount_ > 1) {
            pass.attachments.colors[0] = msaaColorAttView_;
            pass.attachments.resolves[0] = resolveAttView_;
        } else {
            pass.attachments.colors[0] = colorTexture_.getAttachmentView();
        }
        pass.attachments.depth_stencil = depthAttView_;
        pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
        pass.action.colors[0].clear_value = { r, g, b, a };
        pass.action.depth.load_action = SG_LOADACTION_CLEAR;
        pass.action.depth.clear_value = 1.0f;
        sg_begin_pass(&pass);

        // Reset sokol_gl state. We are still mid-FBO (currentTarget is this FBO),
        // so activeFill2D() resolves to the FBO's alpha-blend pipeline.
        sgl_defaults();
        internal::loadPipeline(internal::activeFill2D());
        sgl_matrix_mode_projection();
        sgl_ortho(0.0f, (float)width_, (float)height_, 0.0f, -10000.0f, 10000.0f);
        sgl_matrix_mode_modelview();
        sgl_load_identity();
    }

    // End rendering to FBO
    void end() {
        if (!active_) return;

        auto& shared = getShared(sampleCount_, format_);

        // Draw FBO context contents, interleaving any PBR meshes deferred during
        // this pass per-layer (so lit 3D composites with 2D in submission order).
        internal::flushFboDeferredPbr(shared.context);
        sg_end_pass();

        // If mipmaps were requested, downsample mip 0 into the remaining
        // levels NOW (between the FBO pass ending and the swapchain resuming).
        // Skipped entirely when mipmaps_ is false, so the non-mipmap path is
        // unchanged.
        if (mipmaps_) {
            generateMipmaps_();
        }

        // Reset counters so the next FBO using this shared context starts clean.
        // Buffers stay allocated at their current (possibly grown) size — no
        // allocation or deallocation overhead between sequential FBO draws.
        sgl_tc_context_reset(shared.context);

        // Switch back to default context
        sgl_set_context(sgl_default_context());
        active_ = false;
        internal::inFboPass = false;
        internal::currentFbo = nullptr;
        internal::currentFboColorFormat = SG_PIXELFORMAT_RGBA8;
        internal::currentFboSampleCount = 1;
        internal::currentTarget = &internal::swapchainTarget;   // RenderTarget: back to swapchain
        internal::fboClearColorFunc = nullptr;

        // Restore the screen camera state saved in beginInternal() so
        // worldToScreen / camera-context stamping after end() see the screen
        // camera again instead of this FBO's projection.
        internal::currentScreenFov = savedScreenFov_;
        internal::currentViewW = savedViewW_;
        internal::currentViewH = savedViewH_;
        internal::currentCameraDist = savedCameraDist_;
        internal::currentProjectionMatrix = savedProjectionMatrix_;
        internal::currentViewMatrix = savedViewMatrix_;
        internal::currentCameraContext = savedCameraContext_;
        savedCameraContext_.reset();

        // Resume swapchain pass (if we were in one before)
        if (wasInSwapchainPass_) {
            resumeSwapchainPass();
        }
    }

    // Read pixel data (RGBA8 only, for backward compatibility)
    // Note: Call after rendering is complete (after end())
    // For MSAA, reads from resolved texture
    bool readPixels(unsigned char* pixels) const {
        if (!allocated_ || !pixels) return false;

        // sokol_gfx doesn't have direct pixel reading API
        // Platform-specific implementation required
        // colorTexture_ is always non-MSAA (resolved) so read from there
        return readPixelsPlatform(pixels);
    }

    // Read pixel data as float (for float pixel formats: R16F, R32F, RGBA16F, RGBA32F, etc.)
    // Buffer must be large enough: width * height * channelCount(format) floats
    bool readPixelsFloat(float* pixels) const {
        if (!allocated_ || !pixels) return false;
        return readPixelsFloatPlatform(pixels);
    }

    // Copy to Image
    bool copyTo(Image& image) const {
        if (!allocated_) return false;

        image.allocate(width_, height_, 4);
        bool result = readPixels(image.getPixelsData());
        if (result) {
            image.setDirty();
            image.update();
        }
        return result;
    }

    // Size and state getters
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }
    int getSampleCount() const { return sampleCount_; }
    TextureFormat getTextureFormat() const { return format_; }
    bool isAllocated() const { return allocated_; }
    bool isActive() const { return active_; }

    // === HasTexture implementation ===

    // getTexture() always returns non-MSAA texture (for drawing/reading)
    Texture& getTexture() override { return colorTexture_; }
    const Texture& getTexture() const override { return colorTexture_; }

    // GL backends store FBO color textures bottom-to-top in memory
    // (unlike Metal/D3D11 which are top-to-bottom). Override draw to
    // flip V on GL so the displayed image matches user-space top-left coords.
    void draw(float x, float y) const override {
        if (!hasTexture()) return;
        if (needsGlYFlip()) {
            colorTexture_.drawFlippedY(x, y, (float)width_, (float)height_);
        } else {
            colorTexture_.draw(x, y);
        }
    }

    void draw(float x, float y, float w, float h) const override {
        if (!hasTexture()) return;
        if (needsGlYFlip()) {
            colorTexture_.drawFlippedY(x, y, w, h);
        } else {
            colorTexture_.draw(x, y, w, h);
        }
    }

    // save() override - Save FBO contents to file
    bool save(const fs::path& path) const override {
        Image img;
        if (copyTo(img)) {
            return img.save(path);
        }
        return false;
    }

    // Access to internal resources (for advanced users)
    sg_image getColorImage() const { return colorTexture_.getImage(); }
    sg_view getTextureView() const { return colorTexture_.getView(); }
    sg_sampler getSampler() const { return colorTexture_.getSampler(); }

private:
    static bool needsGlYFlip() {
        sg_backend be = sg_query_backend();
        return be == SG_BACKEND_GLCORE || be == SG_BACKEND_GLES3;
    }

    int width_ = 0;
    int height_ = 0;
    int sampleCount_ = 1;
    TextureFormat format_ = TextureFormat::RGBA8;
    bool allocated_ = false;
    bool active_ = false;
    bool wasInSwapchainPass_ = false;  // Was in swapchain pass when begin() called
    bool mipmaps_ = false;
    int  numMipLevels_ = 1;

    // Screen camera state captured in beginInternal(), restored in end()
    float savedScreenFov_ = 0.0f;
    float savedViewW_ = 0.0f;
    float savedViewH_ = 0.0f;
    float savedCameraDist_ = 0.0f;
    Mat4  savedProjectionMatrix_ = Mat4::identity();
    Mat4  savedViewMatrix_ = Mat4::identity();
    std::shared_ptr<const CameraContext> savedCameraContext_;

    // Non-MSAA texture (always used, resolve target for MSAA)
    Texture colorTexture_;
    // Scratch texture for mipmap generation (avoids sokol validation error
    // when sampling and writing to different mips of the same image).
    Texture mipScratchTexture_;

    // MSAA resources (only used when sampleCount > 1)
    sg_image msaaColorImage_ = {};
    sg_view msaaColorAttView_ = {};
    sg_view resolveAttView_ = {};

    // Common resources
    sg_image depthImage_ = {};
    sg_view depthAttView_ = {};

    // =========================================================================
    // Shared rendering resources (sgl_context + pipelines) per (sampleCount, format).
    // One context per combination, shared across all FBOs with matching params.
    // Command leaking between FBOs is prevented by context_reset:
    //   end()   -> sgl_context_draw() then context_reset (clears command counters)
    //   begin() -> ensure_buffers (allocates if needed)
    // Nested FBO begin/end is NOT supported (sokol doesn't support nested passes).
    // =========================================================================

    struct SharedResources {
        sgl_context context = {};
        internal::RenderTarget target;  // role->pipeline cache for this FBO context
        bool initialized = false;
    };

    // Pack (sampleCount, format) into a uint64_t key
    static uint64_t sharedKey(int sampleCount, TextureFormat format) {
        return ((uint64_t)sampleCount << 32) | (uint64_t)format;
    }

    static std::unordered_map<uint64_t, SharedResources>& sharedMap() {
        static std::unordered_map<uint64_t, SharedResources> map;
        return map;
    }

    static SharedResources& getShared(int sampleCount, TextureFormat format) {
        return sharedMap()[sharedKey(sampleCount, format)];
    }

    static void ensureShared(int sampleCount, TextureFormat format) {
        auto& s = getShared(sampleCount, format);
        if (s.initialized) return;

        sg_pixel_format sgFormat = toSokolFormat(format);

        // Create sgl context (match main context buffer sizes)
        sgl_context_desc_t ctx_desc = {};
        ctx_desc.max_vertices = internal::sglMaxVertices;
        ctx_desc.max_commands = internal::sglMaxCommands;
        ctx_desc.color_format = sgFormat;
        ctx_desc.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
        ctx_desc.sample_count = sampleCount;
        s.context = sgl_make_context(&ctx_desc);

        // RenderTarget for this FBO context (lazy role->pipeline cache; FBO pipelines
        // create fine on first use inside a pass — unlike the swapchain path, which is
        // pre-warmed in tcGlobal because mid-frame creation in setupScreenFov corrupts
        // the frame).
        s.target.context = s.context;
        s.target.isFbo = true;

        s.initialized = true;
    }

    // -------------------------------------------------------------------------
    // Shared mip-downsample resources (keyed by color format).
    // Independent of the sgl_context / sampleCount of SharedResources because
    // mip generation always runs on the resolved (non-MSAA) color texture
    // with a tiny custom pipeline, not through sokol_gl.
    // -------------------------------------------------------------------------
    struct SharedMipResources {
        sg_shader shader = {};
        sg_pipeline pipeline = {};
        sg_shader blitShader = {};
        sg_pipeline blitPipeline = {};
        sg_buffer vbuf = {};
        sg_sampler sampler = {};
        bool initialized = false;
    };

    static std::unordered_map<uint64_t, SharedMipResources>& sharedMipMap() {
        static std::unordered_map<uint64_t, SharedMipResources> map;
        return map;
    }

    static SharedMipResources& getSharedMip(TextureFormat format) {
        return sharedMipMap()[(uint64_t)format];
    }

    static void ensureSharedMip(TextureFormat format) {
        auto& s = getSharedMip(format);
        if (s.initialized) return;

        sg_pixel_format sgFormat = toSokolFormat(format);

        // Fullscreen quad (triangle strip). UV 0..1 across the quad so a
        // bilinear sample of the previous mip is enough to produce a 2x2
        // box-filtered destination level. textureLod() in the shader pins
        // the source level, so a single pipeline handles every level.
        static const float quadVerts[] = {
            // x,     y,    u,    v
            -1.0f, -1.0f, 0.0f, 0.0f,
             1.0f, -1.0f, 1.0f, 0.0f,
            -1.0f,  1.0f, 0.0f, 1.0f,
             1.0f,  1.0f, 1.0f, 1.0f,
        };
        sg_buffer_desc vbuf_desc = {};
        vbuf_desc.usage.vertex_buffer = true;
        vbuf_desc.data.ptr = quadVerts;
        vbuf_desc.data.size = sizeof(quadVerts);
        s.vbuf = sg_make_buffer(&vbuf_desc);

        sg_sampler_desc smp_desc = {};
        smp_desc.min_filter = SG_FILTER_LINEAR;
        smp_desc.mag_filter = SG_FILTER_LINEAR;
        smp_desc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
        smp_desc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
        s.sampler = sg_make_sampler(&smp_desc);

        s.shader = sg_make_shader(tc_fbomip_downsample_shader_desc(sg_query_backend()));

        sg_pipeline_desc pip_desc = {};
        pip_desc.shader = s.shader;
        pip_desc.layout.attrs[ATTR_tc_fbomip_downsample_position].format = SG_VERTEXFORMAT_FLOAT2;
        pip_desc.layout.attrs[ATTR_tc_fbomip_downsample_texcoord0].format = SG_VERTEXFORMAT_FLOAT2;
        pip_desc.primitive_type = SG_PRIMITIVETYPE_TRIANGLE_STRIP;
        pip_desc.colors[0].pixel_format = sgFormat;
        pip_desc.colors[0].blend.enabled = false;
        pip_desc.depth.pixel_format = SG_PIXELFORMAT_NONE;
        pip_desc.sample_count = 1;
        s.pipeline = sg_make_pipeline(&pip_desc);

        // 1:1 blit pipeline for copying scratch mip → main mip
        s.blitShader = sg_make_shader(tc_fbomip_blit_shader_desc(sg_query_backend()));
        sg_pipeline_desc blit_pip_desc = {};
        blit_pip_desc.shader = s.blitShader;
        blit_pip_desc.layout.attrs[ATTR_tc_fbomip_blit_position].format = SG_VERTEXFORMAT_FLOAT2;
        blit_pip_desc.layout.attrs[ATTR_tc_fbomip_blit_texcoord0].format = SG_VERTEXFORMAT_FLOAT2;
        blit_pip_desc.primitive_type = SG_PRIMITIVETYPE_TRIANGLE_STRIP;
        blit_pip_desc.colors[0].pixel_format = sgFormat;
        blit_pip_desc.colors[0].blend.enabled = false;
        blit_pip_desc.depth.pixel_format = SG_PIXELFORMAT_NONE;
        blit_pip_desc.sample_count = 1;
        s.blitPipeline = sg_make_pipeline(&blit_pip_desc);

        s.initialized = true;
    }

    // Downsample mip 0 into mips 1..N-1. Called from end() when the FBO was
    // allocated with mipmaps = true.
    //
    // sokol forbids sampling from an image that has a mip level bound as a
    // color attachment in the same pass (even if different mip levels are
    // involved). We work around this with a two-pass-per-level approach:
    //   Pass 1 (downsample): main[level-1] → scratch[level]
    //   Pass 2 (blit):       scratch[level] → main[level]
    void generateMipmaps_() {
        if (!mipmaps_ || numMipLevels_ <= 1) return;
        ensureSharedMip(format_);
        auto& s = getSharedMip(format_);

        for (int level = 1; level < numMipLevels_; level++) {
            // Pass 1: downsample main[level-1] → scratch[level]
            {
                sg_pass pass = {};
                pass.attachments.colors[0] = mipScratchTexture_.getAttachmentViewForMip(level);
                pass.action.colors[0].load_action = SG_LOADACTION_DONTCARE;
                sg_begin_pass(&pass);

                sg_apply_pipeline(s.pipeline);

                sg_bindings bind = {};
                bind.vertex_buffers[0] = s.vbuf;
                bind.views[VIEW_tc_fbomip_srcTex] = colorTexture_.getViewForMip(level - 1);
                bind.samplers[SMP_tc_fbomip_srcSmp] = s.sampler;
                sg_apply_bindings(&bind);

                sg_draw(0, 4, 1);
                sg_end_pass();
            }

            // Pass 2: blit scratch[level] → main[level]
            {
                sg_pass pass = {};
                pass.attachments.colors[0] = colorTexture_.getAttachmentViewForMip(level);
                pass.action.colors[0].load_action = SG_LOADACTION_DONTCARE;
                sg_begin_pass(&pass);

                sg_apply_pipeline(s.blitPipeline);

                sg_bindings bind = {};
                bind.vertex_buffers[0] = s.vbuf;
                bind.views[VIEW_tc_fbomip_srcTex] = mipScratchTexture_.getViewForMip(level);
                bind.samplers[SMP_tc_fbomip_srcSmp] = s.sampler;
                sg_apply_bindings(&bind);

                sg_draw(0, 4, 1);
                sg_end_pass();
            }
        }
    }

    void beginInternal(float r, float g, float b, float a, bool doClear) {
        if (!allocated_) return;

        // Guard: nested FBO begin is not supported
        if (internal::inFboPass) {
            logWarning("Fbo") << "Nested fbo.begin() is not supported. "
                              << "Call end() on the current FBO first.";
            return;
        }

        auto& shared = getShared(sampleCount_, format_);

        // Suspend if in swapchain pass
        wasInSwapchainPass_ = isInSwapchainPass();
        if (wasInSwapchainPass_) {
            suspendSwapchainPass();
        }

        // Begin offscreen pass
        sg_pass pass = {};

        if (sampleCount_ > 1) {
            // MSAA: Render to MSAA texture, resolve to non-MSAA texture
            pass.attachments.colors[0] = msaaColorAttView_;
            pass.attachments.resolves[0] = resolveAttView_;
        } else {
            // Non-MSAA: Render directly to color texture
            pass.attachments.colors[0] = colorTexture_.getAttachmentView();
        }
        pass.attachments.depth_stencil = depthAttView_;

        if (doClear) {
            pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
            pass.action.colors[0].clear_value = { r, g, b, a };
        } else {
            pass.action.colors[0].load_action = SG_LOADACTION_LOAD;
        }
        pass.action.depth.load_action = SG_LOADACTION_CLEAR;
        pass.action.depth.clear_value = 1.0f;

        // For MSAA, resolve is automatic (store_action defaults to STORE)
        sg_begin_pass(&pass);

        // Switch to shared FBO context and ensure buffers are allocated
        sgl_set_context(shared.context);
        sgl_tc_context_ensure_buffers(shared.context);
        sgl_defaults();

        // Start this FBO pass's deferred-PBR layer counter fresh (mirrors the
        // swapchain's sglLayerNext). Meshes drawn now defer into fboPbrDraws.
        internal::fboPbrDraws.clear();
        internal::fboLayerNext = 0;
        sgl_layer(0);

        // Save the screen camera state so end() can restore it — the FBO's own
        // projection setup below overwrites these globals, and anything drawn
        // after end() (worldToScreen, node camera-context stamping) must see
        // the screen camera again, not the FBO's.
        savedScreenFov_ = internal::currentScreenFov;
        savedViewW_ = internal::currentViewW;
        savedViewH_ = internal::currentViewH;
        savedCameraDist_ = internal::currentCameraDist;
        savedProjectionMatrix_ = internal::currentProjectionMatrix;
        savedViewMatrix_ = internal::currentViewMatrix;
        savedCameraContext_ = internal::currentCameraContext;

        // Setup screen projection using defaultScreenFov (like main screen).
        // pickable=false: geometry drawn into an offscreen target must not be
        // pickable from main-screen clicks (see tcCameraContext.h).
        internal::setupScreenFovWithSize(internal::defaultScreenFov, (float)width_, (float)height_, 0.0f, 0.0f, false);

        active_ = true;
        internal::inFboPass = true;
        // Retarget to this FBO BEFORE loading its fill pipeline so activeFill2D()
        // resolves in the FBO's context/format (setupScreenFov above still ran on
        // the previous target, as before).
        internal::currentTarget = &shared.target;

        // Use the FBO's alpha-blend Fill2D pipeline (Porter-Duff over); the result
        // is stored as premultiplied alpha in the FBO.
        internal::loadPipeline(internal::activeFill2D());

        internal::currentFbo = this;
        internal::currentFboColorFormat = toSokolFormat(format_);
        internal::currentFboSampleCount = sampleCount_;
        internal::fboClearColorFunc = _fboClearColorHelper;
    }

private:

    void moveFrom(Fbo&& other) {
        width_ = other.width_;
        height_ = other.height_;
        sampleCount_ = other.sampleCount_;
        format_ = other.format_;
        allocated_ = other.allocated_;
        active_ = other.active_;
        wasInSwapchainPass_ = other.wasInSwapchainPass_;
        mipmaps_ = other.mipmaps_;
        numMipLevels_ = other.numMipLevels_;
        savedScreenFov_ = other.savedScreenFov_;
        savedViewW_ = other.savedViewW_;
        savedViewH_ = other.savedViewH_;
        savedCameraDist_ = other.savedCameraDist_;
        savedProjectionMatrix_ = other.savedProjectionMatrix_;
        savedViewMatrix_ = other.savedViewMatrix_;
        savedCameraContext_ = std::move(other.savedCameraContext_);
        colorTexture_ = std::move(other.colorTexture_);
        msaaColorImage_ = other.msaaColorImage_;
        msaaColorAttView_ = other.msaaColorAttView_;
        resolveAttView_ = other.resolveAttView_;
        depthImage_ = other.depthImage_;
        depthAttView_ = other.depthAttView_;

        other.allocated_ = false;
        other.active_ = false;
        other.wasInSwapchainPass_ = false;
        other.mipmaps_ = false;
        other.numMipLevels_ = 1;
        other.width_ = 0;
        other.height_ = 0;
        other.sampleCount_ = 1;
        other.format_ = TextureFormat::RGBA8;
        other.msaaColorImage_ = {};
        other.msaaColorAttView_ = {};
        other.resolveAttView_ = {};
        other.depthImage_ = {};
        other.depthAttView_ = {};
    }

    // Platform-specific pixel reading (implemented in platform files)
    bool readPixelsPlatform(unsigned char* pixels) const;
    bool readPixelsFloatPlatform(float* pixels) const;
};

// ---------------------------------------------------------------------------
// Helper function called from tc::clear()
// ---------------------------------------------------------------------------
inline void _fboClearColorHelper(float r, float g, float b, float a) {
    Fbo* fbo = static_cast<Fbo*>(internal::currentFbo);
    if (fbo) {
        fbo->clearColor(r, g, b, a);
    }
}

} // namespace trussc
