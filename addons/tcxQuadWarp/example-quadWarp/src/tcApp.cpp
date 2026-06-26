#include "tcApp.h"

void tcApp::setup() {
    // Initial setup for the warper
    warper_.setup();
    warper_.setSourceRect(Rect(0, 0, 600, 400));
    warper_.setTargetRect(Rect(100, 100, 600, 400));
    
    // Try to load existing settings
    warper_.load("warp-settings.json");
    
    // Allocate FBO
    testFbo.allocate(600, 400);
    
    // Draw something into the FBO
    testFbo.begin(0.0f, 0.0f, 0.0f, 1.0f); // Clear to black

    pushStyle();

    // Checkerboard background
    float tileSize = 40.0f;
    for(float y=0; y<testFbo.getHeight(); y+=tileSize) {
        for(float x=0; x<testFbo.getWidth(); x+=tileSize) {
            if (int((x+y)/tileSize) % 2 == 0) setColor(0.8f);
            else setColor(0.4f);
            drawRect(x, y, tileSize, tileSize);
        }
    }

    // Draw text
    setColor(1.0f);
    setTextAlign(Center, Baseline);
    drawBitmapString("WARPED FBO", 300, 50, 2.0f);

    popStyle();

    testFbo.end();
}

void tcApp::update() {
    warper_.update();
}

void tcApp::draw() {
    clear(0.2f);

    // 1. Draw something warped
    pushMatrix();
    multMatrix(warper_.getMatrix());

    // Draw the FBO
    setColor(1.0f);
    testFbo.draw(0, 0);

    // Draw a circle
    setColor(1.0f, 0.5f, 0.0f);
    drawCircle(300, 200, 100); 

    popMatrix();
    
    // 2. Draw the warper UI (guidelines and handles) if enabled
    if (warper_.isInputEnabled()) {
        warper_.drawUI();
    }
    
    // Instructions
    setColor(1.0f);
    drawBitmapString("W: Toggle UI / Input", 20, 20);
    drawBitmapString("S: Save settings", 20, 40);
    drawBitmapString("L: Load settings", 20, 60);
    drawBitmapString("Drag corners to warp", 20, 80);
}

void tcApp::keyPressed(int key) {
    if (key == 'w' || key == 'W') {
        warper_.toggleInput(); // Toggle input and visibility
    } else if (key == 's' || key == 'S') {
        warper_.save("warp-settings.json");
    } else if (key == 'l' || key == 'L') {
        warper_.load("warp-settings.json");
    }
}
