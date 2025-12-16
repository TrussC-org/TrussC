#pragma once

// =============================================================================
// tcImage.h - 画像読み込み・描画・保存
// =============================================================================

// このファイルは TrussC.h からインクルードされる
// sokol と internal 名前空間の変数にアクセスするため

#include <filesystem>
#include "stb/stb_image.h"
#include "stb/stb_image_write.h"

namespace trussc {

namespace fs = std::filesystem;

// 画像タイプ
enum class ImageType {
    Color,      // RGBA
    Grayscale   // グレースケール
};

// ---------------------------------------------------------------------------
// Image クラス
// ---------------------------------------------------------------------------
class Image {
public:
    Image() = default;
    ~Image() { clear(); }

    // コピー禁止（リソース管理のため）
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    // ムーブ対応
    Image(Image&& other) noexcept {
        moveFrom(std::move(other));
    }

    Image& operator=(Image&& other) noexcept {
        if (this != &other) {
            clear();
            moveFrom(std::move(other));
        }
        return *this;
    }

    // ファイルから画像を読み込む
    bool load(const fs::path& path) {
        clear();

        // stb_image で読み込み（常にRGBAで）
        int w, h, channels;
        unsigned char* data = stbi_load(path.string().c_str(), &w, &h, &channels, 4);
        if (!data) {
            return false;
        }

        width_ = w;
        height_ = h;
        channels_ = 4;  // 常にRGBA
        originalChannels_ = channels;

        // ピクセルデータをコピー
        size_t size = width_ * height_ * channels_;
        pixels_ = new unsigned char[size];
        memcpy(pixels_, data, size);
        stbi_image_free(data);

        // sokol テクスチャを作成
        createTexture();
        allocated_ = true;

        return true;
    }

    // 空の画像を確保
    void allocate(int w, int h, int channels = 4) {
        clear();

        width_ = w;
        height_ = h;
        channels_ = channels;
        originalChannels_ = channels;

        size_t size = width_ * height_ * channels_;
        pixels_ = new unsigned char[size];
        memset(pixels_, 0, size);

        createTexture();
        allocated_ = true;
    }

    // メモリから画像を読み込む（バッファ）
    bool loadFromMemory(const unsigned char* buffer, int len) {
        clear();

        int w, h, channels;
        unsigned char* data = stbi_load_from_memory(buffer, len, &w, &h, &channels, 4);
        if (!data) {
            return false;
        }

        width_ = w;
        height_ = h;
        channels_ = 4;
        originalChannels_ = channels;

        size_t size = width_ * height_ * channels_;
        pixels_ = new unsigned char[size];
        memcpy(pixels_, data, size);
        stbi_image_free(data);

        createTexture();
        allocated_ = true;

        return true;
    }

    // 画像を保存
    bool save(const fs::path& path) const {
        if (!allocated_ || !pixels_) return false;

        // 拡張子で保存形式を判定
        auto ext = path.extension().string();
        int result = 0;
        auto pathStr = path.string();

        if (ext == ".png" || ext == ".PNG") {
            result = stbi_write_png(pathStr.c_str(), width_, height_, channels_, pixels_, width_ * channels_);
        } else if (ext == ".jpg" || ext == ".jpeg" || ext == ".JPG" || ext == ".JPEG") {
            result = stbi_write_jpg(pathStr.c_str(), width_, height_, channels_, pixels_, 90);
        } else if (ext == ".bmp" || ext == ".BMP") {
            result = stbi_write_bmp(pathStr.c_str(), width_, height_, channels_, pixels_);
        } else {
            // デフォルトはPNG
            result = stbi_write_png(pathStr.c_str(), width_, height_, channels_, pixels_, width_ * channels_);
        }

        return result != 0;
    }

    // リソースを解放
    void clear() {
        if (textureValid_) {
            sg_destroy_sampler(sampler_);
            sg_destroy_view(view_);
            sg_destroy_image(texture_);
            textureValid_ = false;
        }
        if (pixels_) {
            delete[] pixels_;
            pixels_ = nullptr;
        }
        width_ = 0;
        height_ = 0;
        channels_ = 0;
        allocated_ = false;
        textureDirty_ = false;
    }

    // 画像が確保されているか
    bool isAllocated() const { return allocated_; }

    // サイズ取得
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }
    int getChannels() const { return channels_; }

    // ピクセルデータ取得
    unsigned char* getPixels() { return pixels_; }
    const unsigned char* getPixels() const { return pixels_; }

    // 指定座標のピクセル色を取得
    Color getColor(int x, int y) const {
        if (!allocated_ || !pixels_ || x < 0 || x >= width_ || y < 0 || y >= height_) {
            return Color(0, 0, 0, 0);
        }

        int index = (y * width_ + x) * channels_;
        if (channels_ >= 3) {
            float r = pixels_[index] / 255.0f;
            float g = pixels_[index + 1] / 255.0f;
            float b = pixels_[index + 2] / 255.0f;
            float a = (channels_ == 4) ? pixels_[index + 3] / 255.0f : 1.0f;
            return Color(r, g, b, a);
        } else {
            // グレースケール
            float gray = pixels_[index] / 255.0f;
            return Color(gray, gray, gray, 1.0f);
        }
    }

    // 指定座標のピクセル色を設定
    void setColor(int x, int y, const Color& c) {
        if (!allocated_ || !pixels_ || x < 0 || x >= width_ || y < 0 || y >= height_) {
            return;
        }

        int index = (y * width_ + x) * channels_;
        if (channels_ >= 3) {
            pixels_[index] = static_cast<unsigned char>(c.r * 255.0f);
            pixels_[index + 1] = static_cast<unsigned char>(c.g * 255.0f);
            pixels_[index + 2] = static_cast<unsigned char>(c.b * 255.0f);
            if (channels_ == 4) {
                pixels_[index + 3] = static_cast<unsigned char>(c.a * 255.0f);
            }
        } else {
            // グレースケール: 輝度を計算
            float gray = 0.299f * c.r + 0.587f * c.g + 0.114f * c.b;
            pixels_[index] = static_cast<unsigned char>(gray * 255.0f);
        }
        textureDirty_ = true;
    }

    // テクスチャを更新（setColor 後に呼ぶ）
    void update() {
        if (textureDirty_ && textureValid_ && pixels_) {
            updateTexture();
            textureDirty_ = false;
        }
    }

    // 画像を描画（左上座標）
    void draw(float x, float y) const {
        if (!allocated_ || !textureValid_) return;
        drawInternal(x, y, (float)width_, (float)height_);
    }

    // 画像を描画（左上座標 + サイズ指定）
    void draw(float x, float y, float w, float h) const {
        if (!allocated_ || !textureValid_) return;
        drawInternal(x, y, w, h);
    }

    // 画面をキャプチャ（指定範囲）
    // 注意: この関数は draw() の後、present() の前に呼ぶ必要がある
    bool grabScreen(int x, int y, int w, int h) {
        // sokol_gfx には直接フレームバッファを読む API がない
        // プラットフォーム固有の実装が必要
        // TODO: Metal / OpenGL 固有の実装を追加

        // 暫定: 空の画像を作成
        allocate(w, h, 4);

        // プラットフォーム固有の読み取りを試行
        return grabScreenPlatform(x, y, w, h);
    }

private:
    int width_ = 0;
    int height_ = 0;
    int channels_ = 0;
    int originalChannels_ = 0;  // 元のチャンネル数
    unsigned char* pixels_ = nullptr;
    bool allocated_ = false;

    // sokol リソース
    sg_image texture_ = {};
    sg_view view_ = {};
    sg_sampler sampler_ = {};
    bool textureValid_ = false;
    bool textureDirty_ = false;

    void createTexture() {
        if (textureValid_) {
            sg_destroy_sampler(sampler_);
            sg_destroy_view(view_);
            sg_destroy_image(texture_);
        }

        // テクスチャを作成
        sg_image_desc img_desc = {};
        img_desc.width = width_;
        img_desc.height = height_;
        img_desc.pixel_format = (channels_ == 4) ? SG_PIXELFORMAT_RGBA8 : SG_PIXELFORMAT_R8;
        img_desc.data.mip_levels[0].ptr = pixels_;
        img_desc.data.mip_levels[0].size = width_ * height_ * channels_;
        texture_ = sg_make_image(&img_desc);

        // ビューを作成
        sg_view_desc view_desc = {};
        view_desc.texture.image = texture_;
        view_ = sg_make_view(&view_desc);

        // サンプラーを作成（バイリニアフィルタリング）
        sg_sampler_desc smp_desc = {};
        smp_desc.min_filter = SG_FILTER_LINEAR;
        smp_desc.mag_filter = SG_FILTER_LINEAR;
        smp_desc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
        smp_desc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
        sampler_ = sg_make_sampler(&smp_desc);

        textureValid_ = true;
    }

    void updateTexture() {
        if (!textureValid_) return;

        sg_image_data data = {};
        data.mip_levels[0].ptr = pixels_;
        data.mip_levels[0].size = width_ * height_ * channels_;
        sg_update_image(texture_, &data);
    }

    void drawInternal(float x, float y, float w, float h) const {
        // アルファブレンドパイプラインを使用
        sgl_load_pipeline(internal::fontPipeline);
        sgl_enable_texture();
        sgl_texture(view_, sampler_);

        // 現在の色で描画（setColor の色がかかる）
        Color col = getDefaultContext().getColor();
        sgl_begin_quads();
        sgl_c4f(col.r, col.g, col.b, col.a);

        // テクスチャ座標: 左上(0,0) 右下(1,1)
        sgl_v2f_t2f(x, y, 0.0f, 0.0f);
        sgl_v2f_t2f(x + w, y, 1.0f, 0.0f);
        sgl_v2f_t2f(x + w, y + h, 1.0f, 1.0f);
        sgl_v2f_t2f(x, y + h, 0.0f, 1.0f);

        sgl_end();
        sgl_disable_texture();
        sgl_load_default_pipeline();
    }

    void moveFrom(Image&& other) {
        width_ = other.width_;
        height_ = other.height_;
        channels_ = other.channels_;
        originalChannels_ = other.originalChannels_;
        pixels_ = other.pixels_;
        allocated_ = other.allocated_;
        texture_ = other.texture_;
        view_ = other.view_;
        sampler_ = other.sampler_;
        textureValid_ = other.textureValid_;
        textureDirty_ = other.textureDirty_;

        // 元のオブジェクトを無効化
        other.pixels_ = nullptr;
        other.allocated_ = false;
        other.textureValid_ = false;
        other.width_ = 0;
        other.height_ = 0;
    }

    // プラットフォーム固有のスクリーンキャプチャ（tcImage_platform.h で実装）
    bool grabScreenPlatform(int x, int y, int w, int h);
};

} // namespace trussc

// プラットフォーム固有の実装
#include "tc/graphics/tcImage_platform.h"
