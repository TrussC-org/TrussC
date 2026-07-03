#pragma once

// =============================================================================
// tcImage.h - Image loading, drawing, and saving
// =============================================================================

// This file is included from TrussC.h
// Pixels, Texture, HasTexture must be included beforehand

#include <filesystem>

namespace trussc {

namespace fs = std::filesystem;

// Image type
enum class ImageType {
    Color,      // RGBA
    Grayscale   // Grayscale
};

// ---------------------------------------------------------------------------
// Image class - Unified class holding Pixels (CPU) + Texture (GPU)
// ---------------------------------------------------------------------------
class Image : public HasTexture {
public:
    Image() = default;
    ~Image() { clear(); }

    // No copy
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    // Move support
    Image(Image&& other) noexcept
        : pixels_(std::move(other.pixels_))
        , texture_(std::move(other.texture_))
        , dirty_(other.dirty_)
        , mipmaps_(other.mipmaps_)
        , usage_(other.usage_)
    {
        other.dirty_ = false;
        other.mipmaps_ = false;
        other.usage_ = TextureUsage::Immutable;
    }

    Image& operator=(Image&& other) noexcept {
        if (this != &other) {
            pixels_ = std::move(other.pixels_);
            texture_ = std::move(other.texture_);
            dirty_ = other.dirty_;
            mipmaps_ = other.mipmaps_;
            usage_ = other.usage_;
            other.dirty_ = false;
            other.mipmaps_ = false;
            other.usage_ = TextureUsage::Immutable;
        }
        return *this;
    }

    // === File I/O ===

    // Load image from file (relative paths resolved via getDataPath).
    //
    // `mipmaps=true` builds a full mip chain at load time. Recommended when
    // the image will be sampled at varying scales (most commonly mapped
    // onto a 3D surface that moves toward/away from the camera) — without
    // mipmaps, small projected sizes shimmer/moiré. Costs about +33% GPU
    // memory and a one-time CPU box-average to build the chain.
    bool load(const fs::path& path, bool mipmaps = false) {
        clear();

        fs::path resolved = getDataPath(path);   // absolute paths pass through
        if (!pixels_.load(resolved)) {
            return false;
        }

        mipmaps_ = mipmaps;
        usage_ = TextureUsage::Immutable;
        texture_.allocate(pixels_, TextureUsage::Immutable, mipmaps);
        return true;
    }

    // Load image from memory
    bool loadFromMemory(const unsigned char* buffer, int len, bool mipmaps = false) {
        clear();

        if (!pixels_.loadFromMemory(buffer, len)) {
            return false;
        }

        mipmaps_ = mipmaps;
        usage_ = TextureUsage::Immutable;
        texture_.allocate(pixels_, TextureUsage::Immutable, mipmaps);
        return true;
    }

    // Save image (override of HasTexture::save())
    // Image has pixels, so save directly (no need to read back from texture)
    bool save(const fs::path& path) const override {
        return pixels_.save(path);
    }

    // === Allocation / Deallocation ===

    // Allocate empty image (for dynamic updates via setColor + update()).
    //
    // `mipmaps=true` builds a mip chain alongside the Dynamic texture; each
    // subsequent `update()` regenerates the chain CPU-side (2x2 box average)
    // and re-uploads every level. Costs a per-update CPU pass roughly equal
    // to ~1/3 the base level size. Worth it when the image is sampled at
    // varying scales (3D texture mapping, UI scaling) — otherwise leave off.
    void allocate(int width, int height, int channels = 4, bool mipmaps = false) {
        clear();
        pixels_.allocate(width, height, channels);
        mipmaps_ = mipmaps;
        usage_ = TextureUsage::Dynamic;
        // Dynamic so the texture can be re-uploaded with update(). When
        // mipmaps=true, the underlying texture also carries a mip chain.
        texture_.allocate(pixels_, TextureUsage::Dynamic, mipmaps);
    }

    // Release resources
    void clear() {
        pixels_.clear();
        texture_.clear();
        dirty_ = false;
        mipmaps_ = false;
        usage_ = TextureUsage::Immutable;
    }

    // === State ===

    bool isAllocated() const { return pixels_.isAllocated(); }
    int getWidth() const { return pixels_.getWidth(); }
    int getHeight() const { return pixels_.getHeight(); }
    int getChannels() const { return pixels_.getChannels(); }

    // === Pixels access ===

    Pixels& getPixels() { return pixels_; }
    const Pixels& getPixels() const { return pixels_; }

    // Shortcut to raw pointer
    unsigned char* getPixelsData() { return pixels_.getData(); }
    const unsigned char* getPixelsData() const { return pixels_.getData(); }

    // === Pixel operations ===

    Color getColor(int x, int y) const {
        return pixels_.getColor(x, y);
    }

    void setColor(int x, int y, const Color& c) {
        pixels_.setColor(x, y, c);
        dirty_ = true;
    }

    // === Image operations ===
    //
    // These all delegate to the matching Pixels method (which is where the
    // gamma-correct math lives). Because they change pixel data and often
    // image dimensions, the underlying GPU texture is rebuilt at the same
    // time, preserving the usage (Immutable vs Dynamic) and `mipmaps` flag
    // the Image was originally allocated/loaded with.

    // Replace the buffer with its 2x2 box-averaged half. New dimensions are
    // max(width/2, 1) x max(height/2, 1). Useful for cheap downsampling.
    void halve() {
        if (!pixels_.isAllocated()) return;
        pixels_.halve();
        rebuildTexture_();
    }

    // Replace the buffer with a quality-first (newW x newH) resampled
    // version. Each axis is BoxArea on downscale, Catmull-Rom bicubic on
    // upscale; computations run in linear light for U8 buffers.
    void resize(int newW, int newH) {
        if (!pixels_.isAllocated() || newW <= 0 || newH <= 0) return;
        pixels_.resize(newW, newH);
        rebuildTexture_();
    }

    // Replace the buffer with a (w x h) region starting at (x, y).
    // clamp-to-edge for out-of-bounds samples so the destination is always
    // exactly (w x h).
    void crop(int x, int y, int w, int h) {
        if (!pixels_.isAllocated() || w <= 0 || h <= 0) return;
        pixels_.crop(x, y, w, h);
        rebuildTexture_();
    }

    // Flip the image. `horizontal=true` mirrors left↔right, `vertical=true`
    // mirrors top↔bottom. Both true is a 180° rotation. Size is unchanged,
    // but the GPU texture is still pushed so the change is visible after
    // the next draw (no need to call update() separately).
    void mirror(bool horizontal, bool vertical) {
        if (!pixels_.isAllocated() || (!horizontal && !vertical)) return;
        pixels_.mirror(horizontal, vertical);
        rebuildTexture_();
    }
    void mirrorH() { mirror(true, false); }
    void mirrorV() { mirror(false, true); }

    // === Texture update ===

    // Apply pixel changes to texture
    // Note: Due to sokol limitations, can only be called once per frame
    // Calling twice in same frame will ignore the second call (with warning)
    // Call after editing with setColor() or getPixels()
    void update() {
        if (dirty_ && texture_.isAllocated()) {
            texture_.loadData(pixels_);
            dirty_ = false;
        }
    }

    // Manually set dirty flag (when directly editing via getPixels())
    void setDirty() { dirty_ = true; }

    // === HasTexture implementation ===

    Texture& getTexture() override { return texture_; }
    const Texture& getTexture() const override { return texture_; }

    // draw() uses HasTexture default implementation

private:
    // Re-allocate the GPU texture from the current pixels_ contents using the
    // Image's stored usage and mipmaps flag. Called by halve/resize/crop/
    // mirror; works whether the Image was originally Immutable (load) or
    // Dynamic (allocate).
    //
    // Immutable: `Texture::allocate(pixels, Immutable, ...)` uploads the
    //   pixel data as part of allocation, so the texture is in sync with
    //   pixels_ immediately and we can drop the dirty flag.
    // Dynamic:  `Texture::allocate(pixels, Dynamic, ...)` only creates an
    //   empty image; the actual pixel upload still has to go through a
    //   later loadData() call inside a render pass. Keep dirty_=true so
    //   the next update() pushes pixels_ over.
    void rebuildTexture_() {
        texture_.allocate(pixels_, usage_, mipmaps_);
        dirty_ = (usage_ != TextureUsage::Immutable);
    }

    Pixels pixels_;
    Texture texture_;
    bool dirty_ = false;
    bool mipmaps_ = false;
    TextureUsage usage_ = TextureUsage::Immutable;
};

} // namespace trussc
