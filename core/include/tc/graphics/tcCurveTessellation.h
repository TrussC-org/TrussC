#pragma once

// =============================================================================
// tcCurveTessellation.h
//
// Adaptive tessellation for circles, arcs, and Bezier curves.
//
// Two modes:
//   - Tolerance: pick the segment count so the chord-to-curve sagitta is
//                <= tolerance pixels in screen space. Default (0.1 px).
//   - Resolution: fixed segment count, ignoring radius. Opt-in for users
//                who want to bound vertex cost or get a chunky look.
//
// Most users only touch the public API (setCurveTolerance / setCurveResolution
// in tcRenderContext.h). The functions here are the math primitives those
// public functions and individual draw* / Path methods are built on.
// =============================================================================

#include "tcMath.h"   // TAU, HALF_TAU
#include "../utils/tcLog.h"
#include "../utils/tcAnnotations.h"
#include <algorithm>
#include <cmath>

namespace trussc {

struct CurveStyle {
    enum class Mode { Tolerance, Resolution };
    Mode  mode       = Mode::Tolerance;
    float tolerance  = 0.1f;   // pixels in screen space
    int   resolution = 60;     // fallback / fixed-mode value
};

// Lower / upper guards for segment counts. The lower keeps polygons looking
// like polygons even at tiny radii; the upper prevents pathological vertex
// floods from bad input (radius=1e9, tolerance=1e-6, ...).
inline constexpr int kMinCircleSegments = 6;
inline constexpr int kMaxCircleSegments = 1024;

// Number of edges to approximate a full circle of `radius` so that the
// chord-to-arc sagitta stays under `tolerance`. Derivation:
//
//   Sagitta s of an arc spanning angle theta is r*(1 - cos(theta/2)).
//   For N segments, theta = TAU/N. Setting s <= tolerance:
//
//     1 - cos(theta/2) <= tolerance/r
//     theta/2          >= acos(1 - tolerance/r)
//     N                >= HALF_TAU / acos(1 - tolerance/r)
//
// Both inputs are pre-divided by the current scale by the caller.
namespace internal {

inline int segmentsForCircle(float radius, float tolerance) {
    if (radius <= 0.0f) return 0;
    if (tolerance <= 0.0f) return kMaxCircleSegments;
    if (tolerance >= radius) return kMinCircleSegments;
    float n = std::ceil(HALF_TAU / std::acos(1.0f - tolerance / radius));
    int   ni = (int)n;
    if (ni < kMinCircleSegments) ni = kMinCircleSegments;
    if (ni > kMaxCircleSegments) ni = kMaxCircleSegments;
    return ni;
}

// Edges to span an arc of `angleSpan` radians at the given tolerance.
// At least 2 segments so a non-zero arc always has interior points.
inline int segmentsForArc(float radius, float angleSpan, float tolerance) {
    if (radius <= 0.0f || angleSpan == 0.0f) return 0;
    int full = segmentsForCircle(radius, tolerance);
    int n = (int)std::ceil(full * std::abs(angleSpan) / TAU);
    return std::max(2, n);
}

} // namespace internal

// =============================================================================
// Bezier subdivision (de Casteljau) — adaptive flatness-driven recursion.
//
// Each curve type (cubic, quadratic, n-th order) is split until each piece
// is "flat enough" — the maximum perpendicular distance from any control
// point to the line connecting the endpoints is <= tolerance. The piece is
// then output as a straight line.
//
// Recursion depth is hard-capped at kBezierMaxDepth so a pathological
// curve (cusp, near-coincident control points) can't run away.
// =============================================================================

inline constexpr int kBezierMaxDepth = 16;

// Hard cap on the order (point count) accepted by the N-th order Bezier
// tessellator. Each split inside subdivideBezierN does an O(N^2) in-place
// de Casteljau reduction (level k touches N-k pairs of points, summing
// to N*(N-1)/2 lerps per node). Multiplied by the binary recursion tree
// — capped at depth 16, so up to ~2^16 leaves on pathological control
// polygons — total work grows as O(N^2 * 2^depth). Empirically a closed
// loop at N=2000 takes ~80 seconds on a single core, frozen-tab territory.
//
// In practice N>=5 Bezier curves are vanishingly rare: SVG, fonts, and
// every desktop vector tool chain cubic segments together rather than
// raise the order. N=64 is well above any legitimate use we've seen and
// keeps the worst case in the low-hundreds-of-ms range.
//
// If you genuinely need higher-order Bezier (N>10 or so), the bundled
// implementation is intentionally naive — at that point you want a
// purpose-built tessellator with cached factorials / vectorised inner
// loops / GPU dispatch. Roll your own and call the underlying primitives
// from there, or chain cubics.
inline constexpr int kBezierMaxOrder = 64;

namespace internal {

// Squared perpendicular distance from `p` to the line through `a` and `b`
// (works in 3D — the cross product gives the parallelogram area; divided
// by base length squared gives perpendicular distance squared).
inline float pointLineDistSq(const Vec3& p, const Vec3& a, const Vec3& b) {
    Vec3 ab = b - a;
    float len2 = ab.x * ab.x + ab.y * ab.y + ab.z * ab.z;
    if (len2 <= 1e-12f) {
        // Degenerate base: a == b. Distance is just |p - a|^2.
        Vec3 d = p - a;
        return d.x * d.x + d.y * d.y + d.z * d.z;
    }
    Vec3 ap = p - a;
    // |ap × ab|^2 / |ab|^2
    float cx = ap.y * ab.z - ap.z * ab.y;
    float cy = ap.z * ab.x - ap.x * ab.z;
    float cz = ap.x * ab.y - ap.y * ab.x;
    return (cx * cx + cy * cy + cz * cz) / len2;
}

inline Vec3 lerp3(const Vec3& a, const Vec3& b, float t) {
    return Vec3{a.x + (b.x - a.x) * t,
                a.y + (b.y - a.y) * t,
                a.z + (b.z - a.z) * t};
}

// Cubic recursive subdivision. Appends interior + endpoint to `out`; the
// caller is responsible for pushing the very first point (p0) before
// kicking off the recursion at the top level.
template <class Out>
void subdivideCubic(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3,
                    float tolSq, int depth, Out& out) {
    // Flatness test: max perpendicular distance of control points to the
    // chord p0--p3. Squared comparison saves a sqrt in the hot loop.
    float d1 = pointLineDistSq(p1, p0, p3);
    float d2 = pointLineDistSq(p2, p0, p3);
    if (depth >= kBezierMaxDepth || (d1 <= tolSq && d2 <= tolSq)) {
        out.push_back(p3);
        return;
    }
    Vec3 p01   = lerp3(p0,  p1,  0.5f);
    Vec3 p12   = lerp3(p1,  p2,  0.5f);
    Vec3 p23   = lerp3(p2,  p3,  0.5f);
    Vec3 p012  = lerp3(p01, p12, 0.5f);
    Vec3 p123  = lerp3(p12, p23, 0.5f);
    Vec3 p0123 = lerp3(p012, p123, 0.5f);
    subdivideCubic(p0,    p01,  p012, p0123, tolSq, depth + 1, out);
    subdivideCubic(p0123, p123, p23,  p3,    tolSq, depth + 1, out);
}

template <class Out>
void subdivideQuad(const Vec3& p0, const Vec3& p1, const Vec3& p2,
                   float tolSq, int depth, Out& out) {
    float d1 = pointLineDistSq(p1, p0, p2);
    if (depth >= kBezierMaxDepth || d1 <= tolSq) {
        out.push_back(p2);
        return;
    }
    Vec3 p01  = lerp3(p0, p1, 0.5f);
    Vec3 p12  = lerp3(p1, p2, 0.5f);
    Vec3 p012 = lerp3(p01, p12, 0.5f);
    subdivideQuad(p0,   p01,  p012, tolSq, depth + 1, out);
    subdivideQuad(p012, p12,  p2,   tolSq, depth + 1, out);
}

// N-th order Bezier via in-place de Casteljau. `pts` is mutated as scratch.
template <class Out>
void subdivideBezierN(std::vector<Vec3> pts, float tolSq, int depth, Out& out) {
    int n = (int)pts.size();
    if (n < 2) return;

    // Flatness: max perpendicular distance of any interior control point
    // to the chord pts[0]--pts[n-1].
    float maxD = 0.0f;
    for (int i = 1; i < n - 1; i++) {
        float d = pointLineDistSq(pts[i], pts.front(), pts.back());
        if (d > maxD) maxD = d;
    }
    if (depth >= kBezierMaxDepth || maxD <= tolSq) {
        out.push_back(pts.back());
        return;
    }
    // Split at t = 0.5 via de Casteljau: build the left/right control
    // polygons in two extra buffers.
    std::vector<Vec3> left, right;
    left.reserve(n);
    right.reserve(n);
    left.push_back(pts.front());
    right.push_back(pts.back());
    for (int level = 1; level < n; level++) {
        for (int i = 0; i < n - level; i++) {
            pts[i] = lerp3(pts[i], pts[i + 1], 0.5f);
        }
        left.push_back(pts.front());
        right.push_back(pts[n - level - 1]);
    }
    // `right` was built end-first; reverse to get the right half's
    // control polygon in the conventional order.
    std::reverse(right.begin(), right.end());
    subdivideBezierN(std::move(left),  tolSq, depth + 1, out);
    subdivideBezierN(std::move(right), tolSq, depth + 1, out);
}

} // namespace internal

// Tessellate a cubic Bezier into a polyline. The caller passes a writer
// that supports `push_back(Vec3)` (e.g. a std::vector<Vec3>). Includes
// both endpoints; emits at least two points even for a degenerate curve.
template <class Out>
TC_INTERNAL void tessellateCubicBezier(const Vec3& p0, const Vec3& p1,
                           const Vec3& p2, const Vec3& p3,
                           float tolerance, Out& out) {
    out.push_back(p0);
    if (tolerance <= 0.0f) tolerance = 0.1f;
    internal::subdivideCubic(p0, p1, p2, p3, tolerance * tolerance, 0, out);
}

template <class Out>
TC_INTERNAL void tessellateQuadBezier(const Vec3& p0, const Vec3& p1, const Vec3& p2,
                          float tolerance, Out& out) {
    out.push_back(p0);
    if (tolerance <= 0.0f) tolerance = 0.1f;
    internal::subdivideQuad(p0, p1, p2, tolerance * tolerance, 0, out);
}

// N-th order. `pts.size()` is the order + 1 (so 4 points = cubic). For
// n == 3 or n == 4 the dedicated quad/cubic versions above are faster
// and produce identical results — prefer them when the order is fixed.
template <class Out>
TC_INTERNAL void tessellateBezierN(const std::vector<Vec3>& pts, float tolerance, Out& out) {
    if (pts.empty()) return;
    out.push_back(pts.front());
    if (pts.size() < 2) return;
    if ((int)pts.size() > kBezierMaxOrder) {
        // Refuse rather than freeze. See kBezierMaxOrder for the rationale.
        logWarning() << "tessellateBezierN: refusing N=" << pts.size()
                     << " control points (cap " << kBezierMaxOrder
                     << "). Chain cubics for legitimate curves, or write a"
                     << " purpose-built high-order tessellator.";
        return;
    }
    if (tolerance <= 0.0f) tolerance = 0.1f;
    internal::subdivideBezierN(pts, tolerance * tolerance, 0, out);
}

} // namespace trussc
