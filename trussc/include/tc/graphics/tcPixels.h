#pragma once

// =============================================================================
// tcPixels.h - CPU pixel buffer management
// =============================================================================

// This file is included from TrussC.h

#include <filesystem>
#include "stb/stb_image.h"
#include "stb/stb_image_write.h"

namespace trussc {

namespace fs = std::filesystem;

// Pixel data format
enum class PixelFormat { U8, F32 };

// ---------------------------------------------------------------------------
// Pixels class - Manages CPU-side pixel data
// ---------------------------------------------------------------------------
class Pixels {
public:
    Pixels() = default;
    ~Pixels() { clear(); }

    // Copy prohibited
    Pixels(const Pixels&) = delete;
    Pixels& operator=(const Pixels&) = delete;

    // Move support
    Pixels(Pixels&& other) noexcept {
        moveFrom(std::move(other));
    }

    Pixels& operator=(Pixels&& other) noexcept {
        if (this != &other) {
            clear();
            moveFrom(std::move(other));
        }
        return *this;
    }

    // === Allocation/Deallocation ===

    // Allocate empty pixel buffer
    void allocate(int width, int height, int channels = 4, PixelFormat format = PixelFormat::U8) {
        clear();

        width_ = width;
        height_ = height;
        channels_ = channels;
        format_ = format;

        size_t count = (size_t)width_ * height_ * channels_;
        if (format_ == PixelFormat::F32) {
            data_ = new float[count]();
        } else {
            data_ = new unsigned char[count]();
        }
        allocated_ = true;
    }

    // Release resources
    void clear() {
        if (data_) {
            if (format_ == PixelFormat::F32) {
                delete[] static_cast<float*>(data_);
            } else {
                delete[] static_cast<unsigned char*>(data_);
            }
            data_ = nullptr;
        }
        width_ = 0;
        height_ = 0;
        channels_ = 0;
        format_ = PixelFormat::U8;
        allocated_ = false;
    }

    // === State ===

    bool isAllocated() const { return allocated_; }
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }
    int getChannels() const { return channels_; }
    PixelFormat getFormat() const { return format_; }
    bool isFloat() const { return format_ == PixelFormat::F32; }

    size_t getTotalBytes() const {
        size_t count = (size_t)width_ * height_ * channels_;
        return (format_ == PixelFormat::F32) ? count * sizeof(float) : count;
    }

    // === Pixel data access ===

    // U8 access (backward compatible)
    unsigned char* getData() { return static_cast<unsigned char*>(data_); }
    const unsigned char* getData() const { return static_cast<const unsigned char*>(data_); }

    // F32 access
    float* getDataF32() { return static_cast<float*>(data_); }
    const float* getDataF32() const { return static_cast<const float*>(data_); }

    // Raw void pointer
    void* getDataVoid() { return data_; }
    const void* getDataVoid() const { return data_; }

    // Get pixel color at specified coordinates
    Color getColor(int x, int y) const {
        if (!allocated_ || !data_ || x < 0 || x >= width_ || y < 0 || y >= height_) {
            return Color(0, 0, 0, 0);
        }

        int index = (y * width_ + x) * channels_;

        if (format_ == PixelFormat::F32) {
            const float* fd = static_cast<const float*>(data_);
            if (channels_ >= 3) {
                float a = (channels_ == 4) ? fd[index + 3] : 1.0f;
                return Color(fd[index], fd[index + 1], fd[index + 2], a);
            } else {
                return Color(fd[index], fd[index], fd[index], 1.0f);
            }
        }

        const unsigned char* ud = static_cast<const unsigned char*>(data_);
        if (channels_ >= 3) {
            float r = ud[index] / 255.0f;
            float g = ud[index + 1] / 255.0f;
            float b = ud[index + 2] / 255.0f;
            float a = (channels_ == 4) ? ud[index + 3] / 255.0f : 1.0f;
            return Color(r, g, b, a);
        } else {
            // Grayscale
            float gray = ud[index] / 255.0f;
            return Color(gray, gray, gray, 1.0f);
        }
    }

    // Set pixel color at specified coordinates
    void setColor(int x, int y, const Color& c) {
        if (!allocated_ || !data_ || x < 0 || x >= width_ || y < 0 || y >= height_) {
            return;
        }

        int index = (y * width_ + x) * channels_;

        if (format_ == PixelFormat::F32) {
            float* fd = static_cast<float*>(data_);
            if (channels_ >= 3) {
                fd[index] = c.r;
                fd[index + 1] = c.g;
                fd[index + 2] = c.b;
                if (channels_ == 4) fd[index + 3] = c.a;
            } else {
                fd[index] = 0.299f * c.r + 0.587f * c.g + 0.114f * c.b;
            }
            return;
        }

        unsigned char* ud = static_cast<unsigned char*>(data_);
        if (channels_ >= 3) {
            ud[index] = static_cast<unsigned char>(c.r * 255.0f);
            ud[index + 1] = static_cast<unsigned char>(c.g * 255.0f);
            ud[index + 2] = static_cast<unsigned char>(c.b * 255.0f);
            if (channels_ == 4) {
                ud[index + 3] = static_cast<unsigned char>(c.a * 255.0f);
            }
        } else {
            // Grayscale (convert by luminance)
            float gray = 0.299f * c.r + 0.587f * c.g + 0.114f * c.b;
            ud[index] = static_cast<unsigned char>(gray * 255.0f);
        }
    }

    // === Bulk operations ===

    // Copy from external U8 data
    void setFromPixels(const unsigned char* srcData, int width, int height, int channels) {
        allocate(width, height, channels, PixelFormat::U8);
        memcpy(data_, srcData, getTotalBytes());
    }

    // Copy from external F32 data
    void setFromFloats(const float* srcData, int width, int height, int channels) {
        allocate(width, height, channels, PixelFormat::F32);
        memcpy(data_, srcData, getTotalBytes());
    }

    // Copy to external buffer
    void copyTo(unsigned char* dst) const {
        if (allocated_ && data_ && dst) {
            memcpy(dst, data_, getTotalBytes());
        }
    }

    // === File I/O ===

    // Load from file
    bool load(const fs::path& path) {
        clear();

        int w, h, channels;
        unsigned char* loaded = stbi_load(path.string().c_str(), &w, &h, &channels, 4);
        if (!loaded) {
            return false;
        }

        width_ = w;
        height_ = h;
        channels_ = 4;  // Always load as RGBA
        format_ = PixelFormat::U8;

        size_t size = width_ * height_ * channels_;
        data_ = new unsigned char[size];
        memcpy(data_, loaded, size);
        stbi_image_free(loaded);

        allocated_ = true;
        return true;
    }

    // Load from memory
    bool loadFromMemory(const unsigned char* buffer, int len) {
        clear();

        int w, h, channels;
        unsigned char* loaded = stbi_load_from_memory(buffer, len, &w, &h, &channels, 4);
        if (!loaded) {
            return false;
        }

        width_ = w;
        height_ = h;
        channels_ = 4;
        format_ = PixelFormat::U8;

        size_t size = width_ * height_ * channels_;
        data_ = new unsigned char[size];
        memcpy(data_, loaded, size);
        stbi_image_free(loaded);

        allocated_ = true;
        return true;
    }

    // Save to file (implemented in tcPixels.cpp for dataPath support)
    bool save(const fs::path& path) const;

private:
    void* data_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    int channels_ = 0;
    PixelFormat format_ = PixelFormat::U8;
    bool allocated_ = false;

    void moveFrom(Pixels&& other) {
        data_ = other.data_;
        width_ = other.width_;
        height_ = other.height_;
        channels_ = other.channels_;
        format_ = other.format_;
        allocated_ = other.allocated_;

        other.data_ = nullptr;
        other.width_ = 0;
        other.height_ = 0;
        other.channels_ = 0;
        other.format_ = PixelFormat::U8;
        other.allocated_ = false;
    }
};

} // namespace trussc
