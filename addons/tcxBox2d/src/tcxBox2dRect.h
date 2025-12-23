// =============================================================================
// tcxBox2dRect.h - Box2D Rectangle Body
// =============================================================================

#pragma once

#include "tcxBox2dBody.h"

namespace tcx::box2d {

// =============================================================================
// Rectangle Body
// =============================================================================
class RectBody : public Body {
public:
    RectBody() = default;
    ~RectBody() override = default;

    // Movable
    RectBody(RectBody&& other) noexcept;
    RectBody& operator=(RectBody&& other) noexcept;

    // -------------------------------------------------------------------------
    // Creation
    // -------------------------------------------------------------------------

    // Create rectangle in world
    // x, y: center coordinates (pixels)
    // width, height: size (pixels)
    void setup(World& world, float x, float y, float width, float height);

    // -------------------------------------------------------------------------
    // Properties
    // -------------------------------------------------------------------------
    float getWidth() const { return width_; }
    float getHeight() const { return height_; }

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
    float width_ = 0;
    float height_ = 0;
};

} // namespace tcx::box2d
