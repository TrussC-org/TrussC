// =============================================================================
// shaderExample - Demonstrating pushShader() with various draw functions
// =============================================================================
// Shows how custom shaders can be applied to any geometry:
//   - drawRect, drawCircle, drawTriangle, drawEllipse
//   - beginShape/endShape
//   - Mesh.draw()
//   - StrokeMesh.draw()
// NOTE: drawLine is NOT supported in shader mode (use StrokeMesh instead)
// =============================================================================

#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// Include generated shader header
#include "shaders/effect.glsl.h"

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    Shader shader;
    Mesh starMesh;

    float effectStrength = 0.8f;

    void createStarMesh();
    void drawShapes();
};
