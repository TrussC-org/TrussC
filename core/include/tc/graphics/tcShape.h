#pragma once

// This file is included from TrussC.h
// To access variables in internal namespace

#include <vector>

namespace trussc {

// ---------------------------------------------------------------------------
// Stroke vertex (position + color + width)
// ---------------------------------------------------------------------------
struct StrokeVertex {
    Vec3 pos;
    Color color;
    float width;
};

// Internal state for shape/stroke drawing
namespace internal {
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

} // namespace trussc
