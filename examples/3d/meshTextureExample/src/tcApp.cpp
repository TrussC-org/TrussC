// =============================================================================
// Mesh Texture Mapping Example
// =============================================================================
//
// Demonstrates how to apply textures to 3D primitives using Mesh::draw(Texture&).
//
// TEXTURE COORDINATE LAYOUT FOR EACH PRIMITIVE:
//
// -----------------------------------------------------------------------------
// 1. PLANE (createPlane)
// -----------------------------------------------------------------------------
//    Simple UV mapping. Texture stretches across the entire plane.
//
//    (0,0)-----------(1,0)
//      |               |
//      |   Texture     |
//      |   Image       |
//      |               |
//    (0,1)-----------(1,1)
//
// -----------------------------------------------------------------------------
// 2. BOX (createBox)
// -----------------------------------------------------------------------------
//    Each face has independent UV coordinates (0,0)-(1,1).
//    The SAME texture is applied to all 6 faces.
//    This is NOT a UV unwrap/cross layout.
//
//         +-------+
//        /|  Top /|      Each face:
//       / |     / |      (0,0)---(1,0)
//      +-------+  |        |       |
//      |  +---|--+         |  Tex  |
//      | /Back|  /         |       |
//      |/     | /        (0,1)---(1,1)
//      +-------+
//       Front
//
//    Face orientation (texture appears correctly when viewed from outside):
//    - Front  (Z+): left-to-right = U, bottom-to-top = V
//    - Back   (Z-): mirrored horizontally
//    - Top    (Y+): looking down, left-to-right = U, front-to-back = V
//    - Bottom (Y-): looking up, left-to-right = U, back-to-front = V
//    - Right  (X+): front-to-back = U, bottom-to-top = V
//    - Left   (X-): back-to-front = U, bottom-to-top = V
//
// -----------------------------------------------------------------------------
// 3. SPHERE (createSphere)
// -----------------------------------------------------------------------------
//    Equirectangular (latitude-longitude) mapping.
//    Same projection used for world maps and 360° photos.
//
//    U (horizontal) = longitude: 0.0 = 0°, 1.0 = 360° (wraps around)
//    V (vertical)   = latitude:  0.0 = North Pole, 1.0 = South Pole
//
//    Texture image layout:
//    (0,0)=========================(1,0)
//      |                             |     <- North Pole (top edge)
//      |      +----+                 |
//      |     /      \                |     <- Equator (middle)
//      |    +        +               |
//      |     \      /                |
//      |      +----+                 |     <- South Pole (bottom edge)
//    (0,1)=========================(1,1)
//         ^                       ^
//       0° lon                  360° lon (same as 0°)
//
//    NOTE: Poles will have texture distortion (singularity).
//          For seamless wrapping, texture left edge must match right edge.
//
// -----------------------------------------------------------------------------
// 4. CYLINDER (createCylinder)
// -----------------------------------------------------------------------------
//    Side: U wraps around circumference (0.0 = 0°, 1.0 = 360°)
//          V = 0.0 at top, V = 1.0 at bottom
//    Caps: Circular mapping centered at (0.5, 0.5)
//
// -----------------------------------------------------------------------------
// 5. CONE (createCone)
// -----------------------------------------------------------------------------
//    Side: U wraps around circumference, V = 0.0 at apex, V = 1.0 at base
//    Bottom cap: Circular mapping centered at (0.5, 0.5)
//
// -----------------------------------------------------------------------------
// 6. TORUS (createTorus)
// -----------------------------------------------------------------------------
//    U = angle around the torus (0.0 = 0°, 1.0 = 360°)
//    V = angle around the tube (0.0 = 0°, 1.0 = 360°)
//
// -----------------------------------------------------------------------------
// NOTE: createIcoSphere has no UV coordinates
// =============================================================================

#include "tcApp.h"

// ---------------------------------------------------------------------------
// setup - Initialization
// ---------------------------------------------------------------------------
void tcApp::setup() {
    logNotice("tcApp") << "Mesh Texture Mapping Example";
    logNotice("tcApp") << "  - SPACE: Toggle texture";
    logNotice("tcApp") << "  - W: Toggle wireframe";
    logNotice("tcApp") << "  - ESC: Exit";
    
    // Create checkerboard texture
    const int texSize = 256;
    const int checkerSize = 32;
    checkerTexture_.allocate(texSize, texSize, 4);
    
    for (int y = 0; y < texSize; y++) {
        for (int x = 0; x < texSize; x++) {
            int checkerX = x / checkerSize;
            int checkerY = y / checkerSize;
            bool isWhite = (checkerX + checkerY) % 2 == 0;
            Color color = isWhite ? Color(1.0f, 1.0f, 1.0f, 1.0f) 
                                  : Color(0.0f, 0.0f, 0.0f, 1.0f);
            checkerTexture_.setColor(x, y, color);
        }
    }
    // Note: update() will be called in draw() to ensure it's within a render pass
    
    // Create gradient texture
    gradientTexture_.allocate(texSize, texSize, 4);
    for (int y = 0; y < texSize; y++) {
        for (int x = 0; x < texSize; x++) {
            float u = (float)x / texSize;
            float v = (float)y / texSize;
            Color color = Color(u, v, 0.5f, 1.0f);
            gradientTexture_.setColor(x, y, color);
        }
    }
    // Note: update() will be called in draw() to ensure it's within a render pass
    
    // Create all 6 primitives with texture coordinates
    plane_ = createPlane(200, 200, 4, 4);
    box_ = createBox(150);
    sphere_ = createSphere(80, 16);
    cylinder_ = createCylinder(60, 180, 16);
    cone_ = createCone(80, 180, 16);
    torus_ = createTorus(60, 25, 24, 16);
}

// ---------------------------------------------------------------------------
// update - Update
// ---------------------------------------------------------------------------
void tcApp::update() {
}

// ---------------------------------------------------------------------------
// draw - Drawing
// ---------------------------------------------------------------------------
void tcApp::draw() {
    clear(0.1f, 0.1f, 0.12f);
    
    // Update textures on first draw (ensures it's called within a render pass)
    static bool texturesUpdated = false;
    if (!texturesUpdated) {
        checkerTexture_.update();
        gradientTexture_.update();
        texturesUpdated = true;
    }
    
    // Enable 3D drawing mode
    setupScreenPerspective(45.0f);
    
    float time = (float)getElapsedTime();
    
    // Camera rotation (like 3DPrimitivesExample)
    float spinX = sin(time * 0.35f);
    float spinY = cos(time * 0.075f);
    
    // Select texture
    Image& currentTex = (currentTexture_ == 0) ? checkerTexture_ : gradientTexture_;

    // Helper to draw a mesh at position
    auto drawMesh = [&](Mesh& mesh, float x, float y) {
        pushMatrix();
        translate(x, y, -8.0f);
        rotateY(spinX);
        rotateX(spinY);
        scale(0.01f, 0.01f, 0.01f);
        if (showWireframe_) {
            mesh.drawWireframe();
        } else {
            mesh.draw(currentTex);
        }
        popMatrix();
    };

    // Top row: Plane, Box, Sphere
    drawMesh(plane_,    -3.0f,  1.5f);
    drawMesh(box_,       0.0f,  1.5f);
    drawMesh(sphere_,    3.0f,  1.5f);

    // Bottom row: Cylinder, Cone, Torus
    drawMesh(cylinder_, -3.0f, -1.5f);
    drawMesh(cone_,      0.0f, -1.5f);
    drawMesh(torus_,     3.0f, -1.5f);
    
    // Return to 2D mode
    setupScreenOrtho();
    
    // Draw info text
    setColor(1.0f, 1.0f, 1.0f);
    drawBitmapString("Mesh Texture Mapping Example", 10, 30);
    drawBitmapString("SPACE: Toggle texture (" + string(currentTexture_ == 0 ? "Checker" : "Gradient") + ")", 10, 50);
    drawBitmapString("W: Toggle wireframe (" + string(showWireframe_ ? "ON" : "OFF") + ")", 10, 70);
}

// ---------------------------------------------------------------------------
// keyPressed
// ---------------------------------------------------------------------------
void tcApp::keyPressed(int key) {
    if (key == ' ') {
        currentTexture_ = (currentTexture_ + 1) % 2;
    } else if (key == 'w' || key == 'W') {
        showWireframe_ = !showWireframe_;
    }
}

