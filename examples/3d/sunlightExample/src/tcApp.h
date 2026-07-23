#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// sunlightExample
// A single directional "sun" arcs over a small city block from sunrise to
// sunset. Directional lights use an orthographic shadow map (setShadowArea),
// so every building casts a parallel shadow -- watch them sweep and stretch
// as the day goes by. Press 's' to cycle the sun's softness between a point
// sun (razor-sharp), the real sun (subtle penumbra), and a huge overcast-like
// emitter (very soft, contact-hardening clearly visible).

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    EasyCam cam;

    // Scene geometry
    Mesh floorMesh;
    vector<Mesh> buildingMeshes;
    vector<Vec3> buildingPositions;
    vector<float> buildingRotations;
    vector<int> buildingMatIndex;

    // Materials
    Material floorMat;
    Material buildingMats[3];

    // Sun + sky fill
    Light sunLight;
    Light skyLight;

    // Day cycle state
    float sunPhase = 0.08f;   // 0 = sunrise, 0.5 = noon, 1 = sunset
    float lastTime = 0.0f;
    bool paused = false;

    // Softness presets cycled with 's'
    int softnessIndex = 1;
    static constexpr float softnessPresets[3] = {0.0f, 0.9f, 4.0f};

    void drawScene(bool shadowPass);
};
