#pragma once

#include <TrussC.h>   // tc::PointCloudRenderer is part of the core umbrella
using namespace std;
using namespace tc;

// Benchmark: draw a STATIC point cloud of N points every frame, comparing
// immediate-mode (tc::Mesh.draw -> sokol_gl, re-streams every vertex per frame)
// vs a GPU VBO path (instanced splats, upload-once). The cloud is rebuilt only
// when N changes, so this isolates the *draw* cost from any per-frame meshing.
//
//   G       toggle immediate <-> GPU VBO
//   1..6    set N (50k / 100k / 250k / 500k / 1M / 2M)
//   [ ]     splat size (GPU mode)
//   drag    orbit
class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    void rebuild(int n);

    EasyCam view;
    Mesh    cloud;                       // static point cloud (rebuilt only on N change)
    PointCloudRenderer gpu;              // VBO instanced splat renderer (tc:: core)

    int   n_ = 250000;
    bool  useGpu_ = false;
    float pointSize_ = 0.02f;

    float fps_ = 0.0f;
    float lastTime_ = 0.0f;
};
