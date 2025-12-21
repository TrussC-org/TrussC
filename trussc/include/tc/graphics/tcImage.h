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
    {
        other.dirty_ = false;
    }

    Image& operator=(Image&& other) noexcept {
        if (this != &other) {
            pixels_ = std::move(other.pixels_);
            texture_ = std::move(other.texture_);
            dirty_ = other.dirty_;
            other.dirty_ = false;
        }
        return *this;
    }

    // === File I/O ===

    // Load image from file
    bool load(const fs::path& path) {
        clear();

        if (!pixels_.load(path)) {
            return false;
        }

        // Create immutable texture
        texture_.allocate(pixels_, TextureUsage::Immutable);
        return true;
    }

    // Load image from memory
    bool loadFromMemory(const unsigned char* buffer, int len) {
        clear();

        if (!pixels_.loadFromMemory(buffer, len)) {
            return false;
        }

        texture_.allocate(pixels_, TextureUsage::Immutable);
        return true;
    }

    // Save image (override of HasTexture::save())
    // Image has pixels, so save directly (no need to read back from texture)
    bool save(const fs::path& path) const override {
        return pixels_.save(path);
    }

    // === Allocation / Deallocation ===

    // Allocate empty image (for dynamic updates)
    void allocate(int width, int height, int channels = 4) {
        clear();
        pixels_.allocate(width, height, channels);
        // Create dynamic texture (can be updated later)
        texture_.allocate(pixels_, TextureUsage::Dynamic);
    }

    // Release resources
    void clear() {
        pixels_.clear();
        texture_.clear();
        dirty_ = false;
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
    Pixels pixels_;
    Texture texture_;
    bool dirty_ = false;
};

} // namespace trussc
