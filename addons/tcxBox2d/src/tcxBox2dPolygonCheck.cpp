// =============================================================================
// tcxBox2dPolygonCheck.cpp - Shared polygon point checks (internal)
// =============================================================================

#include "tcxBox2dPolygonCheck.h"
#include "tcxBox2dWorld.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <limits>

namespace tcx::box2d::detail {

namespace {

// True when `shape` holds every point of `input` and `input`, read in its own
// order, walks around the hull: a cyclic rotation of shape.m_vertices, in
// either direction. Only then is the caller's order a simple convex polygon;
// a crossing order of hull vertices (a Z-ordered square, a star-ordered
// pentagon) is not.
bool keepsHullOrder(const std::vector<b2Vec2>& input, const b2PolygonShape& shape) {
    const size_t count = input.size();
    if (static_cast<size_t>(shape.m_count) != count) return false;
    // Position of each input point in Box2D's hull. The points are distinct
    // (none were merged) and Set() copies them unchanged, so they match exactly.
    int32 pos[b2_maxPolygonVertices];
    for (size_t i = 0; i < count; ++i) {
        pos[i] = -1;
        for (int32 k = 0; k < shape.m_count; ++k) {
            if (shape.m_vertices[k].x == input[i].x && shape.m_vertices[k].y == input[i].y) {
                pos[i] = k;
                break;
            }
        }
        if (pos[i] < 0) return false;
    }
    const int32 m = shape.m_count;
    bool forward = true, backward = true;
    for (size_t i = 0; i < count; ++i) {
        const int32 next = pos[(i + 1) % count];
        if (next != (pos[i] + 1) % m) forward = false;
        if (next != (pos[i] + m - 1) % m) backward = false;
    }
    return forward || backward;
}

} // namespace

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
    b2PolygonShape built;
    built.Set(input.data(), static_cast<int32>(count));

    // A dynamic body's b2Body::ResetMassData() takes the polygon's inertia
    // about the body origin and subtracts mass * |centroid|^2. For a polygon
    // that is tiny next to its distance from the origin the two nearly cancel
    // in float, and the result can be <= 0: an assert (Debug) or NaN motion
    // (Release). Mirror that step at density 1 and demand a margin of
    // 16 float epsilons of the subtracted term, so the result stays positive
    // at any density (fuzzed at densities 0.001 to 1000).
    b2MassData md;
    built.ComputeMass(&md, 1.0f);
    b2Vec2 center = b2Vec2_zero;
    center += md.mass * md.center;
    center *= 1.0f / md.mass;
    const float shift = md.mass * b2Dot(center, center);
    const float centered = md.I - shift;
    if (!(centered > 16.0f * FLT_EPSILON * shift)) return PolygonError::TooSmallForOffset;

    shape = built;
    hull.clear();
    if (keepsHullOrder(input, shape)) {
        // Box2D kept every point and the caller's order already goes around
        // the hull: hand the points back as given (Set() starts at the
        // rightmost point and may flip the winding).
        hull = points;
    } else {
        hull.reserve(shape.m_count);
        for (int32 i = 0; i < shape.m_count; ++i) hull.push_back(World::toPixels(shape.m_vertices[i]));
    }
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
        case PolygonError::TooSmallForOffset:
            return "the polygon is too small for its distance from the body origin (its rotational inertia "
                   "is lost to float rounding); give points relative to the body position instead";
    }
    return "";
}

std::string describeCollapsedHull(const std::string& caller) {
    return "fewer than 3 distinct, non-collinear points (" + caller
           + " drops duplicate and collinear points)";
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

} // namespace tcx::box2d::detail
