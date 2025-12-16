#pragma once

// =============================================================================
// TrussC - Thin, Modern, and Native Creative Coding Framework
// Version 0.0.1
// =============================================================================

// sokol ヘッダー
#include "sokol/sokol_log.h"
#define SOKOL_NO_ENTRY  // main() を自分で定義するため
#include "sokol/sokol_app.h"
#include "sokol/sokol_gfx.h"
#include "sokol/sokol_glue.h"
#include "sokol/sokol_gl.h"

// 標準ライブラリ
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <chrono>

// TrussC 数学ライブラリ
#include "tcMath.h"

// TrussC ノイズ関数
#include "tc/math/tcNoise.h"

// TrussC レイ（Hit Test 用）
#include "tc/math/tcRay.h"

// TrussC カラーライブラリ
#include "tcColor.h"

// TrussC ビットマップフォント
#include "tcBitmapFont.h"

// TrussC プラットフォーム固有機能
#include "tcPlatform.h"

// TrussC イベントシステム
#include "tc/events/tcCoreEvents.h"

// TrussC ユーティリティ
#include "tc/utils/tcUtils.h"

// =============================================================================
// trussc 名前空間
// =============================================================================
namespace trussc {

// 前方宣言（RenderContext 用）
namespace internal {
    inline sg_image fontTexture = {};
    inline sg_view fontView = {};
    inline sg_sampler fontSampler = {};
    inline sgl_pipeline fontPipeline = {};
    inline bool fontInitialized = false;
    inline sgl_pipeline pipeline3d = {};
    inline bool pipeline3dInitialized = false;
    inline bool pixelPerfectMode = false;
}

} // namespace trussc (一時的に閉じる)

// RenderContext クラス（描画状態を保持）
#include "tc/graphics/tcRenderContext.h"

// 名前空間を再開
namespace trussc {

// バージョン情報
constexpr int VERSION_MAJOR = 0;
constexpr int VERSION_MINOR = 0;
constexpr int VERSION_PATCH = 1;

// 数学定数は tcMath.h で定義: TAU, HALF_TAU, QUARTER_TAU, PI

// ---------------------------------------------------------------------------
// 内部状態（アプリケーション・ウィンドウ関連）
// 描画状態は RenderContext に移動済み
// ---------------------------------------------------------------------------
namespace internal {
    // ---------------------------------------------------------------------------
    // Loop Architecture (Decoupled Update/Draw)
    // ---------------------------------------------------------------------------

    // Draw Loop 設定
    inline bool drawVsyncEnabled = true;   // VSync 有効（デフォルト）
    inline int drawTargetFps = 0;          // 0 = VSync使用, >0 = 固定FPS, <0 = 自動描画停止
    inline bool needsRedraw = true;        // redraw() フラグ

    // Update Loop 設定
    inline bool updateSyncedToDraw = true; // true = draw直前にupdate（デフォルト）
    inline int updateTargetFps = 0;        // 0 = syncedToDraw使用, >0 = 独立した固定Hz

    // Update タイミング用
    inline std::chrono::high_resolution_clock::time_point lastUpdateTime;
    inline bool lastUpdateTimeInitialized = false;
    inline double updateAccumulator = 0.0; // 独立Updateの蓄積時間

    // Draw タイミング用（フレームスキップ）
    inline std::chrono::high_resolution_clock::time_point lastDrawTime;
    inline bool lastDrawTimeInitialized = false;
    inline double drawAccumulator = 0.0;

    // マウス状態
    inline float mouseX = 0.0f;
    inline float mouseY = 0.0f;
    inline float pmouseX = 0.0f;  // 前フレームのマウス位置
    inline float pmouseY = 0.0f;
    inline int mouseButton = -1;  // 現在押されているボタン (-1 = なし)
    inline bool mousePressed = false;

    // フレームレート計測用（10フレーム移動平均）
    inline double frameTimeBuffer[10] = {};
    inline int frameTimeIndex = 0;
    inline bool frameTimeBufferFilled = false;

    // 経過時間計測用
    inline std::chrono::high_resolution_clock::time_point startTime;
    inline bool startTimeInitialized = false;

    // パス状態（FBO のためにスワップチェーンパスを一時中断するため）
    inline bool inSwapchainPass = false;
}

// ---------------------------------------------------------------------------
// 初期化・終了
// ---------------------------------------------------------------------------

// sokol_gfx + sokol_gl を初期化（setup コールバック内で呼ぶ）
inline void setup() {
    // sokol_gfx 初期化
    sg_desc sgdesc = {};
    sgdesc.environment = sglue_environment();
    sgdesc.logger.func = slog_func;
    sg_setup(&sgdesc);

    // sokol_gl 初期化
    sgl_desc_t sgldesc = {};
    sgldesc.logger.func = slog_func;
    sgl_setup(&sgldesc);

    // ビットマップフォントテクスチャを初期化
    if (!internal::fontInitialized) {
        // テクスチャアトラスのピクセルデータを生成
        unsigned char* pixels = bitmapfont::generateAtlasPixels();

        // テクスチャを作成
        sg_image_desc img_desc = {};
        img_desc.width = bitmapfont::ATLAS_WIDTH;
        img_desc.height = bitmapfont::ATLAS_HEIGHT;
        img_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
        img_desc.data.mip_levels[0].ptr = pixels;
        img_desc.data.mip_levels[0].size = bitmapfont::ATLAS_WIDTH * bitmapfont::ATLAS_HEIGHT * 4;
        internal::fontTexture = sg_make_image(&img_desc);

        // テクスチャビューを作成
        sg_view_desc view_desc = {};
        view_desc.texture.image = internal::fontTexture;
        internal::fontView = sg_make_view(&view_desc);

        // サンプラーを作成（ニアレストネイバー、ピクセルパーフェクト）
        sg_sampler_desc smp_desc = {};
        smp_desc.min_filter = SG_FILTER_NEAREST;
        smp_desc.mag_filter = SG_FILTER_NEAREST;
        smp_desc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
        smp_desc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
        internal::fontSampler = sg_make_sampler(&smp_desc);

        // アルファブレンド用パイプラインを作成
        // RGB: 標準アルファブレンド
        // Alpha: 上書き（FBOに描画時、FBOが半透明にならないように）
        sg_pipeline_desc pip_desc = {};
        pip_desc.colors[0].blend.enabled = true;
        pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
        pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        pip_desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
        pip_desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ZERO;
        internal::fontPipeline = sgl_make_pipeline(&pip_desc);

        delete[] pixels;
        internal::fontInitialized = true;
    }

    // 3D描画用パイプラインを作成（sokol-samples の sgl-sapp.c を参考）
    if (!internal::pipeline3dInitialized) {
        sg_pipeline_desc pip_desc = {};
        pip_desc.cull_mode = SG_CULLMODE_NONE;  // カリングなし
        pip_desc.depth.write_enabled = true;
        pip_desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
        pip_desc.depth.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
        internal::pipeline3d = sgl_make_pipeline(&pip_desc);
        internal::pipeline3dInitialized = true;
    }
}

// sokol_gfx + sokol_gl を終了（cleanup コールバック内で呼ぶ）
inline void cleanup() {
    // 3Dパイプラインを解放
    if (internal::pipeline3dInitialized) {
        sgl_destroy_pipeline(internal::pipeline3d);
        internal::pipeline3dInitialized = false;
    }
    // フォントリソースを解放
    if (internal::fontInitialized) {
        sgl_destroy_pipeline(internal::fontPipeline);
        sg_destroy_sampler(internal::fontSampler);
        sg_destroy_view(internal::fontView);
        sg_destroy_image(internal::fontTexture);
        internal::fontInitialized = false;
    }
    sgl_shutdown();
    sg_shutdown();
}

// ---------------------------------------------------------------------------
// フレーム制御
// ---------------------------------------------------------------------------

// DPIスケールを取得（Retinaディスプレイでは2.0など）
inline float getDpiScale() {
    return sapp_dpi_scale();
}

// フレームバッファの実サイズを取得（ピクセル単位）
inline int getFramebufferWidth() {
    return sapp_width();
}

inline int getFramebufferHeight() {
    return sapp_height();
}

// フレーム開始時に呼ぶ（clearの前に）
inline void beginFrame() {
    // デフォルトのビューポート設定
    sgl_defaults();
    sgl_matrix_mode_projection();

    if (internal::pixelPerfectMode) {
        // ピクセルパーフェクト: 座標系 = フレームバッファサイズ
        // near/far: 正の値で指定（OpenGL慣例）
        // Z=0を含む範囲: near=-10000, far=10000 ではなく
        // 対称な範囲を指定
        sgl_ortho(0.0f, (float)sapp_width(), (float)sapp_height(), 0.0f, -10000.0f, 10000.0f);
    } else {
        // 論理座標系: DPIスケールを考慮
        float dpiScale = sapp_dpi_scale();
        float logicalWidth = (float)sapp_width() / dpiScale;
        float logicalHeight = (float)sapp_height() / dpiScale;
        sgl_ortho(0.0f, logicalWidth, logicalHeight, 0.0f, -10000.0f, 10000.0f);
    }

    sgl_matrix_mode_modelview();
    sgl_load_identity();
}

// 画面クリア (RGB float: 0.0 ~ 1.0)
inline void clear(float r, float g, float b, float a = 1.0f) {
    sg_pass pass = {};
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value = { r, g, b, a };
    // 深度バッファもクリア（3D描画用）
    pass.action.depth.load_action = SG_LOADACTION_CLEAR;
    pass.action.depth.clear_value = 1.0f;
    pass.swapchain = sglue_swapchain();
    sg_begin_pass(&pass);
    internal::inSwapchainPass = true;
}

// 画面クリア (グレースケール)
inline void clear(float gray, float a = 1.0f) {
    clear(gray, gray, gray, a);
}

// 画面クリア (8bit RGB: 0 ~ 255)
inline void clear(int r, int g, int b, int a = 255) {
    clear(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
}

// 画面クリア (Color)
inline void clear(const Color& c) {
    clear(c.r, c.g, c.b, c.a);
}

// パス終了 & コミット（draw の最後で呼ぶ）
inline void present() {
    sgl_draw();
    sg_end_pass();
    internal::inSwapchainPass = false;
    sg_commit();
}

// スワップチェーンパス状態を取得（FBO 用）
inline bool isInSwapchainPass() {
    return internal::inSwapchainPass;
}

// スワップチェーンパスを一時中断（FBO 用）
// FBO 描画前に呼んで、デフォルトコンテキストの内容を flush してからパスを終了する
inline void suspendSwapchainPass() {
    if (internal::inSwapchainPass) {
        sgl_draw();  // デフォルトコンテキストの描画コマンドを flush
        sg_end_pass();
        internal::inSwapchainPass = false;
    }
}

// スワップチェーンパスを再開（FBO 用）
// FBO 描画後に呼んで、スワップチェーンへの描画を継続する
inline void resumeSwapchainPass() {
    if (!internal::inSwapchainPass) {
        sg_pass pass = {};
        pass.action.colors[0].load_action = SG_LOADACTION_LOAD;  // 既存の内容を保持
        pass.action.depth.load_action = SG_LOADACTION_LOAD;
        pass.swapchain = sglue_swapchain();
        sg_begin_pass(&pass);
        internal::inSwapchainPass = true;
        // sokol_gl の状態をリセット
        sgl_defaults();
        beginFrame();  // 投影行列を再設定
    }
}

// ---------------------------------------------------------------------------
// 色設定（RenderContext への委譲）
// ---------------------------------------------------------------------------

// 描画色設定 (float: 0.0 ~ 1.0)
inline void setColor(float r, float g, float b, float a = 1.0f) {
    getDefaultContext().setColor(r, g, b, a);
}

// 描画色設定 (int: 0 ~ 255)
inline void setColor(int r, int g, int b, int a = 255) {
    getDefaultContext().setColor(r, g, b, a);
}

// グレースケール
inline void setColor(float gray, float a = 1.0f) {
    getDefaultContext().setColor(gray, a);
}

inline void setColor(int gray, int a = 255) {
    getDefaultContext().setColor(gray, a);
}

// Color 構造体で設定
inline void setColor(const Color& c) {
    getDefaultContext().setColor(c);
}

// HSB で設定 (H: 0-TAU, S: 0-1, B: 0-1)
inline void setColorHSB(float h, float s, float b, float a = 1.0f) {
    getDefaultContext().setColorHSB(h, s, b, a);
}

// OKLab で設定 (L: 0-1, a: ~-0.4-0.4, b: ~-0.4-0.4)
inline void setColorOKLab(float L, float a_lab, float b_lab, float alpha = 1.0f) {
    getDefaultContext().setColorOKLab(L, a_lab, b_lab, alpha);
}

// OKLCH で設定 (L: 0-1, C: 0-0.4, H: 0-TAU) - 最も知覚的に自然
inline void setColorOKLCH(float L, float C, float H, float alpha = 1.0f) {
    getDefaultContext().setColorOKLCH(L, C, H, alpha);
}

// 塗りつぶしを有効化
inline void fill() {
    getDefaultContext().fill();
}

// 塗りつぶしを無効化
inline void noFill() {
    getDefaultContext().noFill();
}

// ストロークを有効化
inline void stroke() {
    getDefaultContext().stroke();
}

// ストロークを無効化
inline void noStroke() {
    getDefaultContext().noStroke();
}

// ストロークの太さ
inline void setStrokeWeight(float weight) {
    getDefaultContext().setStrokeWeight(weight);
}

// ---------------------------------------------------------------------------
// 変形（RenderContext への委譲）
// ---------------------------------------------------------------------------

// 行列をスタックに保存
inline void pushMatrix() {
    getDefaultContext().pushMatrix();
}

// 行列をスタックから復元
inline void popMatrix() {
    getDefaultContext().popMatrix();
}

// 平行移動
inline void translate(float x, float y) {
    getDefaultContext().translate(x, y);
}

// 3D移動
inline void translate(float x, float y, float z) {
    getDefaultContext().translate(x, y, z);
}

// Z軸回転（ラジアン）
inline void rotate(float radians) {
    getDefaultContext().rotate(radians);
}

// X軸回転（ラジアン）
inline void rotateX(float radians) {
    getDefaultContext().rotateX(radians);
}

// Y軸回転（ラジアン）
inline void rotateY(float radians) {
    getDefaultContext().rotateY(radians);
}

// Z軸回転（ラジアン）- 明示的
inline void rotateZ(float radians) {
    getDefaultContext().rotateZ(radians);
}

// 度数法でも回転できる
inline void rotateDeg(float degrees) {
    getDefaultContext().rotateDeg(degrees);
}

inline void rotateXDeg(float degrees) {
    getDefaultContext().rotateXDeg(degrees);
}

inline void rotateYDeg(float degrees) {
    getDefaultContext().rotateYDeg(degrees);
}

inline void rotateZDeg(float degrees) {
    getDefaultContext().rotateZDeg(degrees);
}

// スケール（均一）
inline void scale(float s) {
    getDefaultContext().scale(s);
}

// スケール（非均一 2D）
inline void scale(float sx, float sy) {
    getDefaultContext().scale(sx, sy);
}

// スケール（非均一 3D）
inline void scale(float sx, float sy, float sz) {
    getDefaultContext().scale(sx, sy, sz);
}

// 現在の変換行列を取得
inline Mat4 getCurrentMatrix() {
    return getDefaultContext().getCurrentMatrix();
}

// 変換行列をリセット
inline void resetMatrix() {
    getDefaultContext().resetMatrix();
}

// 変換行列を直接設定
inline void setMatrix(const Mat4& mat) {
    getDefaultContext().setMatrix(mat);
}

// ---------------------------------------------------------------------------
// 3D描画モード
// ---------------------------------------------------------------------------

// 3D描画モードを有効化（深度テスト + 背面カリング）
inline void enable3D() {
    if (internal::pipeline3dInitialized) {
        sgl_load_pipeline(internal::pipeline3d);
    }
}

// 3D描画モード（パースペクティブ）を有効化
// fov: 視野角（ラジアン）, near/far: クリップ面
inline void enable3DPerspective(float fovY = 0.785f, float nearZ = 0.1f, float farZ = 1000.0f) {
    if (internal::pipeline3dInitialized) {
        sgl_load_pipeline(internal::pipeline3d);
    }
    // パースペクティブ投影を設定
    sgl_matrix_mode_projection();
    sgl_load_identity();
    float dpiScale = sapp_dpi_scale();
    float w = (float)sapp_width() / dpiScale;
    float h = (float)sapp_height() / dpiScale;
    float aspect = w / h;
    sgl_perspective(fovY, aspect, nearZ, farZ);
    sgl_matrix_mode_modelview();
    sgl_load_identity();
}

// 3D描画モードを無効化（デフォルトの2D描画に戻る）
inline void disable3D() {
    sgl_load_default_pipeline();
    // 2D用の正射影に戻す
    beginFrame();
}

// ---------------------------------------------------------------------------
// 基本図形描画（RenderContext への委譲）
// ---------------------------------------------------------------------------

// 四角形 (左上座標 + サイズ)
inline void drawRect(float x, float y, float w, float h) {
    getDefaultContext().drawRect(x, y, w, h);
}

// 円
inline void drawCircle(float cx, float cy, float radius) {
    getDefaultContext().drawCircle(cx, cy, radius);
}

// 楕円
inline void drawEllipse(float cx, float cy, float rx, float ry) {
    getDefaultContext().drawEllipse(cx, cy, rx, ry);
}

// 線
inline void drawLine(float x1, float y1, float x2, float y2) {
    getDefaultContext().drawLine(x1, y1, x2, y2);
}

// 三角形
inline void drawTriangle(float x1, float y1, float x2, float y2, float x3, float y3) {
    getDefaultContext().drawTriangle(x1, y1, x2, y2, x3, y3);
}

// 点
inline void drawPoint(float x, float y) {
    getDefaultContext().drawPoint(x, y);
}

// 円の分割数を設定
inline void setCircleResolution(int res) {
    getDefaultContext().setCircleResolution(res);
}

// ---------------------------------------------------------------------------
// ビットマップ文字列描画（RenderContext への委譲）
// ---------------------------------------------------------------------------

// テキストのバウンディングボックスを計算
inline void getBitmapStringBounds(const std::string& text, float& width, float& height) {
    float maxWidth = 0;
    float cursorX = 0;
    int lines = 1;

    for (char c : text) {
        if (c == '\n') {
            if (cursorX > maxWidth) maxWidth = cursorX;
            cursorX = 0;
            lines++;
        } else if (c == '\t') {
            cursorX += bitmapfont::CHAR_WIDTH * 8;
        } else {
            cursorX += bitmapfont::CHAR_WIDTH;
        }
    }
    if (cursorX > maxWidth) maxWidth = cursorX;

    width = maxWidth;
    height = lines * bitmapfont::CHAR_TEX_HEIGHT;
}

// ビットマップ文字列を描画
// screenFixed = true (デフォルト): スクリーン固定（回転・スケールをキャンセル）
// screenFixed = false: 現在の行列変換に追従（回転・スケールも適用）
inline void drawBitmapString(const std::string& text, float x, float y, bool screenFixed = true) {
    getDefaultContext().drawBitmapString(text, x, y, screenFixed);
}

// ビットマップ文字列を描画（スケール付き）
inline void drawBitmapString(const std::string& text, float x, float y, float scale) {
    getDefaultContext().drawBitmapString(text, x, y, scale);
}

// ビットマップ文字列を背景付きで描画
inline void drawBitmapStringHighlight(const std::string& text, float x, float y,
                                       const Color& background = Color(0, 0, 0),
                                       const Color& foreground = Color(1, 1, 1)) {
    if (text.empty()) return;

    // テキストのサイズを計算
    float textWidth, textHeight;
    getBitmapStringBounds(text, textWidth, textHeight);

    // パディング
    const float padding = 4.0f;

    // ローカル座標をワールド座標に変換
    Mat4 currentMat = getCurrentMatrix();
    float worldX = currentMat.m[0]*x + currentMat.m[1]*y + currentMat.m[3];
    float worldY = currentMat.m[4]*x + currentMat.m[5]*y + currentMat.m[7];

    // 行列を保存
    pushMatrix();
    resetMatrix();

    // アルファブレンドパイプラインで背景を描画
    sgl_load_pipeline(internal::fontPipeline);
    setColor(background);
    drawRect(worldX - padding, worldY - padding,
             textWidth + padding * 2, textHeight + padding * 2);
    sgl_load_default_pipeline();

    popMatrix();

    // 前景色で文字を描画
    setColor(foreground);
    drawBitmapString(text, x, y);
}

// ---------------------------------------------------------------------------
// ウィンドウ制御
// ---------------------------------------------------------------------------

// ウィンドウタイトルを設定
inline void setWindowTitle(const std::string& title) {
    sapp_set_window_title(title.c_str());
}

// ウィンドウサイズを変更（座標系に対応したサイズで指定）
// pixelPerfect=true: フレームバッファサイズで指定
// pixelPerfect=false: 論理サイズで指定
inline void setWindowSize(int width, int height) {
    if (internal::pixelPerfectMode) {
        // ピクセルパーフェクトモード: フレームバッファサイズ → 論理サイズに変換
        float scale = sapp_dpi_scale();
        platform::setWindowSize(static_cast<int>(width / scale), static_cast<int>(height / scale));
    } else {
        // 論理座標モード: そのまま
        platform::setWindowSize(width, height);
    }
}

// フルスクリーン切り替え
inline void setFullscreen(bool full) {
    if (full != sapp_is_fullscreen()) {
        sapp_toggle_fullscreen();
    }
}

// フルスクリーン状態を取得
inline bool isFullscreen() {
    return sapp_is_fullscreen();
}

// フルスクリーンをトグル
inline void toggleFullscreen() {
    sapp_toggle_fullscreen();
}

// ---------------------------------------------------------------------------
// ウィンドウ情報（座標系に対応したサイズ）
// ---------------------------------------------------------------------------

// ウィンドウ幅を取得（座標系に対応したサイズ）
inline int getWindowWidth() {
    if (internal::pixelPerfectMode) {
        return sapp_width();  // フレームバッファサイズ
    }
    return static_cast<int>(sapp_width() / sapp_dpi_scale());  // 論理サイズ
}

// ウィンドウ高さを取得（座標系に対応したサイズ）
inline int getWindowHeight() {
    if (internal::pixelPerfectMode) {
        return sapp_height();  // フレームバッファサイズ
    }
    return static_cast<int>(sapp_height() / sapp_dpi_scale());  // 論理サイズ
}

// アスペクト比
inline float getAspectRatio() {
    return static_cast<float>(sapp_width()) / static_cast<float>(sapp_height());
}

// ---------------------------------------------------------------------------
// 時間
// ---------------------------------------------------------------------------

inline double getElapsedTime() {
    auto now = std::chrono::high_resolution_clock::now();
    if (!internal::startTimeInitialized) {
        internal::startTime = now;
        internal::startTimeInitialized = true;
        return 0.0;
    }
    auto duration = std::chrono::duration<double>(now - internal::startTime);
    return duration.count();
}

inline uint64_t getFrameCount() {
    return sapp_frame_count();
}

inline double getDeltaTime() {
    return sapp_frame_duration();
}

// フレームレート取得（10フレーム移動平均）
inline double getFrameRate() {
    // 現在のフレーム時間をバッファに追加
    double dt = sapp_frame_duration();
    internal::frameTimeBuffer[internal::frameTimeIndex] = dt;
    internal::frameTimeIndex = (internal::frameTimeIndex + 1) % 10;
    if (internal::frameTimeIndex == 0) {
        internal::frameTimeBufferFilled = true;
    }

    // 平均を計算
    int count = internal::frameTimeBufferFilled ? 10 : internal::frameTimeIndex;
    if (count == 0) return 0.0;

    double sum = 0.0;
    for (int i = 0; i < count; i++) {
        sum += internal::frameTimeBuffer[i];
    }
    double avgDt = sum / count;
    return avgDt > 0.0 ? 1.0 / avgDt : 0.0;
}

// ---------------------------------------------------------------------------
// マウス状態（グローバル / ウィンドウ座標）
// ---------------------------------------------------------------------------

// 現在のマウスX座標（ウィンドウ座標）
inline float getGlobalMouseX() {
    return internal::mouseX;
}

// 現在のマウスY座標（ウィンドウ座標）
inline float getGlobalMouseY() {
    return internal::mouseY;
}

// 前フレームのマウスX座標（ウィンドウ座標）
inline float getGlobalPMouseX() {
    return internal::pmouseX;
}

// 前フレームのマウスY座標（ウィンドウ座標）
inline float getGlobalPMouseY() {
    return internal::pmouseY;
}

// マウスボタンが押されているか
inline bool isMousePressed() {
    return internal::mousePressed;
}

// 現在押されているマウスボタン (-1 = なし)
inline int getMouseButton() {
    return internal::mouseButton;
}

// ---------------------------------------------------------------------------
// Loop Architecture (Decoupled Update/Draw)
// ---------------------------------------------------------------------------

// --- Draw Loop 制御 ---

// VSync を有効/無効にする（デフォルト: true）
inline void setDrawVsync(bool enabled) {
    internal::drawVsyncEnabled = enabled;
    if (enabled) {
        internal::drawTargetFps = 0;  // VSync時はFPS指定を無効化
        internal::drawAccumulator = 0.0;  // アキュムレータリセット
    }
}

// 描画FPSを設定
// n > 0: 固定FPS（VSyncは自動的にOFF）
// n <= 0: 自動描画停止、redraw()呼び出し時のみ描画
inline void setDrawFps(int fps) {
    internal::drawTargetFps = fps;
    internal::drawVsyncEnabled = false;  // FPS指定時は常にVSyncをOFF
}

// 現在の描画FPS設定を取得
inline int getDrawFps() {
    return internal::drawTargetFps;
}

// VSync が有効かどうか
inline bool isDrawVsync() {
    return internal::drawVsyncEnabled;
}

// --- Update Loop 制御 ---

// Update を Draw に同期するか設定（デフォルト: true）
// true: update() は draw() の直前に1回呼ばれる
// false: setUpdateFps() の設定に従う
inline void syncUpdateToDraw(bool synced) {
    internal::updateSyncedToDraw = synced;
    if (synced) {
        internal::updateTargetFps = 0;
        internal::updateAccumulator = 0.0;  // アキュムレータリセット
    }
}

// Update の Hz を設定（Draw とは独立）
// n > 0: 指定した Hz で定期的に update() が呼ばれる
// n <= 0: Update ループ停止（イベント駆動のみ）
inline void setUpdateFps(int fps) {
    internal::updateTargetFps = fps;
    if (fps > 0) {
        internal::updateSyncedToDraw = false;  // 独立Update時は同期をOFF
    }
}

// 現在の Update Hz 設定を取得
inline int getUpdateFps() {
    return internal::updateTargetFps;
}

// Update が Draw に同期しているか
inline bool isUpdateSyncedToDraw() {
    return internal::updateSyncedToDraw;
}

// --- ヘルパー関数 ---

// 固定FPSモードに設定（Draw + Update同期）
inline void setFps(int fps) {
    setDrawFps(fps);
    syncUpdateToDraw(true);
}

// VSyncモードに設定（Draw + Update同期）
inline void setVsync(bool enabled) {
    setDrawVsync(enabled);
    syncUpdateToDraw(true);
}

// 再描画をリクエスト（自動描画停止時に使用）
inline void redraw() {
    internal::needsRedraw = true;
}

// ---------------------------------------------------------------------------
// 数学ユーティリティ（tcMath.h から提供）
// ---------------------------------------------------------------------------
// Vec2, Vec3, Vec4, Mat3, Mat4, lerp, clamp, map, radians, degrees など
// 詳細は tcMath.h を参照

// ---------------------------------------------------------------------------
// キーコード定数（sokol_app のキーコードをラップ）
// ---------------------------------------------------------------------------

// 特殊キー
constexpr int KEY_SPACE = SAPP_KEYCODE_SPACE;
constexpr int KEY_ESCAPE = SAPP_KEYCODE_ESCAPE;
constexpr int KEY_ENTER = SAPP_KEYCODE_ENTER;
constexpr int KEY_TAB = SAPP_KEYCODE_TAB;
constexpr int KEY_BACKSPACE = SAPP_KEYCODE_BACKSPACE;
constexpr int KEY_DELETE = SAPP_KEYCODE_DELETE;

// 矢印キー
constexpr int KEY_RIGHT = SAPP_KEYCODE_RIGHT;
constexpr int KEY_LEFT = SAPP_KEYCODE_LEFT;
constexpr int KEY_DOWN = SAPP_KEYCODE_DOWN;
constexpr int KEY_UP = SAPP_KEYCODE_UP;

// 修飾キー
constexpr int KEY_LEFT_SHIFT = SAPP_KEYCODE_LEFT_SHIFT;
constexpr int KEY_RIGHT_SHIFT = SAPP_KEYCODE_RIGHT_SHIFT;
constexpr int KEY_LEFT_CONTROL = SAPP_KEYCODE_LEFT_CONTROL;
constexpr int KEY_RIGHT_CONTROL = SAPP_KEYCODE_RIGHT_CONTROL;
constexpr int KEY_LEFT_ALT = SAPP_KEYCODE_LEFT_ALT;
constexpr int KEY_RIGHT_ALT = SAPP_KEYCODE_RIGHT_ALT;
constexpr int KEY_LEFT_SUPER = SAPP_KEYCODE_LEFT_SUPER;
constexpr int KEY_RIGHT_SUPER = SAPP_KEYCODE_RIGHT_SUPER;

// ファンクションキー
constexpr int KEY_F1 = SAPP_KEYCODE_F1;
constexpr int KEY_F2 = SAPP_KEYCODE_F2;
constexpr int KEY_F3 = SAPP_KEYCODE_F3;
constexpr int KEY_F4 = SAPP_KEYCODE_F4;
constexpr int KEY_F5 = SAPP_KEYCODE_F5;
constexpr int KEY_F6 = SAPP_KEYCODE_F6;
constexpr int KEY_F7 = SAPP_KEYCODE_F7;
constexpr int KEY_F8 = SAPP_KEYCODE_F8;
constexpr int KEY_F9 = SAPP_KEYCODE_F9;
constexpr int KEY_F10 = SAPP_KEYCODE_F10;
constexpr int KEY_F11 = SAPP_KEYCODE_F11;
constexpr int KEY_F12 = SAPP_KEYCODE_F12;

// マウスボタン
constexpr int MOUSE_BUTTON_LEFT = SAPP_MOUSEBUTTON_LEFT;
constexpr int MOUSE_BUTTON_RIGHT = SAPP_MOUSEBUTTON_RIGHT;
constexpr int MOUSE_BUTTON_MIDDLE = SAPP_MOUSEBUTTON_MIDDLE;

// ---------------------------------------------------------------------------
// ウィンドウ設定
// ---------------------------------------------------------------------------

struct WindowSettings {
    int width = 1280;
    int height = 720;
    std::string title = "TrussC App";
    bool highDpi = true;  // High DPI対応（Retinaで鮮明に描画）
    bool pixelPerfect = false;  // true: 座標系=フレームバッファサイズ, false: 座標系=論理サイズ
    int sampleCount = 4;  // MSAA
    bool fullscreen = false;
    // bool headless = false;  // 将来用

    WindowSettings& setSize(int w, int h) {
        width = w;
        height = h;
        return *this;
    }

    WindowSettings& setTitle(const std::string& t) {
        title = t;
        return *this;
    }

    WindowSettings& setHighDpi(bool enabled) {
        highDpi = enabled;
        return *this;
    }

    // ピクセルパーフェクトモード
    // true: 座標系がフレームバッファサイズと一致（Retinaで2560x1440座標）
    // false: 座標系は論理サイズ（Retinaでも1280x720座標）
    WindowSettings& setPixelPerfect(bool enabled) {
        pixelPerfect = enabled;
        return *this;
    }

    WindowSettings& setSampleCount(int count) {
        sampleCount = count;
        return *this;
    }

    WindowSettings& setFullscreen(bool enabled) {
        fullscreen = enabled;
        return *this;
    }
};

// ---------------------------------------------------------------------------
// アプリケーション実行（内部実装）
// ---------------------------------------------------------------------------

namespace internal {
    // アプリインスタンス（void* で保持）
    inline void* appInstance = nullptr;
    inline int currentMouseButton = -1;

    // コールバック関数ポインタ
    inline void (*appSetupFunc)() = nullptr;
    inline void (*appUpdateFunc)() = nullptr;
    inline void (*appDrawFunc)() = nullptr;
    inline void (*appCleanupFunc)() = nullptr;
    inline void (*appKeyPressedFunc)(int) = nullptr;
    inline void (*appKeyReleasedFunc)(int) = nullptr;
    inline void (*appMousePressedFunc)(int, int, int) = nullptr;
    inline void (*appMouseReleasedFunc)(int, int, int) = nullptr;
    inline void (*appMouseMovedFunc)(int, int) = nullptr;
    inline void (*appMouseDraggedFunc)(int, int, int) = nullptr;
    inline void (*appMouseScrolledFunc)(float, float) = nullptr;
    inline void (*appWindowResizedFunc)(int, int) = nullptr;

    inline void _setup_cb() {
        setup();
        if (appSetupFunc) appSetupFunc();
    }

    inline void _frame_cb() {
        auto now = std::chrono::high_resolution_clock::now();

        // タイミング初期化
        if (!lastUpdateTimeInitialized) {
            lastUpdateTime = now;
            lastUpdateTimeInitialized = true;
        }
        if (!lastDrawTimeInitialized) {
            lastDrawTime = now;
            lastDrawTimeInitialized = true;
        }

        // --- Update Loop 処理 ---
        if (updateSyncedToDraw) {
            // Draw に同期: 後で shouldDraw と連動
        } else if (updateTargetFps > 0) {
            // 独立した固定 Hz で Update
            double updateInterval = 1.0 / updateTargetFps;
            double elapsed = std::chrono::duration<double>(now - lastUpdateTime).count();
            updateAccumulator += elapsed;
            lastUpdateTime = now;

            while (updateAccumulator >= updateInterval) {
                if (appUpdateFunc) appUpdateFunc();
                updateAccumulator -= updateInterval;
            }
        }
        // updateTargetFps <= 0 の場合は Update しない（イベント駆動）

        // --- Draw Loop 処理 ---
        bool shouldDraw = false;

        if (drawVsyncEnabled) {
            // VSync: 毎フレーム描画（sokol_app がタイミング制御）
            shouldDraw = true;
        } else if (drawTargetFps > 0) {
            // 固定FPS: フレームスキップで制御
            double drawInterval = 1.0 / drawTargetFps;
            double elapsed = std::chrono::duration<double>(now - lastDrawTime).count();
            drawAccumulator += elapsed;
            lastDrawTime = now;

            if (drawAccumulator >= drawInterval) {
                shouldDraw = true;
                // 1回分だけ消費（複数フレーム溜まっても1回だけ描画）
                drawAccumulator -= drawInterval;
                // 溜まりすぎ防止
                if (drawAccumulator > drawInterval) {
                    drawAccumulator = 0.0;
                }
            }
        } else {
            // 自動描画停止: redraw() 時のみ描画
            shouldDraw = needsRedraw;
        }

        if (shouldDraw) {
            beginFrame();

            // Update が Draw に同期している場合、ここで Update
            if (updateSyncedToDraw && appUpdateFunc) {
                appUpdateFunc();
            }

            if (appDrawFunc) appDrawFunc();
            present();
            needsRedraw = false;
        }

        // 前フレームのマウス位置を保存
        pmouseX = mouseX;
        pmouseY = mouseY;
    }

    inline void _cleanup_cb() {
        if (appCleanupFunc) appCleanupFunc();
        cleanup();
    }

    inline void _event_cb(const sapp_event* ev) {
        // ev->mouse_x/y はフレームバッファ座標で届く
        // pixelPerfectMode = true: そのまま使う（座標系=フレームバッファサイズ）
        // pixelPerfectMode = false: DPIスケールで割って論理座標に変換
        float scale = pixelPerfectMode ? 1.0f : (1.0f / sapp_dpi_scale());
        bool hasModShift = (ev->modifiers & SAPP_MODIFIER_SHIFT) != 0;
        bool hasModCtrl = (ev->modifiers & SAPP_MODIFIER_CTRL) != 0;
        bool hasModAlt = (ev->modifiers & SAPP_MODIFIER_ALT) != 0;
        bool hasModSuper = (ev->modifiers & SAPP_MODIFIER_SUPER) != 0;

        switch (ev->type) {
            case SAPP_EVENTTYPE_KEY_DOWN: {
                // イベントシステムに通知
                KeyEventArgs args;
                args.key = ev->key_code;
                args.isRepeat = ev->key_repeat;
                args.shift = hasModShift;
                args.ctrl = hasModCtrl;
                args.alt = hasModAlt;
                args.super = hasModSuper;
                events().keyPressed.notify(args);

                // 従来のコールバック（互換性）
                if (!ev->key_repeat && appKeyPressedFunc) {
                    appKeyPressedFunc(ev->key_code);
                }
                break;
            }
            case SAPP_EVENTTYPE_KEY_UP: {
                KeyEventArgs args;
                args.key = ev->key_code;
                args.isRepeat = false;
                args.shift = hasModShift;
                args.ctrl = hasModCtrl;
                args.alt = hasModAlt;
                args.super = hasModSuper;
                events().keyReleased.notify(args);

                if (appKeyReleasedFunc) appKeyReleasedFunc(ev->key_code);
                break;
            }
            case SAPP_EVENTTYPE_MOUSE_DOWN: {
                currentMouseButton = ev->mouse_button;
                mouseX = ev->mouse_x * scale;
                mouseY = ev->mouse_y * scale;
                mouseButton = ev->mouse_button;
                mousePressed = true;

                MouseEventArgs args;
                args.x = mouseX;
                args.y = mouseY;
                args.button = ev->mouse_button;
                args.shift = hasModShift;
                args.ctrl = hasModCtrl;
                args.alt = hasModAlt;
                args.super = hasModSuper;
                events().mousePressed.notify(args);

                if (appMousePressedFunc) appMousePressedFunc((int)mouseX, (int)mouseY, ev->mouse_button);
                break;
            }
            case SAPP_EVENTTYPE_MOUSE_UP: {
                currentMouseButton = -1;
                mouseX = ev->mouse_x * scale;
                mouseY = ev->mouse_y * scale;
                mouseButton = -1;
                mousePressed = false;

                MouseEventArgs args;
                args.x = mouseX;
                args.y = mouseY;
                args.button = ev->mouse_button;
                args.shift = hasModShift;
                args.ctrl = hasModCtrl;
                args.alt = hasModAlt;
                args.super = hasModSuper;
                events().mouseReleased.notify(args);

                if (appMouseReleasedFunc) appMouseReleasedFunc((int)mouseX, (int)mouseY, ev->mouse_button);
                break;
            }
            case SAPP_EVENTTYPE_MOUSE_MOVE: {
                float prevX = mouseX;
                float prevY = mouseY;
                mouseX = ev->mouse_x * scale;
                mouseY = ev->mouse_y * scale;

                if (currentMouseButton >= 0) {
                    MouseDragEventArgs args;
                    args.x = mouseX;
                    args.y = mouseY;
                    args.deltaX = mouseX - prevX;
                    args.deltaY = mouseY - prevY;
                    args.button = currentMouseButton;
                    events().mouseDragged.notify(args);

                    if (appMouseDraggedFunc) appMouseDraggedFunc((int)mouseX, (int)mouseY, currentMouseButton);
                } else {
                    MouseMoveEventArgs args;
                    args.x = mouseX;
                    args.y = mouseY;
                    args.deltaX = mouseX - prevX;
                    args.deltaY = mouseY - prevY;
                    events().mouseMoved.notify(args);

                    if (appMouseMovedFunc) appMouseMovedFunc((int)mouseX, (int)mouseY);
                }
                break;
            }
            case SAPP_EVENTTYPE_MOUSE_SCROLL: {
                ScrollEventArgs args;
                args.scrollX = ev->scroll_x;
                args.scrollY = ev->scroll_y;
                events().mouseScrolled.notify(args);

                if (appMouseScrolledFunc) appMouseScrolledFunc(ev->scroll_x, ev->scroll_y);
                break;
            }
            case SAPP_EVENTTYPE_RESIZED: {
                int w = static_cast<int>(ev->window_width * scale);
                int h = static_cast<int>(ev->window_height * scale);

                ResizeEventArgs args;
                args.width = w;
                args.height = h;
                events().windowResized.notify(args);

                if (appWindowResizedFunc) appWindowResizedFunc(w, h);
                break;
            }
            default:
                break;
        }
    }
}

// ---------------------------------------------------------------------------
// アプリケーション実行
// ---------------------------------------------------------------------------

template<typename AppClass>
int runApp(const WindowSettings& settings = WindowSettings()) {
    // ピクセルパーフェクトモードを設定
    internal::pixelPerfectMode = settings.pixelPerfect;

    // アプリインスタンスを生成
    static AppClass* app = nullptr;

    // コールバックを設定
    internal::appSetupFunc = []() {
        app = new AppClass();
        app->setup();
    };
    internal::appUpdateFunc = []() {
        if (app) app->updateTree();  // シーングラフ全体を更新
    };
    internal::appDrawFunc = []() {
        if (app) app->drawTree();  // シーングラフ全体を描画
    };
    internal::appCleanupFunc = []() {
        if (app) {
            app->cleanup();
            delete app;
            app = nullptr;
        }
    };
    internal::appKeyPressedFunc = [](int key) {
        if (app) app->keyPressed(key);
    };
    internal::appKeyReleasedFunc = [](int key) {
        if (app) app->keyReleased(key);
    };
    internal::appMousePressedFunc = [](int x, int y, int button) {
        if (app) app->mousePressed(x, y, button);
    };
    internal::appMouseReleasedFunc = [](int x, int y, int button) {
        if (app) app->mouseReleased(x, y, button);
    };
    internal::appMouseMovedFunc = [](int x, int y) {
        if (app) app->mouseMoved(x, y);
    };
    internal::appMouseDraggedFunc = [](int x, int y, int button) {
        if (app) app->mouseDragged(x, y, button);
    };
    internal::appMouseScrolledFunc = [](float dx, float dy) {
        if (app) app->mouseScrolled(dx, dy);
    };
    internal::appWindowResizedFunc = [](int w, int h) {
        if (app) app->windowResized(w, h);
    };

    // sapp_desc を構築
    sapp_desc desc = {};
    if (settings.pixelPerfect) {
        // ピクセルパーフェクトの場合、指定サイズをフレームバッファサイズとして扱い、
        // 論理ウィンドウサイズに変換する
        float displayScale = platform::getDisplayScaleFactor();
        desc.width = static_cast<int>(settings.width / displayScale);
        desc.height = static_cast<int>(settings.height / displayScale);
    } else {
        desc.width = settings.width;
        desc.height = settings.height;
    }
    desc.window_title = settings.title.c_str();
    desc.high_dpi = settings.highDpi;
    desc.sample_count = settings.sampleCount;
    desc.fullscreen = settings.fullscreen;
    desc.init_cb = internal::_setup_cb;
    desc.frame_cb = internal::_frame_cb;
    desc.cleanup_cb = internal::_cleanup_cb;
    desc.event_cb = internal::_event_cb;
    desc.logger.func = slog_func;

    // アプリを実行
    sapp_run(&desc);

    return 0;
}

} // namespace trussc

// TrussC シェイプ描画
#include "tc/graphics/tcShape.h"

// TrussC ポリライン
#include "tc/graphics/tcPolyline.h"

// TrussC メッシュ
#include "tc/graphics/tcMesh.h"

// TrussC ストロークメッシュ（太線描画）
#include "tc/graphics/tcStrokeMesh.h"

// TrussC 画像
#include "tc/graphics/tcImage.h"

// TrussC FBO（オフスクリーンレンダリング）
#include "tc/gl/tcFbo.h"

// TrussC 3Dプリミティブ
#include <map>
#include "tc/3d/tcPrimitives.h"

// TrussC EasyCam（3Dカメラ）
#include "tc/3d/tcEasyCam.h"

// 短縮エイリアス
namespace tc = trussc;
