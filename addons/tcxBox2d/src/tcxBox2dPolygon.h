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
// One body made of one convex Box2D polygon (Box2D limitation: convex, 3 to 8
// points).
//   - setup():       3 to 8 points. Concave input silently becomes its convex
//                    hull, as Box2D does; draw(), drawFill() and getVertices()
//                    show that hull, so what you see is what collides.
//   - setupConvex(): any number of points, approximated by a convex hull of at
//                    most 8 points.
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
    // More than 8 points, or degenerate points (collinear, nearly coincident,
    // tiny next to their distance from the local origin), log a warning and
    // create no body: check isCreated(). For more points use setupConvex()
    // (convex approximation).
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
    void setupConvex(World& world, const std::vector<tc::Vec2>& points, float x, float y);

    // Same as above with every point of the path (all subpaths together).
    void setupConvex(World& world, const tc::Path& path, float x, float y);

    // Create regular polygon
    // sides: number of sides (3-8)
    // radius: circumscribed circle radius
    void setupRegular(World& world, float x, float y, float radius, int sides);

    // -------------------------------------------------------------------------
    // Properties
    // -------------------------------------------------------------------------
    // The polygon Box2D built (local pixels): the convex hull of the setup()
    // points, or the reduced hull of setupConvex(). When every setup() point
    // is a hull vertex (convex input), they come back as given, in their
    // order; otherwise the order is Box2D's (from the rightmost point).
    // Empty without a body.
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
    // Create the body with one fixture from a checked shape and its hull.
    void createBody(World& world, const b2PolygonShape& polygon,
                    const std::vector<tc::Vec2>& hull, float cx, float cy);

    std::vector<tc::Vec2> vertices_;
};

} // namespace tcx::box2d
