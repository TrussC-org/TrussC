#pragma once

// =============================================================================
// RenderContext - Context class holding rendering state
// =============================================================================
//
// Foundation for future multi-context support.
// Currently used as default context (singleton).
//
// Usage:
//   // Global functions (same as before)
//   setColor(255, 0, 0);
//   drawRect(10, 10, 100, 100);
//
//   // Explicit context (for future use)
//   auto& ctx = getDefaultContext();
//   ctx.setColor(255, 0, 0);
//   ctx.drawRect(10, 10, 100, 100);
//
// =============================================================================

// This file is included from TrussC.h, so required headers
// are already included: tcMath.h, tcColor.h, tcBitmapFont.h, sokol_gl.h

#include <vector>
#include <string>

namespace trussc {

// Forward declarations
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
// RenderContext class
// ---------------------------------------------------------------------------
class RenderContext {
public:
    // -----------------------------------------------------------------------
    // Constructor / Destructor
    // -----------------------------------------------------------------------
    RenderContext() = default;
    ~RenderContext() = default;

    // Copy forbidden (sharing state causes confusion)
    RenderContext(const RenderContext&) = delete;
    RenderContext& operator=(const RenderContext&) = delete;

    // Move allowed
    RenderContext(RenderContext&&) = default;
    RenderContext& operator=(RenderContext&&) = default;

    // -----------------------------------------------------------------------
    // Color settings
    // -----------------------------------------------------------------------

    // Set draw color (float: 0.0 ~ 1.0)
    void setColor(float r, float g, float b, float a = 1.0f) {
        currentR_ = r;
        currentG_ = g;
        currentB_ = b;
        currentA_ = a;
    }

    // Grayscale
    void setColor(float gray, float a = 1.0f) {
        setColor(gray, gray, gray, a);
    }

    // Set using Color struct
    void setColor(const Color& c) {
        setColor(c.r, c.g, c.b, c.a);
    }

    // Set with HSB (H: 0-TAU, S: 0-1, B: 0-1)
    void setColorHSB(float h, float s, float b, float a = 1.0f) {
        Color c = ColorHSB(h, s, b, a).toRGB();
        setColor(c.r, c.g, c.b, c.a);
    }

    // Set with OKLab
    void setColorOKLab(float L, float a_lab, float b_lab, float alpha = 1.0f) {
        Color c = ColorOKLab(L, a_lab, b_lab, alpha).toRGB();
        setColor(c.r, c.g, c.b, c.a);
    }

    // Set with OKLCH
    void setColorOKLCH(float L, float C, float H, float alpha = 1.0f) {
        Color c = ColorOKLCH(L, C, H, alpha).toRGB();
        setColor(c.r, c.g, c.b, c.a);
    }

    // Get current color
    Color getColor() const {
        return Color(currentR_, currentG_, currentB_, currentA_);
    }

    // -----------------------------------------------------------------------
    // Fill / Stroke
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
    // Circle resolution
    // -----------------------------------------------------------------------

    void setCircleResolution(int res) { circleResolution_ = res; }
    int getCircleResolution() const { return circleResolution_; }

    // -----------------------------------------------------------------------
    // Matrix operations
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

    // -----------------------------------------------------------------------
    // Style stack
    // -----------------------------------------------------------------------

    void pushStyle() {
        styleStack_.push_back(style_);
    }

    void popStyle() {
        if (!styleStack_.empty()) {
            style_ = styleStack_.back();
            styleStack_.pop_back();
        }
    }

    void translate(float x, float y) {
        currentMatrix_ = currentMatrix_ * Mat4::translate(x, y, 0.0f);
        sgl_translate(x, y, 0.0f);
    }

    void translate(Vec3 pos) {
        translate(pos.x, pos.y, pos.z);
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
    // Basic shape drawing
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

    void drawRect(Vec3 pos, Vec2 size) {
        float x = pos.x, y = pos.y, z = pos.z;
        float w = size.x, h = size.y;
        if (fillEnabled_) {
            sgl_begin_quads();
            sgl_c4f(currentR_, currentG_, currentB_, currentA_);
            sgl_v3f(x, y, z);
            sgl_v3f(x + w, y, z);
            sgl_v3f(x + w, y + h, z);
            sgl_v3f(x, y + h, z);
            sgl_end();
        }
        if (strokeEnabled_) {
            sgl_begin_line_strip();
            sgl_c4f(currentR_, currentG_, currentB_, currentA_);
            sgl_v3f(x, y, z);
            sgl_v3f(x + w, y, z);
            sgl_v3f(x + w, y + h, z);
            sgl_v3f(x, y + h, z);
            sgl_v3f(x, y, z);
            sgl_end();
        }
    }

    void drawRect(Vec3 pos, float w, float h) {
        drawRect(pos, Vec2(w, h));
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

    void drawCircle(Vec3 center, float radius) {
        int segments = circleResolution_;
        float cx = center.x, cy = center.y, cz = center.z;

        if (fillEnabled_) {
            sgl_begin_triangle_strip();
            sgl_c4f(currentR_, currentG_, currentB_, currentA_);
            for (int i = 0; i <= segments; i++) {
                float angle = (float)i / segments * TAU;
                float px = cx + cos(angle) * radius;
                float py = cy + sin(angle) * radius;
                sgl_v3f(cx, cy, cz);
                sgl_v3f(px, py, cz);
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
                sgl_v3f(px, py, cz);
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

    void drawEllipse(Vec3 center, Vec2 radii) {
        int segments = circleResolution_;
        float cx = center.x, cy = center.y, cz = center.z;
        float rx = radii.x, ry = radii.y;

        if (fillEnabled_) {
            sgl_begin_triangle_strip();
            sgl_c4f(currentR_, currentG_, currentB_, currentA_);
            for (int i = 0; i <= segments; i++) {
                float angle = (float)i / segments * TAU;
                float px = cx + cos(angle) * rx;
                float py = cy + sin(angle) * ry;
                sgl_v3f(cx, cy, cz);
                sgl_v3f(px, py, cz);
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
                sgl_v3f(px, py, cz);
            }
            sgl_end();
        }
    }

    void drawEllipse(Vec3 center, float rx, float ry) {
        drawEllipse(center, Vec2(rx, ry));
    }

    void drawLine(float x1, float y1, float x2, float y2) {
        sgl_begin_lines();
        sgl_c4f(currentR_, currentG_, currentB_, currentA_);
        sgl_v2f(x1, y1);
        sgl_v2f(x2, y2);
        sgl_end();
    }

    void drawLine(Vec3 p1, Vec3 p2) {
        sgl_begin_lines();
        sgl_c4f(currentR_, currentG_, currentB_, currentA_);
        sgl_v3f(p1.x, p1.y, p1.z);
        sgl_v3f(p2.x, p2.y, p2.z);
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

    void drawTriangle(Vec3 p1, Vec3 p2, Vec3 p3) {
        if (fillEnabled_) {
            sgl_begin_triangles();
            sgl_c4f(currentR_, currentG_, currentB_, currentA_);
            sgl_v3f(p1.x, p1.y, p1.z);
            sgl_v3f(p2.x, p2.y, p2.z);
            sgl_v3f(p3.x, p3.y, p3.z);
            sgl_end();
        }
        if (strokeEnabled_) {
            sgl_begin_line_strip();
            sgl_c4f(currentR_, currentG_, currentB_, currentA_);
            sgl_v3f(p1.x, p1.y, p1.z);
            sgl_v3f(p2.x, p2.y, p2.z);
            sgl_v3f(p3.x, p3.y, p3.z);
            sgl_v3f(p1.x, p1.y, p1.z);
            sgl_end();
        }
    }

    void drawPoint(float x, float y) {
        sgl_begin_points();
        sgl_c4f(currentR_, currentG_, currentB_, currentA_);
        sgl_v2f(x, y);
        sgl_end();
    }

    void drawPoint(Vec3 pos) {
        sgl_begin_points();
        sgl_c4f(currentR_, currentG_, currentB_, currentA_);
        sgl_v3f(pos.x, pos.y, pos.z);
        sgl_end();
    }

    // -----------------------------------------------------------------------
    // Bitmap string drawing
    // -----------------------------------------------------------------------

    void drawBitmapString(const std::string& text, float x, float y, bool screenFixed = true) {
        if (text.empty() || !internal::fontInitialized) return;

        // Calculate offset based on current alignment settings
        Vec2 offset = calcBitmapAlignOffset(text, textAlignH_, textAlignV_);

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
        float cursorY = 0;  // Top-aligned (specified coordinate is top of text)

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

    void drawBitmapString(const std::string& text, Vec3 pos, bool screenFixed = true) {
        drawBitmapString(text, pos.x, pos.y, screenFixed);
    }

    void drawBitmapString(const std::string& text, float x, float y, float scale) {
        if (text.empty() || !internal::fontInitialized) return;

        // Calculate offset based on current alignment settings (with scale)
        Vec2 offset = calcBitmapAlignOffset(text, textAlignH_, textAlignV_);
        offset.x *= scale;
        offset.y *= scale;

        Mat4 currentMat = getCurrentMatrix();
        float worldX = currentMat.m[0]*(x + offset.x) + currentMat.m[1]*(y + offset.y) + currentMat.m[3];
        float worldY = currentMat.m[4]*(x + offset.x) + currentMat.m[5]*(y + offset.y) + currentMat.m[7];

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
        float cursorY = 0;  // Top-aligned

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

    void drawBitmapString(const std::string& text, Vec3 pos, float scale) {
        drawBitmapString(text, pos.x, pos.y, scale);
    }

    // -----------------------------------------------------------------------
    // Text alignment
    // -----------------------------------------------------------------------

    void setTextAlign(Direction h, Direction v) {
        textAlignH_ = h;
        textAlignV_ = v;
    }

    Direction getTextAlignH() const { return textAlignH_; }
    Direction getTextAlignV() const { return textAlignV_; }

    // Draw with alignment specified
    void drawBitmapString(const std::string& text, float x, float y,
                          Direction h, Direction v, bool screenFixed = true) {
        if (text.empty() || !internal::fontInitialized) return;

        // Calculate offset
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
        float cursorY = 0;  // Top-aligned (specified coordinate is top of text)

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

    void drawBitmapString(const std::string& text, Vec3 pos,
                          Direction h, Direction v, bool screenFixed = true) {
        drawBitmapString(text, pos.x, pos.y, h, v, screenFixed);
    }

    // -----------------------------------------------------------------------
    // Bitmap string metrics
    // -----------------------------------------------------------------------

    // Font line height (pixels per line)
    float getBitmapFontHeight() const {
        return bitmapfont::CHAR_TEX_HEIGHT;
    }

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
    // Style structure
    struct Style {
        float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
        bool fillEnabled = true;
        bool strokeEnabled = false;
        float strokeWeight = 1.0f;
        int circleResolution = 20;
        Direction textAlignH = Direction::Left;
        Direction textAlignV = Direction::Top;
    };

    // Current style
    Style style_;

    // Style stack
    std::vector<Style> styleStack_;

    // Draw color (shortcut to style_)
    float& currentR_ = style_.r;
    float& currentG_ = style_.g;
    float& currentB_ = style_.b;
    float& currentA_ = style_.a;

    // Fill / Stroke (shortcut to style_)
    bool& fillEnabled_ = style_.fillEnabled;
    bool& strokeEnabled_ = style_.strokeEnabled;
    float& strokeWeight_ = style_.strokeWeight;

    // Circle resolution (shortcut to style_)
    int& circleResolution_ = style_.circleResolution;

    // Matrix stack
    Mat4 currentMatrix_ = Mat4::identity();
    std::vector<Mat4> matrixStack_;

    // Text alignment (shortcut to style_)
    Direction& textAlignH_ = style_.textAlignH;
    Direction& textAlignV_ = style_.textAlignV;

    // Calculate bitmap string alignment offset
    Vec2 calcBitmapAlignOffset(const std::string& text, Direction h, Direction v) const {
        float offsetX = 0;
        float offsetY = 0;

        // Horizontal offset
        float w = getBitmapStringWidth(text);
        switch (h) {
            case Direction::Left:   offsetX = 0; break;
            case Direction::Center: offsetX = -w / 2; break;
            case Direction::Right:  offsetX = -w; break;
            default: break;
        }

        // Vertical offset
        const float charH = bitmapfont::CHAR_HEIGHT;  // Actual character height
        const float totalH = bitmapfont::CHAR_TEX_HEIGHT;  // Texture height

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
// Default context (singleton)
// ---------------------------------------------------------------------------
inline RenderContext& getDefaultContext() {
    static RenderContext instance;
    return instance;
}

} // namespace trussc
