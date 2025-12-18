#pragma once

// =============================================================================
// tcFbo.h - フレームバッファオブジェクト（オフスクリーンレンダリング）
// =============================================================================

// このファイルは TrussC.h からインクルードされる
// sokol と internal 名前空間の変数にアクセスするため
// Texture, HasTexture が先にインクルードされている必要がある

namespace trussc {

// 前方宣言
class Fbo;

// FBO の clearColor を呼ぶための static ヘルパー関数
inline void _fboClearColorHelper(float r, float g, float b, float a);

// ---------------------------------------------------------------------------
// Fbo クラス - HasTexture を継承
// ---------------------------------------------------------------------------
class Fbo : public HasTexture {
public:
    Fbo() = default;
    ~Fbo() { clear(); }

    // コピー禁止
    Fbo(const Fbo&) = delete;
    Fbo& operator=(const Fbo&) = delete;

    // ムーブ対応
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

    // FBO を確保（MSAA 対応）
    // sampleCount: 1, 2, 4, 8 など（1 = MSAA なし）
    void allocate(int w, int h, int sampleCount = 1) {
        clear();

        width_ = w;
        height_ = h;
        sampleCount_ = sampleCount;

        // MSAA の場合
        if (sampleCount_ > 1) {
            // MSAA カラーテクスチャ（描画先）
            sg_image_desc msaa_desc = {};
            msaa_desc.usage.color_attachment = true;
            msaa_desc.width = w;
            msaa_desc.height = h;
            msaa_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
            msaa_desc.sample_count = sampleCount_;
            msaaColorImage_ = sg_make_image(&msaa_desc);

            // MSAA カラーアタッチメントビュー
            sg_view_desc msaa_att_desc = {};
            msaa_att_desc.color_attachment.image = msaaColorImage_;
            msaaColorAttView_ = sg_make_view(&msaa_att_desc);

            // resolve 用カラーテクスチャ（非 MSAA、読み取り/表示用）
            colorTexture_.allocate(w, h, 4, TextureUsage::RenderTarget, 1);

            // resolve 用ビュー（resolve_attachment として作成する必要がある）
            sg_view_desc resolve_view_desc = {};
            resolve_view_desc.resolve_attachment.image = colorTexture_.getImage();
            resolveAttView_ = sg_make_view(&resolve_view_desc);

            // MSAA 深度バッファ
            sg_image_desc depth_desc = {};
            depth_desc.usage.depth_stencil_attachment = true;
            depth_desc.width = w;
            depth_desc.height = h;
            depth_desc.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
            depth_desc.sample_count = sampleCount_;
            depthImage_ = sg_make_image(&depth_desc);
        } else {
            // 非 MSAA の場合（従来通り）
            colorTexture_.allocate(w, h, 4, TextureUsage::RenderTarget, 1);

            // 深度バッファ
            sg_image_desc depth_desc = {};
            depth_desc.usage.depth_stencil_attachment = true;
            depth_desc.width = w;
            depth_desc.height = h;
            depth_desc.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
            depth_desc.sample_count = 1;
            depthImage_ = sg_make_image(&depth_desc);
        }

        // 深度アタッチメントビュー
        sg_view_desc depth_att_desc = {};
        depth_att_desc.depth_stencil_attachment.image = depthImage_;
        depthAttView_ = sg_make_view(&depth_att_desc);

        // FBO 用 sokol_gl コンテキストを作成
        sgl_context_desc_t ctx_desc = {};
        ctx_desc.color_format = SG_PIXELFORMAT_RGBA8;
        ctx_desc.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
        ctx_desc.sample_count = sampleCount_;
        context_ = sgl_make_context(&ctx_desc);

        // FBO 用パイプライン（アルファブレンド）- コンテキストに紐づく
        {
            sg_pipeline_desc pip_desc = {};
            pip_desc.sample_count = sampleCount_;  // MSAA サンプル数を一致させる
            pip_desc.colors[0].blend.enabled = true;
            pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
            pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            pip_desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
            pip_desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ZERO;
            pipelineBlend_ = sgl_context_make_pipeline(context_, &pip_desc);
        }

        // FBO 用パイプライン（クリア用、ブレンドなし）
        {
            sg_pipeline_desc pip_desc = {};
            pip_desc.sample_count = sampleCount_;
            pip_desc.colors[0].blend.enabled = false;  // ブレンドなし = 上書き
            pipelineClear_ = sgl_context_make_pipeline(context_, &pip_desc);
        }

        allocated_ = true;
    }

    // リソースを解放
    void clear() {
        if (allocated_) {
            sgl_destroy_pipeline(pipelineClear_);
            sgl_destroy_pipeline(pipelineBlend_);
            sgl_destroy_context(context_);
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

    // FBO への描画を開始
    void begin() {
        beginInternal(0.0f, 0.0f, 0.0f, 0.0f);  // 透明でクリア
    }

    // 背景色を指定して描画開始
    void begin(float r, float g, float b, float a = 1.0f) {
        beginInternal(r, g, b, a);
    }

    // FBO 描画中に背景色を変更（パスを再開始）
    // tc::clear() から呼ばれる
    void clearColor(float r, float g, float b, float a) {
        if (!active_) return;

        // 現在のパスを終了
        sgl_context_draw(context_);
        sg_end_pass();

        // 新しいクリア色でパスを再開始
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

        // sokol_gl の状態をリセット
        sgl_defaults();
        sgl_load_pipeline(pipelineBlend_);
        sgl_matrix_mode_projection();
        sgl_ortho(0.0f, (float)width_, (float)height_, 0.0f, -10000.0f, 10000.0f);
        sgl_matrix_mode_modelview();
        sgl_load_identity();
    }

    // FBO への描画を終了
    void end() {
        if (!active_) return;

        // FBO コンテキストの内容を描画
        sgl_context_draw(context_);
        sg_end_pass();

        // デフォルトコンテキストに戻す
        sgl_set_context(sgl_default_context());
        active_ = false;
        internal::inFboPass = false;
        internal::currentFboClearPipeline = {};
        internal::currentFboBlendPipeline = {};
        internal::currentFbo = nullptr;
        internal::fboClearColorFunc = nullptr;

        // スワップチェーンパスを再開（元々パス中だった場合）
        if (wasInSwapchainPass_) {
            resumeSwapchainPass();
        }
    }

    // ピクセルデータを読み取る
    // 注意: 描画完了後（end() の後）に呼ぶこと
    // MSAA の場合は resolve 済みのテクスチャから読み取る
    bool readPixels(unsigned char* pixels) const {
        if (!allocated_ || !pixels) return false;

        // sokol_gfx には直接ピクセルを読む API がない
        // プラットフォーム固有の実装が必要
        // colorTexture_ は常に非 MSAA（resolve 済み）なのでそこから読む
        return readPixelsPlatform(pixels);
    }

    // Image にコピー
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

    // サイズ・状態取得
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }
    int getSampleCount() const { return sampleCount_; }
    bool isAllocated() const { return allocated_; }
    bool isActive() const { return active_; }

    // === HasTexture 実装 ===

    // getTexture() は常に非 MSAA のテクスチャを返す（描画・読み取り用）
    Texture& getTexture() override { return colorTexture_; }
    const Texture& getTexture() const override { return colorTexture_; }

    // draw() は HasTexture のデフォルト実装を使用

    // save() オーバーライド - FBO の内容をファイルに保存
    bool save(const fs::path& path) const override {
        Image img;
        if (copyTo(img)) {
            return img.save(path);
        }
        return false;
    }

    // 内部リソースへのアクセス（上級者向け）
    sg_image getColorImage() const { return colorTexture_.getImage(); }
    sg_view getTextureView() const { return colorTexture_.getView(); }
    sg_sampler getSampler() const { return colorTexture_.getSampler(); }

private:
    int width_ = 0;
    int height_ = 0;
    int sampleCount_ = 1;
    bool allocated_ = false;
    bool active_ = false;
    bool wasInSwapchainPass_ = false;  // begin() 時にスワップチェーンパス中だったか

    // 非 MSAA テクスチャ（常に使用、MSAA 時は resolve 先）
    Texture colorTexture_;

    // MSAA 用リソース（sampleCount > 1 の時のみ使用）
    sg_image msaaColorImage_ = {};
    sg_view msaaColorAttView_ = {};
    sg_view resolveAttView_ = {};

    // 共通リソース
    sg_image depthImage_ = {};
    sg_view depthAttView_ = {};
    sgl_context context_ = {};
    sgl_pipeline pipelineBlend_ = {};
    sgl_pipeline pipelineClear_ = {};  // clear() 用（ブレンドなし）

    void beginInternal(float r, float g, float b, float a) {
        if (!allocated_) return;

        // スワップチェーンパス中なら一時中断
        wasInSwapchainPass_ = isInSwapchainPass();
        if (wasInSwapchainPass_) {
            suspendSwapchainPass();
        }

        // オフスクリーンパスを開始
        sg_pass pass = {};

        if (sampleCount_ > 1) {
            // MSAA: 描画先は MSAA テクスチャ、resolve 先は非 MSAA テクスチャ
            pass.attachments.colors[0] = msaaColorAttView_;
            pass.attachments.resolves[0] = resolveAttView_;
        } else {
            // 非 MSAA: 直接カラーテクスチャに描画
            pass.attachments.colors[0] = colorTexture_.getAttachmentView();
        }
        pass.attachments.depth_stencil = depthAttView_;

        pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
        pass.action.colors[0].clear_value = { r, g, b, a };
        pass.action.depth.load_action = SG_LOADACTION_CLEAR;
        pass.action.depth.clear_value = 1.0f;

        // MSAA の場合、resolve は自動で行われる（store_action はデフォルトで STORE）
        sg_begin_pass(&pass);

        // FBO 用 sokol_gl コンテキストに切り替え
        sgl_set_context(context_);
        sgl_defaults();
        // FBO 用パイプラインを使用（アルファブレンド対応）
        sgl_load_pipeline(pipelineBlend_);
        sgl_matrix_mode_projection();
        sgl_ortho(0.0f, (float)width_, (float)height_, 0.0f, -10000.0f, 10000.0f);
        sgl_matrix_mode_modelview();
        sgl_load_identity();

        active_ = true;
        internal::inFboPass = true;
        internal::currentFboClearPipeline = pipelineClear_;
        internal::currentFboBlendPipeline = pipelineBlend_;
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
        context_ = other.context_;
        pipelineBlend_ = other.pipelineBlend_;
        pipelineClear_ = other.pipelineClear_;

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
        other.context_ = {};
        other.pipelineBlend_ = {};
        other.pipelineClear_ = {};
    }

    // プラットフォーム固有のピクセル読み取り（tcFbo_platform.h で実装）
    bool readPixelsPlatform(unsigned char* pixels) const;
};

// ---------------------------------------------------------------------------
// tc::clear() から呼ばれるヘルパー関数
// ---------------------------------------------------------------------------
inline void _fboClearColorHelper(float r, float g, float b, float a) {
    Fbo* fbo = static_cast<Fbo*>(internal::currentFbo);
    if (fbo) {
        fbo->clearColor(r, g, b, a);
    }
}

} // namespace trussc
