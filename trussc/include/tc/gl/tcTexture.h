#pragma once

// =============================================================================
// tcTexture.h - GPU テクスチャ管理
// =============================================================================

// このファイルは TrussC.h からインクルードされる
// sokol と internal 名前空間の変数にアクセスするため

namespace trussc {

// テクスチャの使用モード
enum class TextureUsage {
    Immutable,      // 一度だけ設定、更新不可（Image::load 用）
    Dynamic,        // CPU から定期的に更新（Image::allocate 用）
    Stream,         // 毎フレーム更新（VideoGrabber 用）
    RenderTarget    // FBO のカラーアタッチメント用
};

// ---------------------------------------------------------------------------
// Texture クラス - GPU 側のテクスチャを管理
// ---------------------------------------------------------------------------
class Texture {
public:
    Texture() = default;
    ~Texture() { clear(); }

    // コピー禁止
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    // ムーブ対応
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

    // === 確保・解放 ===

    // 空のテクスチャを確保
    void allocate(int width, int height, int channels = 4,
                  TextureUsage usage = TextureUsage::Immutable,
                  int sampleCount = 1) {
        clear();

        width_ = width;
        height_ = height;
        channels_ = channels;
        usage_ = usage;
        sampleCount_ = sampleCount;

        createResources(nullptr);
    }

    // Pixels からテクスチャを確保
    void allocate(const Pixels& pixels, TextureUsage usage = TextureUsage::Immutable) {
        clear();

        width_ = pixels.getWidth();
        height_ = pixels.getHeight();
        channels_ = pixels.getChannels();
        usage_ = usage;

        if (usage == TextureUsage::Immutable) {
            // Immutable は作成時にデータを渡す
            createResources(pixels.getData());
        } else {
            // Dynamic/Stream は空で作成、ユーザーが update() を呼ぶまでデータなし
            createResources(nullptr);
        }
    }

    // リソースを解放
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
        image_ = {};
        view_ = {};
        attachmentView_ = {};
        sampler_ = {};
    }

    // === 状態 ===

    bool isAllocated() const { return allocated_; }
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }
    int getChannels() const { return channels_; }
    TextureUsage getUsage() const { return usage_; }
    int getSampleCount() const { return sampleCount_; }

    // === データ更新（Immutable 以外） ===

    void loadData(const Pixels& pixels) {
        loadData(pixels.getData(), pixels.getWidth(), pixels.getHeight(), pixels.getChannels());
    }

    // ピクセルデータをテクスチャにアップロード
    // 注意: sokol の制限により、1フレームに1回しか呼べない
    // 同じフレームで2回呼ぶと、2回目は無視されて警告が出る
    void loadData(const unsigned char* data, int width, int height, int channels) {
        if (!allocated_ || usage_ == TextureUsage::Immutable) return;
        if (width != width_ || height != height_ || channels != channels_) return;

        // 1フレームに1回だけ更新可能（sokol の制限）
        uint64_t currentFrame = sapp_frame_count();
        if (lastUpdateFrame_ == currentFrame) {
            // 同じフレームでの2回目は無視して警告
            tcLogWarning() << "[Texture] loadData() called twice in same frame, skipped";
            return;
        }
        lastUpdateFrame_ = currentFrame;

        sg_image_data img_data = {};
        img_data.mip_levels[0].ptr = data;
        img_data.mip_levels[0].size = width * height * channels;
        sg_update_image(image_, &img_data);
    }

    // === フィルター設定 ===

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

    // === ラップモード設定 ===

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

    // === 描画 ===

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

    // 部分描画（スプライトシート用）
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

    // === バインド（シェーダー連携用） ===

    void bind() const {
        if (allocated_) {
            sgl_enable_texture();
            sgl_texture(view_, sampler_);
        }
    }

    void unbind() const {
        sgl_disable_texture();
    }

    // === 内部リソースアクセス（上級者向け） ===

    sg_image getImage() const { return image_; }
    sg_view getView() const { return view_; }
    sg_sampler getSampler() const { return sampler_; }

    // RenderTarget 用: アタッチメントビュー（FBO でレンダリング先として使う）
    sg_view getAttachmentView() const { return attachmentView_; }

private:
    sg_image image_ = {};
    sg_view view_ = {};              // テクスチャビュー（サンプリング用）
    sg_view attachmentView_ = {};    // アタッチメントビュー（RenderTarget 用）
    sg_sampler sampler_ = {};

    int width_ = 0;
    int height_ = 0;
    int channels_ = 0;
    int sampleCount_ = 1;
    bool allocated_ = false;
    TextureUsage usage_ = TextureUsage::Immutable;
    uint64_t lastUpdateFrame_ = UINT64_MAX;  // 最後に更新したフレーム

    TextureFilter minFilter_ = TextureFilter::Linear;
    TextureFilter magFilter_ = TextureFilter::Linear;
    TextureWrap wrapU_ = TextureWrap::ClampToEdge;
    TextureWrap wrapV_ = TextureWrap::ClampToEdge;

    void createResources(const unsigned char* initialData) {
        // イメージを作成
        sg_image_desc img_desc = {};
        img_desc.width = width_;
        img_desc.height = height_;
        img_desc.pixel_format = (channels_ == 4) ? SG_PIXELFORMAT_RGBA8 : SG_PIXELFORMAT_R8;

        switch (usage_) {
            case TextureUsage::Immutable:
                if (initialData) {
                    img_desc.data.mip_levels[0].ptr = initialData;
                    img_desc.data.mip_levels[0].size = width_ * height_ * channels_;
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
                img_desc.usage.resolve_attachment = true;  // MSAA resolve 先としても使えるように
                img_desc.sample_count = sampleCount_;
                break;
        }

        image_ = sg_make_image(&img_desc);

        // テクスチャビューを作成（サンプリング用）
        sg_view_desc view_desc = {};
        view_desc.texture.image = image_;
        view_ = sg_make_view(&view_desc);

        // RenderTarget の場合はアタッチメントビューも作成
        if (usage_ == TextureUsage::RenderTarget) {
            sg_view_desc att_desc = {};
            att_desc.color_attachment.image = image_;
            attachmentView_ = sg_make_view(&att_desc);
        }

        // サンプラーを作成
        createSampler();

        allocated_ = true;
    }

    void createSampler() {
        sg_sampler_desc smp_desc = {};

        // フィルター設定
        smp_desc.min_filter = (minFilter_ == TextureFilter::Nearest) ? SG_FILTER_NEAREST : SG_FILTER_LINEAR;
        smp_desc.mag_filter = (magFilter_ == TextureFilter::Nearest) ? SG_FILTER_NEAREST : SG_FILTER_LINEAR;

        // ラップモード設定
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
        // アルファブレンドパイプラインを使用
        sgl_load_pipeline(internal::fontPipeline);
        sgl_enable_texture();
        sgl_texture(view_, sampler_);

        // 現在の色で描画
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
    }
};

} // namespace trussc
