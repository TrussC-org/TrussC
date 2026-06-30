#pragma once

// This file is included from TrussC.h
// To access variables in internal namespace

#include <vector>
#include "../utils/tcAnnotations.h"

namespace trussc {

// Internal state for shape/stroke drawing
namespace internal {

    // Stroke vertex (position + color + width)
    struct StrokeVertex {
        Vec3 pos;
        Color color;
        float width;
    };

    // Shape (polygon) vertices
    inline std::vector<Vec3> shapeVertices;
    inline bool shapeStarted = false;

    // Lines (independent line segments) vertices + colors
    struct LinesVertex {
        Vec3 pos;
        Color color;
    };
    inline std::vector<LinesVertex> linesVertices;
    inline bool linesStarted = false;

    // Stroke vertices
    inline std::vector<StrokeVertex> strokeVertices;
    inline bool strokeStarted = false;
    inline StrokeCap strokeStartCap = StrokeCap::Butt;  // Cap at first vertex
}

// ===========================================================================
// Shape drawing (polygons)
// ===========================================================================

// Begin shape drawing
inline void beginShape() {
    internal::shapeVertices.clear();
    internal::shapeStarted = true;
    internal::strokeStarted = false;
}

// End shape drawing
// close: if true, connects start and end points
inline void endShape(bool close = false) {
    if (!internal::shapeStarted || internal::shapeVertices.empty()) {
        internal::shapeStarted = false;
        return;
    }

    auto& verts = internal::shapeVertices;
    size_t n = verts.size();
    auto& ctx = getDefaultContext();
    Color col = ctx.getColor();
    auto& writer = internal::getActiveWriter();

    // Fill mode: triangle fan (only renders convex shapes correctly)
    if (ctx.isFillEnabled() && n >= 3) {
        writer.begin(PrimitiveType::Triangles);
        writer.color(col.r, col.g, col.b, col.a);
        // Triangle fan: vertex 0 as center
        for (size_t i = 1; i < n - 1; i++) {
            writer.vertex(verts[0].x, verts[0].y, verts[0].z);
            writer.vertex(verts[i].x, verts[i].y, verts[i].z);
            writer.vertex(verts[i+1].x, verts[i+1].y, verts[i+1].z);
        }
        writer.end();
    }

    // Stroke mode: line strip
    if (ctx.isStrokeEnabled() && n >= 2) {
        writer.begin(PrimitiveType::LineStrip);
        writer.color(col.r, col.g, col.b, col.a);
        for (size_t i = 0; i < n; i++) {
            writer.vertex(verts[i].x, verts[i].y, verts[i].z);
        }
        if (close && n > 2) {
            writer.vertex(verts[0].x, verts[0].y, verts[0].z);
        }
        writer.end();
    }

    internal::shapeVertices.clear();
    internal::shapeStarted = false;
}

// ===========================================================================
// Lines drawing (independent line segments, pairs of vertices)
// ===========================================================================

// Begin batch line drawing
inline void beginLines() {
    internal::linesVertices.clear();
    internal::linesStarted = true;
    internal::shapeStarted = false;
    internal::strokeStarted = false;
}

// End batch line drawing — draws all accumulated vertex pairs as independent lines
inline void endLines() {
    if (!internal::linesStarted || internal::linesVertices.size() < 2) {
        internal::linesStarted = false;
        return;
    }

    auto& verts = internal::linesVertices;
    size_t n = verts.size();
    auto& writer = internal::getActiveWriter();

    writer.begin(PrimitiveType::Lines);
    for (size_t i = 0; i + 1 < n; i += 2) {
        writer.color(verts[i].color.r, verts[i].color.g, verts[i].color.b, verts[i].color.a);
        writer.vertex(verts[i].pos.x, verts[i].pos.y, verts[i].pos.z);
        writer.color(verts[i+1].color.r, verts[i+1].color.g, verts[i+1].color.b, verts[i+1].color.a);
        writer.vertex(verts[i+1].pos.x, verts[i+1].pos.y, verts[i+1].pos.z);
    }
    writer.end();

    internal::linesVertices.clear();
    internal::linesStarted = false;
}

// ===========================================================================
// Stroke drawing (lines with width/cap/join)
// ===========================================================================

// Begin stroke drawing
inline void beginStroke() {
    internal::strokeVertices.clear();
    internal::strokeStarted = true;
    internal::shapeStarted = false;
}

// End stroke drawing - renders using StrokeMesh
// close: if true, connects end to start
inline void endStroke(bool close = false);  // Forward declaration (implemented after StrokeMesh)

// ===========================================================================
// Vertex functions (shared between shape and stroke)
// ===========================================================================

// Add vertex (3D)
inline void vertex(float x, float y, float z) {
    if (internal::linesStarted) {
        auto& ctx = getDefaultContext();
        internal::linesVertices.push_back({Vec3{x, y, z}, ctx.getColor()});
    } else if (internal::shapeStarted) {
        internal::shapeVertices.push_back(Vec3{x, y, z});
    } else if (internal::strokeStarted) {
        auto& ctx = getDefaultContext();
        // Save start cap on first vertex
        if (internal::strokeVertices.empty()) {
            internal::strokeStartCap = ctx.getStrokeCap();
        }
        internal::strokeVertices.push_back({
            Vec3{x, y, z},
            ctx.getColor(),
            ctx.getStrokeWeight()
        });
    }
}

// Add vertex (2D)
inline void vertex(float x, float y) {
    vertex(x, y, 0.0f);
}

// Add vertex (Vec2)
inline void vertex(const Vec2& v) {
    vertex(v.x, v.y);
}

// Add vertex (Vec3)
inline void vertex(const Vec3& v) {
    vertex(v.x, v.y, v.z);
}

// ===========================================================================
// Curve appenders (shape buffer)
//
// Push a curved sequence of vertices into the active beginShape() /
// beginStroke() / beginLines() buffer using the current CurveStyle. All
// `append*` functions are pure conveniences — calling vertex() in a loop
// gives the same result.
// ===========================================================================

// Append an arc spanning [angleBegin, angleEnd] radians around `(cx, cy)`.
// Includes BOTH endpoints. If the previous shape vertex coincides with the
// arc start, the duplicate is benign (degenerate triangle in the fan).
inline void appendArc(float cx, float cy, float radius,
                      float angleBegin, float angleEnd) {
    if (radius <= 0.0f) return;
    float span = angleEnd - angleBegin;
    if (span == 0.0f) return;
    auto& ctx = getDefaultContext();
    int segs = ctx.decideArcSegments(radius, std::abs(span));
    if (segs < 2) segs = 2;
    for (int i = 0; i <= segs; i++) {
        float a = angleBegin + span * ((float)i / (float)segs);
        vertex(cx + std::cos(a) * radius, cy + std::sin(a) * radius);
    }
}

inline void appendArc(const Vec2& center, float radius,
                      float angleBegin, float angleEnd) {
    appendArc(center.x, center.y, radius, angleBegin, angleEnd);
}

// Helper — tessellate one Catmull-Rom segment (with p0/p3 as tangent
// influences, p1->p2 as the visible piece) into the active vertex buffer.
namespace internal {
inline void appendCatmullRomSegment_(const Vec3& p0, const Vec3& p1,
                                     const Vec3& p2, const Vec3& p3) {
    const auto& ctx = getDefaultContext();
    Vec3 b1{p1.x + (p2.x - p0.x) / 6.0f,
            p1.y + (p2.y - p0.y) / 6.0f,
            p1.z + (p2.z - p0.z) / 6.0f};
    Vec3 b2{p2.x - (p3.x - p1.x) / 6.0f,
            p2.y - (p3.y - p1.y) / 6.0f,
            p2.z - (p3.z - p1.z) / 6.0f};
    if (ctx.getCurveMode() == CurveStyle::Mode::Tolerance) {
        std::vector<Vec3> tmp;
        tessellateCubicBezier(p1, b1, b2, p2,
                              ctx.getCurveTolerance() / ctx.getScale(),
                              tmp);
        for (auto& q : tmp) vertex(q.x, q.y, q.z);
    } else {
        int n = std::max(2, ctx.getCurveResolution());
        for (int j = 0; j <= n; j++) {
            float t = (float)j / n;
            float u = 1 - t;
            float c0 = u*u*u, c1 = 3*u*u*t, c2 = 3*u*t*t, c3 = t*t*t;
            vertex(c0*p1.x + c1*b1.x + c2*b2.x + c3*p2.x,
                   c0*p1.y + c1*b1.y + c2*b2.y + c3*p2.y,
                   c0*p1.z + c1*b1.z + c2*b2.z + c3*p2.z);
        }
    }
}
} // namespace internal

// Hard cap on the number of points accepted by appendCurve. Catmull-Rom
// chains expand each segment into a cubic Bezier and tessellate it
// adaptively (up to ~131k recursive nodes at the depth cap, ~1-100ms per
// segment depending on geometry). N segments → linear DoS surface; at
// N=1e6 a single appendCurve call could spend hours of CPU.
//
// 100k is generous — a hand-drawn pen-tool path tops out in the low
// thousands. If you really need more, your art tool is doing something
// the framework wasn't designed for; split into multiple appendCurve
// calls, or batch-tessellate yourself.
inline constexpr int kAppendCurveMaxPoints = 100000;

// Append a chained Catmull-Rom curve through `points`. Emits (N-3)
// segments; needs N >= 4. Tangent points (first and last) are NOT
// pushed as vertices — only the visible interior is appended.
inline void appendCurve(const std::vector<Vec3>& points) {
    if (points.size() < 4) return;
    if ((int)points.size() > kAppendCurveMaxPoints) {
        logWarning() << "appendCurve: refusing N=" << points.size()
                     << " points (cap " << kAppendCurveMaxPoints << ").";
        return;
    }
    for (size_t i = 0; i + 3 < points.size(); i++) {
        internal::appendCatmullRomSegment_(points[i], points[i+1],
                                           points[i+2], points[i+3]);
    }
}

// Closed loop variant — wraparound tangents, all N points are passed
// through.
inline void appendCurve(const std::vector<Vec3>& points, bool closed) {
    if (!closed) { appendCurve(points); return; }
    const int n = (int)points.size();
    if (n < 3) return;
    if (n > kAppendCurveMaxPoints) {
        logWarning() << "appendCurve (closed): refusing N=" << n
                     << " points (cap " << kAppendCurveMaxPoints << ").";
        return;
    }
    for (int i = 0; i < n; i++) {
        internal::appendCatmullRomSegment_(points[(i - 1 + n) % n],
                                           points[i],
                                           points[(i + 1) % n],
                                           points[(i + 2) % n]);
    }
}

} // namespace trussc
