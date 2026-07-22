#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// multiShadowExample
// Two spot lights with shadow maps at the same time: a warm light from the
// left and a cool light from the right, each casting its own shadow of the
// same objects in a different direction. Press '1' / '2' to toggle each
// light's shadow pass and see the per-light shadow independence.

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
    Mesh boxMesh;
    Mesh tallBoxMesh;
    Mesh sphereMesh;

    // Materials
    Material floorMat;
    Material boxMat;
    Material sphereMat;

    // Lights: warm from the left, cool from the right
    Light warmLight;
    Light coolLight;
    Light fillLight;

    // Shadow pass toggles ('1' / '2')
    bool warmShadowOn = true;
    bool coolShadowOn = true;

    void drawScene(bool shadowPass);
};
