// =============================================================================
// tcxBox2dCircle.h - Box2D Circle Body
// =============================================================================

#pragma once

#include "tcxBox2dBody.h"

namespace tcx::box2d {

// =============================================================================
// Circle Body
// =============================================================================
class CircleBody : public Body {
public:
    CircleBody() = default;
    ~CircleBody() override = default;

    // Movable
    CircleBody(CircleBody&& other) noexcept;
    CircleBody& operator=(CircleBody&& other) noexcept;

    // -------------------------------------------------------------------------
    // Creation
    // -------------------------------------------------------------------------

    // Create circle in world
    // x, y: center coordinates (pixels)
    // radius: radius (pixels)
    void setup(World& world, float x, float y, float radius);

    // -------------------------------------------------------------------------
    // Properties
    // -------------------------------------------------------------------------
    float getRadius() const { return radius_; }

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
    float radius_ = 0;
};

} // namespace tcx::box2d
