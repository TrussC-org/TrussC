#pragma once

// =============================================================================
// tcRectangle.h - 矩形構造体
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

    // 右端・下端
    float getRight() const { return x + width; }
    float getBottom() const { return y + height; }

    // 中心
    float getCenterX() const { return x + width / 2; }
    float getCenterY() const { return y + height / 2; }

    // 点が矩形内にあるか
    bool contains(float px, float py) const {
        return px >= x && px < x + width && py >= y && py < y + height;
    }

    // 他の矩形と交差しているか
    bool intersects(const Rect& other) const {
        return !(x >= other.getRight() || getRight() <= other.x ||
                 y >= other.getBottom() || getBottom() <= other.y);
    }
};

} // namespace trussc
