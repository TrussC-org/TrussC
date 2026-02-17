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

// ---------------------------------------------------------------------------
// Stroke style enums (used by RenderContext and StrokeMesh)
// ---------------------------------------------------------------------------
enum class StrokeCap {
    Butt,    // Cut flat at endpoint
    Round,   // Semicircle at endpoint
    Square   // Extend by half width
};

enum class StrokeJoin {
    Miter,   // Pointed sharp corners
    Round,   // Rounded corners
    Bevel    // Flat cut corners
};

// Forward declarations
namespace internal {
    extern sg_view fontView;
    extern sg_sampler fontSampler;
    extern sgl_pipeline fontPipeline;
    extern bool fontInitialized;
    extern sgl_pipeline pipeline3d;
    extern bool pipeline3dInitialized;
    extern bool pixelPerfectMode;
    // Current screen setup state (for 2D drawing in perspective mode)
    extern float currentScreenFov;
    extern float currentViewW;
    extern float currentViewH;
    extern float currentCameraDist;

    // View/Projection matrix tracking (for worldToScreen/screenToWorld)
    extern Mat4 currentViewMatrix;
    extern Mat4 currentProjectionMatrix;
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

    // Move allowed (only construction)
    RenderContext(RenderContext&&) = default;
    RenderContext& operator=(RenderContext&&) = delete;

    // -------------------------------------------------------------------------
    // Singleton access
    // -------------------------------------------------------------------------

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
    // Fill / Stroke (oF-style: fill and stroke are mutually exclusive)
    // -----------------------------------------------------------------------

    // Enable fill mode (solid shapes)
    void fill() {
        fillEnabled_ = true;
        strokeEnabled_ = false;
    }

    // Enable stroke mode (outlines only) - like oF's noFill()
    void noFill() {
        fillEnabled_ = false;
        strokeEnabled_ = true;
    }

    void setStrokeWeight(float weight) { strokeWeight_ = weight; }
    void setStrokeCap(StrokeCap cap) { style_.strokeCap = cap; }
    void setStrokeJoin(StrokeJoin join) { style_.strokeJoin = join; }

    bool isFillEnabled() const { return fillEnabled_; }
    bool isStrokeEnabled() const { return strokeEnabled_; }
    float getStrokeWeight() const { return strokeWeight_; }
    StrokeCap getStrokeCap() const { return style_.strokeCap; }
    StrokeJoin getStrokeJoin() const { return style_.strokeJoin; }

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

    // Reset style to default values (white color, fill enabled, etc.)
    void resetStyle() {
        style_ = Style();
    }

    // Main implementation (Vec3)
    void translate(Vec3 pos) {
        currentMatrix_ = currentMatrix_ * Mat4::translate(pos.x, pos.y, pos.z);
        sgl_translate(pos.x, pos.y, pos.z);
    }

    void translate(float x, float y, float z) {
        translate(Vec3(x, y, z));
    }

    void translate(float x, float y) {
        translate(Vec3(x, y, 0));
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

    void rotateDeg(float degrees) { rotate(deg2rad(degrees)); }
    void rotateXDeg(float degrees) { rotateX(deg2rad(degrees)); }
    void rotateYDeg(float degrees) { rotateY(deg2rad(degrees)); }
    void rotateZDeg(float degrees) { rotateZ(deg2rad(degrees)); }

    void rotate(const Quaternion& quat) {
        Mat4 rotMat = quat.toMatrix();
        currentMatrix_ = currentMatrix_ * rotMat;
        sgl_mult_matrix(rotMat.m);
    }

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
        // Restore default view matrix (camera lookat) set by setupScreenFovWithSize
        // This works correctly both in main screen and FBO contexts
        // Note: Mat4 is row-major, sokol_gl expects column-major, so transpose
        Mat4 t = internal::currentViewMatrix.transposed();
        sgl_load_matrix(t.m);
    }

    // Apply transformation matrix (multiplies with current matrix, like translate/rotate)
    void setMatrix(const Mat4& mat) {
        currentMatrix_ = currentMatrix_ * mat;
        // sokol_gl expects column-major, but Mat4 is row-major
        Mat4 t = mat.transposed();
        sgl_mult_matrix(t.m);
    }

    // Load matrix directly (replaces current matrix - use with caution, may break camera setup)
    void loadMatrix(const Mat4& mat) {
        currentMatrix_ = mat;
        Mat4 t = mat.transposed();
        sgl_load_matrix(t.m);
    }

    // -----------------------------------------------------------------------
    // Basic shape drawing (uses VertexWriter for shader support)
    // -----------------------------------------------------------------------

    // Main implementation (Vec3)
    void drawRect(Vec3 pos, Vec2 size) {
        float x = pos.x, y = pos.y, z = pos.z;
        float w = size.x, h = size.y;
        auto& writer = internal::getActiveWriter();

        if (fillEnabled_) {
            writer.begin(PrimitiveType::Quads);
            writer.color(currentR_, currentG_, currentB_, currentA_);
            writer.vertex(x, y, z);
            writer.vertex(x + w, y, z);
            writer.vertex(x + w, y + h, z);
            writer.vertex(x, y + h, z);
            writer.end();
        }
        if (strokeEnabled_) {
            writer.begin(PrimitiveType::LineStrip);
            writer.color(currentR_, currentG_, currentB_, currentA_);
            writer.vertex(x, y, z);
            writer.vertex(x + w, y, z);
            writer.vertex(x + w, y + h, z);
            writer.vertex(x, y + h, z);
            writer.vertex(x, y, z);
            writer.end();
        }
    }

    void drawRect(Vec3 pos, float w, float h) {
        drawRect(pos, Vec2(w, h));
    }

    void drawRect(float x, float y, float w, float h) {
        drawRect(Vec3(x, y, 0), Vec2(w, h));
    }

    // -------------------------------------------------------------------------
    // Rounded Rectangle (circular arc corners)
    // (Implementation in tcRenderContext.cpp)
    // -------------------------------------------------------------------------
    void drawRectRounded(Vec3 pos, Vec2 size, float radius);

    void drawRectRounded(float x, float y, float w, float h, float radius) {
        drawRectRounded(Vec3(x, y, 0), Vec2(w, h), radius);
    }

    // -------------------------------------------------------------------------
    // Squircle Rectangle (superellipse corners, curvature continuous)
    // (Implementation in tcRenderContext.cpp)
    // -------------------------------------------------------------------------
    void drawRectSquircle(Vec3 pos, Vec2 size, float radius);

    void drawRectSquircle(float x, float y, float w, float h, float radius) {
        drawRectSquircle(Vec3(x, y, 0), Vec2(w, h), radius);
    }

    // Main implementation (Vec3)
    void drawCircle(Vec3 center, float radius) {
        int segments = circleResolution_;
        float cx = center.x, cy = center.y, cz = center.z;
        auto& writer = internal::getActiveWriter();

        if (fillEnabled_) {
            writer.begin(PrimitiveType::TriangleStrip);
            writer.color(currentR_, currentG_, currentB_, currentA_);
            for (int i = 0; i <= segments; i++) {
                float angle = (float)i / segments * TAU;
                float px = cx + cos(angle) * radius;
                float py = cy + sin(angle) * radius;
                writer.vertex(cx, cy, cz);
                writer.vertex(px, py, cz);
            }
            writer.end();
        }
        if (strokeEnabled_) {
            writer.begin(PrimitiveType::LineStrip);
            writer.color(currentR_, currentG_, currentB_, currentA_);
            for (int i = 0; i <= segments; i++) {
                float angle = (float)i / segments * TAU;
                float px = cx + cos(angle) * radius;
                float py = cy + sin(angle) * radius;
                writer.vertex(px, py, cz);
            }
            writer.end();
        }
    }

    void drawCircle(float cx, float cy, float radius) {
        drawCircle(Vec3(cx, cy, 0), radius);
    }

    // Main implementation (Vec3)
    void drawEllipse(Vec3 center, Vec2 radii) {
        int segments = circleResolution_;
        float cx = center.x, cy = center.y, cz = center.z;
        float rx = radii.x, ry = radii.y;
        auto& writer = internal::getActiveWriter();

        if (fillEnabled_) {
            writer.begin(PrimitiveType::TriangleStrip);
            writer.color(currentR_, currentG_, currentB_, currentA_);
            for (int i = 0; i <= segments; i++) {
                float angle = (float)i / segments * TAU;
                float px = cx + cos(angle) * rx;
                float py = cy + sin(angle) * ry;
                writer.vertex(cx, cy, cz);
                writer.vertex(px, py, cz);
            }
            writer.end();
        }
        if (strokeEnabled_) {
            writer.begin(PrimitiveType::LineStrip);
            writer.color(currentR_, currentG_, currentB_, currentA_);
            for (int i = 0; i <= segments; i++) {
                float angle = (float)i / segments * TAU;
                float px = cx + cos(angle) * rx;
                float py = cy + sin(angle) * ry;
                writer.vertex(px, py, cz);
            }
            writer.end();
        }
    }

    void drawEllipse(Vec3 center, float rx, float ry) {
        drawEllipse(center, Vec2(rx, ry));
    }

    void drawEllipse(float cx, float cy, float rx, float ry) {
        drawEllipse(Vec3(cx, cy, 0), Vec2(rx, ry));
    }

    // Main implementation (Vec3)
    // NOTE: drawLine uses GL_LINES (1px fixed width, not affected by strokeWeight)
    //       For thick lines or shader support, use StrokeMesh instead
    void drawLine(Vec3 p1, Vec3 p2) {
        auto& writer = internal::getActiveWriter();
        writer.begin(PrimitiveType::Lines);
        writer.color(currentR_, currentG_, currentB_, currentA_);
        writer.vertex(p1.x, p1.y, p1.z);
        writer.vertex(p2.x, p2.y, p2.z);
        writer.end();
    }

    void drawLine(float x1, float y1, float x2, float y2) {
        drawLine(Vec3(x1, y1, 0), Vec3(x2, y2, 0));
    }

    // Main implementation (Vec3)
    void drawTriangle(Vec3 p1, Vec3 p2, Vec3 p3) {
        auto& writer = internal::getActiveWriter();

        if (fillEnabled_) {
            writer.begin(PrimitiveType::Triangles);
            writer.color(currentR_, currentG_, currentB_, currentA_);
            writer.vertex(p1.x, p1.y, p1.z);
            writer.vertex(p2.x, p2.y, p2.z);
            writer.vertex(p3.x, p3.y, p3.z);
            writer.end();
        }
        if (strokeEnabled_) {
            writer.begin(PrimitiveType::LineStrip);
            writer.color(currentR_, currentG_, currentB_, currentA_);
            writer.vertex(p1.x, p1.y, p1.z);
            writer.vertex(p2.x, p2.y, p2.z);
            writer.vertex(p3.x, p3.y, p3.z);
            writer.vertex(p1.x, p1.y, p1.z);
            writer.end();
        }
    }

    void drawTriangle(float x1, float y1, float x2, float y2, float x3, float y3) {
        drawTriangle(Vec3(x1, y1, 0), Vec3(x2, y2, 0), Vec3(x3, y3, 0));
    }

    // Main implementation (Vec3)
    void drawPoint(Vec3 pos) {
        auto& writer = internal::getActiveWriter();
        writer.begin(PrimitiveType::Points);
        writer.color(currentR_, currentG_, currentB_, currentA_);
        writer.vertex(pos.x, pos.y, pos.z);
        writer.end();
    }

    void drawPoint(float x, float y) {
        drawPoint(Vec3(x, y, 0));
    }

    // -----------------------------------------------------------------------
    // Bitmap string drawing
    // (Large implementations in tcRenderContext.cpp)
    // -----------------------------------------------------------------------

    void drawBitmapString(const std::string& text, float x, float y, bool screenFixed = true);

    void drawBitmapString(const std::string& text, Vec3 pos, bool screenFixed = true) {
        drawBitmapString(text, pos.x, pos.y, screenFixed);
    }

    void drawBitmapString(const std::string& text, float x, float y, float scale);

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

    // Bitmap font line height (spacing for \n)
    void setBitmapLineHeight(float h) { style_.bitmapLineHeight = h; }
    float getBitmapLineHeight() const { return style_.bitmapLineHeight; }

    // Draw with alignment specified (Implementation in tcRenderContext.cpp)
    void drawBitmapString(const std::string& text, float x, float y,
                          Direction h, Direction v, bool screenFixed = true);

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
        // First line uses CHAR_TEX_HEIGHT, subsequent lines use bitmapLineHeight
        return bitmapfont::CHAR_TEX_HEIGHT + (lines - 1) * style_.bitmapLineHeight;
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
        StrokeCap strokeCap = StrokeCap::Butt;
        StrokeJoin strokeJoin = StrokeJoin::Miter;
        int circleResolution = 20;
        Direction textAlignH = Direction::Left;
        Direction textAlignV = Direction::Top;
        float bitmapLineHeight = bitmapfont::CHAR_TEX_HEIGHT;
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
