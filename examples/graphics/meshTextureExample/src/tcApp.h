#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// =============================================================================
// tcApp - Mesh Texture Mapping Example
// Demonstrates texture mapping on Mesh using 3D primitives
// =============================================================================

class tcApp : public App {
public:
    // Lifecycle
    void setup() override;
    void update() override;
    void draw() override;

    // Input events
    void keyPressed(int key) override;

private:
    // Meshes
    Mesh plane_;
    Mesh box_;
    Mesh sphere_;
    
    // Textures
    Image checkerTexture_;
    Image gradientTexture_;
    
    // State
    bool showWireframe_ = false;
    int currentTexture_ = 0;  // 0: checker, 1: gradient
};

