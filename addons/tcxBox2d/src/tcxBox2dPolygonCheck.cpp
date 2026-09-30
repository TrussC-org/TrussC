// =============================================================================
// tcxBox2dPolygonCheck.cpp - Shared polygon point checks (internal)
// =============================================================================

#include "tcxBox2dPolygonCheck.h"
#include "tcxBox2dWorld.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace tcx::box2d::detail {

PolygonError makePolygonShape(const std::vector<tc::Vec2>& points,
                              b2PolygonShape& shape,
                              std::vector<tc::Vec2>& hull) {
    const size_t count = points.size();
    if (count < 3) return PolygonError::TooFewPoints;
    if (count > b2_maxPolygonVertices) return PolygonError::TooManyPoints;

    // The steps below mirror b2PolygonShape::Set() in Box2D v2.4.1 exactly, so
    // what fails here is what Set() would assert on.
    std::vector<b2Vec2> input(count);
    for (size_t i = 0; i < count; ++i) input[i] = World::toBox2d(points[i]);

    // Merge points closer than 0.5 * b2_linearSlop (Set() reads at most
    // b2_maxPolygonVertices points).
    const float weld = 0.5f * b2_linearSlop;
    const size_t used = std::min(count, static_cast<size_t>(b2_maxPolygonVertices));
    b2Vec2 ps[b2_maxPolygonVertices];
    int32 n = 0;
    for (size_t i = 0; i < used; ++i) {
        bool unique = true;
        for (int32 j = 0; j < n; ++j) {
            if (b2DistanceSquared(input[i], ps[j]) < weld * weld) {
                unique = false;
                break;
            }
        }
        if (unique) ps[n++] = input[i];
    }
    if (n < 3) return PolygonError::MergedPoints;

    // Gift-wrap hull, starting from the rightmost (then lowest) point.
    int32 i0 = 0;
    float x0 = ps[0].x;
    for (int32 i = 1; i < n; ++i) {
        float x = ps[i].x;
        if (x > x0 || (x == x0 && ps[i].y < ps[i0].y)) {
            i0 = i;
            x0 = x;
        }
    }
    int32 hullIdx[b2_maxPolygonVertices];
    int32 m = 0;
    int32 ih = i0;
    for (;;) {
        if (m >= b2_maxPolygonVertices) return PolygonError::Degenerate;  // can't happen for n <= 8
        hullIdx[m] = ih;
        int32 ie = 0;
        for (int32 j = 1; j < n; ++j) {
            if (ie == ih) {
                ie = j;
                continue;
            }
            b2Vec2 r = ps[ie] - ps[hullIdx[m]];
            b2Vec2 v = ps[j] - ps[hullIdx[m]];
            float c = b2Cross(r, v);
            if (c < 0.0f) ie = j;
            if (c == 0.0f && v.LengthSquared() > r.LengthSquared()) ie = j;
        }
        ++m;
        ih = ie;
        if (ie == i0) break;
    }
    if (m < 3) return PolygonError::Degenerate;

    // Area, computed the way ComputeCentroid() / ComputeMass() do; both assert
    // area > b2_epsilon (and divide by it in Release).
    b2Vec2 s = ps[hullIdx[0]];
    float area = 0.0f;
    for (int32 i = 0; i < m; ++i) {
        b2Vec2 e1 = ps[hullIdx[i]] - s;
        b2Vec2 e2 = (i + 1 < m) ? ps[hullIdx[i + 1]] - s : ps[hullIdx[0]] - s;
        area += 0.5f * b2Cross(e1, e2);
    }
    if (!(area > b2_epsilon)) return PolygonError::Degenerate;

    // Same input as the checks above, so Set() takes the same path.
    shape.Set(input.data(), static_cast<int32>(count));
    hull.clear();
    hull.reserve(shape.m_count);
    for (int32 i = 0; i < shape.m_count; ++i) hull.push_back(World::toPixels(shape.m_vertices[i]));
    return PolygonError::None;
}

std::string describePolygonError(PolygonError err) {
    switch (err) {
        case PolygonError::None:          return "";
        case PolygonError::TooFewPoints:  return "a polygon needs at least 3 points";
        case PolygonError::TooManyPoints: return "a Box2D polygon has at most 8 points";
        case PolygonError::MergedPoints:  return "fewer than 3 points are left after merging points closer than "
                                                 + tc::toString(World::toPixels(0.5f * b2_linearSlop), 3) + " px";
        case PolygonError::Degenerate:    return "the points are collinear or enclose almost no area";
    }
    return "";
}

std::vector<tc::Vec2> reducedConvexHull(const std::vector<tc::Vec2>& points, size_t maxPoints) {
    if (maxPoints < 3) maxPoints = 3;

    std::vector<tc::Vec2> pts = points;
    std::sort(pts.begin(), pts.end(), [](const tc::Vec2& a, const tc::Vec2& b) {
        return a.x < b.x || (a.x == b.x && a.y < b.y);
    });
    pts.erase(std::unique(pts.begin(), pts.end(), [](const tc::Vec2& a, const tc::Vec2& b) {
        return a.x == b.x && a.y == b.y;
    }), pts.end());
    if (pts.size() < 3) return pts;

    // z of (a - o) x (b - o), in double.
    auto cross = [](const tc::Vec2& o, const tc::Vec2& a, const tc::Vec2& b) {
        return (double(a.x) - o.x) * (double(b.y) - o.y) - (double(a.y) - o.y) * (double(b.x) - o.x);
    };

    // Andrew's monotone chain; collinear points are dropped (<= 0).
    std::vector<tc::Vec2> hull(pts.size() * 2);
    size_t k = 0;
    for (size_t i = 0; i < pts.size(); ++i) {
        while (k >= 2 && cross(hull[k - 2], hull[k - 1], pts[i]) <= 0.0) --k;
        hull[k++] = pts[i];
    }
    for (size_t i = pts.size() - 1, lower = k + 1; i > 0; --i) {
        while (k >= lower && cross(hull[k - 2], hull[k - 1], pts[i - 1]) <= 0.0) --k;
        hull[k++] = pts[i - 1];
    }
    hull.resize(k - 1);  // the last point repeats the first

    // Drop the vertex whose triangle with its neighbours is smallest (the
    // area its removal loses) until maxPoints remain.
    while (hull.size() > maxPoints) {
        const size_t n = hull.size();
        size_t best = 0;
        double bestArea = std::numeric_limits<double>::max();
        for (size_t i = 0; i < n; ++i) {
            double a = std::abs(cross(hull[(i + n - 1) % n], hull[i], hull[(i + 1) % n]));
            if (a < bestArea) {
                bestArea = a;
                best = i;
            }
        }
        hull.erase(hull.begin() + static_cast<std::ptrdiff_t>(best));
    }
    return hull;
}

std::vector<tc::Vec2> pathPoints(const tc::Path& path) {
    std::vector<tc::Vec2> out;
    out.reserve(path.getVertices().size());
    for (const auto& v : path.getVertices()) out.push_back(tc::Vec2(v.x, v.y));
    return out;
}

bool convexRing(const tc::Path& path, std::vector<tc::Vec2>& ring) {
    // Collect the rings the way buildFillTriangles() does.
    std::vector<tc::Vec2> found;
    int rings = 0;
    const auto& verts = path.getVertices();
    for (size_t si = 0; si < path.getNumSubpaths(); ++si) {
        auto [s, e] = path.getSubpathRange(si);
        if (e - s < 3) continue;
        std::vector<tc::Vec2> r;
        for (size_t k = s; k < e; ++k) {
            tc::Vec2 p(verts[k].x, verts[k].y);
            if (!r.empty() && r.back().x == p.x && r.back().y == p.y) continue;
            r.push_back(p);
        }
        while (r.size() >= 2 && r.front().x == r.back().x && r.front().y == r.back().y) r.pop_back();
        if (r.size() < 3) continue;
        if (++rings > 1) return false;
        found = std::move(r);
    }
    if (rings != 1 || found.size() > b2_maxPolygonVertices) return false;

    // Convex: every turn goes the same way (collinear allowed), and the edges
    // turn once around in total (a star has same-sign turns too, but turns
    // twice or more).
    const size_t n = found.size();
    bool left = false, right = false;
    double turning = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const tc::Vec2& a = found[i];
        const tc::Vec2& b = found[(i + 1) % n];
        const tc::Vec2& c = found[(i + 2) % n];
        double e1x = double(b.x) - a.x, e1y = double(b.y) - a.y;
        double e2x = double(c.x) - b.x, e2y = double(c.y) - b.y;
        double cr = e1x * e2y - e1y * e2x;
        if (cr > 0.0) left = true;
        if (cr < 0.0) right = true;
        turning += std::atan2(cr, e1x * e2x + e1y * e2y);
    }
    if ((left && right) || (!left && !right)) return false;
    if (std::abs(turning) > 1.5 * tc::TAU) return false;
    ring = std::move(found);
    return true;
}

void drawPathOutline(const tc::Path& path) {
    const auto& verts = path.getVertices();
    for (size_t si = 0; si < path.getNumSubpaths(); ++si) {
        auto [s, e] = path.getSubpathRange(si);
        if (e - s < 2) continue;
        for (size_t k = s; k < e; ++k) {
            const tc::Vec3& a = verts[k];
            const tc::Vec3& b = verts[(k + 1 < e) ? k + 1 : s];
            tc::drawLine(a.x, a.y, b.x, b.y);
        }
    }
}

bool makeCompoundShapes(const tc::Path& path, CompoundShapes& out) {
    out = CompoundShapes();

    std::vector<tc::Vec2> ring;
    if (convexRing(path, ring)) {
        b2PolygonShape shape;
        std::vector<tc::Vec2> hull;
        if (makePolygonShape(ring, shape, hull) == PolygonError::None) {
            out.shapes.push_back(shape);
            return true;
        }
    }

    const std::vector<std::array<float, 2>> tris = path.buildFillTriangles();
    out.triangles = tris.size() / 3;
    std::vector<tc::Vec2> tri(3);
    for (size_t t = 0; t < out.triangles; ++t) {
        for (size_t k = 0; k < 3; ++k) tri[k] = tc::Vec2(tris[t * 3 + k][0], tris[t * 3 + k][1]);
        b2PolygonShape shape;
        std::vector<tc::Vec2> hull;
        if (makePolygonShape(tri, shape, hull) == PolygonError::None) {
            out.shapes.push_back(shape);
        } else {
            ++out.skipped;
        }
    }
    return !out.shapes.empty();
}

} // namespace tcx::box2d::detail
