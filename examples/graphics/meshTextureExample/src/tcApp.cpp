#include "tcApp.h"

// ---------------------------------------------------------------------------
// setup - Initialization
// ---------------------------------------------------------------------------
void tcApp::setup() {
    tcLogNotice("tcApp") << "Mesh Texture Mapping Example";
    tcLogNotice("tcApp") << "  - SPACE: Toggle texture";
    tcLogNotice("tcApp") << "  - W: Toggle wireframe";
    tcLogNotice("tcApp") << "  - ESC: Exit";
    
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
    
    // Create plane with texture coordinates
    plane_ = createPlane(200, 200, 4, 4);
    
    // Create box with texture coordinates
    box_ = createBox(150);
    
    // Create sphere with texture coordinates
    sphere_ = createSphere(80, 16);
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
    enable3DPerspective(deg2rad(45.0f), 0.1f, 100.0f);
    
    float time = (float)getElapsedTime();
    
    // Camera rotation (like 3DPrimitivesExample)
    float spinX = sin(time * 0.35f);
    float spinY = cos(time * 0.075f);
    
    // Select texture
    Image& currentTex = (currentTexture_ == 0) ? checkerTexture_ : gradientTexture_;
    
    // Draw plane (left, on ground)
    pushMatrix();
    translate(-3.0f, 0.0f, -8.0f);  // Left side, on ground level, in front of camera
    rotateX(deg2rad(-90));  // Rotate to be horizontal (ground)
    rotateY(spinX);
    rotateX(spinY);
    scale(0.01f, 0.01f, 0.01f);  // Scale down like 3DPrimitivesExample
    if (showWireframe_) {
        plane_.drawWireframe();
    } else {
        plane_.draw(currentTex);
    }
    popMatrix();
    
    // Draw box (center)
    pushMatrix();
    translate(0.0f, 1.5f, -8.0f);  // Center, above ground, in front of camera
    rotateY(spinX);
    rotateX(spinY);
    scale(0.01f, 0.01f, 0.01f);
    if (showWireframe_) {
        box_.drawWireframe();
    } else {
        box_.draw(currentTex);
    }
    popMatrix();
    
    // Draw sphere (right)
    pushMatrix();
    translate(3.0f, 1.5f, -8.0f);  // Right side, above ground, in front of camera
    rotateY(spinX);
    rotateX(spinY);
    scale(0.01f, 0.01f, 0.01f);
    if (showWireframe_) {
        sphere_.drawWireframe();
    } else {
        sphere_.draw(currentTex);
    }
    popMatrix();
    
    // Disable 3D mode
    disable3D();
    
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

