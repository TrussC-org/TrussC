#pragma once

// =============================================================================
// tcRectangle.h - Rectangle structure
// =============================================================================

namespace trussc {

struct Rect {
    float x = 0;
    float y = 0;
    float width = 0;
    float height = 0;

    Rect() = default;
    Rect(float x_, float y_, float w_, float h_)
        : x(x_), y(y_), width(w_), height(h_) {}

    // Right/Bottom edges
    float getRight() const { return x + width; }
    float getBottom() const { return y + height; }

    // Center
    float getCenterX() const { return x + width / 2; }
    float getCenterY() const { return y + height / 2; }

    // Check if point is inside rectangle
    bool contains(float px, float py) const {
        return px >= x && px < x + width && py >= y && py < y + height;
    }

    // Check if intersects with other rectangle
    bool intersects(const Rect& other) const {
        return !(x >= other.getRight() || getRight() <= other.x ||
                 y >= other.getBottom() || getBottom() <= other.y);
    }
};

} // namespace trussc
