// =============================================================================
// tcxBox2dPolygonCheck.h - Shared polygon point checks (internal)
// =============================================================================
// Used by PolyShape (classic API) and RigidBody2D / Shape2D (Mod API), so both
// accept and refuse exactly the same point lists.
// =============================================================================

#pragma once

#include <TrussC.h>
#include <box2d/box2d.h>
#include <string>
#include <vector>

namespace tcx::box2d::detail {

// Why a point list can't become one b2PolygonShape (None = it can).
enum class PolygonError {
    None,
    TooFewPoints,   // fewer than 3 points
    TooManyPoints,  // more than b2_maxPolygonVertices (8)
    MergedPoints,   // fewer than 3 left after Box2D merges near-coincident points
    Degenerate,     // collinear, or the hull encloses (almost) no area
    TooSmallForOffset,  // so small next to its distance from the body origin
                        // that a dynamic body's inertia rounds to <= 0
};

// Check `points` (local pixel coordinates) against everything
// b2PolygonShape::Set(), b2PolygonShape::ComputeMass() and a dynamic body's
// b2Body::ResetMassData() (inertia > 0) assert on, and build the shape.
// Box2D's own steps are mirrored (point merging at 0.5 * b2_linearSlop,
// gift-wrap hull, area > b2_epsilon); the inertia check keeps a margin so it
// holds at any density. A list that passes never reaches those asserts
// (Debug), the SetAsBox(1, 1) fallback or NaN inertia (Release).
// On success `shape` is set and `hull` receives the polygon Box2D built, in
// pixels. When Box2D kept every point and `points` already go around the hull
// in order (either winding, any starting point), that is `points` unchanged;
// otherwise (concave input, collinear middle points, merged points, or hull
// points in a crossing order) it is Box2D's hull in Box2D's order, starting at
// the rightmost point. On failure neither is touched.
PolygonError makePolygonShape(const std::vector<tc::Vec2>& points,
                              b2PolygonShape& shape,
                              std::vector<tc::Vec2>& hull);

// Short English reason for a warning ("" for None).
std::string describePolygonError(PolygonError err);

// Reason for fewer than 3 points left after reducedConvexHull(), which drops
// duplicate and collinear points; `caller` names the call that did it
// (e.g. "setupConvex()").
std::string describeCollapsedHull(const std::string& caller);

// Convex hull of any number of points (pixels), reduced to at most `maxPoints`
// by repeatedly dropping the vertex whose removal loses the least area (ties:
// the lowest index). No point is guaranteed to survive: tips and extents can
// shrink, and a symmetric outline can come back lopsided. Collinear and
// duplicate points are dropped, so fewer than 3 points come back for
// degenerate input.
std::vector<tc::Vec2> reducedConvexHull(const std::vector<tc::Vec2>& points,
                                        size_t maxPoints = b2_maxPolygonVertices);

// Every point of every subpath of `path` (z dropped).
std::vector<tc::Vec2> pathPoints(const tc::Path& path);

// If `path` holds exactly one ring of 3 or more points (the rings
// Path::buildFillTriangles() uses) and that ring is convex with at most
// b2_maxPolygonVertices points, put it in `ring` and return true. Consecutive
// duplicates and a closing point equal to the first are dropped; collinear
// points are allowed.
bool convexRing(const tc::Path& path, std::vector<tc::Vec2>& ring);

// The fixtures for an outline of any shape (setupCompound() / Shape2D::compound()).
struct CompoundShapes {
    std::vector<b2PolygonShape> shapes;  // one fixture each
    size_t triangles = 0;                // from the triangulation (0: one convex polygon)
    size_t skipped = 0;                  // of those, slivers makePolygonShape() refused
};

// A path that convexRing() accepts, and makePolygonShape() too, gives one
// polygon. Anything else is triangulated with Path::buildFillTriangles()
// (non-zero winding, holes, self-intersections split), and every triangle
// that passes makePolygonShape() becomes one shape; the others are counted in
// `skipped`. Returns false when no shape could be made.
bool makeCompoundShapes(const tc::Path& path, CompoundShapes& out);

// Outline every subpath of `path` as a closed loop (a compound body's outline;
// Path::draw() would also fan-fill each subpath, which is wrong for concave
// shapes).
void drawPathOutline(const tc::Path& path);

} // namespace tcx::box2d::detail
