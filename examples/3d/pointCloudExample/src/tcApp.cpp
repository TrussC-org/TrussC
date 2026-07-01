// Draw a point cloud as a GPU-resident Mesh (PrimitiveMode::Points). All three
// PointStyles are GPU-resident — they differ only in SHAPE:
//   Pixel  - a true 1px GPU point primitive (ignores point size)
//   Square - a billboarded quad splat, sized by setPointSize
//   Round  - an anti-aliased disc splat
//
//   G       cycle shape (Square / Round / Pixel)
//   1..6    point count (50k / 100k / 250k / 500k / 1M / 2M)
//   [ ]     point size in logical px (Square/Round only)
//   drag    orbit

#include "tcApp.h"

void tcApp::setup() {
    setWindowTitle("pointCloudExample");

    // set up view
    view.setTarget(0.0f, 0.0f, 0.0f);
    view.setDistance(360.0f);
    view.enableMouseInput();

    // make point cloud with initial number of points
    rebuild(n_);
}

// Build a static point cloud of n points (deterministic pseudo-random positions
// in a cube, per-vertex color). Rebuilt only when n changes - NOT per frame:
// once drawn, the cloud lives in a GPU buffer and is reused every frame, so this
// is the only place vertices are touched on the CPU.
void tcApp::rebuild(int n) {
    n_ = n;
    cloud = Mesh();
    cloud.setMode(PrimitiveMode::Points);   // draw the vertices as points, not triangles

    // Hash-based pseudo-random in [0,1] - keeps the cloud identical every run
    // without pulling in <random>.
    auto frac = [](float x) { return x - floorf(x); };
    auto rnd  = [&](int i, int salt) {
        return frac(sinf(i * 12.9898f + salt * 78.233f) * 43758.5453f);
    };
    for (int i = 0; i < n; ++i) {
        const float x = (rnd(i, 1) * 2.0f - 1.0f) * 120.0f;
        const float y = (rnd(i, 2) * 2.0f - 1.0f) * 120.0f;
        const float z = (rnd(i, 3) * 2.0f - 1.0f) * 120.0f;
        cloud.addVertex(Vec3(x, y, z));
        cloud.addColor(Color(rnd(i, 4), rnd(i, 5), rnd(i, 6), 1.0f));
    }
}

void tcApp::draw() {
    clear(0.08f, 0.09f, 0.11f);

    // Point look is draw state, just like setColor / setStrokeWeight:
    //   size  - logical px (ignored by the Pixel shape, which is always 1px)
    //   shape - Square / Round (billboarded splats) or Pixel (1px point)
    setPointSize(pointSize_);
    setPointStyle(style_);

    // Draw the GPU-resident cloud in camera space. cloud.draw() issues a single
    // instanced/point draw from the GPU buffer - no per-frame vertex streaming,
    // so the cost barely grows with the point count.
    view.begin();
        cloud.draw();
    view.end();

    // draw info text
    setColor(1.0f);
    string hud = string(enumLabel(style_)) + "\n";
    hud += toString(n_) + " points";
    if (style_ != PointStyle::Pixel) hud += ", size " + toString((int)pointSize_) + "px";
    hud += "\n" + toString(getFps(), 1) + " fps\n";
    hud += "\nG: shape   1-6: N (50k/100k/250k/500k/1M/2M)   [ ]: size   drag: orbit";
    drawBitmapString(hud, 20, 20);
}

void tcApp::keyPressed(int key) {
    switch (key) {
        // change point style
        case 'G':
            style_ = (style_ == PointStyle::Square) ? PointStyle::Round
                   : (style_ == PointStyle::Round)  ? PointStyle::Pixel
                                                    : PointStyle::Square;
            break;

        // change point count
        case '1': rebuild(50000);   break;
        case '2': rebuild(100000);  break;
        case '3': rebuild(250000);  break;
        case '4': rebuild(500000);  break;
        case '5': rebuild(1000000); break;
        case '6': rebuild(2000000); break;

        // change point size
        case '[': pointSize_ = std::max(1.0f, pointSize_ - 1.0f); break;
        case ']': pointSize_ += 1.0f; break;
    }
}
