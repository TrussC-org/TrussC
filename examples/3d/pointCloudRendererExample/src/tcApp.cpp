#include "tcApp.h"

void tcApp::setup() {
    setWindowTitle("pointCloudRendererExample - immediate vs GPU VBO");
    view.setTarget(0.0f, 0.0f, 0.0f);
    view.setDistance(360.0f);
    view.enableMouseInput();
    rebuild(n_);
}

// Build a static point cloud of n points (deterministic pseudo-random positions
// in a cube, per-vertex color). Rebuilt only when n changes - NOT per frame.
void tcApp::rebuild(int n) {
    n_ = n;
    cloud = Mesh();
    cloud.setMode(PrimitiveMode::Points);

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

void tcApp::update() {
    // Smoothed frame rate from wall-clock delta.
    const float now = getElapsedTimef();
    const float dt  = now - lastTime_;
    lastTime_ = now;
    if (dt > 0.0f) {
        const float inst = 1.0f / dt;
        fps_ = (fps_ <= 0.0f) ? inst : fps_ * 0.92f + inst * 0.08f;
    }
}

void tcApp::draw() {
    clear(0.08f, 0.09f, 0.11f);

    if (useGpu_) {
        view.begin();
        gpu.draw(cloud, {.pointSize = pointSize_, .round = false});
        view.end();
    } else {
        view.begin();
        cloud.draw();
        view.end();
    }

    setColor(1.0f);
    const int f10 = (int)(fps_ * 10.0f + 0.5f);
    string hud;
    hud  = (useGpu_ ? "GPU VBO (instanced splats)\n" : "immediate (sokol_gl)\n");
    hud += to_string(n_) + " points\n";
    hud += to_string(f10 / 10) + "." + to_string(f10 % 10) + " fps\n";
    hud += "\nG: immediate/GPU   1-6: N (50k/100k/250k/500k/1M/2M)   [ ]: size   drag: orbit";
    drawBitmapString(hud, 20, 20);
}

void tcApp::keyPressed(int key) {
    switch (key) {
        case 'g': case 'G': useGpu_ = !useGpu_; break;
        case '1': rebuild(50000);   break;
        case '2': rebuild(100000);  break;
        case '3': rebuild(250000);  break;
        case '4': rebuild(500000);  break;
        case '5': rebuild(1000000); break;
        case '6': rebuild(2000000); break;
        case '[': pointSize_ = std::max(0.004f, pointSize_ - 0.005f); break;
        case ']': pointSize_ += 0.005f; break;
    }
}
