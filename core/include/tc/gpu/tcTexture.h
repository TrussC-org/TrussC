#pragma once

// =============================================================================
// tcTexture.h - GPU texture management
// =============================================================================

// This file is included from TrussC.h
// to access sokol and internal namespace variables

#include "../utils/tcAnnotations.h"

namespace trussc {

// Pixel format for Texture/FBO allocation
enum class TextureFormat {
    RGBA8,       // 4ch, 8-bit/ch (default)
    RGBA16F,     // 4ch, 16-bit float/ch
    RGBA32F,     // 4ch, 32-bit float/ch
    R8,          // 1ch, 8-bit
    R16F,        // 1ch, 16-bit float
    R32F,        // 1ch, 32-bit float
    RG8,         // 2ch, 8-bit/ch
    RG16F,       // 2ch, 16-bit float/ch
    RG32F,       // 2ch, 32-bit float/ch
    BGRA8,       // 4ch, 8-bit/ch, B-G-R-A byte order (swapchain / Syphon / video interop)
    RGBA16,      // 4ch, 16-bit unorm/ch (high-precision integer; texture-sharing interop)
};

// Convert TextureFormat to sokol format.
// Public: app code interfacing with sokol/shaders legitimately needs this
// (e.g. setColorFormat); it is NOT a framework-private helper.
inline sg_pixel_format toSokolFormat(TextureFormat fmt) {
    switch (fmt) {
        case TextureFormat::RGBA8:   return SG_PIXELFORMAT_RGBA8;
        case TextureFormat::RGBA16F: return SG_PIXELFORMAT_RGBA16F;
        case TextureFormat::RGBA32F: return SG_PIXELFORMAT_RGBA32F;
        case TextureFormat::R8:      return SG_PIXELFORMAT_R8;
        case TextureFormat::R16F:    return SG_PIXELFORMAT_R16F;
        case TextureFormat::R32F:    return SG_PIXELFORMAT_R32F;
        case TextureFormat::RG8:     return SG_PIXELFORMAT_RG8;
        case TextureFormat::RG16F:   return SG_PIXELFORMAT_RG16F;
        case TextureFormat::RG32F:   return SG_PIXELFORMAT_RG32F;
        case TextureFormat::BGRA8:   return SG_PIXELFORMAT_BGRA8;
        case TextureFormat::RGBA16:  return SG_PIXELFORMAT_RGBA16;
        default:                   return SG_PIXELFORMAT_RGBA8;
    }
}

// Number of color channels
inline int channelCount(TextureFormat fmt) {
    switch (fmt) {
        case TextureFormat::R8:
        case TextureFormat::R16F:
        case TextureFormat::R32F:    return 1;
        case TextureFormat::RG8:
        case TextureFormat::RG16F:
        case TextureFormat::RG32F:   return 2;
        case TextureFormat::BGRA8:   return 4;
        case TextureFormat::RGBA16:  return 4;
        default:                   return 4;
    }
}

// Bytes per pixel
inline int bytesPerPixel(TextureFormat fmt) {
    switch (fmt) {
        case TextureFormat::R8:      return 1;
        case TextureFormat::RG8:     return 2;
        case TextureFormat::RGBA8:   return 4;
        case TextureFormat::R16F:    return 2;
        case TextureFormat::RG16F:   return 4;
        case TextureFormat::RGBA16F: return 8;
        case TextureFormat::R32F:    return 4;
        case TextureFormat::RG32F:   return 8;
        case TextureFormat::RGBA32F: return 16;
        case TextureFormat::BGRA8:   return 4;
        case TextureFormat::RGBA16:  return 8;
        default:                   return 4;
    }
}

// Whether the format uses floating-point components
inline bool isFloatFormat(TextureFormat fmt) {
    switch (fmt) {
        case TextureFormat::RGBA16F:
        case TextureFormat::RGBA32F:
        case TextureFormat::R16F:
        case TextureFormat::R32F:
        case TextureFormat::RG16F:
        case TextureFormat::RG32F:   return true;
        default:                   return false;
    }
}

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

    // Allocate empty texture (channel-based, backward compatible)
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

    // Allocate empty texture with explicit pixel format.
    //
    // `mipLevels` > 1 allocates a mip chain. With usage = RenderTarget this
    // lets each mip level be used as a render attachment (see
    // `getAttachmentViewForMip(level)`); the sampler is automatically set to
    // trilinear filtering. mipLevels = 1 keeps the historical behavior.
    void allocate(int width, int height, TextureFormat format,
                  TextureUsage usage = TextureUsage::Immutable,
                  int sampleCount = 1,
                  int mipLevels = 1) {
        clear();

        width_ = width;
        height_ = height;
        channels_ = channelCount(format);
        usage_ = usage;
        sampleCount_ = sampleCount;
        numMipLevels_ = mipLevels < 1 ? 1 : mipLevels;
        mipmapped_ = numMipLevels_ > 1;
        pixelFormat_ = toSokolFormat(format);

        createResources(nullptr);
    }

    // -------------------------------------------------------------------------
    // Cubemap allocation
    // -------------------------------------------------------------------------
    //
    // A cubemap texture has 6 square faces (+X, -X, +Y, -Y, +Z, -Z) sampled
    // by a 3D direction vector in the shader. Used for skyboxes, environment
    // probes, and image-based lighting.
    //
    // `mipLevels` > 1 allocates a mip chain (rounded-down halving); useful
    // for prefiltered environment maps.
    //
    // Allocation without initial data; use uploadCubemapFace() (Dynamic)
    // or render into each face via getCubemapFaceAttachmentView() (RenderTarget).

    void allocateCubemap(int sideSize, TextureFormat format,
                         TextureUsage usage = TextureUsage::RenderTarget,
                         int mipLevels = 1) {
        clear();

        width_ = sideSize;
        height_ = sideSize;
        channels_ = channelCount(format);
        usage_ = usage;
        sampleCount_ = 1;
        pixelFormat_ = toSokolFormat(format);
        isCubemap_ = true;
        numMipLevels_ = (mipLevels < 1) ? 1 : mipLevels;
        mipmapped_ = numMipLevels_ > 1;
        // Cubemaps default to ClampToEdge wrap (any other choice creates seams).
        wrapU_ = TextureWrap::ClampToEdge;
        wrapV_ = TextureWrap::ClampToEdge;

        createCubemapResources();
    }

    // Upload data for one face at one mip level. Requires Dynamic usage.
    // `data` is a single-face buffer (sideSize(mip) * sideSize(mip) * bpp bytes).
    void uploadCubemapFace(int face, int mipLevel, const void* data, size_t dataSize) {
        if (!allocated_ || !isCubemap_) return;
        if (face < 0 || face >= 6) return;
        if (mipLevel < 0 || mipLevel >= numMipLevels_) return;
        if (usage_ == TextureUsage::Immutable) return;

        // sokol's sg_update_image for cubemap expects all 6 faces of one mip
        // level concatenated. For single-face updates we stage a buffer and
        // fill the remaining 5 faces with zeros. This is wasteful for rapid
        // updates but fine for one-off IBL-style baking.
        int mipW = mipDim(width_, mipLevel);
        int mipH = mipDim(height_, mipLevel);
        size_t bpp = computeBytesPerPixel();
        size_t faceSize = (size_t)mipW * mipH * bpp;
        if (dataSize != faceSize) {
            logWarning() << "[Texture] uploadCubemapFace: data size mismatch, expected "
                         << faceSize << " got " << dataSize;
            return;
        }
        std::vector<uint8_t> buffer(faceSize * 6, 0);
        std::memcpy(buffer.data() + face * faceSize, data, faceSize);

        sg_image_data img_data = {};
        for (int m = 0; m < numMipLevels_; ++m) {
            // Only fill the target mip; other mips get null (ignored by update).
            if (m == mipLevel) {
                img_data.mip_levels[m].ptr = buffer.data();
                img_data.mip_levels[m].size = buffer.size();
            }
        }
        sg_update_image(image_, &img_data);
    }

    // Upload all 6 faces for one mip level at once. `data` points to 6 face
    // buffers laid out consecutively (face 0..5, each sideSize(mip)^2 * bpp).
    void uploadCubemapMip(int mipLevel, const void* data, size_t dataSize) {
        if (!allocated_ || !isCubemap_) return;
        if (mipLevel < 0 || mipLevel >= numMipLevels_) return;
        if (usage_ == TextureUsage::Immutable) return;

        int mipW = mipDim(width_, mipLevel);
        int mipH = mipDim(height_, mipLevel);
        size_t bpp = computeBytesPerPixel();
        size_t expected = (size_t)mipW * mipH * bpp * 6;
        if (dataSize != expected) {
            logWarning() << "[Texture] uploadCubemapMip: data size mismatch, expected "
                         << expected << " got " << dataSize;
            return;
        }

        sg_image_data img_data = {};
        img_data.mip_levels[mipLevel].ptr = data;
        img_data.mip_levels[mipLevel].size = dataSize;
        sg_update_image(image_, &img_data);
    }

    // Get (and lazily create) an attachment view targeting one (face, mip) of
    // this cubemap. Used as color_attachment in sg_pass when rendering into
    // cube faces for IBL generation.
    sg_view getCubemapFaceAttachmentView(int face, int mipLevel) {
        if (!allocated_ || !isCubemap_) return sg_view{};
        if (face < 0 || face >= 6) return sg_view{};
        if (mipLevel < 0 || mipLevel >= numMipLevels_) return sg_view{};

        int idx = face * numMipLevels_ + mipLevel;
        if ((int)cubeFaceAttachmentViews_.size() <= idx) {
            cubeFaceAttachmentViews_.resize(idx + 1, sg_view{});
        }
        if (cubeFaceAttachmentViews_[idx].id == 0) {
            sg_view_desc att_desc = {};
            att_desc.color_attachment.image = image_;
            att_desc.color_attachment.mip_level = mipLevel;
            att_desc.color_attachment.slice = face;
            cubeFaceAttachmentViews_[idx] = sg_make_view(&att_desc);
        }
        return cubeFaceAttachmentViews_[idx];
    }

    bool isCubemap() const { return isCubemap_; }
    int getNumMipLevels() const { return numMipLevels_; }

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

        // Release old resources (deferred: draws recorded this frame may
        // still reference them — destroyed after end-of-frame flush)
        internal::deferGpuDestroy(sampler_);
        internal::deferGpuDestroy(view_);
        internal::deferGpuDestroy(image_);

        // Recreate with new data
        createCompressedResources(data, dataSize);
    }

    bool isCompressed() const {
        // Compressed formats (BC/ETC/ASTC) start at BC1_RGBA in sokol's enum
        return pixelFormat_ >= SG_PIXELFORMAT_BC1_RGBA;
    }

    // Allocate texture from Pixels (auto-detects F32 → RGBA32F).
    //
    // `mipmaps=true` builds a full mip chain. Supported for Immutable
    // (chain is generated from initial pixels at allocation) and Dynamic
    // (chain is regenerated each time `loadData(pixels)` is called).
    // Stream usage keeps mipmaps off — the per-frame upload cost would
    // dominate any sampling benefit.
    void allocate(const Pixels& pixels, TextureUsage usage = TextureUsage::Immutable,
                  bool mipmaps = false) {
        clear();

        width_ = pixels.getWidth();
        height_ = pixels.getHeight();
        channels_ = pixels.getChannels();
        usage_ = usage;
        const bool mipmapSupported = (usage == TextureUsage::Immutable ||
                                      usage == TextureUsage::Dynamic);
        mipmapped_ = mipmaps && mipmapSupported;
        if (mipmapped_) {
            numMipLevels_ = 1 + (int)std::floor(std::log2((float)std::max(width_, height_)));
            if (numMipLevels_ > SG_MAX_MIPMAPS) numMipLevels_ = SG_MAX_MIPMAPS;
            if (numMipLevels_ < 1) numMipLevels_ = 1;
        } else {
            numMipLevels_ = 1;
        }

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
            // Deferred destroy: draws recorded this frame (sokol_gl quads,
            // deferred PBR bindings) may still reference these handles.
            internal::deferGpuDestroy(sampler_);
            internal::deferGpuDestroy(view_);
            internal::deferGpuDestroy(attachmentView_);
            for (sg_view v : cubeFaceAttachmentViews_) {
                internal::deferGpuDestroy(v);
            }
            cubeFaceAttachmentViews_.clear();
            for (sg_view v : mipAttachmentViews_) {
                internal::deferGpuDestroy(v);
            }
            mipAttachmentViews_.clear();
            for (sg_view v : mipSamplingViews_) {
                internal::deferGpuDestroy(v);
            }
            mipSamplingViews_.clear();
            internal::deferGpuDestroy(image_);
            allocated_ = false;
        }
        width_ = 0;
        height_ = 0;
        channels_ = 0;
        mipmapped_ = false;
        isCubemap_ = false;
        numMipLevels_ = 1;
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
    sg_pixel_format getPixelFormat() const { return pixelFormat_; }

    // === Data update (except Immutable) ===

    void loadData(const Pixels& pixels) {
        // Dynamic + mipmaps: D3D11 only allows a single mip level on
        // DYNAMIC-usage textures, so we can't use sg_update_image with a
        // full mip chain. Instead we destroy the current image and recreate
        // it as immutable with all mip levels baked in from CPU data.
        // The view and sampler are also rebuilt so handles stay consistent.
        if (mipmapped_ && allocated_ && usage_ != TextureUsage::Immutable) {
            if (pixels.getWidth() != width_ ||
                pixels.getHeight() != height_ ||
                pixels.getChannels() != channels_) return;

            uint64_t currentFrame = sapp_frame_count();
            if (lastUpdateFrame_ == currentFrame) {
                logWarning() << "[Texture] loadData() called twice in same frame, skipped";
                return;
            }
            lastUpdateFrame_ = currentFrame;

            const size_t bpp = computeBytesPerPixel();

            sg_image_desc img_desc = {};
            img_desc.width  = width_;
            img_desc.height = height_;
            img_desc.pixel_format = (pixelFormat_ != SG_PIXELFORMAT_NONE)
                                  ? pixelFormat_
                                  : ((channels_ == 4) ? SG_PIXELFORMAT_RGBA8 : SG_PIXELFORMAT_R8);
            img_desc.num_mipmaps  = numMipLevels_;

            img_desc.data.mip_levels[0].ptr  = pixels.getDataVoid();
            img_desc.data.mip_levels[0].size = (size_t)width_ * height_ * bpp;

            // Lower mip levels: chain Pixels::halve() starting from a clone of
            // the source. halve() is gamma-correct for U8 buffers (matches the
            // FBO mipmap convention); F32 buffers are averaged directly.
            // mipChain entries keep the data alive for the sg_make_image call
            // below — the pointers we hand to sg_image_data must remain valid
            // until sg_make_image returns.
            std::vector<Pixels> mipChain;
            mipChain.reserve(numMipLevels_ > 1 ? numMipLevels_ - 1 : 0);
            Pixels current = pixels.clone();
            for (int level = 1; level < numMipLevels_; level++) {
                current.halve();
                mipChain.emplace_back(current.clone());
                img_desc.data.mip_levels[level].ptr  = mipChain.back().getDataVoid();
                img_desc.data.mip_levels[level].size = mipChain.back().getTotalBytes();
            }

            // Tear down old GPU resources (deferred — a draw recorded earlier
            // this frame may still reference the old view) and rebuild.
            internal::deferGpuDestroy(view_);
            internal::deferGpuDestroy(image_);

            image_ = sg_make_image(&img_desc);

            sg_view_desc view_desc = {};
            view_desc.texture.image = image_;
            view_ = sg_make_view(&view_desc);
            return;
        }
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

        size_t dataSize = (size_t)width * height * computeBytesPerPixel();

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

    // Premultiplied alpha flag (set automatically for FBO color textures)
    void setPremultipliedAlpha(bool v) { premultipliedAlpha_ = v; }
    bool isPremultipliedAlpha() const { return premultipliedAlpha_; }

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

    // Draw with vertical flip (v swapped). Used by Fbo on GL backends
    // where framebuffer textures are stored bottom-to-top in memory.
    void drawFlippedY(float x, float y, float w, float h) const {
        if (allocated_) {
            drawInternal(x, y, w, h, 0.0f, 1.0f, 1.0f, 0.0f);
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

    // Per-mip color attachment view (only populated when allocated with
    // mipLevels > 1 and usage = RenderTarget). For mipLevels == 1 the
    // single `attachmentView_` is returned regardless of `level`.
    sg_view getAttachmentViewForMip(int level) const {
        if ((int)mipAttachmentViews_.size() > level && level >= 0) {
            return mipAttachmentViews_[level];
        }
        return attachmentView_;
    }

    // Per-mip texture view for sampling a single mip level. Needed when
    // another mip of the same image is bound as a color attachment — using
    // the full-chain view would trigger a validation error on D3D11/WebGPU.
    sg_view getViewForMip(int level) const {
        if ((int)mipSamplingViews_.size() > level && level >= 0) {
            return mipSamplingViews_[level];
        }
        return view_;
    }

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
    bool mipmapped_ = false;
    TextureUsage usage_ = TextureUsage::Immutable;
    uint64_t lastUpdateFrame_ = UINT64_MAX;  // Last updated frame
    sg_pixel_format pixelFormat_ = SG_PIXELFORMAT_NONE;

    TextureFilter minFilter_ = TextureFilter::Linear;
    TextureFilter magFilter_ = TextureFilter::Linear;
    TextureWrap wrapU_ = TextureWrap::ClampToEdge;
    TextureWrap wrapV_ = TextureWrap::ClampToEdge;
    bool premultipliedAlpha_ = false;

    // Cubemap state
    bool isCubemap_ = false;
    int numMipLevels_ = 1;
    std::vector<sg_view> cubeFaceAttachmentViews_;
    // Per-mip color attachment views for 2D RenderTarget with mipLevels > 1.
    // Empty for mipLevels == 1 (use `attachmentView_` directly).
    std::vector<sg_view> mipAttachmentViews_;
    // Per-mip sampling views (single-level texture views). Used by Fbo
    // mipmap generation to avoid binding the full-chain view while another
    // mip of the same image is a color attachment.
    std::vector<sg_view> mipSamplingViews_;

    static int mipDim(int base, int mip) {
        int d = base >> mip;
        return d < 1 ? 1 : d;
    }

    // Bytes per pixel for current pixelFormat_ / channels_
    size_t computeBytesPerPixel() const {
        switch (pixelFormat_) {
            case SG_PIXELFORMAT_R8:       return 1;
            case SG_PIXELFORMAT_RG8:      return 2;
            case SG_PIXELFORMAT_RGBA8:    return 4;
            case SG_PIXELFORMAT_R16F:     return 2;
            case SG_PIXELFORMAT_RG16F:    return 4;
            case SG_PIXELFORMAT_RGBA16F:  return 8;
            case SG_PIXELFORMAT_R32F:     return 4;
            case SG_PIXELFORMAT_RG32F:    return 8;
            case SG_PIXELFORMAT_RGBA32F:  return 16;
            default:
                // Fallback for NONE (legacy path, based on channels_)
                return (size_t)channels_;
        }
    }

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

        // Compute data size based on pixel format
        size_t bpp = computeBytesPerPixel();
        bool isFloat = (pixelFormat_ == SG_PIXELFORMAT_RGBA32F);
        size_t dataSize = (size_t)width_ * height_ * bpp;

        // Storage for mip chain Pixels (kept alive until sg_make_image
        // copies them). Used by the Immutable + auto-mipmap path below.
        std::vector<Pixels> mipChain;

        switch (usage_) {
            case TextureUsage::Immutable:
                if (initialData) {
                    img_desc.data.mip_levels[0].ptr = initialData;
                    img_desc.data.mip_levels[0].size = dataSize;

                    // Generate mip chain via chained Pixels::halve().
                    // Pixels::halve is gamma-correct for U8 (matches the FBO
                    // mipmap downsample) and direct-average for F32 (assumed
                    // already linear). Restricted to channels_ == 4 same as
                    // before, since Pixels currently only handles 4-channel
                    // RGBA / RGBAF32 here.
                    if (mipmapped_ && channels_ == 4) {
                        int numLevels = 1 + (int)std::floor(std::log2((float)std::max(width_, height_)));
                        if (numLevels > SG_MAX_MIPMAPS) numLevels = SG_MAX_MIPMAPS;
                        img_desc.num_mipmaps = numLevels;

                        Pixels current;
                        if (isFloat) {
                            current.setFromFloats(static_cast<const float*>(initialData),
                                                  width_, height_, channels_);
                        } else {
                            current.setFromPixels(static_cast<const unsigned char*>(initialData),
                                                  width_, height_, channels_);
                        }
                        mipChain.reserve(numLevels - 1);
                        for (int level = 1; level < numLevels; level++) {
                            current.halve();
                            mipChain.emplace_back(current.clone());
                            img_desc.data.mip_levels[level].ptr  = mipChain.back().getDataVoid();
                            img_desc.data.mip_levels[level].size = mipChain.back().getTotalBytes();
                        }
                    }
                }
                break;
            case TextureUsage::Dynamic:
                // D3D11's DYNAMIC usage only supports a single mip level.
                // When mipmaps are requested we still allocate the base
                // image as a plain dynamic texture (1 mip); the full mip
                // chain is built on each loadData() call by recreating the
                // image as immutable with all levels baked in.
                img_desc.usage.dynamic_update = true;
                break;
            case TextureUsage::Stream:
                img_desc.usage.stream_update = true;
                break;
            case TextureUsage::RenderTarget:
                img_desc.usage.color_attachment = true;
                img_desc.usage.resolve_attachment = true;  // Can also be used as MSAA resolve target
                img_desc.sample_count = sampleCount_;
                img_desc.num_mipmaps = numMipLevels_;
                break;
        }

        image_ = sg_make_image(&img_desc);
        if (sg_query_image_state(image_) == SG_RESOURCESTATE_FAILED) {
            logError("Texture") << "GPU texture allocation failed ("
                << width_ << "x" << height_ << ", format "
                << (int)img_desc.pixel_format << ")";
        }

        // Create texture view (for sampling)
        sg_view_desc view_desc = {};
        view_desc.texture.image = image_;
        view_ = sg_make_view(&view_desc);

        // Create attachment view for RenderTarget. `attachmentView_` always
        // targets mip 0 (preserves the existing single-attachment API).
        // For mipLevels > 1, also build a per-mip attachment view list so
        // callers can render into each level individually (see
        // `getAttachmentViewForMip()`), used by Fbo to generate mipmaps.
        if (usage_ == TextureUsage::RenderTarget) {
            sg_view_desc att_desc = {};
            att_desc.color_attachment.image = image_;
            attachmentView_ = sg_make_view(&att_desc);
            if (numMipLevels_ > 1) {
                // Independent views per mip level (mip 0 is also a fresh view,
                // not aliased to `attachmentView_`, so clear() can destroy
                // both without a double-free).
                mipAttachmentViews_.resize(numMipLevels_);
                for (int level = 0; level < numMipLevels_; level++) {
                    sg_view_desc mip_att_desc = {};
                    mip_att_desc.color_attachment.image = image_;
                    mip_att_desc.color_attachment.mip_level = level;
                    mipAttachmentViews_[level] = sg_make_view(&mip_att_desc);
                }
                // Single-level sampling views for mipmap generation passes
                mipSamplingViews_.resize(numMipLevels_);
                for (int level = 0; level < numMipLevels_; level++) {
                    sg_view_desc mip_tex_desc = {};
                    mip_tex_desc.texture.image = image_;
                    mip_tex_desc.texture.mip_levels.base = level;
                    mip_tex_desc.texture.mip_levels.count = 1;
                    mipSamplingViews_[level] = sg_make_view(&mip_tex_desc);
                }
            }
        }

        // Create sampler
        createSampler();

        allocated_ = true;
    }

    void createCubemapResources() {
        sg_image_desc img_desc = {};
        img_desc.type = SG_IMAGETYPE_CUBE;
        img_desc.width = width_;
        img_desc.height = height_;
        img_desc.num_mipmaps = numMipLevels_;
        img_desc.pixel_format = pixelFormat_;

        switch (usage_) {
            case TextureUsage::Immutable:
                // Immutable cubemap not supported yet (would need initial data
                // for all 6 faces and all mips). Use Dynamic + uploadCubemapFace
                // or RenderTarget + face attachment views instead.
                logWarning() << "[Texture] allocateCubemap Immutable not supported; using Dynamic";
                img_desc.usage.dynamic_update = true;
                break;
            case TextureUsage::Dynamic:
                img_desc.usage.dynamic_update = true;
                break;
            case TextureUsage::Stream:
                img_desc.usage.stream_update = true;
                break;
            case TextureUsage::RenderTarget:
                img_desc.usage.color_attachment = true;
                img_desc.sample_count = 1;  // MSAA cube attachments add complexity we skip here
                break;
        }

        image_ = sg_make_image(&img_desc);

        // Sampling view covers the whole cube (all faces, all mips).
        sg_view_desc view_desc = {};
        view_desc.texture.image = image_;
        view_ = sg_make_view(&view_desc);

        // Per-face attachment views are created lazily in
        // getCubemapFaceAttachmentView() only for RenderTarget cubemaps.

        createSampler();
        allocated_ = true;
    }

    void createSampler() {
        sg_sampler_desc smp_desc = {};

        // Filter settings
        smp_desc.min_filter = (minFilter_ == TextureFilter::Nearest) ? SG_FILTER_NEAREST : SG_FILTER_LINEAR;
        smp_desc.mag_filter = (magFilter_ == TextureFilter::Nearest) ? SG_FILTER_NEAREST : SG_FILTER_LINEAR;

        // Trilinear filtering for mipmapped textures
        if (mipmapped_) {
            smp_desc.mipmap_filter = SG_FILTER_LINEAR;
        }

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
        // Deferred: a draw recorded this frame may still bind the old sampler
        internal::deferGpuDestroy(sampler_);
        createSampler();
    }

    void drawInternal(float x, float y, float w, float h,
                      float u0, float v0, float u1, float v1) const {
        // Blend pipeline for the active target. An explicit non-Alpha blend mode
        // set via setBlendMode() takes priority — texture draws (including
        // Fbo::draw) must honor it, e.g. compositing an Fbo with Subtract.
        // Under the default Alpha mode, behavior is preserved: premultiplied
        // sources use the premult pipeline on the swapchain, FBO passes use
        // Fill2D even for premultiplied sources, as before.
        BlendMode blend = internal::currentWindowContext().currentBlendMode;
        if (blend != BlendMode::Alpha) {
            internal::loadPipeline(internal::active2D(blend));
        } else if (internal::currentWindowContext().inFboPass) {
            internal::loadPipeline(internal::activeFill2D());
        } else if (premultipliedAlpha_) {
            internal::loadPipeline(internal::activePremult());
        } else {
            internal::loadPipeline(internal::activeFill2D());
        }
        sgl_enable_texture();
        sgl_texture(view_, sampler_);

        // Draw with current color
        Color col = getColor();
        sgl_begin_quads();
        sgl_c4f(col.r, col.g, col.b, col.a);

        sgl_v2f_t2f(x, y, u0, v0);
        sgl_v2f_t2f(x + w, y, u1, v0);
        sgl_v2f_t2f(x + w, y + h, u1, v1);
        sgl_v2f_t2f(x, y + h, u0, v1);

        sgl_end();
        sgl_disable_texture();
        internal::restoreCurrentPipeline();
    }

    // Mipmap chain generation lives in Pixels::halve (gamma-correct for U8,
    // direct-average for F32). The old static generateMipLevel() helper that
    // used to live here has been folded into that single implementation.

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
        mipmapped_ = other.mipmapped_;
        numMipLevels_ = other.numMipLevels_;
        mipAttachmentViews_ = std::move(other.mipAttachmentViews_);
        mipSamplingViews_ = std::move(other.mipSamplingViews_);
        usage_ = other.usage_;
        lastUpdateFrame_ = other.lastUpdateFrame_;
        pixelFormat_ = other.pixelFormat_;
        minFilter_ = other.minFilter_;
        magFilter_ = other.magFilter_;
        wrapU_ = other.wrapU_;
        wrapV_ = other.wrapV_;
        premultipliedAlpha_ = other.premultipliedAlpha_;

        other.image_ = {};
        other.view_ = {};
        other.attachmentView_ = {};
        other.sampler_ = {};
        other.width_ = 0;
        other.height_ = 0;
        other.channels_ = 0;
        other.sampleCount_ = 1;
        other.allocated_ = false;
        other.mipmapped_ = false;
        other.numMipLevels_ = 1;
        other.mipAttachmentViews_.clear();
        other.mipSamplingViews_.clear();
        other.pixelFormat_ = SG_PIXELFORMAT_NONE;
    }
};

} // namespace trussc
