// =============================================================================
// PointCurveNode - tone-curve style editor: drag control points
//
// Self-contained: copy this single file into your project to reuse it.
//
// Interpolation: cubic Hermite with finite-difference slopes (non-uniform x).
// Interior tangents use the central difference, endpoint tangents use the
// one-sided slope of the adjacent segment — NO damping, no forced flatness:
//   - collinear points produce an exactly straight curve (no wobble)
//   - a steep last segment really does slam into the end at full speed
// This C1 tangent policy is deliberately simple and yours to change when you
// adapt this editor (e.g. monotone/PCHIP slopes to forbid overshoot between
// points, or scaled tangents for snappier motion).
// =============================================================================
#pragma once

#include <TrussC.h>
#include <vector>

class PointCurveNode : public tc::RectNode {
public:
    static constexpr float VAL_MIN = -0.25f, VAL_MAX = 1.25f;

    // Normalized curve space: x = t (0..1), y = value (may exceed 0..1)
    std::vector<tc::Vec2> points = {
        {0.0f, 0.0f}, {0.25f, 0.25f}, {0.5f, 0.5f}, {0.75f, 0.75f}, {1.0f, 1.0f}
    };

    void setup() override {
        enableEvents();
    }

    // dy/dx slope used as the Hermite tangent at point i
    float slopeAt(int i) const {
        int n = (int)points.size();
        if (i <= 0)     return segSlope(0);
        if (i >= n - 1) return segSlope(n - 2);
        return (points[i + 1].y - points[i - 1].y) /
               (points[i + 1].x - points[i - 1].x);
    }

    float eval(float t) const {
        using namespace tc;
        t = clamp(t, 0.0f, 1.0f);
        int n = (int)points.size();
        int seg = 0;
        while (seg < n - 2 && t > points[seg + 1].x) seg++;
        float x0 = points[seg].x, x1 = points[seg + 1].x;
        float h = x1 - x0;
        if (h <= 0.0f) return points[seg].y;
        float u = (t - x0) / h;
        float y0 = points[seg].y, y1 = points[seg + 1].y;
        float m0 = slopeAt(seg) * h, m1 = slopeAt(seg + 1) * h;
        float u2 = u * u, u3 = u2 * u;
        return (2*u3 - 3*u2 + 1) * y0 + (u3 - 2*u2 + u) * m0
             + (-2*u3 + 3*u2)    * y1 + (u3 - u2)       * m1;
    }

    bool onMousePress(tc::Vec2 local, int button) override {
        float w = getWidth(), h = getHeight();
        dragIndex_ = -1;
        for (int i = 0; i < (int)points.size(); i++) {
            tc::Vec2 screen(points[i].x * w,
                            (VAL_MAX - points[i].y) / (VAL_MAX - VAL_MIN) * h);
            if (local.distance(screen) < 14.0f) { dragIndex_ = i; break; }
        }
        return dragIndex_ >= 0;
    }

    bool onMouseDrag(tc::Vec2 local, int button) override {
        using namespace tc;
        if (dragIndex_ < 0) return false;
        float w = getWidth(), h = getHeight();
        int n = (int)points.size();
        float x = clamp(local.x / w, 0.0f, 1.0f);
        float y = clamp(VAL_MAX - local.y / h * (VAL_MAX - VAL_MIN),
                        VAL_MIN, VAL_MAX);  // overshoot ok
        // Endpoints keep x fixed at 0 / 1; middle points stay between neighbors
        if (dragIndex_ == 0)          x = 0.0f;
        else if (dragIndex_ == n - 1) x = 1.0f;
        else x = clamp(x, points[dragIndex_ - 1].x + 0.02f,
                          points[dragIndex_ + 1].x - 0.02f);
        points[dragIndex_] = Vec2(x, y);
        return true;
    }

    bool onMouseRelease(tc::Vec2 local, int button) override {
        dragIndex_ = -1;
        return false;
    }

    void draw() override {
        using namespace tc;
        float w = getWidth(), h = getHeight();
        auto vy = [&](float v) { return (VAL_MAX - v) / (VAL_MAX - VAL_MIN) * h; };

        setColor(0.1f, 0.1f, 0.13f);
        drawRect(0, 0, w, h);

        // Grid (quarters of the 0..1 unit box) + diagonal reference
        setColor(0.22f, 0.22f, 0.27f);
        for (int i = 1; i < 4; i++) {
            drawLine(w * i / 4, 0, w * i / 4, h);
            drawLine(0, vy(i / 4.0f), w, vy(i / 4.0f));
        }
        drawLine(0, vy(0), w, vy(1));

        // Unit-box bounds (v=0 / v=1)
        setColor(0.35f, 0.35f, 0.42f);
        drawLine(0, vy(0), w, vy(0));
        drawLine(0, vy(1), w, vy(1));

        // The curve
        setColor(0.3f, 0.85f, 0.55f);
        noFill();
        beginShape();
        for (int i = 0; i <= 128; i++) {
            float t = i / 128.0f;
            vertex(t * w, vy(eval(t)));
        }
        endShape();

        // Control points
        setColor(1.0f, 0.85f, 0.3f);
        fill();
        for (auto& p : points) drawCircle(p.x * w, vy(p.y), 6);

        // Border
        setColor(0.45f);
        noFill();
        drawRect(0, 0, w, h);
        fill();
    }

private:
    int dragIndex_ = -1;

    float segSlope(int seg) const {
        float dx = points[seg + 1].x - points[seg].x;
        return (dx > 0.0f) ? (points[seg + 1].y - points[seg].y) / dx : 0.0f;
    }
};
