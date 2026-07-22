// =============================================================================
// FreehandCurveNode - draw an easing curve directly with the mouse (LUT)
//
// Self-contained: copy this single file into your project to reuse it.
// Easing is a 1:1 mapping of x -> value, so dragging over the same x region
// simply overwrites it. Values outside 0..1 (overshoot) are allowed; the
// whole VAL_MIN..VAL_MAX range maps onto the node height so every reachable
// value stays inside the node rect (mouse events only hit within the rect).
// =============================================================================
#pragma once

#include <TrussC.h>
#include <array>

class FreehandCurveNode : public tc::RectNode {
public:
    static constexpr float VAL_MIN = -0.25f, VAL_MAX = 1.25f;
    static constexpr int RES = 128;
    std::array<float, RES> lut;

    FreehandCurveNode() {
        for (int i = 0; i < RES; i++) lut[i] = (float)i / (RES - 1);  // start linear
    }

    void setup() override {
        enableEvents();
    }

    // Evaluate with linear interpolation between LUT entries
    float eval(float t) const {
        float f = tc::clamp(t, 0.0f, 1.0f) * (RES - 1);
        int i = (int)f;
        if (i >= RES - 1) return lut[RES - 1];
        float frac = f - i;
        return lut[i] * (1.0f - frac) + lut[i + 1] * frac;
    }

    bool onMousePress(tc::Vec2 local, int button) override {
        lastLocal_ = local;
        paint(local, local);
        return true;
    }

    bool onMouseDrag(tc::Vec2 local, int button) override {
        paint(lastLocal_, local);
        lastLocal_ = local;
        return true;
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
        for (int i = 0; i < RES; i++) {
            vertex((float)i / (RES - 1) * w, vy(lut[i]));
        }
        endShape();

        // Border
        setColor(0.45f);
        drawRect(0, 0, w, h);
        fill();
    }

private:
    tc::Vec2 lastLocal_;

    // Write a segment into the LUT, interpolating between the previous and
    // current mouse position so fast drags don't leave gaps.
    void paint(tc::Vec2 a, tc::Vec2 b) {
        using namespace tc;
        float w = getWidth(), h = getHeight();
        if (a.x > b.x) std::swap(a, b);
        int i0 = (int)clamp(a.x / w * (RES - 1), 0.0f, (float)(RES - 1));
        int i1 = (int)clamp(b.x / w * (RES - 1), 0.0f, (float)(RES - 1));
        float v0 = clamp(VAL_MAX - a.y / h * (VAL_MAX - VAL_MIN), VAL_MIN, VAL_MAX);
        float v1 = clamp(VAL_MAX - b.y / h * (VAL_MAX - VAL_MIN), VAL_MIN, VAL_MAX);
        for (int i = i0; i <= i1; i++) {
            float frac = (i1 > i0) ? (float)(i - i0) / (i1 - i0) : 0.0f;
            lut[i] = v0 * (1.0f - frac) + v1 * frac;
        }
    }
};
