#pragma once

// =============================================================================
// RenderContext - 描画状態を保持するコンテキストクラス
// =============================================================================
//
// 将来的なマルチコンテキスト対応のための基盤。
// 現在はデフォルトコンテキスト（シングルトン）として使用。
//
// 使い方:
//   // グローバル関数（従来通り）
//   setColor(255, 0, 0);
//   drawRect(10, 10, 100, 100);
//
//   // コンテキスト明示（将来用）
//   auto& ctx = getDefaultContext();
//   ctx.setColor(255, 0, 0);
//   ctx.drawRect(10, 10, 100, 100);
//
// =============================================================================

// このファイルは TrussC.h からインクルードされるため、
// 必要なヘッダーは既にインクルード済み
// tcMath.h, tcColor.h, tcBitmapFont.h, sokol_gl.h

#include <vector>
#include <string>

namespace trussc {

// 前方宣言
namespace internal {
    extern sg_view fontView;
    extern sg_sampler fontSampler;
    extern sgl_pipeline fontPipeline;
    extern bool fontInitialized;
    extern sgl_pipeline pipeline3d;
    extern bool pipeline3dInitialized;
    extern bool pixelPerfectMode;
}

// ---------------------------------------------------------------------------
// RenderContext クラス
// ---------------------------------------------------------------------------
class RenderContext {
public:
    // -----------------------------------------------------------------------
    // コンストラクタ・デストラクタ
    // -----------------------------------------------------------------------
    RenderContext() = default;
    ~RenderContext() = default;

    // コピー禁止（状態を共有すると混乱するため）
    RenderContext(const RenderContext&) = delete;
    RenderContext& operator=(const RenderContext&) = delete;

    // ムーブは許可
    RenderContext(RenderContext&&) = default;
    RenderContext& operator=(RenderContext&&) = default;

    // -----------------------------------------------------------------------
    // 色設定
    // -----------------------------------------------------------------------

    // 描画色設定 (float: 0.0 ~ 1.0)
    void setColor(float r, float g, float b, float a = 1.0f) {
        currentR_ = r;
        currentG_ = g;
        currentB_ = b;
        currentA_ = a;
    }

    // 描画色設定 (int: 0 ~ 255)
    void setColor(int r, int g, int b, int a = 255) {
        setColor(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
    }

    // グレースケール
    void setColor(float gray, float a = 1.0f) {
        setColor(gray, gray, gray, a);
    }

    void setColor(int gray, int a = 255) {
        setColor(gray, gray, gray, a);
    }

    // Color 構造体で設定
    void setColor(const Color& c) {
        setColor(c.r, c.g, c.b, c.a);
    }

    // HSB で設定 (H: 0-TAU, S: 0-1, B: 0-1)
    void setColorHSB(float h, float s, float b, float a = 1.0f) {
        Color c = ColorHSB(h, s, b, a).toRGB();
        setColor(c.r, c.g, c.b, c.a);
    }

    // OKLab で設定
    void setColorOKLab(float L, float a_lab, float b_lab, float alpha = 1.0f) {
        Color c = ColorOKLab(L, a_lab, b_lab, alpha).toRGB();
        setColor(c.r, c.g, c.b, c.a);
    }

    // OKLCH で設定
    void setColorOKLCH(float L, float C, float H, float alpha = 1.0f) {
        Color c = ColorOKLCH(L, C, H, alpha).toRGB();
        setColor(c.r, c.g, c.b, c.a);
    }

    // 現在の色を取得
    Color getColor() const {
        return Color(currentR_, currentG_, currentB_, currentA_);
    }

    // -----------------------------------------------------------------------
    // 塗りつぶし / ストローク
    // -----------------------------------------------------------------------

    void fill() { fillEnabled_ = true; }
    void noFill() { fillEnabled_ = false; }
    void stroke() { strokeEnabled_ = true; }
    void noStroke() { strokeEnabled_ = false; }
    void setStrokeWeight(float weight) { strokeWeight_ = weight; }

    bool isFillEnabled() const { return fillEnabled_; }
    bool isStrokeEnabled() const { return strokeEnabled_; }
    float getStrokeWeight() const { return strokeWeight_; }

    // -----------------------------------------------------------------------
    // 円の分割数
    // -----------------------------------------------------------------------

    void setCircleResolution(int res) { circleResolution_ = res; }
    int getCircleResolution() const { return circleResolution_; }

    // -----------------------------------------------------------------------
    // 行列操作
    // -----------------------------------------------------------------------

    void pushMatrix() {
        matrixStack_.push_back(currentMatrix_);
        sgl_push_matrix();
    }

    void popMatrix() {
        if (!matrixStack_.empty()) {
            currentMatrix_ = matrixStack_.back();
            matrixStack_.pop_back();
        }
        sgl_pop_matrix();
    }

    void translate(float x, float y) {
        currentMatrix_ = currentMatrix_ * Mat4::translate(x, y, 0.0f);
        sgl_translate(x, y, 0.0f);
    }

    void translate(float x, float y, float z) {
        currentMatrix_ = currentMatrix_ * Mat4::translate(x, y, z);
        sgl_translate(x, y, z);
    }

    void rotate(float radians) {
        currentMatrix_ = currentMatrix_ * Mat4::rotateZ(radians);
        sgl_rotate(radians, 0.0f, 0.0f, 1.0f);
    }

    void rotateX(float radians) {
        currentMatrix_ = currentMatrix_ * Mat4::rotateX(radians);
        sgl_rotate(radians, 1.0f, 0.0f, 0.0f);
    }

    void rotateY(float radians) {
        currentMatrix_ = currentMatrix_ * Mat4::rotateY(radians);
        sgl_rotate(radians, 0.0f, 1.0f, 0.0f);
    }

    void rotateZ(float radians) {
        currentMatrix_ = currentMatrix_ * Mat4::rotateZ(radians);
        sgl_rotate(radians, 0.0f, 0.0f, 1.0f);
    }

    void rotateDeg(float degrees) { rotate(degrees * PI / 180.0f); }
    void rotateXDeg(float degrees) { rotateX(degrees * PI / 180.0f); }
    void rotateYDeg(float degrees) { rotateY(degrees * PI / 180.0f); }
    void rotateZDeg(float degrees) { rotateZ(degrees * PI / 180.0f); }

    void scale(float s) {
        currentMatrix_ = currentMatrix_ * Mat4::scale(s, s, 1.0f);
        sgl_scale(s, s, 1.0f);
    }

    void scale(float sx, float sy) {
        currentMatrix_ = currentMatrix_ * Mat4::scale(sx, sy, 1.0f);
        sgl_scale(sx, sy, 1.0f);
    }

    void scale(float sx, float sy, float sz) {
        currentMatrix_ = currentMatrix_ * Mat4::scale(sx, sy, sz);
        sgl_scale(sx, sy, sz);
    }

    Mat4 getCurrentMatrix() const { return currentMatrix_; }

    void resetMatrix() {
        currentMatrix_ = Mat4::identity();
        sgl_load_identity();
    }

    void setMatrix(const Mat4& mat) {
        currentMatrix_ = mat;
        sgl_load_matrix(currentMatrix_.m);
    }

    // -----------------------------------------------------------------------
    // 基本図形描画
    // -----------------------------------------------------------------------

    void drawRect(float x, float y, float w, float h) {
        if (fillEnabled_) {
            sgl_begin_quads();
            sgl_c4f(currentR_, currentG_, currentB_, currentA_);
            sgl_v2f(x, y);
            sgl_v2f(x + w, y);
            sgl_v2f(x + w, y + h);
            sgl_v2f(x, y + h);
            sgl_end();
        }
        if (strokeEnabled_) {
            sgl_begin_line_strip();
            sgl_c4f(currentR_, currentG_, currentB_, currentA_);
            sgl_v2f(x, y);
            sgl_v2f(x + w, y);
            sgl_v2f(x + w, y + h);
            sgl_v2f(x, y + h);
            sgl_v2f(x, y);
            sgl_end();
        }
    }

    void drawCircle(float cx, float cy, float radius) {
        int segments = circleResolution_;

        if (fillEnabled_) {
            sgl_begin_triangle_strip();
            sgl_c4f(currentR_, currentG_, currentB_, currentA_);
            for (int i = 0; i <= segments; i++) {
                float angle = (float)i / segments * TAU;
                float px = cx + cos(angle) * radius;
                float py = cy + sin(angle) * radius;
                sgl_v2f(cx, cy);
                sgl_v2f(px, py);
            }
            sgl_end();
        }
        if (strokeEnabled_) {
            sgl_begin_line_strip();
            sgl_c4f(currentR_, currentG_, currentB_, currentA_);
            for (int i = 0; i <= segments; i++) {
                float angle = (float)i / segments * TAU;
                float px = cx + cos(angle) * radius;
                float py = cy + sin(angle) * radius;
                sgl_v2f(px, py);
            }
            sgl_end();
        }
    }

    void drawEllipse(float cx, float cy, float rx, float ry) {
        int segments = circleResolution_;

        if (fillEnabled_) {
            sgl_begin_triangle_strip();
            sgl_c4f(currentR_, currentG_, currentB_, currentA_);
            for (int i = 0; i <= segments; i++) {
                float angle = (float)i / segments * TAU;
                float px = cx + cos(angle) * rx;
                float py = cy + sin(angle) * ry;
                sgl_v2f(cx, cy);
                sgl_v2f(px, py);
            }
            sgl_end();
        }
        if (strokeEnabled_) {
            sgl_begin_line_strip();
            sgl_c4f(currentR_, currentG_, currentB_, currentA_);
            for (int i = 0; i <= segments; i++) {
                float angle = (float)i / segments * TAU;
                float px = cx + cos(angle) * rx;
                float py = cy + sin(angle) * ry;
                sgl_v2f(px, py);
            }
            sgl_end();
        }
    }

    void drawLine(float x1, float y1, float x2, float y2) {
        sgl_begin_lines();
        sgl_c4f(currentR_, currentG_, currentB_, currentA_);
        sgl_v2f(x1, y1);
        sgl_v2f(x2, y2);
        sgl_end();
    }

    void drawTriangle(float x1, float y1, float x2, float y2, float x3, float y3) {
        if (fillEnabled_) {
            sgl_begin_triangles();
            sgl_c4f(currentR_, currentG_, currentB_, currentA_);
            sgl_v2f(x1, y1);
            sgl_v2f(x2, y2);
            sgl_v2f(x3, y3);
            sgl_end();
        }
        if (strokeEnabled_) {
            sgl_begin_line_strip();
            sgl_c4f(currentR_, currentG_, currentB_, currentA_);
            sgl_v2f(x1, y1);
            sgl_v2f(x2, y2);
            sgl_v2f(x3, y3);
            sgl_v2f(x1, y1);
            sgl_end();
        }
    }

    void drawPoint(float x, float y) {
        sgl_begin_points();
        sgl_c4f(currentR_, currentG_, currentB_, currentA_);
        sgl_v2f(x, y);
        sgl_end();
    }

    // -----------------------------------------------------------------------
    // ビットマップ文字列描画
    // -----------------------------------------------------------------------

    void drawBitmapString(const std::string& text, float x, float y, bool screenFixed = true) {
        if (text.empty() || !internal::fontInitialized) return;

        pushMatrix();

        if (screenFixed) {
            Mat4 currentMat = getCurrentMatrix();
            float worldX = currentMat.m[0]*x + currentMat.m[1]*y + currentMat.m[3];
            float worldY = currentMat.m[4]*x + currentMat.m[5]*y + currentMat.m[7];
            resetMatrix();
            translate(worldX, worldY);
        } else {
            translate(x, y);
        }

        sgl_load_pipeline(internal::fontPipeline);
        sgl_enable_texture();
        sgl_texture(internal::fontView, internal::fontSampler);

        sgl_begin_quads();
        sgl_c4f(currentR_, currentG_, currentB_, currentA_);

        const float charW = bitmapfont::CHAR_TEX_WIDTH;
        const float charH = bitmapfont::CHAR_TEX_HEIGHT;
        float cursorX = 0;
        float cursorY = -charH;

        for (char c : text) {
            if (c == '\n') {
                cursorX = 0;
                cursorY += charH;
                continue;
            }
            if (c == '\t') {
                cursorX += charW * 8;
                continue;
            }
            if (c < 32) continue;

            float u, v;
            bitmapfont::getCharTexCoord(c, u, v);
            float u2 = u + bitmapfont::TEX_CHAR_WIDTH;
            float v2 = v + bitmapfont::TEX_CHAR_HEIGHT;

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

        popMatrix();
    }

    void drawBitmapString(const std::string& text, float x, float y, float scale) {
        if (text.empty() || !internal::fontInitialized) return;

        Mat4 currentMat = getCurrentMatrix();
        float worldX = currentMat.m[0]*x + currentMat.m[1]*y + currentMat.m[3];
        float worldY = currentMat.m[4]*x + currentMat.m[5]*y + currentMat.m[7];

        pushMatrix();
        resetMatrix();
        translate(worldX, worldY);

        sgl_load_pipeline(internal::fontPipeline);
        sgl_enable_texture();
        sgl_texture(internal::fontView, internal::fontSampler);

        sgl_begin_quads();
        sgl_c4f(currentR_, currentG_, currentB_, currentA_);

        const float charW = bitmapfont::CHAR_TEX_WIDTH * scale;
        const float charH = bitmapfont::CHAR_TEX_HEIGHT * scale;
        float cursorX = 0;
        float cursorY = -charH;

        for (char c : text) {
            if (c == '\n') {
                cursorX = 0;
                cursorY += charH;
                continue;
            }
            if (c == '\t') {
                cursorX += charW * 8;
                continue;
            }
            if (c < 32) continue;

            float u, v;
            bitmapfont::getCharTexCoord(c, u, v);
            float u2 = u + bitmapfont::TEX_CHAR_WIDTH;
            float v2 = v + bitmapfont::TEX_CHAR_HEIGHT;

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

        popMatrix();
    }

    // -----------------------------------------------------------------------
    // ビットマップ文字列アラインメント
    // -----------------------------------------------------------------------

    void setBitmapTextAlign(Direction h, Direction v) {
        bitmapAlignH_ = h;
        bitmapAlignV_ = v;
    }

    Direction getBitmapAlignH() const { return bitmapAlignH_; }
    Direction getBitmapAlignV() const { return bitmapAlignV_; }

    // アラインメント指定付き描画
    void drawBitmapString(const std::string& text, float x, float y,
                          Direction h, Direction v, bool screenFixed = true) {
        if (text.empty() || !internal::fontInitialized) return;

        // オフセット計算
        Vec2 offset = calcBitmapAlignOffset(text, h, v);

        pushMatrix();

        if (screenFixed) {
            Mat4 currentMat = getCurrentMatrix();
            float worldX = currentMat.m[0]*(x + offset.x) + currentMat.m[1]*(y + offset.y) + currentMat.m[3];
            float worldY = currentMat.m[4]*(x + offset.x) + currentMat.m[5]*(y + offset.y) + currentMat.m[7];
            resetMatrix();
            translate(worldX, worldY);
        } else {
            translate(x + offset.x, y + offset.y);
        }

        sgl_load_pipeline(internal::fontPipeline);
        sgl_enable_texture();
        sgl_texture(internal::fontView, internal::fontSampler);

        sgl_begin_quads();
        sgl_c4f(currentR_, currentG_, currentB_, currentA_);

        const float charW = bitmapfont::CHAR_TEX_WIDTH;
        const float charH = bitmapfont::CHAR_TEX_HEIGHT;
        float cursorX = 0;
        float cursorY = -charH;

        for (char c : text) {
            if (c == '\n') {
                cursorX = 0;
                cursorY += charH;
                continue;
            }
            if (c == '\t') {
                cursorX += charW * 8;
                continue;
            }
            if (c < 32) continue;

            float u, v_;
            bitmapfont::getCharTexCoord(c, u, v_);
            float u2 = u + bitmapfont::TEX_CHAR_WIDTH;
            float v2 = v_ + bitmapfont::TEX_CHAR_HEIGHT;

            float px = cursorX;
            float py = cursorY;

            sgl_v2f_t2f(px, py, u, v_);
            sgl_v2f_t2f(px + charW, py, u2, v_);
            sgl_v2f_t2f(px + charW, py + charH, u2, v2);
            sgl_v2f_t2f(px, py + charH, u, v2);

            cursorX += charW;
        }

        sgl_end();
        sgl_disable_texture();
        sgl_load_default_pipeline();

        popMatrix();
    }

    // -----------------------------------------------------------------------
    // ビットマップ文字列メトリクス
    // -----------------------------------------------------------------------

    float getBitmapStringWidth(const std::string& text) const {
        float width = 0;
        float maxWidth = 0;
        const float charW = bitmapfont::CHAR_TEX_WIDTH;

        for (char c : text) {
            if (c == '\n') {
                if (width > maxWidth) maxWidth = width;
                width = 0;
                continue;
            }
            if (c == '\t') {
                width += charW * 8;
                continue;
            }
            if (c >= 32) {
                width += charW;
            }
        }

        return (width > maxWidth) ? width : maxWidth;
    }

    float getBitmapStringHeight(const std::string& text) const {
        int lines = 1;
        for (char c : text) {
            if (c == '\n') lines++;
        }
        return bitmapfont::CHAR_TEX_HEIGHT * lines;
    }

    Rect getBitmapStringBBox(const std::string& text) const {
        return Rect(0, 0, getBitmapStringWidth(text), getBitmapStringHeight(text));
    }

private:
    // 描画色
    float currentR_ = 1.0f;
    float currentG_ = 1.0f;
    float currentB_ = 1.0f;
    float currentA_ = 1.0f;

    // 塗りつぶし / ストローク
    bool fillEnabled_ = true;
    bool strokeEnabled_ = false;
    float strokeWeight_ = 1.0f;

    // 円の分割数
    int circleResolution_ = 20;

    // 行列スタック
    Mat4 currentMatrix_ = Mat4::identity();
    std::vector<Mat4> matrixStack_;

    // ビットマップ文字列アラインメント
    Direction bitmapAlignH_ = Direction::Left;
    Direction bitmapAlignV_ = Direction::Top;

    // ビットマップ文字列アラインメントオフセット計算
    Vec2 calcBitmapAlignOffset(const std::string& text, Direction h, Direction v) const {
        float offsetX = 0;
        float offsetY = 0;

        // 水平オフセット
        float w = getBitmapStringWidth(text);
        switch (h) {
            case Direction::Left:   offsetX = 0; break;
            case Direction::Center: offsetX = -w / 2; break;
            case Direction::Right:  offsetX = -w; break;
            default: break;
        }

        // 垂直オフセット
        const float charH = bitmapfont::CHAR_HEIGHT;  // 文字の実際の高さ
        const float totalH = bitmapfont::CHAR_TEX_HEIGHT;  // テクスチャの高さ

        switch (v) {
            case Direction::Top:      offsetY = 0; break;
            case Direction::Baseline: offsetY = -charH; break;
            case Direction::Center:   offsetY = -totalH / 2; break;
            case Direction::Bottom:   offsetY = -totalH; break;
            default: break;
        }

        return Vec2(offsetX, offsetY);
    }
};

// ---------------------------------------------------------------------------
// デフォルトコンテキスト（シングルトン）
// ---------------------------------------------------------------------------
inline RenderContext& getDefaultContext() {
    static RenderContext instance;
    return instance;
}

} // namespace trussc
