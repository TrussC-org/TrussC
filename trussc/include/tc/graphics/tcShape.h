#pragma once

// This file is included from TrussC.h
// To access variables in internal namespace

#include <vector>

namespace trussc {

// Internal state for shape drawing
namespace internal {
    inline std::vector<Vec3> shapeVertices;
    inline bool shapeStarted = false;
}

// Begin shape drawing
// Supports both fill and stroke
inline void beginShape() {
    internal::shapeVertices.clear();
    internal::shapeStarted = true;
}

// Add vertex (2D)
inline void vertex(float x, float y) {
    if (internal::shapeStarted) {
        internal::shapeVertices.push_back(Vec3{x, y, 0.0f});
    }
}

// Add vertex (3D)
inline void vertex(float x, float y, float z) {
    if (internal::shapeStarted) {
        internal::shapeVertices.push_back(Vec3{x, y, z});
    }
}

// Add vertex (Vec2)
inline void vertex(const Vec2& v) {
    vertex(v.x, v.y);
}

// Add vertex (Vec3)
inline void vertex(const Vec3& v) {
    vertex(v.x, v.y, v.z);
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

    // Fill mode: triangle fan (only renders convex shapes correctly)
    if (ctx.isFillEnabled() && n >= 3) {
        sgl_begin_triangles();
        sgl_c4f(col.r, col.g, col.b, col.a);
        // Triangle fan: vertex 0 as center
        for (size_t i = 1; i < n - 1; i++) {
            sgl_v3f(verts[0].x, verts[0].y, verts[0].z);
            sgl_v3f(verts[i].x, verts[i].y, verts[i].z);
            sgl_v3f(verts[i+1].x, verts[i+1].y, verts[i+1].z);
        }
        sgl_end();
    }

    // Stroke mode: line strip
    if (ctx.isStrokeEnabled() && n >= 2) {
        sgl_c4f(col.r, col.g, col.b, col.a);
        sgl_begin_line_strip();
        for (size_t i = 0; i < n; i++) {
            sgl_v3f(verts[i].x, verts[i].y, verts[i].z);
        }
        if (close && n > 2) {
            sgl_v3f(verts[0].x, verts[0].y, verts[0].z);
        }
        sgl_end();
    }

    internal::shapeVertices.clear();
    internal::shapeStarted = false;
}

} // namespace trussc
