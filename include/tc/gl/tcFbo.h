#pragma once

// =============================================================================
// tcFbo.h - フレームバッファオブジェクト（オフスクリーンレンダリング）
// =============================================================================

// このファイルは TrussC.h からインクルードされる
// sokol と internal 名前空間の変数にアクセスするため

namespace trussc {

// ---------------------------------------------------------------------------
// Fbo クラス
// ---------------------------------------------------------------------------
class Fbo {
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

    // FBO を確保
    void allocate(int w, int h) {
        clear();

        width_ = w;
        height_ = h;

        // カラーアタッチメント用テクスチャ
        // sokol では color_attachment を設定すればテクスチャとしてもサンプリング可能
        sg_image_desc color_desc = {};
        color_desc.usage.color_attachment = true;
        color_desc.width = w;
        color_desc.height = h;
        color_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
        color_desc.sample_count = 1;
        colorImage_ = sg_make_image(&color_desc);

        // 深度バッファ用テクスチャ
        sg_image_desc depth_desc = {};
        depth_desc.usage.depth_stencil_attachment = true;
        depth_desc.width = w;
        depth_desc.height = h;
        depth_desc.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
        depth_desc.sample_count = 1;
        depthImage_ = sg_make_image(&depth_desc);

        // カラーアタッチメントビュー（レンダリング用）
        sg_view_desc color_att_desc = {};
        color_att_desc.color_attachment.image = colorImage_;
        colorAttView_ = sg_make_view(&color_att_desc);

        // 深度アタッチメントビュー
        sg_view_desc depth_att_desc = {};
        depth_att_desc.depth_stencil_attachment.image = depthImage_;
        depthAttView_ = sg_make_view(&depth_att_desc);

        // テクスチャビュー（サンプリング用）
        sg_view_desc tex_desc = {};
        tex_desc.texture.image = colorImage_;
        textureView_ = sg_make_view(&tex_desc);

        // サンプラー（バイリニアフィルタリング）
        sg_sampler_desc smp_desc = {};
        smp_desc.min_filter = SG_FILTER_LINEAR;
        smp_desc.mag_filter = SG_FILTER_LINEAR;
        smp_desc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
        smp_desc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
        sampler_ = sg_make_sampler(&smp_desc);

        // FBO 用 sokol_gl コンテキストを作成（RGBA8 フォーマット用）
        sgl_context_desc_t ctx_desc = {};
        ctx_desc.color_format = SG_PIXELFORMAT_RGBA8;
        ctx_desc.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
        ctx_desc.sample_count = 1;
        context_ = sgl_make_context(&ctx_desc);

        // FBO 用パイプライン（アルファブレンド）- コンテキストに紐づく
        {
            sg_pipeline_desc pip_desc = {};
            pip_desc.colors[0].blend.enabled = true;
            pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
            pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            pip_desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
            pip_desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ZERO;
            pipelineBlend_ = sgl_context_make_pipeline(context_, &pip_desc);
        }

        allocated_ = true;
    }

    // リソースを解放
    void clear() {
        if (allocated_) {
            sgl_destroy_pipeline(pipelineBlend_);
            sgl_destroy_context(context_);
            sg_destroy_sampler(sampler_);
            sg_destroy_view(textureView_);
            sg_destroy_view(depthAttView_);
            sg_destroy_view(colorAttView_);
            sg_destroy_image(depthImage_);
            sg_destroy_image(colorImage_);
            allocated_ = false;
        }
        width_ = 0;
        height_ = 0;
        active_ = false;
    }

    // FBO への描画を開始
    void begin() {
        if (!allocated_) return;

        // スワップチェーンパス中なら一時中断
        wasInSwapchainPass_ = isInSwapchainPass();
        if (wasInSwapchainPass_) {
            suspendSwapchainPass();
        }

        // オフスクリーンパスを開始
        sg_pass pass = {};
        pass.attachments.colors[0] = colorAttView_;
        pass.attachments.depth_stencil = depthAttView_;
        pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
        pass.action.colors[0].clear_value = { 0.0f, 0.0f, 0.0f, 0.0f };  // 透明でクリア
        pass.action.depth.load_action = SG_LOADACTION_CLEAR;
        pass.action.depth.clear_value = 1.0f;
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
    }

    // 背景色を指定して描画開始
    void begin(float r, float g, float b, float a = 1.0f) {
        if (!allocated_) return;

        // スワップチェーンパス中なら一時中断
        wasInSwapchainPass_ = isInSwapchainPass();
        if (wasInSwapchainPass_) {
            suspendSwapchainPass();
        }

        sg_pass pass = {};
        pass.attachments.colors[0] = colorAttView_;
        pass.attachments.depth_stencil = depthAttView_;
        pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
        pass.action.colors[0].clear_value = { r, g, b, a };
        pass.action.depth.load_action = SG_LOADACTION_CLEAR;
        pass.action.depth.clear_value = 1.0f;
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

        // スワップチェーンパスを再開（元々パス中だった場合）
        if (wasInSwapchainPass_) {
            resumeSwapchainPass();
        }
    }

    // FBO の内容を描画（左上座標）
    void draw(float x, float y) const {
        if (!allocated_) return;
        drawInternal(x, y, (float)width_, (float)height_);
    }

    // FBO の内容を描画（左上座標 + サイズ指定）
    void draw(float x, float y, float w, float h) const {
        if (!allocated_) return;
        drawInternal(x, y, w, h);
    }

    // ピクセルデータを読み取る
    // 注意: 描画完了後（end() の後）に呼ぶこと
    bool readPixels(unsigned char* pixels) const {
        if (!allocated_ || !pixels) return false;

        // sokol_gfx には直接ピクセルを読む API がない
        // プラットフォーム固有の実装が必要
        return readPixelsPlatform(pixels);
    }

    // Image にコピー
    bool copyTo(Image& image) const {
        if (!allocated_) return false;

        image.allocate(width_, height_, 4);
        return readPixels(image.getPixels());
    }

    // サイズ取得
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }
    bool isAllocated() const { return allocated_; }
    bool isActive() const { return active_; }

    // 内部リソースへのアクセス（上級者向け）
    sg_image getColorImage() const { return colorImage_; }
    sg_view getTextureView() const { return textureView_; }
    sg_sampler getSampler() const { return sampler_; }

private:
    int width_ = 0;
    int height_ = 0;
    bool allocated_ = false;
    bool active_ = false;
    bool wasInSwapchainPass_ = false;  // begin() 時にスワップチェーンパス中だったか

    sg_image colorImage_ = {};
    sg_image depthImage_ = {};
    sg_view colorAttView_ = {};
    sg_view depthAttView_ = {};
    sg_view textureView_ = {};
    sg_sampler sampler_ = {};
    sgl_context context_ = {};             // FBO 用 sokol_gl コンテキスト
    sgl_pipeline pipelineBlend_ = {};      // FBO 用パイプライン（アルファブレンド）

    void drawInternal(float x, float y, float w, float h) const {
        // アルファブレンドパイプラインを使用
        sgl_load_pipeline(internal::fontPipeline);
        sgl_enable_texture();
        sgl_texture(textureView_, sampler_);

        sgl_begin_quads();
        sgl_c4f(internal::currentR, internal::currentG, internal::currentB, internal::currentA);

        // FBO は Y が反転している場合があるので確認が必要
        // sokol/Metal では反転しないはず
        sgl_v2f_t2f(x, y, 0.0f, 0.0f);
        sgl_v2f_t2f(x + w, y, 1.0f, 0.0f);
        sgl_v2f_t2f(x + w, y + h, 1.0f, 1.0f);
        sgl_v2f_t2f(x, y + h, 0.0f, 1.0f);

        sgl_end();
        sgl_disable_texture();
        sgl_load_default_pipeline();
    }

    void moveFrom(Fbo&& other) {
        width_ = other.width_;
        height_ = other.height_;
        allocated_ = other.allocated_;
        active_ = other.active_;
        wasInSwapchainPass_ = other.wasInSwapchainPass_;
        colorImage_ = other.colorImage_;
        depthImage_ = other.depthImage_;
        colorAttView_ = other.colorAttView_;
        depthAttView_ = other.depthAttView_;
        textureView_ = other.textureView_;
        sampler_ = other.sampler_;
        context_ = other.context_;
        pipelineBlend_ = other.pipelineBlend_;

        other.allocated_ = false;
        other.active_ = false;
        other.wasInSwapchainPass_ = false;
        other.width_ = 0;
        other.height_ = 0;
    }

    // プラットフォーム固有のピクセル読み取り（tcFbo_platform.h で実装）
    bool readPixelsPlatform(unsigned char* pixels) const;
};

} // namespace trussc
