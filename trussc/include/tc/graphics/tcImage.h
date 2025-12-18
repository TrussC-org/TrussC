#pragma once

// =============================================================================
// tcImage.h - 画像読み込み・描画・保存
// =============================================================================

// このファイルは TrussC.h からインクルードされる
// Pixels, Texture, HasTexture が先にインクルードされている必要がある

#include <filesystem>

namespace trussc {

namespace fs = std::filesystem;

// 画像タイプ
enum class ImageType {
    Color,      // RGBA
    Grayscale   // グレースケール
};

// ---------------------------------------------------------------------------
// Image クラス - Pixels (CPU) + Texture (GPU) を持つ統合クラス
// ---------------------------------------------------------------------------
class Image : public HasTexture {
public:
    Image() = default;
    ~Image() { clear(); }

    // コピー禁止
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    // ムーブ対応
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

    // === ファイル I/O ===

    // ファイルから画像を読み込む
    bool load(const fs::path& path) {
        clear();

        if (!pixels_.load(path)) {
            return false;
        }

        // Immutable テクスチャを作成
        texture_.allocate(pixels_, TextureUsage::Immutable);
        return true;
    }

    // メモリから画像を読み込む
    bool loadFromMemory(const unsigned char* buffer, int len) {
        clear();

        if (!pixels_.loadFromMemory(buffer, len)) {
            return false;
        }

        texture_.allocate(pixels_, TextureUsage::Immutable);
        return true;
    }

    // 画像を保存（HasTexture::save() のオーバーライド）
    // Image は pixels を持っているので直接保存（テクスチャからの読み戻し不要）
    bool save(const fs::path& path) const override {
        return pixels_.save(path);
    }

    // === 確保・解放 ===

    // 空の画像を確保（動的更新用）
    void allocate(int width, int height, int channels = 4) {
        clear();
        pixels_.allocate(width, height, channels);
        // Dynamic テクスチャを作成（後から更新可能）
        texture_.allocate(pixels_, TextureUsage::Dynamic);
    }

    // リソースを解放
    void clear() {
        pixels_.clear();
        texture_.clear();
        dirty_ = false;
    }

    // === 状態 ===

    bool isAllocated() const { return pixels_.isAllocated(); }
    int getWidth() const { return pixels_.getWidth(); }
    int getHeight() const { return pixels_.getHeight(); }
    int getChannels() const { return pixels_.getChannels(); }

    // === Pixels アクセス ===

    Pixels& getPixels() { return pixels_; }
    const Pixels& getPixels() const { return pixels_; }

    // 生ポインタへのショートカット
    unsigned char* getPixelsData() { return pixels_.getData(); }
    const unsigned char* getPixelsData() const { return pixels_.getData(); }

    // === ピクセル操作 ===

    Color getColor(int x, int y) const {
        return pixels_.getColor(x, y);
    }

    void setColor(int x, int y, const Color& c) {
        pixels_.setColor(x, y, c);
        dirty_ = true;
    }

    // === テクスチャ更新 ===

    // ピクセルの変更をテクスチャに反映する
    // 注意: sokol の制限により、1フレームに1回しか呼べない
    // 同じフレームで2回呼ぶと、2回目は無視される（警告が出る）
    // setColor() や getPixels() で編集した後に呼ぶ
    void update() {
        if (dirty_ && texture_.isAllocated()) {
            texture_.loadData(pixels_);
            dirty_ = false;
        }
    }

    // dirty フラグを手動で設定（getPixels() で直接編集した場合）
    void setDirty() { dirty_ = true; }

    // === HasTexture 実装 ===

    Texture& getTexture() override { return texture_; }
    const Texture& getTexture() const override { return texture_; }

    // draw() は HasTexture のデフォルト実装を使用

private:
    Pixels pixels_;
    Texture texture_;
    bool dirty_ = false;
};

} // namespace trussc
