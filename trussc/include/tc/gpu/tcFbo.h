#pragma once

// =============================================================================
// tcFbo.h - Framebuffer Object (off-screen rendering)
// =============================================================================

// This file is included from TrussC.h
// Requires access to sokol and internal namespace variables
// Texture and HasTexture must be included first

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

    // Allocate FBO (MSAA supported)
    // sampleCount: 1, 2, 4, 8, etc. (1 = no MSAA)
    void allocate(int w, int h, int sampleCount = 1) {
        // Skip in headless mode (no graphics context)
        if (headless::isActive()) return;

        clear();

        width_ = w;
        height_ = h;
        sampleCount_ = sampleCount;

        // MSAA case
        if (sampleCount_ > 1) {
            // MSAA color texture (render target)
            sg_image_desc msaa_desc = {};
            msaa_desc.usage.color_attachment = true;
            msaa_desc.width = w;
            msaa_desc.height = h;
            msaa_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
            msaa_desc.sample_count = sampleCount_;
            msaaColorImage_ = sg_make_image(&msaa_desc);

            // MSAA color attachment view
            sg_view_desc msaa_att_desc = {};
            msaa_att_desc.color_attachment.image = msaaColorImage_;
            msaaColorAttView_ = sg_make_view(&msaa_att_desc);

            // Resolve color texture (non-MSAA, for reading/display)
            colorTexture_.allocate(w, h, 4, TextureUsage::RenderTarget, 1);

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
            // Non-MSAA case (same as before)
            colorTexture_.allocate(w, h, 4, TextureUsage::RenderTarget, 1);

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

        // Ensure shared rendering resources exist for this sample count
        ensureShared(sampleCount_);

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
            allocated_ = false;
        }
        width_ = 0;
        height_ = 0;
        sampleCount_ = 1;
        active_ = false;
        msaaColorImage_ = {};
        msaaColorAttView_ = {};
        resolveAttView_ = {};
        depthImage_ = {};
        depthAttView_ = {};
    }

    // Begin rendering to FBO
    void begin() {
        // Skip in headless mode (no graphics context)
        if (headless::isActive()) return;
        beginInternal(0.0f, 0.0f, 0.0f, 0.0f);  // Clear with transparent
    }

    // Begin with specified background color
    void begin(float r, float g, float b, float a = 1.0f) {
        // Skip in headless mode (no graphics context)
        if (headless::isActive()) return;
        beginInternal(r, g, b, a);
    }

    // Change background color during FBO rendering (restart pass)
    // Called from tc::clear()
    void clearColor(float r, float g, float b, float a) {
        if (!active_) return;

        auto& shared = getShared(sampleCount_);

        // End current pass
        sgl_context_draw(shared.context);
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

        // Reset sokol_gl state
        sgl_defaults();
        sgl_load_pipeline(shared.pipelineBlend);
        sgl_matrix_mode_projection();
        sgl_ortho(0.0f, (float)width_, (float)height_, 0.0f, -10000.0f, 10000.0f);
        sgl_matrix_mode_modelview();
        sgl_load_identity();
    }

    // End rendering to FBO
    void end() {
        if (!active_) return;

        auto& shared = getShared(sampleCount_);

        // Draw FBO context contents
        sgl_context_draw(shared.context);
        sg_end_pass();

        // Switch back to default context
        sgl_set_context(sgl_default_context());
        active_ = false;
        internal::inFboPass = false;
        internal::currentFboClearPipeline = {};
        internal::currentFboBlendPipeline = {};
        internal::currentFbo = nullptr;
        internal::fboClearColorFunc = nullptr;

        // Resume swapchain pass (if we were in one before)
        if (wasInSwapchainPass_) {
            resumeSwapchainPass();
        }
    }

    // Read pixel data
    // Note: Call after rendering is complete (after end())
    // For MSAA, reads from resolved texture
    bool readPixels(unsigned char* pixels) const {
        if (!allocated_ || !pixels) return false;

        // sokol_gfx doesn't have direct pixel reading API
        // Platform-specific implementation required
        // colorTexture_ is always non-MSAA (resolved) so read from there
        return readPixelsPlatform(pixels);
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
    bool isAllocated() const { return allocated_; }
    bool isActive() const { return active_; }

    // === HasTexture implementation ===

    // getTexture() always returns non-MSAA texture (for drawing/reading)
    Texture& getTexture() override { return colorTexture_; }
    const Texture& getTexture() const override { return colorTexture_; }

    // draw() uses HasTexture's default implementation

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
    int width_ = 0;
    int height_ = 0;
    int sampleCount_ = 1;
    bool allocated_ = false;
    bool active_ = false;
    bool wasInSwapchainPass_ = false;  // Was in swapchain pass when begin() called

    // Non-MSAA texture (always used, resolve target for MSAA)
    Texture colorTexture_;

    // MSAA resources (only used when sampleCount > 1)
    sg_image msaaColorImage_ = {};
    sg_view msaaColorAttView_ = {};
    sg_view resolveAttView_ = {};

    // Common resources
    sg_image depthImage_ = {};
    sg_view depthAttView_ = {};

    // =========================================================================
    // Shared rendering resources (sgl_context + pipelines)
    // One set per sample count, shared across all FBOs.
    // Nested FBO begin/end is NOT supported (sokol doesn't support nested passes).
    // =========================================================================

    struct SharedResources {
        sgl_context context = {};
        sgl_pipeline pipelineBlend = {};
        sgl_pipeline pipelineClear = {};
        bool initialized = false;
    };

    // Map sampleCount to index: 1->0, 2->1, 4->2, 8->3
    static int sharedIndex(int sc) {
        switch (sc) {
            case 2: return 1;
            case 4: return 2;
            case 8: return 3;
            default: return 0;
        }
    }

    static SharedResources& getShared(int sampleCount) {
        static SharedResources resources[4];
        return resources[sharedIndex(sampleCount)];
    }

    static void ensureShared(int sampleCount) {
        auto& s = getShared(sampleCount);
        if (s.initialized) return;

        // Create sgl context
        sgl_context_desc_t ctx_desc = {};
        ctx_desc.color_format = SG_PIXELFORMAT_RGBA8;
        ctx_desc.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
        ctx_desc.sample_count = sampleCount;
        s.context = sgl_make_context(&ctx_desc);

        // Alpha blend pipeline (Porter-Duff over, produces premultiplied alpha)
        {
            sg_pipeline_desc pip_desc = {};
            pip_desc.sample_count = sampleCount;
            pip_desc.depth.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
            pip_desc.colors[0].blend.enabled = true;
            pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
            pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            pip_desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
            pip_desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            pip_desc.colors[0].write_mask = SG_COLORMASK_RGBA;
            s.pipelineBlend = sgl_context_make_pipeline(s.context, &pip_desc);
        }

        // Overwrite pipeline (no blend, for clear drawing)
        {
            sg_pipeline_desc pip_desc = {};
            pip_desc.sample_count = sampleCount;
            pip_desc.depth.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
            pip_desc.colors[0].blend.enabled = false;
            pip_desc.colors[0].write_mask = SG_COLORMASK_RGBA;
            s.pipelineClear = sgl_context_make_pipeline(s.context, &pip_desc);
        }

        s.initialized = true;
    }

    void beginInternal(float r, float g, float b, float a) {
        if (!allocated_) return;

        // Guard: nested FBO begin is not supported
        if (internal::inFboPass) {
            logWarning("Fbo") << "Nested fbo.begin() is not supported. "
                              << "Call end() on the current FBO first.";
            return;
        }

        auto& shared = getShared(sampleCount_);

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

        pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
        pass.action.colors[0].clear_value = { r, g, b, a };
        pass.action.depth.load_action = SG_LOADACTION_CLEAR;
        pass.action.depth.clear_value = 1.0f;

        // For MSAA, resolve is automatic (store_action defaults to STORE)
        sg_begin_pass(&pass);

        // Switch to shared FBO context
        sgl_set_context(shared.context);
        sgl_defaults();

        // Setup screen projection using defaultScreenFov (like main screen)
        internal::setupScreenFovWithSize(internal::defaultScreenFov, (float)width_, (float)height_, 0.0f, 0.0f);

        // Use alpha blend pipeline (Porter-Duff over)
        // Result stored as premultiplied alpha in FBO
        sgl_load_pipeline(shared.pipelineBlend);

        active_ = true;
        internal::inFboPass = true;
        internal::currentFboClearPipeline = shared.pipelineClear;
        internal::currentFboBlendPipeline = shared.pipelineBlend;
        internal::currentFbo = this;
        internal::fboClearColorFunc = _fboClearColorHelper;
    }

private:

    void moveFrom(Fbo&& other) {
        width_ = other.width_;
        height_ = other.height_;
        sampleCount_ = other.sampleCount_;
        allocated_ = other.allocated_;
        active_ = other.active_;
        wasInSwapchainPass_ = other.wasInSwapchainPass_;
        colorTexture_ = std::move(other.colorTexture_);
        msaaColorImage_ = other.msaaColorImage_;
        msaaColorAttView_ = other.msaaColorAttView_;
        resolveAttView_ = other.resolveAttView_;
        depthImage_ = other.depthImage_;
        depthAttView_ = other.depthAttView_;

        other.allocated_ = false;
        other.active_ = false;
        other.wasInSwapchainPass_ = false;
        other.width_ = 0;
        other.height_ = 0;
        other.sampleCount_ = 1;
        other.msaaColorImage_ = {};
        other.msaaColorAttView_ = {};
        other.resolveAttView_ = {};
        other.depthImage_ = {};
        other.depthAttView_ = {};
    }

    // Platform-specific pixel reading (implemented in tcFbo_platform.h)
    bool readPixelsPlatform(unsigned char* pixels) const;
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
