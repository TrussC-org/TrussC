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

// バージョン情報
constexpr int VERSION_MAJOR = 0;
constexpr int VERSION_MINOR = 0;
constexpr int VERSION_PATCH = 1;

// 数学定数は tcMath.h で定義: TAU, HALF_TAU, QUARTER_TAU, PI

// ---------------------------------------------------------------------------
// LoopMode
// ---------------------------------------------------------------------------
enum class LoopMode {
    Game,   // 毎フレーム自動的にupdate/drawが呼ばれる（デフォルト）
    Demand  // redraw()が呼ばれた時だけupdate/drawが呼ばれる（省電力モード）
};

// ---------------------------------------------------------------------------
// 内部状態
// ---------------------------------------------------------------------------
namespace internal {
    // 現在の描画色
    inline float currentR = 1.0f;
    inline float currentG = 1.0f;
    inline float currentB = 1.0f;
    inline float currentA = 1.0f;

    // 塗りつぶし / ストローク
    inline bool fillEnabled = true;
    inline bool strokeEnabled = false;
    inline float strokeWeight = 1.0f;

    // 円の分割数（oFと同じデフォルト値）
    inline int circleResolution = 20;

    // LoopMode
    inline LoopMode loopMode = LoopMode::Game;
    inline bool needsRedraw = true;

    // マウス状態
    inline float mouseX = 0.0f;
    inline float mouseY = 0.0f;
    inline float pmouseX = 0.0f;  // 前フレームのマウス位置
    inline float pmouseY = 0.0f;
    inline int mouseButton = -1;  // 現在押されているボタン (-1 = なし)
    inline bool mousePressed = false;

    // 行列スタック（自前管理）
    inline Mat4 currentMatrix = Mat4::identity();
    inline std::vector<Mat4> matrixStack;

    // ビットマップフォント用テクスチャ
    inline sg_image fontTexture = {};
    inline sg_view fontView = {};
    inline sg_sampler fontSampler = {};
    inline sgl_pipeline fontPipeline = {};
    inline bool fontInitialized = false;

    // ピクセルパーフェクトモード（座標系=フレームバッファサイズ）
    inline bool pixelPerfectMode = false;

    // 3D描画用パイプライン（深度テスト + 背面カリング）
    inline sgl_pipeline pipeline3d = {};
    inline bool pipeline3dInitialized = false;

    // フレームレート計測用（10フレーム移動平均）
    inline double frameTimeBuffer[10] = {};
    inline int frameTimeIndex = 0;
    inline bool frameTimeBufferFilled = false;

    // 経過時間計測用
    inline std::chrono::high_resolution_clock::time_point startTime;
    inline bool startTimeInitialized = false;
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
    sg_commit();
}

// ---------------------------------------------------------------------------
// 色設定
// ---------------------------------------------------------------------------

// 描画色設定 (float: 0.0 ~ 1.0)
inline void setColor(float r, float g, float b, float a = 1.0f) {
    internal::currentR = r;
    internal::currentG = g;
    internal::currentB = b;
    internal::currentA = a;
}

// 描画色設定 (int: 0 ~ 255)
inline void setColor(int r, int g, int b, int a = 255) {
    setColor(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
}

// グレースケール
inline void setColor(float gray, float a = 1.0f) {
    setColor(gray, gray, gray, a);
}

inline void setColor(int gray, int a = 255) {
    setColor(gray, gray, gray, a);
}

// Color 構造体で設定
inline void setColor(const Color& c) {
    setColor(c.r, c.g, c.b, c.a);
}

// HSB で設定 (H: 0-TAU, S: 0-1, B: 0-1)
inline void setColorHSB(float h, float s, float b, float a = 1.0f) {
    Color c = ColorHSB(h, s, b, a).toRGB();
    setColor(c.r, c.g, c.b, c.a);
}

// OKLab で設定 (L: 0-1, a: ~-0.4-0.4, b: ~-0.4-0.4)
inline void setColorOKLab(float L, float a_lab, float b_lab, float alpha = 1.0f) {
    Color c = ColorOKLab(L, a_lab, b_lab, alpha).toRGB();
    setColor(c.r, c.g, c.b, c.a);
}

// OKLCH で設定 (L: 0-1, C: 0-0.4, H: 0-TAU) - 最も知覚的に自然
inline void setColorOKLCH(float L, float C, float H, float alpha = 1.0f) {
    Color c = ColorOKLCH(L, C, H, alpha).toRGB();
    setColor(c.r, c.g, c.b, c.a);
}

// 塗りつぶしを有効化
inline void fill() {
    internal::fillEnabled = true;
}

// 塗りつぶしを無効化
inline void noFill() {
    internal::fillEnabled = false;
}

// ストロークを有効化
inline void stroke() {
    internal::strokeEnabled = true;
}

// ストロークを無効化
inline void noStroke() {
    internal::strokeEnabled = false;
}

// ストロークの太さ
inline void setStrokeWeight(float weight) {
    internal::strokeWeight = weight;
}

// ---------------------------------------------------------------------------
// 変形（自前の行列スタック管理）
// ---------------------------------------------------------------------------

// 内部: 自前の行列を sokol_gl に同期
inline void syncMatrixToSokol() {
    sgl_load_matrix(internal::currentMatrix.m);
}

// 行列をスタックに保存
inline void pushMatrix() {
    internal::matrixStack.push_back(internal::currentMatrix);
    sgl_push_matrix();
}

// 行列をスタックから復元
inline void popMatrix() {
    if (!internal::matrixStack.empty()) {
        internal::currentMatrix = internal::matrixStack.back();
        internal::matrixStack.pop_back();
    }
    sgl_pop_matrix();
}

// 平行移動
inline void translate(float x, float y) {
    internal::currentMatrix = internal::currentMatrix * Mat4::translate(x, y, 0.0f);
    sgl_translate(x, y, 0.0f);
}

// 3D移動
inline void translate(float x, float y, float z) {
    internal::currentMatrix = internal::currentMatrix * Mat4::translate(x, y, z);
    sgl_translate(x, y, z);
}

// Z軸回転（ラジアン）
inline void rotate(float radians) {
    internal::currentMatrix = internal::currentMatrix * Mat4::rotateZ(radians);
    sgl_rotate(radians, 0.0f, 0.0f, 1.0f);
}

// X軸回転（ラジアン）
inline void rotateX(float radians) {
    internal::currentMatrix = internal::currentMatrix * Mat4::rotateX(radians);
    sgl_rotate(radians, 1.0f, 0.0f, 0.0f);
}

// Y軸回転（ラジアン）
inline void rotateY(float radians) {
    internal::currentMatrix = internal::currentMatrix * Mat4::rotateY(radians);
    sgl_rotate(radians, 0.0f, 1.0f, 0.0f);
}

// Z軸回転（ラジアン）- 明示的
inline void rotateZ(float radians) {
    internal::currentMatrix = internal::currentMatrix * Mat4::rotateZ(radians);
    sgl_rotate(radians, 0.0f, 0.0f, 1.0f);
}

// 度数法でも回転できる
inline void rotateDeg(float degrees) {
    rotate(degrees * PI / 180.0f);
}

inline void rotateXDeg(float degrees) {
    rotateX(degrees * PI / 180.0f);
}

inline void rotateYDeg(float degrees) {
    rotateY(degrees * PI / 180.0f);
}

inline void rotateZDeg(float degrees) {
    rotateZ(degrees * PI / 180.0f);
}

// スケール（均一）
inline void scale(float s) {
    internal::currentMatrix = internal::currentMatrix * Mat4::scale(s, s, 1.0f);
    sgl_scale(s, s, 1.0f);
}

// スケール（非均一 2D）
inline void scale(float sx, float sy) {
    internal::currentMatrix = internal::currentMatrix * Mat4::scale(sx, sy, 1.0f);
    sgl_scale(sx, sy, 1.0f);
}

// スケール（非均一 3D）
inline void scale(float sx, float sy, float sz) {
    internal::currentMatrix = internal::currentMatrix * Mat4::scale(sx, sy, sz);
    sgl_scale(sx, sy, sz);
}

// 現在の変換行列を取得
inline Mat4 getCurrentMatrix() {
    return internal::currentMatrix;
}

// 変換行列をリセット
inline void resetMatrix() {
    internal::currentMatrix = Mat4::identity();
    sgl_load_identity();
}

// 変換行列を直接設定
inline void setMatrix(const Mat4& mat) {
    internal::currentMatrix = mat;
    syncMatrixToSokol();
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
// 基本図形描画
// ---------------------------------------------------------------------------

// 四角形 (左上座標 + サイズ)
inline void drawRect(float x, float y, float w, float h) {
    if (internal::fillEnabled) {
        sgl_begin_quads();
        sgl_c4f(internal::currentR, internal::currentG, internal::currentB, internal::currentA);
        sgl_v2f(x, y);
        sgl_v2f(x + w, y);
        sgl_v2f(x + w, y + h);
        sgl_v2f(x, y + h);
        sgl_end();
    }
    if (internal::strokeEnabled) {
        sgl_begin_line_strip();
        sgl_c4f(internal::currentR, internal::currentG, internal::currentB, internal::currentA);
        sgl_v2f(x, y);
        sgl_v2f(x + w, y);
        sgl_v2f(x + w, y + h);
        sgl_v2f(x, y + h);
        sgl_v2f(x, y);
        sgl_end();
    }
}

// 円
inline void drawCircle(float cx, float cy, float radius) {
    int segments = internal::circleResolution;

    if (internal::fillEnabled) {
        sgl_begin_triangle_strip();
        sgl_c4f(internal::currentR, internal::currentG, internal::currentB, internal::currentA);
        for (int i = 0; i <= segments; i++) {
            float angle = (float)i / segments * TAU;
            float px = cx + cos(angle) * radius;
            float py = cy + sin(angle) * radius;
            sgl_v2f(cx, cy);
            sgl_v2f(px, py);
        }
        sgl_end();
    }
    if (internal::strokeEnabled) {
        sgl_begin_line_strip();
        sgl_c4f(internal::currentR, internal::currentG, internal::currentB, internal::currentA);
        for (int i = 0; i <= segments; i++) {
            float angle = (float)i / segments * TAU;
            float px = cx + cos(angle) * radius;
            float py = cy + sin(angle) * radius;
            sgl_v2f(px, py);
        }
        sgl_end();
    }
}

// 楕円
inline void drawEllipse(float cx, float cy, float rx, float ry) {
    int segments = internal::circleResolution;

    if (internal::fillEnabled) {
        sgl_begin_triangle_strip();
        sgl_c4f(internal::currentR, internal::currentG, internal::currentB, internal::currentA);
        for (int i = 0; i <= segments; i++) {
            float angle = (float)i / segments * TAU;
            float px = cx + cos(angle) * rx;
            float py = cy + sin(angle) * ry;
            sgl_v2f(cx, cy);
            sgl_v2f(px, py);
        }
        sgl_end();
    }
    if (internal::strokeEnabled) {
        sgl_begin_line_strip();
        sgl_c4f(internal::currentR, internal::currentG, internal::currentB, internal::currentA);
        for (int i = 0; i <= segments; i++) {
            float angle = (float)i / segments * TAU;
            float px = cx + cos(angle) * rx;
            float py = cy + sin(angle) * ry;
            sgl_v2f(px, py);
        }
        sgl_end();
    }
}

// 線
inline void drawLine(float x1, float y1, float x2, float y2) {
    sgl_begin_lines();
    sgl_c4f(internal::currentR, internal::currentG, internal::currentB, internal::currentA);
    sgl_v2f(x1, y1);
    sgl_v2f(x2, y2);
    sgl_end();
}

// 三角形
inline void drawTriangle(float x1, float y1, float x2, float y2, float x3, float y3) {
    if (internal::fillEnabled) {
        sgl_begin_triangles();
        sgl_c4f(internal::currentR, internal::currentG, internal::currentB, internal::currentA);
        sgl_v2f(x1, y1);
        sgl_v2f(x2, y2);
        sgl_v2f(x3, y3);
        sgl_end();
    }
    if (internal::strokeEnabled) {
        sgl_begin_line_strip();
        sgl_c4f(internal::currentR, internal::currentG, internal::currentB, internal::currentA);
        sgl_v2f(x1, y1);
        sgl_v2f(x2, y2);
        sgl_v2f(x3, y3);
        sgl_v2f(x1, y1);
        sgl_end();
    }
}

// 点
inline void drawPoint(float x, float y) {
    sgl_begin_points();
    sgl_c4f(internal::currentR, internal::currentG, internal::currentB, internal::currentA);
    sgl_v2f(x, y);
    sgl_end();
}

// 円の分割数を設定
inline void setCircleResolution(int res) {
    internal::circleResolution = res;
}

// ---------------------------------------------------------------------------
// ビットマップ文字列描画（テクスチャアトラス方式）
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

// ビットマップ文字列を描画（回転・スケールをキャンセルしてtranslate成分のみ使用）
// openFrameworks の ofDrawBitmapString と同様の動作
inline void drawBitmapString(const std::string& text, float x, float y) {
    if (text.empty() || !internal::fontInitialized) return;

    // ローカル座標をワールド座標に変換（行列全体を適用）
    // Mat4は行優先: m[0],m[1]が1行目、m[3]=tx, m[7]=ty
    Mat4 currentMat = getCurrentMatrix();
    float worldX = currentMat.m[0]*x + currentMat.m[1]*y + currentMat.m[3];
    float worldY = currentMat.m[4]*x + currentMat.m[5]*y + currentMat.m[7];

    // 行列を保存して、translate のみの行列に切り替え
    pushMatrix();
    resetMatrix();
    translate(worldX, worldY);

    // アルファブレンドパイプラインとテクスチャを有効化
    sgl_load_pipeline(internal::fontPipeline);
    sgl_enable_texture();
    sgl_texture(internal::fontView, internal::fontSampler);

    // 全文字をバッチで描画
    sgl_begin_quads();
    sgl_c4f(internal::currentR, internal::currentG, internal::currentB, internal::currentA);

    float cursorX = 0;
    float cursorY = 0;
    const float charW = bitmapfont::CHAR_TEX_WIDTH;
    const float charH = bitmapfont::CHAR_TEX_HEIGHT;

    for (char c : text) {
        // 改行処理
        if (c == '\n') {
            cursorX = 0;
            cursorY += charH;
            continue;
        }

        // タブ処理（8文字分のスペース、oFと同じ）
        if (c == '\t') {
            cursorX += charW * 8;
            continue;
        }

        // 制御文字はスキップ
        if (c < 32) continue;

        // テクスチャ座標を取得
        float u, v;
        bitmapfont::getCharTexCoord(c, u, v);
        float u2 = u + bitmapfont::TEX_CHAR_WIDTH;
        float v2 = v + bitmapfont::TEX_CHAR_HEIGHT;

        // 四角形を描画（テクスチャ座標付き）
        float px = cursorX;
        float py = cursorY;

        sgl_v2f_t2f(px, py, u, v);
        sgl_v2f_t2f(px + charW, py, u2, v);
        sgl_v2f_t2f(px + charW, py + charH, u2, v2);
        sgl_v2f_t2f(px, py + charH, u, v2);

        cursorX += charW;
    }

    sgl_end();
    sgl_disable_texture();
    sgl_load_default_pipeline();

    // 行列を復元
    popMatrix();
}

// ビットマップ文字列を描画（スケール付き）
inline void drawBitmapString(const std::string& text, float x, float y, float scale) {
    if (text.empty() || !internal::fontInitialized) return;

    // ローカル座標をワールド座標に変換（行列全体を適用）
    // Mat4は行優先: m[0],m[1]が1行目、m[3]=tx, m[7]=ty
    Mat4 currentMat = getCurrentMatrix();
    float worldX = currentMat.m[0]*x + currentMat.m[1]*y + currentMat.m[3];
    float worldY = currentMat.m[4]*x + currentMat.m[5]*y + currentMat.m[7];

    // 行列を保存して、translate のみの行列に切り替え
    pushMatrix();
    resetMatrix();
    translate(worldX, worldY);

    // アルファブレンドパイプラインとテクスチャを有効化
    sgl_load_pipeline(internal::fontPipeline);
    sgl_enable_texture();
    sgl_texture(internal::fontView, internal::fontSampler);

    // 全文字をバッチで描画
    sgl_begin_quads();
    sgl_c4f(internal::currentR, internal::currentG, internal::currentB, internal::currentA);

    float cursorX = 0;
    float cursorY = 0;
    const float charW = bitmapfont::CHAR_TEX_WIDTH * scale;
    const float charH = bitmapfont::CHAR_TEX_HEIGHT * scale;

    for (char c : text) {
        // 改行処理
        if (c == '\n') {
            cursorX = 0;
            cursorY += charH;
            continue;
        }

        // タブ処理（8文字分のスペース）
        if (c == '\t') {
            cursorX += charW * 8;
            continue;
        }

        // 制御文字はスキップ
        if (c < 32) continue;

        // テクスチャ座標を取得
        float u, v;
        bitmapfont::getCharTexCoord(c, u, v);
        float u2 = u + bitmapfont::TEX_CHAR_WIDTH;
        float v2 = v + bitmapfont::TEX_CHAR_HEIGHT;

        // 四角形を描画（テクスチャ座標付き）
        float px = cursorX;
        float py = cursorY;

        sgl_v2f_t2f(px, py, u, v);
        sgl_v2f_t2f(px + charW, py, u2, v);
        sgl_v2f_t2f(px + charW, py + charH, u2, v2);
        sgl_v2f_t2f(px, py + charH, u, v2);

        cursorX += charW;
    }

    sgl_end();
    sgl_disable_texture();
    sgl_load_default_pipeline();

    // 行列を復元
    popMatrix();
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

    // ローカル座標をワールド座標に変換（行列全体を適用）
    // Mat4は行優先: m[0],m[1]が1行目、m[3]=tx, m[7]=ty
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
// LoopMode 制御
// ---------------------------------------------------------------------------

// LoopModeを設定
inline void setLoopMode(LoopMode mode) {
    internal::loopMode = mode;
    if (mode == LoopMode::Game) {
        internal::needsRedraw = true;
    }
}

// 現在のLoopModeを取得
inline LoopMode getLoopMode() {
    return internal::loopMode;
}

// 再描画をリクエスト（Demandモードで使用）
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
        if (loopMode == LoopMode::Game || needsRedraw) {
            beginFrame();
            if (appUpdateFunc) appUpdateFunc();
            if (appDrawFunc) appDrawFunc();
            present();
            needsRedraw = false;
        }
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

// TrussC 3Dプリミティブ
#include <map>
#include "tc/3d/tcPrimitives.h"

// 短縮エイリアス
namespace tc = trussc;
