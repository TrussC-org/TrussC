#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// =============================================================================
// tcApp - 3D Primitives Demo
// Simple version based on oF's 3DPrimitivesExample
// =============================================================================

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    // Primitive meshes
    Mesh plane;
    Mesh box;
    Mesh sphere;
    Mesh icoSphere;
    Mesh cylinder;
    Mesh cone;

    // Drawing mode
    bool bFill = true;
    bool bWireframe = true;
    bool bLighting = true;

    // Resolution mode (1-4)
    int resolution = 2;

    // Lighting
    Light light_;
    Material materials_[6];  // Material for each primitive

    // Rebuild primitives
    void rebuildPrimitives();
};
