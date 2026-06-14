#pragma once

// This file is included from TrussC.h

// Disable Windows GDI Path macro
#ifdef _WIN32
#undef Path
#endif

#include <vector>
#include <cmath>
#include <deque>
#include <array>
#include <algorithm>
#include <limits>
#include "../../earcut/earcut.hpp"

namespace trussc {

// Path — vertex container with optional curve generation. Supports multiple
// **subpaths**: a single Path can contain several disjoint contours separated
// by `moveTo()` (think SVG `<path>` with `M ... M ...`). Each subpath has its
// own close state, so glyphs like `O` / `日` / `あ` (outer + holes) fit in one
// Path. `draw()`, `drawStroke()`, `drawFill()`, `getPerimeter()` all iterate
// subpaths internally; the legacy single-contour API (lineTo / close, no
// moveTo) keeps working unchanged because the Path starts with a single
// implicit subpath.
class Path {
public:
    Path() {
        subpathStarts_.push_back(0);
        subpathClosed_.push_back(false);
    }

    // Constructor (from vertex list) — single subpath, not closed.
    Path(const std::vector<Vec2>& verts) : Path() {
        for (const auto& v : verts) {
            vertices_.push_back(Vec3{v.x, v.y, 0.0f});
        }
    }

    Path(const std::vector<Vec3>& verts) : Path() {
        vertices_ = verts;
    }

    // Add vertex
    void addVertex(float x, float y) {
        vertices_.push_back(Vec3{x, y, 0.0f});
    }

    void addVertex(float x, float y, float z) {
        vertices_.push_back(Vec3{x, y, z});
    }

    void addVertex(const Vec2& v) {
        addVertex(v.x, v.y);
    }

    void addVertex(const Vec3& v) {
        vertices_.push_back(v);
    }

    // Add multiple vertices at once
    void addVertices(const std::vector<Vec2>& verts) {
        for (const auto& v : verts) {
            addVertex(v);
        }
    }

    void addVertices(const std::vector<Vec3>& verts) {
        for (const auto& v : verts) {
            addVertex(v);
        }
    }

    // Get vertices
    const std::vector<Vec3>& getVertices() const {
        return vertices_;
    }

    std::vector<Vec3>& getVertices() {
        return vertices_;
    }

    // Vertex count
    int size() const {
        return static_cast<int>(vertices_.size());
    }

    bool empty() const {
        return vertices_.empty();
    }

    // Access specific vertex
    Vec3& operator[](int index) {
        return vertices_[index];
    }

    const Vec3& operator[](int index) const {
        return vertices_[index];
    }

    // Clear (resets to a single empty open subpath).
    void clear() {
        vertices_.clear();
        curveVertices_.clear();
        subpathStarts_.clear();
        subpathClosed_.clear();
        subpathStarts_.push_back(0);
        subpathClosed_.push_back(false);
    }

    // -------------------------------------------------------------------------
    // Subpath API — call moveTo() to start a new disjoint contour. The first
    // moveTo on an empty Path just places the starting vertex (it doesn't
    // create a second subpath because subpath 0 is empty). Subsequent moveTo
    // calls open a new subpath each time. close() / isClosed() / setClosed()
    // operate on the **current** (last) subpath only — single-subpath callers
    // see the original behavior. Use getNumSubpaths() / getSubpathRange(i) /
    // isSubpathClosed(i) to walk subpaths in custom drawing code.
    // -------------------------------------------------------------------------
    void moveTo(float x, float y, float z = 0) {
        if (!vertices_.empty() && vertices_.size() > subpathStarts_.back()) {
            subpathStarts_.push_back(vertices_.size());
            subpathClosed_.push_back(false);
        }
        vertices_.push_back(Vec3{x, y, z});
        curveVertices_.clear();  // Catmull-Rom buffer is per-subpath
    }
    void moveTo(const Vec2& p) { moveTo(p.x, p.y, 0); }
    void moveTo(const Vec3& p) { moveTo(p.x, p.y, p.z); }

    size_t getNumSubpaths() const { return subpathStarts_.size(); }

    // [start, end) index range into getVertices() for subpath i.
    std::pair<size_t, size_t> getSubpathRange(size_t i) const {
        const size_t s = subpathStarts_[i];
        const size_t e = (i + 1 < subpathStarts_.size())
                       ? subpathStarts_[i + 1]
                       : vertices_.size();
        return {s, e};
    }

    bool isSubpathClosed(size_t i) const { return subpathClosed_[i]; }

    // =========================================================================
    // Adding lines and curves
    // =========================================================================

    // lineTo is an alias for addVertex
    void lineTo(float x, float y, float z = 0) {
        addVertex(x, y, z);
    }

    void lineTo(const Vec2& p) {
        addVertex(p);
    }

    void lineTo(const Vec3& p) {
        addVertex(p);
    }

    // Cubic Bezier curve
    // -----------------------------------------------------------------------
    // bezierTo / quadBezierTo / curveTo
    //
    // Default behaviour (no `resolution` arg, or resolution < 0): inherit
    // the current CurveStyle. In Tolerance mode the curve is adaptively
    // subdivided so the polyline stays within the configured pixel
    // tolerance (scale-aware). In Resolution mode it falls back to N
    // uniform t-samples.
    //
    // Pass an explicit positive `resolution` to force N uniform t-samples
    // regardless of the CurveStyle (the original behaviour — kept so
    // existing callers that asked for, say, resolution=64 still get
    // exactly 64 samples).
    //
    // NOTE: this changes the default of `resolution` from 20 to -1 (=
    // sentinel for "use CurveStyle"). Existing `path.bezierTo(c1, c2, t)`
    // calls now produce smoother curves at small radii and may emit a
    // different vertex count than before — visually equivalent or better.
    // -----------------------------------------------------------------------

    // Cubic Bezier — cp1, cp2 control points, `to` endpoint.
    void bezierTo(const Vec3& cp1, const Vec3& cp2, const Vec3& to, int resolution = -1) {
        if (vertices_.empty()) vertices_.push_back(Vec3{0, 0, 0});
        Vec3 p0 = vertices_.back();

        if (resolution < 0) {
            const auto& ctx = getDefaultContext();
            if (ctx.getCurveMode() == CurveStyle::Mode::Tolerance) {
                std::vector<Vec3> tmp;
                tessellateCubicBezier(p0, cp1, cp2, to,
                                      ctx.getCurveTolerance() / ctx.getCurrentScale(),
                                      tmp);
                // tmp[0] == p0 — already at vertices_.back(), skip it.
                for (size_t i = 1; i < tmp.size(); i++) vertices_.push_back(tmp[i]);
                return;
            }
            resolution = std::max(2, ctx.getCurveResolution());
        }
        for (int i = 1; i <= resolution; i++) {
            float t  = (float)i / resolution;
            float t2 = t * t,  t3 = t2 * t;
            float mt = 1 - t,  mt2 = mt * mt, mt3 = mt2 * mt;
            vertices_.push_back(Vec3{
                mt3*p0.x + 3*mt2*t*cp1.x + 3*mt*t2*cp2.x + t3*to.x,
                mt3*p0.y + 3*mt2*t*cp1.y + 3*mt*t2*cp2.y + t3*to.y,
                mt3*p0.z + 3*mt2*t*cp1.z + 3*mt*t2*cp2.z + t3*to.z});
        }
    }

    void bezierTo(float cx1, float cy1, float cx2, float cy2, float x, float y, int resolution = -1) {
        bezierTo(Vec3{cx1, cy1, 0}, Vec3{cx2, cy2, 0}, Vec3{x, y, 0}, resolution);
    }
    void bezierTo(const Vec2& cp1, const Vec2& cp2, const Vec2& to, int resolution = -1) {
        bezierTo(Vec3{cp1.x, cp1.y, 0}, Vec3{cp2.x, cp2.y, 0}, Vec3{to.x, to.y, 0}, resolution);
    }

    // Quadratic Bezier — cp control point, `to` endpoint.
    void quadBezierTo(const Vec3& cp, const Vec3& to, int resolution = -1) {
        if (vertices_.empty()) vertices_.push_back(Vec3{0, 0, 0});
        Vec3 p0 = vertices_.back();

        if (resolution < 0) {
            const auto& ctx = getDefaultContext();
            if (ctx.getCurveMode() == CurveStyle::Mode::Tolerance) {
                std::vector<Vec3> tmp;
                tessellateQuadBezier(p0, cp, to,
                                     ctx.getCurveTolerance() / ctx.getCurrentScale(),
                                     tmp);
                for (size_t i = 1; i < tmp.size(); i++) vertices_.push_back(tmp[i]);
                return;
            }
            resolution = std::max(2, ctx.getCurveResolution());
        }
        for (int i = 1; i <= resolution; i++) {
            float t  = (float)i / resolution;
            float t2 = t * t;
            float mt = 1 - t, mt2 = mt * mt;
            vertices_.push_back(Vec3{
                mt2*p0.x + 2*mt*t*cp.x + t2*to.x,
                mt2*p0.y + 2*mt*t*cp.y + t2*to.y,
                mt2*p0.z + 2*mt*t*cp.z + t2*to.z});
        }
    }

    void quadBezierTo(float cx, float cy, float x, float y, int resolution = -1) {
        quadBezierTo(Vec3{cx, cy, 0}, Vec3{x, y, 0}, resolution);
    }
    void quadBezierTo(const Vec2& cp, const Vec2& to, int resolution = -1) {
        quadBezierTo(Vec3{cp.x, cp.y, 0}, Vec3{to.x, to.y, 0}, resolution);
    }

    // Catmull-Rom spline. Each consecutive curveTo() emits the segment
    // between the previous-1 and current points using the previous-2 and
    // next as tangent influences (i.e. you must call curveTo at least 4
    // times before any geometry appears). When `resolution < 0`, the
    // segment is converted to its equivalent cubic Bezier and adaptively
    // subdivided per CurveStyle.
    void curveTo(const Vec3& to, int resolution = -1) {
        curveVertices_.push_back(to);
        if (curveVertices_.size() < 4) return;

        size_t n = curveVertices_.size();
        const Vec3& p0 = curveVertices_[n - 4];
        const Vec3& p1 = curveVertices_[n - 3];
        const Vec3& p2 = curveVertices_[n - 2];
        const Vec3& p3 = curveVertices_[n - 1];

        if (vertices_.empty() ||
            vertices_.back().x != p1.x || vertices_.back().y != p1.y || vertices_.back().z != p1.z) {
            vertices_.push_back(p1);
        }

        if (resolution < 0) {
            const auto& ctx = getDefaultContext();
            if (ctx.getCurveMode() == CurveStyle::Mode::Tolerance) {
                // Convert Catmull-Rom (uniform) segment to its cubic-Bezier
                // equivalent (B1 = p1 + (p2-p0)/6, B2 = p2 - (p3-p1)/6) and
                // reuse the same adaptive tessellator the rest of the
                // plumbing depends on.
                Vec3 b1{p1.x + (p2.x - p0.x) / 6.0f,
                        p1.y + (p2.y - p0.y) / 6.0f,
                        p1.z + (p2.z - p0.z) / 6.0f};
                Vec3 b2{p2.x - (p3.x - p1.x) / 6.0f,
                        p2.y - (p3.y - p1.y) / 6.0f,
                        p2.z - (p3.z - p1.z) / 6.0f};
                std::vector<Vec3> tmp;
                tessellateCubicBezier(p1, b1, b2, p2,
                                      ctx.getCurveTolerance() / ctx.getCurrentScale(),
                                      tmp);
                for (size_t i = 1; i < tmp.size(); i++) vertices_.push_back(tmp[i]);
                return;
            }
            resolution = std::max(2, ctx.getCurveResolution());
        }
        for (int i = 1; i <= resolution; i++) {
            float t = (float)i / resolution;
            vertices_.push_back(catmullRom(p0, p1, p2, p3, t));
        }
    }

    void curveTo(float x, float y, float z = 0, int resolution = -1) {
        curveTo(Vec3{x, y, z}, resolution);
    }

    void curveTo(const Vec2& to, int resolution = -1) {
        curveTo(Vec3{to.x, to.y, 0}, resolution);
    }

    // Arc
    void arc(const Vec3& center, float radiusX, float radiusY,
             float angleBegin, float angleEnd, bool clockwise = true, int circleResolution = 20) {

        // Degrees to radians
        float startRad = angleBegin * TAU / 360.0f;
        float endRad = angleEnd * TAU / 360.0f;

        float diff = endRad - startRad;

        // Adjust segment count based on angle size
        int segments = std::max(2, (int)(std::abs(diff) / TAU * circleResolution));

        for (int i = 0; i <= segments; i++) {
            float t = (float)i / segments;
            float angle = clockwise ? (startRad + diff * t) : (startRad - diff * t);

            Vec3 p;
            p.x = center.x + std::cos(angle) * radiusX;
            p.y = center.y + std::sin(angle) * radiusY;
            p.z = center.z;

            vertices_.push_back(p);
        }
    }

    void arc(float x, float y, float radiusX, float radiusY,
             float angleBegin, float angleEnd, int circleResolution = 20) {
        arc(Vec3{x, y, 0}, radiusX, radiusY, angleBegin, angleEnd, true, circleResolution);
    }

    void arc(const Vec2& center, float radiusX, float radiusY,
             float angleBegin, float angleEnd, int circleResolution = 20) {
        arc(Vec3{center.x, center.y, 0}, radiusX, radiusY, angleBegin, angleEnd, true, circleResolution);
    }

    // -----------------------------------------------------------------------
    // Circular arc — radians, tolerance-aware (uses current CurveStyle).
    //
    // Disambiguated from the elliptical overloads above by parameter count:
    // these take a single `radius` instead of `radiusX, radiusY`. Angles
    // are in **radians** (matches TrussC's overall convention; the older
    // overloads take degrees for backward-compat reasons).
    // -----------------------------------------------------------------------
    void arc(const Vec3& center, float radius,
             float angleBegin, float angleEnd, bool clockwise = true) {
        if (radius <= 0.0f) return;
        float diff = angleEnd - angleBegin;
        if (!clockwise) diff = -diff;
        if (diff == 0.0f) return;
        int segments = getDefaultContext().decideArcSegments(radius, std::abs(diff));
        for (int i = 0; i <= segments; i++) {
            float t = (float)i / (float)segments;
            float a = angleBegin + diff * t;
            vertices_.push_back(Vec3{
                center.x + std::cos(a) * radius,
                center.y + std::sin(a) * radius,
                center.z
            });
        }
    }
    void arc(float x, float y, float radius,
             float angleBegin, float angleEnd, bool clockwise = true) {
        arc(Vec3{x, y, 0}, radius, angleBegin, angleEnd, clockwise);
    }
    void arc(const Vec2& center, float radius,
             float angleBegin, float angleEnd, bool clockwise = true) {
        arc(Vec3{center.x, center.y, 0}, radius, angleBegin, angleEnd, clockwise);
    }

    // Close/Open path — operates on the current (last) subpath.
    void close()                  { subpathClosed_.back() = true; }
    void setClosed(bool closed)   { subpathClosed_.back() = closed; }
    bool isClosed() const         { return subpathClosed_.back(); }

    // Reverse the winding direction (vertex order) of one subpath, or of
    // every subpath. Under drawFill()'s non-zero winding rule, reversing a
    // subpath toggles it between filling and cutting — e.g. build a circle
    // with the same helper you use for outlines, then reverseWinding() it
    // into a hole punch. Reversing ALL subpaths leaves the rendered result
    // unchanged (only relative direction matters), which also makes this
    // the fix for imported outlines that use the opposite convention.
    Path& reverseWinding(size_t i) {
        auto [s, e] = getSubpathRange(i);
        std::reverse(vertices_.begin() + s, vertices_.begin() + e);
        return *this;
    }
    Path& reverseWinding() {
        for (size_t i = 0; i < subpathStarts_.size(); ++i) reverseWinding(i);
        return *this;
    }

    // Draw — fill (per-subpath triangle fan) + 1px stroke (per-subpath line
    // strip). Fill is convex-only; for concave / holed shapes call drawFill().
    void draw() const {
        if (vertices_.empty()) return;
        auto& ctx = getDefaultContext();
        Color col = ctx.getColor();

        for (size_t si = 0; si < subpathStarts_.size(); ++si) {
            auto [s, e] = getSubpathRange(si);
            const size_t n = e - s;

            if (ctx.isFillEnabled() && n >= 3) {
                sgl_begin_triangles();
                sgl_c4f(col.r, col.g, col.b, col.a);
                for (size_t i = 1; i < n - 1; ++i) {
                    sgl_v3f(vertices_[s].x,     vertices_[s].y,     vertices_[s].z);
                    sgl_v3f(vertices_[s+i].x,   vertices_[s+i].y,   vertices_[s+i].z);
                    sgl_v3f(vertices_[s+i+1].x, vertices_[s+i+1].y, vertices_[s+i+1].z);
                }
                sgl_end();
            }

            if (ctx.isStrokeEnabled() && n >= 2) {
                sgl_c4f(col.r, col.g, col.b, col.a);
                sgl_begin_line_strip();
                for (size_t i = 0; i < n; ++i) {
                    sgl_v3f(vertices_[s+i].x, vertices_[s+i].y, vertices_[s+i].z);
                }
                if (subpathClosed_[si] && n > 2) {
                    sgl_v3f(vertices_[s].x, vertices_[s].y, vertices_[s].z);
                }
                sgl_end();
            }
        }
    }

    // Fill the path as a concave polygon with holes (earcut tessellation).
    //
    // Subpaths are grouped following the non-zero winding rule — the default
    // fill rule of SVG / PostScript and the rule TrueType / CFF rasterizers
    // use. For each subpath we compute the winding number of the region just
    // inside it: its own direction (sign of the shoelace area) plus the
    // directions of every subpath containing it. Winding 0 → hole boundary,
    // attached to the smallest enclosing filled subpath; non-zero → filled.
    //
    // Because only the RELATIVE direction of a ring and its container
    // matters, this handles TrueType-flavored (CW outers) and CFF-flavored
    // (CCW outers) fonts, either Y-axis orientation, and even paths that mix
    // both conventions (e.g. glyph paths from two fonts merged into one
    // Path) — exactly like a real non-zero winding rasterizer.
    //
    // Same-direction subpaths never punch holes in each other (winding adds
    // up, never cancels), so glyphs built from several overlapping contours
    // (e.g. serif caps overlapping the main stem in Noto Serif JP's 'W')
    // fill correctly as their union. Containment between such overlapping
    // same-direction rings is geometrically ambiguous, but harmless: it only
    // moves the winding between +1 and +2, never to 0.
    //
    // To cut a hole in a hand-built Path, wind the inner subpath opposite to
    // the outer one (see reverseWinding()). Zero-winding subpaths with no
    // enclosing filled ring are drawn as independent polygons (fallback).
    //
    // Self-intersecting subpaths are split at each crossing into simple
    // rings before tessellation. Machine-converted fonts (e.g. CFF →
    // TrueType builds of Noto) often carry tiny self-crossing loops at
    // stroke joins; rasterizers never notice (non-zero winding scanning is
    // immune) but ear clipping requires simple polygons. After splitting,
    // an inverted micro-loop gets winding 0 and vanishes as a sub-pixel
    // hole, and a genuine figure-8 fills both lobes — both matching what a
    // real non-zero winding rasterizer produces.
    // Use draw() with `fill` enabled for the fast convex-only fan path.
    void drawFill() const {
        if (vertices_.empty()) return;
        auto& ctx = getDefaultContext();
        Color col = ctx.getColor();

        using Point   = std::array<float, 2>;
        using Ring    = std::vector<Point>;
        using Polygon = std::vector<Ring>;

        // ---- Collect subpaths into working rings (fill is 2D; z dropped).
        // Consecutive duplicates and the closing vertex (== start) are
        // removed so zero-length edges can't confuse the geometry below.
        std::vector<Ring> rings;
        rings.reserve(subpathStarts_.size());
        for (size_t si = 0; si < subpathStarts_.size(); ++si) {
            auto [s, e] = getSubpathRange(si);
            if (e - s < 3) continue;
            Ring r;
            r.reserve(e - s);
            for (size_t k = s; k < e; ++k) {
                const Point p{vertices_[k].x, vertices_[k].y};
                if (!r.empty() && r.back() == p) continue;
                r.push_back(p);
            }
            while (r.size() >= 2 && r.front() == r.back()) r.pop_back();
            if (r.size() >= 3) rings.push_back(std::move(r));
        }
        if (rings.empty()) return;

        // ---- Split self-intersecting rings into simple rings. Each proper
        // crossing X of two edges splits the ring into two rings touching at
        // X; every split strictly reduces the remaining crossing count, so
        // this terminates (guard cap for float pathologies). Piece edges are
        // subsets of the original edges, so no new crossings appear.
        {
            auto cleanRing = [](Ring& r) {
                Ring out;
                out.reserve(r.size());
                for (const Point& p : r) {
                    if (out.empty() || out.back() != p) out.push_back(p);
                }
                while (out.size() >= 2 && out.front() == out.back()) out.pop_back();
                r = std::move(out);
            };

            int guard = 1024;
            for (size_t ri = 0; ri < rings.size() && guard > 0; ) {
                const size_t n = rings[ri].size();
                bool didSplit = false;
                for (size_t i = 0; i < n && !didSplit; ++i) {
                    const size_t i2 = (i + 1) % n;
                    for (size_t j = i + 1; j < n && !didSplit; ++j) {
                        const size_t j2 = (j + 1) % n;
                        if (i2 == j || j2 == i) continue;  // adjacent edges share a vertex
                        const Point& A = rings[ri][i];
                        const Point& B = rings[ri][i2];
                        const Point& C = rings[ri][j];
                        const Point& D = rings[ri][j2];
                        // bbox reject
                        if (std::max(A[0], B[0]) < std::min(C[0], D[0]) ||
                            std::max(C[0], D[0]) < std::min(A[0], B[0]) ||
                            std::max(A[1], B[1]) < std::min(C[1], D[1]) ||
                            std::max(C[1], D[1]) < std::min(A[1], B[1])) continue;
                        // strict proper crossing (touching endpoints excluded —
                        // touching rings are handled fine by the parity vote)
                        const double abx = (double)B[0] - A[0], aby = (double)B[1] - A[1];
                        const double cdx = (double)D[0] - C[0], cdy = (double)D[1] - C[1];
                        const double denom = abx * cdy - aby * cdx;
                        if (denom == 0.0) continue;
                        const double acx = (double)C[0] - A[0], acy = (double)C[1] - A[1];
                        const double t = (acx * cdy - acy * cdx) / denom;  // along AB
                        const double u = (acx * aby - acy * abx) / denom;  // along CD
                        if (t <= 0.0 || t >= 1.0 || u <= 0.0 || u >= 1.0) continue;

                        const Point X{(float)(A[0] + t * abx), (float)(A[1] + t * aby)};
                        Ring r1, r2;
                        r1.push_back(X);
                        for (size_t k = i2; ; k = (k + 1) % n) {
                            r1.push_back(rings[ri][k]);
                            if (k == j) break;
                        }
                        r2.push_back(X);
                        for (size_t k = j2; ; k = (k + 1) % n) {
                            r2.push_back(rings[ri][k]);
                            if (k == i) break;
                        }
                        cleanRing(r1);
                        cleanRing(r2);
                        // Replace the ring with the valid pieces (a piece that
                        // collapsed below 3 verts was a zero-area sliver).
                        if (r1.size() >= 3 && r2.size() >= 3) {
                            rings[ri] = std::move(r1);
                            rings.push_back(std::move(r2));
                        } else if (r1.size() >= 3) {
                            rings[ri] = std::move(r1);
                        } else if (r2.size() >= 3) {
                            rings[ri] = std::move(r2);
                        } else {
                            rings.erase(rings.begin() + (ptrdiff_t)ri);
                        }
                        didSplit = true;
                        --guard;
                    }
                }
                if (!didSplit) ++ri;  // ring is simple; re-scan same index after a split
            }
        }
        if (rings.empty()) return;

        const size_t N = rings.size();
        struct RingInfo {
            float minX, minY, maxX, maxY;
            float area;       // signed shoelace area; sign = winding direction
            int   winding = 0;
            int   parent  = -1;
        };
        std::vector<RingInfo> info(N);
        for (size_t i = 0; i < N; ++i) {
            const Ring& r = rings[i];
            float mnX = r[0][0], mxX = mnX;
            float mnY = r[0][1], mxY = mnY;
            float area = 0.f;
            for (size_t k = 0, j = r.size() - 1; k < r.size(); j = k++) {
                mnX = std::min(mnX, r[k][0]);
                mxX = std::max(mxX, r[k][0]);
                mnY = std::min(mnY, r[k][1]);
                mxY = std::max(mxY, r[k][1]);
                area += r[j][0] * r[k][1] - r[k][0] * r[j][1];
            }
            info[i].minX = mnX; info[i].maxX = mxX;
            info[i].minY = mnY; info[i].maxY = mxY;
            info[i].area = area * 0.5f;
        }

        auto ringSign = [&](size_t i) { return info[i].area >= 0.f ? 1 : -1; };

        auto pointInRing = [&](float px, float py, size_t j) -> bool {
            const RingInfo& ji = info[j];
            if (px < ji.minX || px > ji.maxX || py < ji.minY || py > ji.maxY) return false;
            const Ring& r = rings[j];
            bool inside = false;
            for (size_t k = 0, j2 = r.size() - 1; k < r.size(); j2 = k++) {
                const Point& a = r[k];
                const Point& b = r[j2];
                const bool cross = ((a[1] > py) != (b[1] > py)) &&
                                   (px < (b[0] - a[0]) * (py - a[1]) / (b[1] - a[1]) + a[0]);
                if (cross) inside = !inside;
            }
            return inside;
        };

        // Ring-in-ring containment by majority vote over 3 spread-out
        // vertices — robust to a single vertex touching the candidate's
        // boundary (bridged contours, split points). Holes never cross
        // their enclosing rings once self-intersections are resolved, so
        // parity is well-defined where the answer matters.
        auto containedIn = [&](size_t i, size_t j) -> bool {
            const size_t n = rings[i].size();
            const size_t sample[3] = {0, n / 3, (2 * n) / 3};
            int votes = 0;
            for (int t = 0; t < 3; ++t) {
                const Point& p = rings[i][sample[t]];
                if (pointInRing(p[0], p[1], j)) ++votes;
            }
            return votes >= 2;
        };

        // Containment matrix + winding number per ring.
        std::vector<uint8_t> cont(N * N, 0);
        for (size_t i = 0; i < N; ++i) {
            int w = ringSign(i);
            for (size_t j = 0; j < N; ++j) {
                if (j == i) continue;
                if (containedIn(i, j)) {
                    cont[i * N + j] = 1;
                    w += ringSign(j);
                }
            }
            info[i].winding = w;
        }

        // Attach each hole (winding 0) to the smallest enclosing filled ring.
        for (size_t i = 0; i < N; ++i) {
            if (info[i].winding != 0) continue;
            float bestBboxArea = std::numeric_limits<float>::infinity();
            for (size_t j = 0; j < N; ++j) {
                if (!cont[i * N + j] || info[j].winding == 0) continue;
                const float a = (info[j].maxX - info[j].minX) *
                                (info[j].maxY - info[j].minY);
                if (a < bestBboxArea) { bestBboxArea = a; info[i].parent = (int)j; }
            }
        }

        sgl_begin_triangles();
        sgl_c4f(col.r, col.g, col.b, col.a);
        for (size_t i = 0; i < N; ++i) {
            // Draw filled rings and orphan holes; holes that found a parent
            // are added to that parent's polygon below.
            if (info[i].winding == 0 && info[i].parent >= 0) continue;

            Polygon poly;
            poly.push_back(rings[i]);

            // Attach direct-child holes.
            for (size_t j = 0; j < N; ++j) {
                if (info[j].parent != (int)i) continue;
                poly.push_back(rings[j]);
            }

            // Tessellate. Earcut returns indices into the flat (outer + holes)
            // vertex list in `poly` traversal order.
            std::vector<uint32_t> tris = mapbox::earcut<uint32_t>(poly);
            std::vector<Point> flat;
            flat.reserve(tris.size());  // upper bound
            for (const Ring& r : poly) for (const Point& p : r) flat.push_back(p);

            for (size_t t = 0; t + 2 < tris.size(); t += 3) {
                const Point& a = flat[tris[t]];
                const Point& b = flat[tris[t + 1]];
                const Point& c = flat[tris[t + 2]];
                sgl_v2f(a[0], a[1]);
                sgl_v2f(b[0], b[1]);
                sgl_v2f(c[0], c[1]);
            }
        }
        sgl_end();
    }

    // Draw the path as a thick stroke (StrokeMesh, respects strokeWeight /
    // strokeCap / strokeJoin). Use draw() for 1-pixel line rendering.
    void drawStroke() const {
        if (vertices_.empty()) return;
        for (size_t si = 0; si < subpathStarts_.size(); ++si) {
            auto [s, e] = getSubpathRange(si);
            if (e - s < 2) continue;
            beginStroke();
            for (size_t i = s; i < e; ++i) vertex(vertices_[i]);
            endStroke(subpathClosed_[si]);
        }
    }

    // Get bounding box as Rect
    Rect getBounds() const {
        if (vertices_.empty()) {
            return Rect{0, 0, 0, 0};
        }
        float minX = vertices_[0].x, maxX = vertices_[0].x;
        float minY = vertices_[0].y, maxY = vertices_[0].y;
        for (const auto& v : vertices_) {
            if (v.x < minX) minX = v.x;
            if (v.x > maxX) maxX = v.x;
            if (v.y < minY) minY = v.y;
            if (v.y > maxY) maxY = v.y;
        }
        return Rect{minX, minY, maxX - minX, maxY - minY};
    }

    // Calculate length (sum over all subpaths).
    float getPerimeter() const {
        if (vertices_.size() < 2) return 0;
        float len = 0;
        for (size_t si = 0; si < subpathStarts_.size(); ++si) {
            auto [s, e] = getSubpathRange(si);
            if (e - s < 2) continue;
            for (size_t i = s + 1; i < e; ++i) {
                float dx = vertices_[i].x - vertices_[i-1].x;
                float dy = vertices_[i].y - vertices_[i-1].y;
                float dz = vertices_[i].z - vertices_[i-1].z;
                len += sqrt(dx*dx + dy*dy + dz*dz);
            }
            if (subpathClosed_[si] && e - s > 2) {
                float dx = vertices_[s].x   - vertices_[e-1].x;
                float dy = vertices_[s].y   - vertices_[e-1].y;
                float dz = vertices_[s].z   - vertices_[e-1].z;
                len += sqrt(dx*dx + dy*dy + dz*dz);
            }
        }
        return len;
    }

private:
    std::vector<Vec3>   vertices_;
    std::deque<Vec3>    curveVertices_;       // Buffer for curveTo
    std::vector<size_t> subpathStarts_;       // Always non-empty; [0] = 0
    std::vector<bool>   subpathClosed_;       // Same size as subpathStarts_

    // Catmull-Rom spline interpolation
    static Vec3 catmullRom(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, float t) {
        float t2 = t * t;
        float t3 = t2 * t;

        // Catmull-Rom matrix
        Vec3 result;
        result.x = 0.5f * ((2 * p1.x) +
                          (-p0.x + p2.x) * t +
                          (2 * p0.x - 5 * p1.x + 4 * p2.x - p3.x) * t2 +
                          (-p0.x + 3 * p1.x - 3 * p2.x + p3.x) * t3);
        result.y = 0.5f * ((2 * p1.y) +
                          (-p0.y + p2.y) * t +
                          (2 * p0.y - 5 * p1.y + 4 * p2.y - p3.y) * t2 +
                          (-p0.y + 3 * p1.y - 3 * p2.y + p3.y) * t3);
        result.z = 0.5f * ((2 * p1.z) +
                          (-p0.z + p2.z) * t +
                          (2 * p0.z - 5 * p1.z + 4 * p2.z - p3.z) * t2 +
                          (-p0.z + 3 * p1.z - 3 * p2.z + p3.z) * t3);
        return result;
    }
};

} // namespace trussc
