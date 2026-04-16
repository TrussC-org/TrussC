#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// projectorSimulationExample
// A projector placed on the ground projects a dynamic image (FBO gobo)
// onto a folded screen (byoubu). Demonstrates lens shift, FOV, and
// texture projection in a realistic setup.

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;

    void mouseDragged(Vec2 pos, int button) override;
    void mousePressed(Vec2 pos, int button) override;
    void mouseReleased(Vec2 pos, int button) override;

private:
    EasyCam cam;

    // Scene geometry
    Mesh screenLeft, screenCenter, screenRight;
    Mesh floorMesh;

    // Materials
    PbrMaterial wallMat;
    PbrMaterial floorMat;

    // Projector
    Light projector;
    Fbo goboFbo;
    float projectorX = 0.0f;
    float projectorZ = 400.0f;

    // Ambient
    Light ambientLight;
    Environment env;

    // Mouse interaction
    bool isDraggingProjector = false;

    // Helpers
    Mesh createPlane(float w, float h, int segsW = 1, int segsH = 1);
    void drawGoboContent();
};
