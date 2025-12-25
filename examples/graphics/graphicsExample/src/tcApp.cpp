#include "tcApp.h"

// ---------------------------------------------------------------------------
// setup - Initialization
// ---------------------------------------------------------------------------
void tcApp::setup() {
    tcLogNotice("tcApp") << "setup() called";
}

// ---------------------------------------------------------------------------
// update - Update
// ---------------------------------------------------------------------------
void tcApp::update() {
    // Logic updates go here
}

// ---------------------------------------------------------------------------
// draw - Drawing
// ---------------------------------------------------------------------------
void tcApp::draw() {
    double t = getElapsedTime();

    // Clear background
    clear(0.15f, 0.15f, 0.2f);

    // ----------------------
    // Rectangle
    // ----------------------
    setColor(0.9f, 0.3f, 0.3f);
    drawRect(50, 50, 150, 100);

    // Rectangle with stroke
    noFill();
    stroke();
    setColor(1.0f, 1.0f, 0.3f);
    drawRect(50, 180, 150, 100);
    fill();
    noStroke();

    // ----------------------
    // Circle
    // ----------------------
    // Higher resolution for larger circles to be smooth
    setCircleResolution(100);
    setColor(0.3f, 0.9f, 0.3f);
    drawCircle(350, 100, 60);
    setCircleResolution(20);  // Reset to default

    // Animated circle (default resolution = 20 segments)
    float pulse = (float)(sin(t * 3.0) * 0.3 + 0.7);
    setColor(0.3f, 0.7f, 0.9f, pulse);
    drawCircle(350, 250, 50);

    // ----------------------
    // Ellipse
    // ----------------------
    setColor(0.9f, 0.5f, 0.9f);
    drawEllipse(550, 100, 80, 50);

    // ----------------------
    // Lines
    // ----------------------
    setColor(1.0f, 1.0f, 1.0f);
    for (int i = 0; i < 10; i++) {
        float angle = (float)i / 10.0f * TAU + (float)t;
        float x2 = 550 + cos(angle) * 80;
        float y2 = 250 + sin(angle) * 80;
        drawLine(550, 250, x2, y2);
    }

    // ----------------------
    // Triangle
    // ----------------------
    setColor(0.9f, 0.6f, 0.2f);
    drawTriangle(750, 50, 850, 150, 650, 150);

    // Rotating triangle
    pushMatrix();
    translate(750, 250);
    rotate((float)t);
    setColor(0.5f, 0.9f, 0.9f);
    drawTriangle(-50, -30, 50, -30, 0, 50);
    popMatrix();

    // ----------------------
    // Custom shapes (beginShape/endShape)
    // ----------------------
    // Pentagon (filled)
    setColor(0.8f, 0.4f, 0.8f);
    beginShape();
    for (int i = 0; i < 5; i++) {
        float angle = TAU * i / 5.0f - HALF_TAU / 2.0f;
        vertex(150 + cos(angle) * 50, 380 + sin(angle) * 50);
    }
    endShape(true);

    // Star shape (stroke)
    noFill();
    stroke();
    setColor(1.0f, 0.9f, 0.2f);
    beginShape();
    for (int i = 0; i < 10; i++) {
        float angle = TAU * i / 10.0f - HALF_TAU / 2.0f;
        float r = (i % 2 == 0) ? 60.0f : 30.0f;
        vertex(350 + cos(angle) * r, 380 + sin(angle) * r);
    }
    endShape(true);
    fill();
    noStroke();

    // Animated custom shape
    setColor(0.3f, 0.8f, 0.9f, 0.8f);
    beginShape();
    int numPoints = 6;
    for (int i = 0; i < numPoints; i++) {
        float angle = TAU * i / numPoints + (float)t;
        float r = 40 + sin(t * 2 + i) * 20;
        vertex(550 + cos(angle) * r, 380 + sin(angle) * r);
    }
    endShape(true);

    // Wave using Path (member variable, 100 vertices)
    noFill();
    stroke();
    setColor(0.2f, 1.0f, 0.6f);
    wave.clear();
    for (int i = 0; i < 100; i++) {
        float x = 650 + i * 2;
        float y = 380 + sin(i * 0.1f + t * 3) * 30;
        wave.addVertex(x, y);
    }
    wave.draw();
    fill();
    noStroke();

    // ----------------------
    // Mesh (triangle with vertex colors)
    // ----------------------
    Mesh mesh;
    mesh.setMode(PrimitiveMode::Triangles);
    // 3 vertices
    mesh.addVertex(750, 460);
    mesh.addVertex(850, 560);
    mesh.addVertex(650, 560);
    // Vertex colors (RGB)
    mesh.addColor(1.0f, 0.0f, 0.0f);  // Red
    mesh.addColor(0.0f, 1.0f, 0.0f);  // Green
    mesh.addColor(0.0f, 0.0f, 1.0f);  // Blue
    mesh.draw();

    // ----------------------
    // Grid drawing
    // ----------------------
    setColor(0.6f, 0.6f, 0.6f, 0.5f);
    for (int x = 0; x < 5; x++) {
        for (int y = 0; y < 2; y++) {
            float px = 100.0f + x * 80.0f;
            float py = 480.0f + y * 60.0f;
            float size = 20.0f + (float)sin(t * 2.0 + x * 0.5 + y * 0.3) * 10.0f;
            drawCircle(px, py, size);
        }
    }

    // ----------------------
    // Draw circle at mouse position (using getGlobalMouseX/Y)
    // ----------------------
    setColor(1.0f, 0.3f, 0.5f, 0.8f);
    drawCircle(getGlobalMouseX(), getGlobalMouseY(), 20);

    // Change color when mouse is pressed
    if (isMousePressed()) {
        setColor(0.3f, 1.0f, 0.5f, 0.8f);
        drawCircle(getGlobalMouseX(), getGlobalMouseY(), 30);
    }

    // FPS display
    setColor(1.0f, 1.0f, 1.0f);
    drawBitmapString("FPS: " + toString(getFrameRate(), 1), 10, 20);
}

// ---------------------------------------------------------------------------
// Input events
// ---------------------------------------------------------------------------

void tcApp::keyPressed(int key) {
    tcLogVerbose("tcApp") << "keyPressed: " << key;

    // ESC to quit
    if (key == KEY_ESCAPE) {
        sapp_request_quit();
    }
}

void tcApp::mousePressed(Vec2 pos, int button) {
    tcLogVerbose("tcApp") << "mousePressed: " << pos.x << ", " << pos.y << " button=" << button;
}

void tcApp::mouseDragged(Vec2 pos, int button) {
    (void)pos; (void)button;
    // Mouse position can be obtained via getMouseX/Y, so nothing here
}
