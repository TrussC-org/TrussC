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
// Represents convex polygons (Box2D limitation: convex only, max 8 vertices)
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

    // Create polygon from vertex list
    // vertices: vertex coordinates (local coordinates, center-based)
    // x, y: center coordinates (world coordinates, pixels)
    void setup(World& world, const std::vector<tc::Vec2>& vertices, float x, float y);

    // Create polygon from Polyline
    void setup(World& world, const tc::Path& polyline, float x, float y);

    // Create regular polygon
    // sides: number of sides (3-8)
    // radius: circumscribed circle radius
    void setupRegular(World& world, float x, float y, float radius, int sides);

    // -------------------------------------------------------------------------
    // Properties
    // -------------------------------------------------------------------------
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
    std::vector<tc::Vec2> vertices_;
};

} // namespace tcx::box2d
