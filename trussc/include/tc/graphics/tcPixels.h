#pragma once

// =============================================================================
// tcPixels.h - CPU ピクセルバッファ管理
// =============================================================================

// このファイルは TrussC.h からインクルードされる

#include <filesystem>
#include "stb/stb_image.h"
#include "stb/stb_image_write.h"

namespace trussc {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Pixels クラス - CPU 側のピクセルデータを管理
// ---------------------------------------------------------------------------
class Pixels {
public:
    Pixels() = default;
    ~Pixels() { clear(); }

    // コピー禁止
    Pixels(const Pixels&) = delete;
    Pixels& operator=(const Pixels&) = delete;

    // ムーブ対応
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

    // === 確保・解放 ===

    // 空のピクセルバッファを確保
    void allocate(int width, int height, int channels = 4) {
        clear();

        width_ = width;
        height_ = height;
        channels_ = channels;

        size_t size = width_ * height_ * channels_;
        data_ = new unsigned char[size];
        memset(data_, 0, size);
        allocated_ = true;
    }

    // リソースを解放
    void clear() {
        if (data_) {
            delete[] data_;
            data_ = nullptr;
        }
        width_ = 0;
        height_ = 0;
        channels_ = 0;
        allocated_ = false;
    }

    // === 状態 ===

    bool isAllocated() const { return allocated_; }
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }
    int getChannels() const { return channels_; }
    size_t getTotalBytes() const { return width_ * height_ * channels_; }

    // === ピクセルデータアクセス ===

    unsigned char* getData() { return data_; }
    const unsigned char* getData() const { return data_; }

    // 指定座標のピクセル色を取得
    Color getColor(int x, int y) const {
        if (!allocated_ || !data_ || x < 0 || x >= width_ || y < 0 || y >= height_) {
            return Color(0, 0, 0, 0);
        }

        int index = (y * width_ + x) * channels_;
        if (channels_ >= 3) {
            float r = data_[index] / 255.0f;
            float g = data_[index + 1] / 255.0f;
            float b = data_[index + 2] / 255.0f;
            float a = (channels_ == 4) ? data_[index + 3] / 255.0f : 1.0f;
            return Color(r, g, b, a);
        } else {
            // グレースケール
            float gray = data_[index] / 255.0f;
            return Color(gray, gray, gray, 1.0f);
        }
    }

    // 指定座標のピクセル色を設定
    void setColor(int x, int y, const Color& c) {
        if (!allocated_ || !data_ || x < 0 || x >= width_ || y < 0 || y >= height_) {
            return;
        }

        int index = (y * width_ + x) * channels_;
        if (channels_ >= 3) {
            data_[index] = static_cast<unsigned char>(c.r * 255.0f);
            data_[index + 1] = static_cast<unsigned char>(c.g * 255.0f);
            data_[index + 2] = static_cast<unsigned char>(c.b * 255.0f);
            if (channels_ == 4) {
                data_[index + 3] = static_cast<unsigned char>(c.a * 255.0f);
            }
        } else {
            // グレースケール（輝度で変換）
            float gray = 0.299f * c.r + 0.587f * c.g + 0.114f * c.b;
            data_[index] = static_cast<unsigned char>(gray * 255.0f);
        }
    }

    // === バルク操作 ===

    // 外部データからコピー
    void setFromPixels(const unsigned char* srcData, int width, int height, int channels) {
        allocate(width, height, channels);
        memcpy(data_, srcData, getTotalBytes());
    }

    // 外部バッファにコピー
    void copyTo(unsigned char* dst) const {
        if (allocated_ && data_ && dst) {
            memcpy(dst, data_, getTotalBytes());
        }
    }

    // === ファイル I/O ===

    // ファイルから読み込み
    bool load(const fs::path& path) {
        clear();

        int w, h, channels;
        unsigned char* loaded = stbi_load(path.string().c_str(), &w, &h, &channels, 4);
        if (!loaded) {
            return false;
        }

        width_ = w;
        height_ = h;
        channels_ = 4;  // 常に RGBA で読み込み

        size_t size = width_ * height_ * channels_;
        data_ = new unsigned char[size];
        memcpy(data_, loaded, size);
        stbi_image_free(loaded);

        allocated_ = true;
        return true;
    }

    // メモリから読み込み
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

        size_t size = width_ * height_ * channels_;
        data_ = new unsigned char[size];
        memcpy(data_, loaded, size);
        stbi_image_free(loaded);

        allocated_ = true;
        return true;
    }

    // ファイルに保存
    bool save(const fs::path& path) const {
        if (!allocated_ || !data_) return false;

        auto ext = path.extension().string();
        auto pathStr = path.string();
        int result = 0;

        if (ext == ".png" || ext == ".PNG") {
            result = stbi_write_png(pathStr.c_str(), width_, height_, channels_, data_, width_ * channels_);
        } else if (ext == ".jpg" || ext == ".jpeg" || ext == ".JPG" || ext == ".JPEG") {
            result = stbi_write_jpg(pathStr.c_str(), width_, height_, channels_, data_, 90);
        } else if (ext == ".bmp" || ext == ".BMP") {
            result = stbi_write_bmp(pathStr.c_str(), width_, height_, channels_, data_);
        } else {
            // デフォルトは PNG
            result = stbi_write_png(pathStr.c_str(), width_, height_, channels_, data_, width_ * channels_);
        }

        return result != 0;
    }

private:
    unsigned char* data_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    int channels_ = 0;
    bool allocated_ = false;

    void moveFrom(Pixels&& other) {
        data_ = other.data_;
        width_ = other.width_;
        height_ = other.height_;
        channels_ = other.channels_;
        allocated_ = other.allocated_;

        other.data_ = nullptr;
        other.width_ = 0;
        other.height_ = 0;
        other.channels_ = 0;
        other.allocated_ = false;
    }
};

} // namespace trussc
