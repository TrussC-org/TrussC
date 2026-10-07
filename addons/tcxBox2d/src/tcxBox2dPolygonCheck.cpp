// =============================================================================
// tcxBox2dPolygonCheck.cpp - Shared polygon point checks (internal)
// =============================================================================

#include "tcxBox2dPolygonCheck.h"
#include "tcxBox2dWorld.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>

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

// A dynamic body's b2Body::ResetMassData() sums the fixtures' mass, mass *
// centroid and inertia about the body origin, then subtracts mass *
// |centroid|^2. For a body that is tiny next to its distance from the origin
// the two nearly cancel in float, and the result can be <= 0: an assert
// (Debug) or NaN motion (Release). Mirror that step at density 1 (`shapes`
// in fixture-list order, which is the reverse of creation order) and demand a
// margin of 16 float epsilons of the subtracted term for one polygon, so the
// result stays positive at any density (fuzzed at densities 0.001 to 1000).
// A compound body needs 16 + (fixtures - 1) epsilons: Box2D sums the
// per-fixture values in float, in order, so the rounding differs from one
// density to another (by up to about 0.8 epsilon per fixture near the limit).
// Measured: with a fixed 16-epsilon margin, bodies accepted at density 1 had
// m_I <= 0 at some density in 0.001 to 1000 (133 cases for a 128-gon, 8642
// for a 2048-gon); with one more epsilon per fixture, 0 cases.
bool inertiaSurvives(const b2PolygonShape* const* shapes, size_t count) {
    float mass = 0.0f, inertia = 0.0f;
    b2Vec2 center = b2Vec2_zero;
    for (size_t i = 0; i < count; ++i) {
        b2MassData md;
        shapes[i]->ComputeMass(&md, 1.0f);
        mass += md.mass;
        center += md.mass * md.center;
        inertia += md.I;
    }
    center *= 1.0f / mass;
    const float shift = mass * b2Dot(center, center);
    const float centered = inertia - shift;
    const float margin = 16.0f + static_cast<float>(count - 1);
    return centered > margin * FLT_EPSILON * shift;
}

} // namespace

PolygonError makePolygonShape(const std::vector<tc::Vec2>& points,
                              b2PolygonShape& shape,
                              std::vector<tc::Vec2>& hull,
                              OffsetCheck offset) {
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

    // The polygon as a body of its own: its inertia about its centroid must
    // survive float rounding (see inertiaSurvives()).
    const b2PolygonShape* one = &built;
    if (offset == OffsetCheck::Apply && !inertiaSurvives(&one, 1)) return PolygonError::TooSmallForOffset;

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

bool keepsInertia(const std::vector<b2PolygonShape>& shapes) {
    if (shapes.empty()) return false;
    // Box2D walks the fixture list, newest fixture first.
    std::vector<const b2PolygonShape*> order;
    order.reserve(shapes.size());
    for (size_t i = shapes.size(); i > 0; --i) order.push_back(&shapes[i - 1]);
    return inertiaSurvives(order.data(), order.size());
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

std::string describeCollapsedHull(const std::string& caller, bool mayHave) {
    return "fewer than 3 distinct, non-collinear points (" + caller
           + (mayHave ? " may have dropped" : " drops") + " duplicate and collinear points)";
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
    // area its removal loses) until maxPoints remain; on equal area the lowest
    // index goes first. Visvalingam-style, O(h log h): each vertex's area sits
    // in a min-heap, and a removal recomputes only its two neighbours (their
    // version goes up, so their old heap entries are skipped when popped).
    const size_t n = hull.size();
    if (n <= maxPoints) return hull;

    std::vector<size_t> prev(n), next(n);
    std::vector<uint32_t> version(n, 0);
    std::vector<char> alive(n, 1);
    for (size_t i = 0; i < n; ++i) {
        prev[i] = (i + n - 1) % n;
        next[i] = (i + 1) % n;
    }
    // Areas that are not below DBL_MAX (inf, NaN) all rank as DBL_MAX, the
    // lowest index first, as a scan keeping the first `area < best` from
    // best = DBL_MAX would pick them. It also keeps the heap order strict.
    auto areaAt = [&](size_t i) {
        double a = std::abs(cross(hull[prev[i]], hull[i], hull[next[i]]));
        return a < std::numeric_limits<double>::max() ? a : std::numeric_limits<double>::max();
    };
    struct Entry {
        double area;
        size_t index;
        uint32_t version;
    };
    // priority_queue pops its largest element: rank smaller area, then lower
    // index, as larger.
    auto popsLater = [](const Entry& a, const Entry& b) {
        return a.area > b.area || (a.area == b.area && a.index > b.index);
    };
    std::priority_queue<Entry, std::vector<Entry>, decltype(popsLater)> heap(popsLater);
    for (size_t i = 0; i < n; ++i) heap.push({areaAt(i), i, 0});

    for (size_t remaining = n; remaining > maxPoints;) {
        const Entry e = heap.top();
        heap.pop();
        if (!alive[e.index] || e.version != version[e.index]) continue;  // stale
        alive[e.index] = 0;
        --remaining;
        const size_t p = prev[e.index], q = next[e.index];
        next[p] = q;
        prev[q] = p;
        heap.push({areaAt(p), p, ++version[p]});
        heap.push({areaAt(q), q, ++version[q]});
    }

    std::vector<tc::Vec2> out;
    out.reserve(maxPoints);
    for (size_t i = 0; i < n; ++i) {
        if (alive[i]) out.push_back(hull[i]);
    }
    return out;
}

size_t countPointsInsideHull(const std::vector<tc::Vec2>& points,
                             const std::vector<tc::Vec2>& hull) {
    if (points.size() <= hull.size() || hull.size() < 3) return 0;
    const double tol = World::toPixels(0.5f * b2_linearSlop);
    size_t inside = 0;
    for (const auto& p : points) {
        // Distance to the nearest hull edge, in double. Every point is inside
        // the hull or on its outline, so this is its distance to the outline.
        double best = std::numeric_limits<double>::max();
        for (size_t i = 0; i < hull.size(); ++i) {
            const tc::Vec2& a = hull[i];
            const tc::Vec2& b = hull[(i + 1) % hull.size()];
            const double ex = double(b.x) - a.x, ey = double(b.y) - a.y;
            const double px = double(p.x) - a.x, py = double(p.y) - a.y;
            const double len2 = ex * ex + ey * ey;
            double t = len2 > 0.0 ? (px * ex + py * ey) / len2 : 0.0;
            t = std::clamp(t, 0.0, 1.0);
            const double dx = px - t * ex, dy = py - t * ey;
            best = std::min(best, std::sqrt(dx * dx + dy * dy));
        }
        if (best > tol) ++inside;
    }
    return inside;
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
        double dot = e1x * e2x + e1y * e2y;
        // An edge that runs straight back is no convex corner, and its turn
        // (atan2 of +-0 gives a half turn, TAU / 2, signed by the zero) could
        // make the total look like one turn around.
        if (cr == 0.0 && dot < 0.0) return false;
        if (cr > 0.0) left = true;
        if (cr < 0.0) right = true;
        turning += std::atan2(cr, dot);
    }
    if ((left && right) || (!left && !right)) return false;
    if (std::abs(turning) > 1.5 * tc::TAU) return false;
    ring = std::move(found);
    return true;
}

tc::Mesh makeFillMesh(const std::vector<tc::Vec2>& fill) {
    tc::Mesh mesh;
    mesh.setMode(tc::PrimitiveMode::Triangles);
    for (const auto& p : fill) mesh.addVertex(tc::Vec3(p.x, p.y, 0.0f));
    return mesh;
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

    // Triangulated once: the fixtures below and the fill that draws them.
    const std::vector<std::array<float, 2>> tris = path.buildFillTriangles();
    auto keepFill = [&]() {
        out.fill.reserve(tris.size());
        for (const auto& p : tris) out.fill.push_back(tc::Vec2(p[0], p[1]));
    };

    std::vector<tc::Vec2> ring;
    if (convexRing(path, ring)) {
        b2PolygonShape shape;
        std::vector<tc::Vec2> hull;
        const PolygonError err = makePolygonShape(ring, shape, hull);
        if (err == PolygonError::None) {
            out.shapes.push_back(shape);
            keepFill();
            return true;
        }
        if (err == PolygonError::TooSmallForOffset) {
            // A usable polygon, only too small for its offset: its triangles
            // would fail the combined check the same way.
            out.error = err;
            return false;
        }
        // Collinear or merged corners: triangulate, and keep what has area.
    }

    out.triangles = tris.size() / 3;
    std::vector<tc::Vec2> tri(3);
    for (size_t t = 0; t < out.triangles; ++t) {
        for (size_t k = 0; k < 3; ++k) tri[k] = tc::Vec2(tris[t * 3 + k][0], tris[t * 3 + k][1]);
        b2PolygonShape shape;
        std::vector<tc::Vec2> hull;
        // Box2D checks the inertia only for the whole body (PolyShape and
        // RigidBody2D add the fixtures at density 0 and reset the mass data
        // once): a tiny ear triangle of an ordinary outline is fine on its
        // own (checked below, together).
        if (makePolygonShape(tri, shape, hull, OffsetCheck::Skip) == PolygonError::None) {
            out.shapes.push_back(shape);
        } else {
            ++out.skipped;
        }
    }
    if (out.shapes.empty()) {
        out.error = PolygonError::Degenerate;
        return false;
    }
    if (!keepsInertia(out.shapes)) {
        out.shapes.clear();
        out.error = PolygonError::TooSmallForOffset;
        return false;
    }
    keepFill();
    return true;
}

} // namespace tcx::box2d::detail
