#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// pbrSpheresExample - PBR material grid demo
// 5x5 sphere grid: X axis = roughness, Y axis = metallic.
// Uses LightingMode::GpuPbr / Cook-Torrance GGX evaluated on the GPU.

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;

    void keyPressed(int key) override;

private:
    EasyCam cam;

    Mesh sphereMesh;

    Light keyLight;
    Light fillLight;

    PbrMaterial materials[5][5];

    bool showHelp = true;
};
