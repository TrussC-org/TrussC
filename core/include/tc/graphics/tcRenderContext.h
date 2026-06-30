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

#include "tc/graphics/tcCurveTessellation.h"

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

// How points are rasterized (drawPoint() and Mesh in PrimitiveMode::Points).
// Square/Round go through the GPU instanced splat (size-controllable, retained
// VBO for a Mesh); Pixel is the classic 1px immediate point (like drawLine).
enum class PointStyle {
    Square,  // solid billboarded quad — lightest, default
    Round,   // anti-aliased disc (alpha-to-coverage)
    Pixel    // true 1px point (sokol_gl immediate; ignores pointSize)
};

// Forward declarations
namespace internal {
    extern sg_view fontView;
    extern sg_sampler fontSampler;
    extern bool fontInitialized;
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
// RenderContext (and getDefaultContext) live in internal:: — the public drawing
// API is the free functions that wrap them. A compat `using` below keeps the
// names reachable from trussc:: for those wrappers and advanced users.
namespace internal {

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

    // Set with HSB (H: 0-1, S: 0-1, B: 0-1)
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

    // Point drawing state (size in logical pixels, matching strokeWeight).
    void setPointSize(float px) { style_.pointSize = px; }
    void setPointStyle(PointStyle s) { style_.pointStyle = s; }

    bool isFillEnabled() const { return fillEnabled_; }
    bool isStrokeEnabled() const { return strokeEnabled_; }
    float getStrokeWeight() const { return strokeWeight_; }
    StrokeCap getStrokeCap() const { return style_.strokeCap; }
    StrokeJoin getStrokeJoin() const { return style_.strokeJoin; }
    float getPointSize() const { return style_.pointSize; }
    PointStyle getPointStyle() const { return style_.pointStyle; }

    // -----------------------------------------------------------------------
    // Curve quality (circles, arcs, beziers, rounded rects, squircle)
    // -----------------------------------------------------------------------
    //
    // Two modes share the same Style.curve field:
    //
    //   Tolerance (default): pick segment count adaptively so the chord
    //     stays within `pixels` of the true curve, in screen space. Set
    //     once with setCurveTolerance(); zoom & scale() are accounted for
    //     via getScale().
    //
    //   Resolution: use a fixed segment count regardless of radius. Useful
    //     for intentionally chunky styling, or to bound vertex cost on
    //     thousands of small circles.
    //
    // setCircleResolution() stays as a deprecated alias forwarding to
    // setCurveResolution(); existing apps keep building, with a warning
    // pointing at the new name.

    void setCurveTolerance(float pixels) {
        style_.curve.mode = CurveStyle::Mode::Tolerance;
        style_.curve.tolerance = pixels;
    }
    void setCurveResolution(int n) {
        style_.curve.mode = CurveStyle::Mode::Resolution;
        style_.curve.resolution = n;
    }
    float getCurveTolerance() const { return style_.curve.tolerance; }
    int   getCurveResolution() const { return style_.curve.resolution; }
    CurveStyle::Mode getCurveMode() const { return style_.curve.mode; }

    // Effective uniform scale derived from the current modelView matrix.
    // Used by tolerance mode to convert "0.1 px on screen" (the default)
    // into the local tolerance the tessellator should aim for. Camera
    // projection (perspective) is intentionally NOT considered — for
    // creative-coding use most rendering is 2D or orthographic, and
    // accounting for perspective would require per-pixel jacobian inversion
    // at every curve point (overkill).
    float getScale() const {
        const auto& m = currentMatrix_.m;
        // Column 0 / column 1 lengths = effective x / y scale of the basis.
        float sx = std::sqrt(m[0]*m[0] + m[4]*m[4] + m[8]*m[8]);
        float sy = std::sqrt(m[1]*m[1] + m[5]*m[5] + m[9]*m[9]);
        float s = std::max(sx, sy);
        return s > 0.0f ? s : 1.0f;
    }
    [[deprecated("Use getScale() instead. Will be removed in v1.0.0")]]
    float getCurrentScale() const { return getScale(); }

    // Segment-count helpers. Used by drawCircle / drawEllipse / Path::arc /
    // drawRectRounded / drawArc — single source of truth so a future
    // tweak to the math (e.g. caching, or a different sagitta formula)
    // propagates everywhere automatically.
    int decideCircleSegments(float radius) const {
        if (style_.curve.mode == CurveStyle::Mode::Resolution) {
            return std::max(3, style_.curve.resolution);
        }
        float effTol = style_.curve.tolerance / getScale();
        return internal::segmentsForCircle(radius, effTol);
    }
    int decideArcSegments(float radius, float angleSpan) const {
        if (style_.curve.mode == CurveStyle::Mode::Resolution) {
            int full = std::max(3, style_.curve.resolution);
            int n = (int)std::ceil(full * std::abs(angleSpan) / TAU);
            return std::max(2, n);
        }
        float effTol = style_.curve.tolerance / getScale();
        return internal::segmentsForArc(radius, angleSpan, effTol);
    }

    // -----------------------------------------------------------------------
    // Circle resolution (legacy — kept undeprecated on the context so the
    // global wrapper in TrussC.h can forward without triggering its own
    // deprecation warning. The user-facing TrussC.h wrapper IS deprecated.)
    // -----------------------------------------------------------------------
    void setCircleResolution(int res) {
        circleResolution_ = res;          // legacy field, still read by
                                          // some drawers until C2 migrates them
        setCurveResolution(res);
    }
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
        // Capture the live blend mode (it lives in internal::currentBlendMode, not in
        // style_) so it is saved/restored alongside color/fill/stroke.
        style_.blend = internal::currentBlendMode;
        styleStack_.push_back(style_);
    }

    void popStyle() {
        if (!styleStack_.empty()) {
            style_ = styleStack_.back();
            styleStack_.pop_back();
            applyBlend(style_.blend);
        }
    }

    // Reset style to default values (white color, fill enabled, etc.)
    void resetStyle() {
        style_ = Style();
        applyBlend(style_.blend);
    }

private:
    // Restore the 2D blend pipeline only when the mode actually changed (a redundant
    // load is otherwise a no-op that sokol_gl batches away, but we skip it anyway).
    // No-op inside an FBO pass, which uses its own accumulating pipeline.
    void applyBlend(BlendMode mode) {
        if (mode == internal::currentBlendMode) return;
        internal::currentBlendMode = mode;
        if (!internal::inFboPass) {
            internal::loadPipeline(internal::active2D(mode));
        }
    }
public:

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
        // Mat4 is row-major, sokol_gl expects column-major — transpose
        Mat4 t = rotMat.transposed();
        sgl_mult_matrix(t.m);
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

    // Current accumulated model matrix.
    Mat4 getMatrix() const { return currentMatrix_; }
    [[deprecated("Use getMatrix() instead. Will be removed in v1.0.0")]]
    Mat4 getCurrentMatrix() const { return getMatrix(); }

    void resetMatrix() {
        currentMatrix_ = Mat4::identity();
        // Restore default view matrix (camera lookat) set by setupScreenFovWithSize
        // This works correctly both in main screen and FBO contexts
        // Note: Mat4 is row-major, sokol_gl expects column-major, so transpose
        Mat4 t = internal::currentViewMatrix.transposed();
        sgl_load_matrix(t.m);
    }

    // Multiply the current matrix by `mat` (relative transform, like translate/rotate).
    // NOTE: this was previously misnamed setMatrix(); that name now REPLACES.
    void multMatrix(const Mat4& mat) {
        currentMatrix_ = currentMatrix_ * mat;
        // sokol_gl expects column-major, but Mat4 is row-major
        Mat4 t = mat.transposed();
        sgl_mult_matrix(t.m);
    }

    // Replace the current matrix with `mat` (absolute - use with caution, may break camera setup).
    void setMatrix(const Mat4& mat) {
        currentMatrix_ = mat;
        Mat4 t = mat.transposed();
        sgl_load_matrix(t.m);
    }
    [[deprecated("Use setMatrix() instead. Will be removed in v1.0.0")]]
    void loadMatrix(const Mat4& mat) { setMatrix(mat); }

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
        int segments = decideCircleSegments(radius);
        if (segments == 0) return;
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
        // Use the larger radius for the segment count — conservative
        // approximation; matches what oF and most engines do for ellipses.
        int segments = decideCircleSegments(std::max(radii.x, radii.y));
        if (segments == 0) return;
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

    // -----------------------------------------------------------------------
    // Arc — circular arc spanning [angleBegin, angleEnd] radians.
    //
    // Stroke: just the arc curve (open).
    // Fill:   pie sector — closes to the center as a fan, like p5.js /
    //         Processing. This is the one place where fill and stroke do
    //         not share the same boundary (other primitives do). It's a
    //         deliberate convenience: filling a partial arc almost always
    //         means "I want a pie slice."
    //
    // If you need a different closure (chord / open fill / custom shape),
    // build it from the low-level primitive `appendArc()` inside
    // beginShape()/endShape(), which emits rim points only and never
    // injects a center vertex.
    //
    // Angles in **radians**, going counter-clockwise (mathematical
    // convention). For a quick semicircle: drawArc(x, y, r, 0, HALF_TAU).
    // -----------------------------------------------------------------------
    void drawArc(Vec3 center, float radius, float angleBegin, float angleEnd) {
        if (radius <= 0.0f) return;
        float span = angleEnd - angleBegin;
        if (span == 0.0f) return;
        int segments = decideArcSegments(radius, std::abs(span));
        if (segments < 2) segments = 2;
        float cx = center.x, cy = center.y, cz = center.z;
        auto& writer = internal::getActiveWriter();

        if (fillEnabled_) {
            // TriangleStrip alternating center / rim — same fan pattern
            // drawCircle uses, just over a partial angular range.
            writer.begin(PrimitiveType::TriangleStrip);
            writer.color(currentR_, currentG_, currentB_, currentA_);
            for (int i = 0; i <= segments; i++) {
                float a = angleBegin + span * ((float)i / (float)segments);
                float px = cx + std::cos(a) * radius;
                float py = cy + std::sin(a) * radius;
                writer.vertex(cx, cy, cz);
                writer.vertex(px, py, cz);
            }
            writer.end();
        }
        if (strokeEnabled_) {
            writer.begin(PrimitiveType::LineStrip);
            writer.color(currentR_, currentG_, currentB_, currentA_);
            for (int i = 0; i <= segments; i++) {
                float a = angleBegin + span * ((float)i / (float)segments);
                writer.vertex(cx + std::cos(a) * radius,
                              cy + std::sin(a) * radius, cz);
            }
            writer.end();
        }
    }

    void drawArc(float x, float y, float radius, float angleBegin, float angleEnd) {
        drawArc(Vec3(x, y, 0), radius, angleBegin, angleEnd);
    }

    // -----------------------------------------------------------------------
    // Bezier and curve primitives — stroke only (open curves have no
    // natural enclosed region; build a Path if you want a filled shape
    // that includes a Bezier piece).
    //
    //   drawBezier(p0, p1, p2, p3)        — cubic
    //   drawBezier(p0, p1, p2)            — quadratic
    //   drawBezier(std::vector<Vec3>)     — n-th order (control polygon)
    //   drawCurve (p0, p1, p2, p3)        — Catmull-Rom interpolating p1->p2,
    //                                       using p0/p3 as tangent influences
    //                                       (same semantics as oF's drawCurve)
    //
    // In Tolerance mode each curve is adaptively subdivided so the polyline
    // stays within `tolerance` (in screen pixels) of the true curve. In
    // Resolution mode the curve is sampled at N+1 uniform t-values.
    // -----------------------------------------------------------------------
private:
    void emitPolylineStroke_(const std::vector<Vec3>& pts) {
        if (pts.size() < 2) return;
        auto& writer = internal::getActiveWriter();
        writer.begin(PrimitiveType::LineStrip);
        writer.color(currentR_, currentG_, currentB_, currentA_);
        for (auto& p : pts) writer.vertex(p.x, p.y, p.z);
        writer.end();
    }

    // Uniform-t cubic sampling for resolution mode.
    static void sampleCubicUniform_(const Vec3& p0, const Vec3& p1,
                                    const Vec3& p2, const Vec3& p3,
                                    int n, std::vector<Vec3>& out) {
        out.reserve(out.size() + n + 1);
        for (int i = 0; i <= n; i++) {
            float t = (float)i / (float)n;
            float u = 1 - t;
            float b0 = u*u*u, b1 = 3*u*u*t, b2 = 3*u*t*t, b3 = t*t*t;
            out.push_back(Vec3{
                b0*p0.x + b1*p1.x + b2*p2.x + b3*p3.x,
                b0*p0.y + b1*p1.y + b2*p2.y + b3*p3.y,
                b0*p0.z + b1*p1.z + b2*p2.z + b3*p3.z});
        }
    }
    static void sampleQuadUniform_(const Vec3& p0, const Vec3& p1, const Vec3& p2,
                                   int n, std::vector<Vec3>& out) {
        out.reserve(out.size() + n + 1);
        for (int i = 0; i <= n; i++) {
            float t = (float)i / (float)n;
            float u = 1 - t;
            float b0 = u*u, b1 = 2*u*t, b2 = t*t;
            out.push_back(Vec3{
                b0*p0.x + b1*p1.x + b2*p2.x,
                b0*p0.y + b1*p1.y + b2*p2.y,
                b0*p0.z + b1*p1.z + b2*p2.z});
        }
    }
public:

    // drawBezier / drawCurve are inherently strokes (open curves cannot be
    // filled unambiguously). Like drawLine, they ignore the fill/stroke mode
    // and always emit a polyline. Use Path for filled curve shapes.
    void drawBezier(Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3) {
        std::vector<Vec3> pts;
        if (style_.curve.mode == CurveStyle::Mode::Tolerance) {
            tessellateCubicBezier(p0, p1, p2, p3,
                                  style_.curve.tolerance / getScale(), pts);
        } else {
            sampleCubicUniform_(p0, p1, p2, p3,
                                std::max(2, style_.curve.resolution), pts);
        }
        emitPolylineStroke_(pts);
    }

    void drawBezier(Vec3 p0, Vec3 p1, Vec3 p2) {
        std::vector<Vec3> pts;
        if (style_.curve.mode == CurveStyle::Mode::Tolerance) {
            tessellateQuadBezier(p0, p1, p2,
                                 style_.curve.tolerance / getScale(), pts);
        } else {
            sampleQuadUniform_(p0, p1, p2,
                               std::max(2, style_.curve.resolution), pts);
        }
        emitPolylineStroke_(pts);
    }

    void drawBezier(const std::vector<Vec3>& controlPoints) {
        if (controlPoints.size() < 2) return;
        if (controlPoints.size() == 2) {
            drawLine(controlPoints[0], controlPoints[1]);
            return;
        }
        // Fast paths for the two common orders. Same numerical result as
        // tessellateBezierN — just avoids the extra std::vector copy that
        // path takes for its in-place de Casteljau scratch buffer.
        if (controlPoints.size() == 3) {
            drawBezier(controlPoints[0], controlPoints[1], controlPoints[2]);
            return;
        }
        if (controlPoints.size() == 4) {
            drawBezier(controlPoints[0], controlPoints[1],
                       controlPoints[2], controlPoints[3]);
            return;
        }
        std::vector<Vec3> pts;
        if (style_.curve.mode == CurveStyle::Mode::Tolerance) {
            tessellateBezierN(controlPoints,
                              style_.curve.tolerance / getScale(), pts);
        } else {
            // Uniform t via repeated de Casteljau evaluation.
            int n = std::max(2, style_.curve.resolution);
            pts.reserve(n + 1);
            for (int i = 0; i <= n; i++) {
                float t = (float)i / (float)n;
                std::vector<Vec3> work = controlPoints;
                int sz = (int)work.size();
                for (int level = 1; level < sz; level++) {
                    for (int j = 0; j < sz - level; j++) {
                        work[j] = Vec3{
                            work[j].x + (work[j+1].x - work[j].x) * t,
                            work[j].y + (work[j+1].y - work[j].y) * t,
                            work[j].z + (work[j+1].z - work[j].z) * t};
                    }
                }
                pts.push_back(work[0]);
            }
        }
        emitPolylineStroke_(pts);
    }

    // Catmull-Rom interpolating p1 -> p2, using p0 and p3 as tangent
    // influences. Matches openFrameworks' ofDrawCurve / curveVertex
    // semantics. Internally converted to the equivalent cubic Bezier
    // (B1 = p1 + (p2-p0)/6, B2 = p2 - (p3-p1)/6) so it inherits the
    // same adaptive subdivision and mode handling as drawBezier.
    void drawCurve(Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3) {
        Vec3 b1{p1.x + (p2.x - p0.x) / 6.0f,
                p1.y + (p2.y - p0.y) / 6.0f,
                p1.z + (p2.z - p0.z) / 6.0f};
        Vec3 b2{p2.x - (p3.x - p1.x) / 6.0f,
                p2.y - (p3.y - p1.y) / 6.0f,
                p2.z - (p3.z - p1.z) / 6.0f};
        drawBezier(p1, b1, b2, p2);
    }

    // Catmull-Rom chained through every point. Needs at least 4 points;
    // emits (N-3) segments, each interpolating points[i+1] -> points[i+2]
    // with points[i] / points[i+3] as tangent influences.
    void drawCurve(const std::vector<Vec3>& points) {
        if (points.size() < 4) return;
        for (size_t i = 0; i + 3 < points.size(); i++) {
            drawCurve(points[i], points[i+1], points[i+2], points[i+3]);
        }
    }

    // Closed (looped) Catmull-Rom — the curve passes through ALL points
    // and joins last->first smoothly. Tangents wrap around, so a minimum
    // of 3 points is enough.
    void drawCurve(const std::vector<Vec3>& points, bool closed) {
        if (!closed) { drawCurve(points); return; }
        const int n = (int)points.size();
        if (n < 3) return;
        for (int i = 0; i < n; i++) {
            const Vec3& p0 = points[(i - 1 + n) % n];
            const Vec3& p1 = points[i];
            const Vec3& p2 = points[(i + 1) % n];
            const Vec3& p3 = points[(i + 2) % n];
            drawCurve(p0, p1, p2, p3);
        }
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

    void drawLine(float x1, float y1, float z1, float x2, float y2, float z2) {
        drawLine(Vec3(x1, y1, z1), Vec3(x2, y2, z2));
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
        const float charW = bitmapfont::CHAR_TEX_WIDTH;  // halfwidth advance

        const char* p   = text.data();
        const char* end = p + text.size();
        while (p < end) {
            // Peek raw byte first for newline / tab fast-path
            unsigned char b0 = (unsigned char)*p;
            if (b0 == '\n') { ++p; if (width > maxWidth) maxWidth = width; width = 0; continue; }
            if (b0 == '\t') { ++p; width += charW * 8; continue; }
            if (b0 < 32)    { ++p; continue; }
            uint32_t cp = bitmapfont::utf8Decode(p, end);
            if (cp == 0) break;
            width += (float)bitmapfont::codepointPixelWidth(cp);
        }

        return (width > maxWidth) ? width : maxWidth;
    }

    float getBitmapStringHeight(const std::string& text) const {
        int lines = 1;
        for (char c : text) {
            if (c == '\n') lines++;
        }
        // Newline byte isn't a UTF-8 continuation byte (always 0x0A),
        // so a raw byte scan is safe.
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
        float pointSize = 1.0f;                    // logical px, like strokeWeight
        PointStyle pointStyle = PointStyle::Square;
        int circleResolution = 20;     // legacy, will be removed once all
                                       // draw* migrate to curve.resolution
        CurveStyle curve;              // shared by all curved primitives
                                       // (circle, ellipse, arc, bezier, ...)
        Direction textAlignH = Direction::Left;
        Direction textAlignV = Direction::Top;
        float bitmapLineHeight = bitmapfont::CHAR_TEX_HEIGHT;
        // Mirror of internal::currentBlendMode. blend state itself lives outside the
        // RenderContext (it maps to an sgl pipeline), so push/popStyle capture and
        // restore it explicitly rather than via this field's value alone.
        BlendMode blend = BlendMode::Alpha;
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
// Non-inline so that Host and Guest (hot-reload DLL) share the same
// static instance — inline would give each module its own copy on Windows.
// ---------------------------------------------------------------------------
RenderContext& getDefaultContext();

} // namespace internal

// Compat: keep RenderContext / getDefaultContext reachable from trussc:: so the
// free drawing wrappers (and any advanced user reaching for the context) keep
// working; the canonical home is internal::. The RenderContext alias is
// [[deprecated]] — the public drawing API is the free functions; reach the
// context via internal::getDefaultContext() only if you truly need it.
using RenderContext [[deprecated("RenderContext is internal; use the free drawing functions (setColor/drawRect/...). Reach the context via internal::getDefaultContext() if you really need it. Will be removed in v1.0.0")]] = internal::RenderContext;
using internal::getDefaultContext;

} // namespace trussc
