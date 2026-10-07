// =============================================================================
// tcxBox2dPolygon.h - Box2D Polygon Body
// =============================================================================

#pragma once

#include "tcxBox2dBody.h"
#include <vector>

namespace tcx::box2d {

// =============================================================================
// Polygon Body
// =============================================================================
// A polygon body. A Box2D polygon is one convex shape of 3 to 8 points.
//   - setup():           one polygon, 3 to 8 points. Concave input becomes
//                        its convex hull, as Box2D does (the first time in
//                        the process, a warning names the points dropped);
//                        draw(), drawFill() and getVertices() show that hull,
//                        so what you see is what collides.
//   - setupSimplified(): any number of points, simplified to one convex hull
//                        of at most 8 points.
//   - setupCompound():   any outline, kept exactly (concave parts, holes), as
//                        one fixture per triangle on one body.
// Input that can't make a polygon (fewer than 3 or more than 8 points for
// setup(), collinear points, points that nearly coincide, a polygon tiny next
// to its distance from the local origin) logs a warning and creates no body,
// in Debug and Release alike. Check isCreated() afterwards.
// =============================================================================
class PolyShape : public Body {
public:
    PolyShape() = default;
    ~PolyShape() override = default;

    // Movable
    PolyShape(PolyShape&& other) noexcept;
    PolyShape& operator=(PolyShape&& other) noexcept;

    // -------------------------------------------------------------------------
    // Creation
    // -------------------------------------------------------------------------

    // Create a convex polygon from 3 to 8 points
    // vertices: vertex coordinates (local coordinates, center-based)
    // x, y: center coordinates (world coordinates, pixels)
    // Concave input becomes its convex hull (getVertices() returns the hull).
    // When the hull leaves input points inside it (concave input, or interior
    // points), the first such call in the process logs one warning with the
    // counts, pointing to setupCompound(). Points on the outline (collinear,
    // duplicates) and hull points in a crossing order don't count.
    // More than 8 points, or degenerate points (collinear, nearly coincident,
    // tiny next to their distance from the local origin), log a warning and
    // create no body: check isCreated(). For more points use setupSimplified()
    // (convex approximation) or setupCompound() (exact shape).
    void setup(World& world, const std::vector<tc::Vec2>& vertices, float x, float y);

    // Same as above with every point of the path (all subpaths together), so
    // the path must have 3 to 8 points in total. Concave paths become their
    // convex hull.
    void setup(World& world, const tc::Path& polyline, float x, float y);

    // Create a convex polygon from any number of points (3 or more): the convex
    // hull of the points, reduced to at most 8 points by dropping the vertices
    // that lose the least area. The shape is an approximation (concave parts
    // and holes are filled, tips and extents can shrink); getVertices()
    // returns the points actually used.
    // Degenerate input (collinear or coincident points) logs a warning and
    // creates no body: check isCreated().
    void setupSimplified(World& world, const std::vector<tc::Vec2>& points, float x, float y);

    // Same as above with every point of the path (all subpaths together).
    void setupSimplified(World& world, const tc::Path& path, float x, float y);

    // Create a body with the exact shape of any outline: concave, with holes,
    // any number of points. Path::buildFillTriangles() triangulates it (the
    // fill drawFill() shows: non-zero winding, a subpath wound opposite to its
    // enclosing one is a hole, self-intersections are split), and each
    // triangle becomes one fixture on the one body. Mass and centroid are the
    // sum over the fixtures. A path that is one convex ring of at most 8
    // points becomes one ordinary polygon fixture instead. Slivers Box2D can't
    // use (collinear or nearly coincident corners, almost no area) are
    // skipped with one warning; if nothing is left, or the whole body is tiny
    // next to its distance from the local origin, a warning and no body
    // (check isCreated()). Collision events come once per touching body pair,
    // however many fixtures touch.
    // Area the fill covers more than once (a self-overlapping outline such as
    // a pentagram's center, overlapping subpaths wound the same way, a hole
    // wound like its outer ring) gets one layer of triangles per cover, so it
    // weighs once per layer: give a simple outline and wind holes opposite.
    // getVertices() returns the outline points (every subpath, in order);
    // draw() outlines each subpath and drawFill() fills like Path::drawFill()
    // (triangulated once, here, not every frame).
    void setupCompound(World& world, const tc::Path& path, float x, float y);

    // Same as above with the points as one closed outline.
    void setupCompound(World& world, const std::vector<tc::Vec2>& points, float x, float y);

    // Create regular polygon
    // sides: number of sides (3-8)
    // radius: circumscribed circle radius
    void setupRegular(World& world, float x, float y, float radius, int sides);

    // -------------------------------------------------------------------------
    // Properties
    // -------------------------------------------------------------------------
    // The polygon Box2D built (local pixels): the convex hull of the setup()
    // points, or the reduced hull of setupSimplified(). When every setup() point
    // is a hull vertex and they already go around the outline in order
    // (either winding, any start), they come back as given; otherwise (points
    // dropped, or listed in a crossing order) the order is Box2D's (from the
    // rightmost point). After setupCompound(), the outline points of every
    // subpath. Empty without a body.
    const std::vector<tc::Vec2>& getVertices() const { return vertices_; }
    int getNumVertices() const { return static_cast<int>(vertices_.size()); }

    // -------------------------------------------------------------------------
    // Drawing (override Node::draw())
    // Draws at origin (0,0). drawTree() applies position/rotation automatically
    // -------------------------------------------------------------------------
    void draw() override;

    // Draw with fill
    void drawFill();

    // Draw with color
    void draw(const tc::Color& color);

private:
    // Create the body with one fixture per checked shape. vertices_ and path_
    // are set by the caller.
    void createBody(World& world, const b2PolygonShape* shapes, size_t count,
                    float cx, float cy);

    std::vector<tc::Vec2> vertices_;
    tc::Path path_;          // setupCompound() outline, drawn instead of vertices_
    tc::Mesh fillMesh_;      // setupCompound() fill, triangulated once
    bool compound_ = false;  // made by setupCompound()
};

} // namespace tcx::box2d
