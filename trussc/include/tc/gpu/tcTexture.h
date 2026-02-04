#pragma once

// =============================================================================
// tcTexture.h - GPU texture management
// =============================================================================

// This file is included from TrussC.h
// to access sokol and internal namespace variables

namespace trussc {

// Texture usage mode
enum class TextureUsage {
    Immutable,      // Set once, cannot update (for Image::load)
    Dynamic,        // Update periodically from CPU (for Image::allocate)
    Stream,         // Update every frame (for VideoGrabber)
    RenderTarget    // For FBO color attachment
};

// ---------------------------------------------------------------------------
// Texture class - Manages GPU-side texture
// ---------------------------------------------------------------------------
class Texture {
public:
    Texture() { internal::textureCount++; }
    ~Texture() { clear(); internal::textureCount--; }

    // Copy prohibited
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    // Move support
    Texture(Texture&& other) noexcept {
        moveFrom(std::move(other));
    }

    Texture& operator=(Texture&& other) noexcept {
        if (this != &other) {
            clear();
            moveFrom(std::move(other));
        }
        return *this;
    }

    // === Allocation/Deallocation ===

    // Allocate empty texture
    void allocate(int width, int height, int channels = 4,
                  TextureUsage usage = TextureUsage::Immutable,
                  int sampleCount = 1) {
        clear();

        width_ = width;
        height_ = height;
        channels_ = channels;
        usage_ = usage;
        sampleCount_ = sampleCount;
        pixelFormat_ = SG_PIXELFORMAT_NONE;  // Use default based on channels

        createResources(nullptr);
    }

    // Allocate compressed texture (BC1/BC3/BC7 etc.)
    // Data must be provided for immutable compressed textures
    void allocateCompressed(int width, int height, sg_pixel_format format,
                            const void* data, size_t dataSize) {
        clear();

        width_ = width;
        height_ = height;
        channels_ = 4;  // Compressed textures treated as RGBA
        usage_ = TextureUsage::Immutable;
        pixelFormat_ = format;

        createCompressedResources(data, dataSize);
    }

    // Update compressed texture (recreates texture - BC textures are immutable)
    void updateCompressed(const void* data, size_t dataSize) {
        if (!allocated_ || pixelFormat_ == SG_PIXELFORMAT_NONE) return;

        // Destroy old resources
        sg_destroy_sampler(sampler_);
        sg_destroy_view(view_);
        sg_destroy_image(image_);

        // Recreate with new data
        createCompressedResources(data, dataSize);
    }

    bool isCompressed() const {
        return pixelFormat_ != SG_PIXELFORMAT_NONE && pixelFormat_ != SG_PIXELFORMAT_RGBA32F;
    }

    // Allocate texture from Pixels (auto-detects F32 → RGBA32F)
    void allocate(const Pixels& pixels, TextureUsage usage = TextureUsage::Immutable) {
        clear();

        width_ = pixels.getWidth();
        height_ = pixels.getHeight();
        channels_ = pixels.getChannels();
        usage_ = usage;

        if (pixels.isFloat()) {
            pixelFormat_ = SG_PIXELFORMAT_RGBA32F;
        }

        if (usage == TextureUsage::Immutable) {
            createResources(pixels.getDataVoid());
        } else {
            createResources(nullptr);
        }
    }

    // Release resources
    void clear() {
        if (allocated_) {
            sg_destroy_sampler(sampler_);
            sg_destroy_view(view_);
            if (attachmentView_.id != 0) {
                sg_destroy_view(attachmentView_);
            }
            sg_destroy_image(image_);
            allocated_ = false;
        }
        width_ = 0;
        height_ = 0;
        channels_ = 0;
        pixelFormat_ = SG_PIXELFORMAT_NONE;
        image_ = {};
        view_ = {};
        attachmentView_ = {};
        sampler_ = {};
    }

    // === State ===

    bool isAllocated() const { return allocated_; }
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }
    int getChannels() const { return channels_; }
    TextureUsage getUsage() const { return usage_; }
    int getSampleCount() const { return sampleCount_; }

    // === Data update (except Immutable) ===

    void loadData(const Pixels& pixels) {
        loadData(pixels.getDataVoid(), pixels.getWidth(), pixels.getHeight(), pixels.getChannels());
    }

    // Upload pixel data to texture
    // Note: Due to sokol limitations, can only be called once per frame
    // Calling twice in same frame ignores second call and logs warning
    void loadData(const void* data, int width, int height, int channels) {
        if (!allocated_ || usage_ == TextureUsage::Immutable) return;
        if (width != width_ || height != height_ || channels != channels_) return;

        // Can only update once per frame (sokol limitation)
        uint64_t currentFrame = sapp_frame_count();
        if (lastUpdateFrame_ == currentFrame) {
            logWarning() << "[Texture] loadData() called twice in same frame, skipped";
            return;
        }
        lastUpdateFrame_ = currentFrame;

        size_t dataSize = (size_t)width * height * channels;
        if (pixelFormat_ == SG_PIXELFORMAT_RGBA32F) {
            dataSize *= sizeof(float);
        }

        sg_image_data img_data = {};
        img_data.mip_levels[0].ptr = data;
        img_data.mip_levels[0].size = dataSize;
        sg_update_image(image_, &img_data);
    }

    // Backward-compatible U8 overload
    void loadData(const unsigned char* data, int width, int height, int channels) {
        loadData(static_cast<const void*>(data), width, height, channels);
    }

    // === Filter settings ===

    void setMinFilter(TextureFilter filter) {
        if (minFilter_ != filter) {
            minFilter_ = filter;
            recreateSampler();
        }
    }

    void setMagFilter(TextureFilter filter) {
        if (magFilter_ != filter) {
            magFilter_ = filter;
            recreateSampler();
        }
    }

    void setFilter(TextureFilter filter) {
        if (minFilter_ != filter || magFilter_ != filter) {
            minFilter_ = filter;
            magFilter_ = filter;
            recreateSampler();
        }
    }

    TextureFilter getMinFilter() const { return minFilter_; }
    TextureFilter getMagFilter() const { return magFilter_; }

    // === Wrap mode settings ===

    void setWrapU(TextureWrap wrap) {
        if (wrapU_ != wrap) {
            wrapU_ = wrap;
            recreateSampler();
        }
    }

    void setWrapV(TextureWrap wrap) {
        if (wrapV_ != wrap) {
            wrapV_ = wrap;
            recreateSampler();
        }
    }

    void setWrap(TextureWrap wrap) {
        if (wrapU_ != wrap || wrapV_ != wrap) {
            wrapU_ = wrap;
            wrapV_ = wrap;
            recreateSampler();
        }
    }

    TextureWrap getWrapU() const { return wrapU_; }
    TextureWrap getWrapV() const { return wrapV_; }

    // === Draw ===

    void draw(float x, float y) const {
        if (allocated_) {
            drawInternal(x, y, (float)width_, (float)height_, 0.0f, 0.0f, 1.0f, 1.0f);
        }
    }

    void draw(float x, float y, float w, float h) const {
        if (allocated_) {
            drawInternal(x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f);
        }
    }

    // Partial draw (for sprite sheets)
    void drawSubsection(float x, float y, float w, float h,
                        float sx, float sy, float sw, float sh) const {
        if (allocated_ && width_ > 0 && height_ > 0) {
            float u0 = sx / width_;
            float v0 = sy / height_;
            float u1 = (sx + sw) / width_;
            float v1 = (sy + sh) / height_;
            drawInternal(x, y, w, h, u0, v0, u1, v1);
        }
    }

    // === Bind (for shader integration) ===

    void bind() const {
        if (allocated_) {
            sgl_enable_texture();
            sgl_texture(view_, sampler_);
        }
    }

    void unbind() const {
        sgl_disable_texture();
    }

    // === Internal resource access (advanced) ===

    sg_image getImage() const { return image_; }
    sg_view getView() const { return view_; }
    sg_sampler getSampler() const { return sampler_; }

    // For RenderTarget: attachment view (used as render target in FBO)
    sg_view getAttachmentView() const { return attachmentView_; }

private:
    sg_image image_ = {};
    sg_view view_ = {};              // Texture view (for sampling)
    sg_view attachmentView_ = {};    // Attachment view (for RenderTarget)
    sg_sampler sampler_ = {};

    int width_ = 0;
    int height_ = 0;
    int channels_ = 0;
    int sampleCount_ = 1;
    bool allocated_ = false;
    TextureUsage usage_ = TextureUsage::Immutable;
    uint64_t lastUpdateFrame_ = UINT64_MAX;  // Last updated frame
    sg_pixel_format pixelFormat_ = SG_PIXELFORMAT_NONE;  // For compressed textures

    TextureFilter minFilter_ = TextureFilter::Linear;
    TextureFilter magFilter_ = TextureFilter::Linear;
    TextureWrap wrapU_ = TextureWrap::ClampToEdge;
    TextureWrap wrapV_ = TextureWrap::ClampToEdge;

    void createCompressedResources(const void* data, size_t dataSize) {
        sg_image_desc img_desc = {};
        img_desc.width = width_;
        img_desc.height = height_;
        img_desc.pixel_format = pixelFormat_;
        img_desc.data.mip_levels[0].ptr = data;
        img_desc.data.mip_levels[0].size = dataSize;

        image_ = sg_make_image(&img_desc);

        // Create texture view
        sg_view_desc view_desc = {};
        view_desc.texture.image = image_;
        view_ = sg_make_view(&view_desc);

        // Create sampler
        createSampler();

        allocated_ = true;
    }

    void createResources(const void* initialData) {
        // Create image
        sg_image_desc img_desc = {};
        img_desc.width = width_;
        img_desc.height = height_;

        // Determine pixel format
        if (pixelFormat_ != SG_PIXELFORMAT_NONE) {
            img_desc.pixel_format = pixelFormat_;
        } else {
            img_desc.pixel_format = (channels_ == 4) ? SG_PIXELFORMAT_RGBA8 : SG_PIXELFORMAT_R8;
        }

        // Compute data size (RGBA32F = 4 bytes per component)
        size_t dataSize = (size_t)width_ * height_ * channels_;
        if (pixelFormat_ == SG_PIXELFORMAT_RGBA32F) {
            dataSize *= sizeof(float);
        }

        switch (usage_) {
            case TextureUsage::Immutable:
                if (initialData) {
                    img_desc.data.mip_levels[0].ptr = initialData;
                    img_desc.data.mip_levels[0].size = dataSize;
                }
                break;
            case TextureUsage::Dynamic:
                img_desc.usage.dynamic_update = true;
                break;
            case TextureUsage::Stream:
                img_desc.usage.stream_update = true;
                break;
            case TextureUsage::RenderTarget:
                img_desc.usage.color_attachment = true;
                img_desc.usage.resolve_attachment = true;  // Can also be used as MSAA resolve target
                img_desc.sample_count = sampleCount_;
                break;
        }

        image_ = sg_make_image(&img_desc);

        // Create texture view (for sampling)
        sg_view_desc view_desc = {};
        view_desc.texture.image = image_;
        view_ = sg_make_view(&view_desc);

        // Create attachment view for RenderTarget
        if (usage_ == TextureUsage::RenderTarget) {
            sg_view_desc att_desc = {};
            att_desc.color_attachment.image = image_;
            attachmentView_ = sg_make_view(&att_desc);
        }

        // Create sampler
        createSampler();

        allocated_ = true;
    }

    void createSampler() {
        sg_sampler_desc smp_desc = {};

        // Filter settings
        smp_desc.min_filter = (minFilter_ == TextureFilter::Nearest) ? SG_FILTER_NEAREST : SG_FILTER_LINEAR;
        smp_desc.mag_filter = (magFilter_ == TextureFilter::Nearest) ? SG_FILTER_NEAREST : SG_FILTER_LINEAR;

        // Wrap mode settings
        auto toSgWrap = [](TextureWrap wrap) -> sg_wrap {
            switch (wrap) {
                case TextureWrap::Repeat: return SG_WRAP_REPEAT;
                case TextureWrap::MirroredRepeat: return SG_WRAP_MIRRORED_REPEAT;
                case TextureWrap::ClampToEdge:
                default: return SG_WRAP_CLAMP_TO_EDGE;
            }
        };
        smp_desc.wrap_u = toSgWrap(wrapU_);
        smp_desc.wrap_v = toSgWrap(wrapV_);

        sampler_ = sg_make_sampler(&smp_desc);
    }

    void recreateSampler() {
        if (!allocated_) return;
        sg_destroy_sampler(sampler_);
        createSampler();
    }

    void drawInternal(float x, float y, float w, float h,
                      float u0, float v0, float u1, float v1) const {
        // Use appropriate alpha blend pipeline (FBO or main swapchain)
        if (internal::inFboPass && internal::currentFboBlendPipeline.id != 0) {
            sgl_load_pipeline(internal::currentFboBlendPipeline);
        } else {
            sgl_load_pipeline(internal::fontPipeline);
        }
        sgl_enable_texture();
        sgl_texture(view_, sampler_);

        // Draw with current color
        Color col = getDefaultContext().getColor();
        sgl_begin_quads();
        sgl_c4f(col.r, col.g, col.b, col.a);

        sgl_v2f_t2f(x, y, u0, v0);
        sgl_v2f_t2f(x + w, y, u1, v0);
        sgl_v2f_t2f(x + w, y + h, u1, v1);
        sgl_v2f_t2f(x, y + h, u0, v1);

        sgl_end();
        sgl_disable_texture();
        sgl_load_default_pipeline();
    }

    void moveFrom(Texture&& other) {
        image_ = other.image_;
        view_ = other.view_;
        attachmentView_ = other.attachmentView_;
        sampler_ = other.sampler_;
        width_ = other.width_;
        height_ = other.height_;
        channels_ = other.channels_;
        sampleCount_ = other.sampleCount_;
        allocated_ = other.allocated_;
        usage_ = other.usage_;
        lastUpdateFrame_ = other.lastUpdateFrame_;
        pixelFormat_ = other.pixelFormat_;
        minFilter_ = other.minFilter_;
        magFilter_ = other.magFilter_;
        wrapU_ = other.wrapU_;
        wrapV_ = other.wrapV_;

        other.image_ = {};
        other.view_ = {};
        other.attachmentView_ = {};
        other.sampler_ = {};
        other.width_ = 0;
        other.height_ = 0;
        other.channels_ = 0;
        other.sampleCount_ = 1;
        other.allocated_ = false;
        other.pixelFormat_ = SG_PIXELFORMAT_NONE;
    }
};

} // namespace trussc
