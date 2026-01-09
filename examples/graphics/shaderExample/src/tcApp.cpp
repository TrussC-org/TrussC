// =============================================================================
// shaderExample - Demonstrating pushShader() with various draw functions
// =============================================================================

#include "tcApp.h"
#include "TrussC.h"

void tcApp::setup() {
    logNotice("tcApp") << "Shader Example - pushShader() demo";
    logNotice("tcApp") << "  Press UP/DOWN to adjust effect strength";

    // Load shader
    if (!shader.load(rainbow_shader_desc)) {
        logError("tcApp") << "Failed to load shader";
    }

    createStarMesh();
}

void tcApp::createStarMesh() {
    // Create a 5-pointed star mesh
    starMesh.setMode(PrimitiveMode::TriangleFan);
    starMesh.clear();

    float outerRadius = 40.0f;
    float innerRadius = 16.0f;
    int points = 5;

    // Center vertex
    starMesh.addVertex(0, 0, 0);
    starMesh.addColor(colors::white);

    // Star points
    for (int i = 0; i <= points * 2; i++) {
        float angle = (float)i / (points * 2) * TAU - TAU * 0.25f;
        float radius = (i % 2 == 0) ? outerRadius : innerRadius;
        float x = cos(angle) * radius;
        float y = sin(angle) * radius;
        starMesh.addVertex(x, y, 0);
        starMesh.addColor(colors::yellow);
    }
}

void tcApp::drawShapes() {
    // Rectangle
    setColor(colors::coral);
    drawRect(-60, 55, 120, 50);

    // Circle
    setColor(colors::skyBlue);
    drawCircle(0, 150, 35);

    // Triangle
    setColor(colors::limeGreen);
    drawTriangle(-45, 250, 45, 250, 0, 200);

    // Line (NOTE: NOT rendered in shader mode - use StrokeMesh instead)
    setColor(colors::cyan);
    drawLine(-50, 280, 50, 310);

    // beginShape polygon
    setColor(colors::orchid);
    beginShape();
    vertex(-35, 340);
    vertex(35, 340);
    vertex(45, 380);
    vertex(0, 410);
    vertex(-45, 380);
    endShape(true);

    // Star Mesh
    pushMatrix();
    translate(0, 460);
    setColor(colors::gold);
    starMesh.draw();
    popMatrix();

    // beginStroke/endStroke (shader-compatible alternative to drawLine)
    pushMatrix();
    translate(0, 530);
    setStrokeWeight(8.0f);
    setStrokeCap(StrokeCap::Round);
    setStrokeJoin(StrokeJoin::Round);
    setColor(colors::hotPink);
    beginStroke();
    vertex(-50, -15);
    vertex(-20, 15);
    vertex(20, -15);
    vertex(50, 15);
    endStroke();
    popMatrix();
}

void tcApp::update() {
}

void tcApp::draw() {
    clear(0.15f);

    float time = getElapsedTimef();
    float winW = (float)getWindowWidth();
    float winH = (float)getWindowHeight();

    // Set shader uniforms
    vs_params_t vsParams = {};
    vsParams.screenSize[0] = winW;
    vsParams.screenSize[1] = winH;

    effect_params_t fsParams = {};
    fsParams.time = time;
    fsParams.effectStrength = effectStrength;

    // =========================================================================
    // LEFT SIDE: Normal drawing (no shader)
    // =========================================================================
    pushMatrix();
    translate(winW * 0.25f, 0);

    setColor(colors::white);
    drawBitmapString("Normal", -30, 30);

    drawShapes();

    popMatrix();

    // =========================================================================
    // RIGHT SIDE: With shader applied
    // =========================================================================
    pushMatrix();
    translate(winW * 0.75f, 0);

    setColor(colors::white);
    drawBitmapString("With Shader", -45, 30);

    // Apply shader to all following draw calls
    pushShader(shader);
    shader.setUniform(1, &vsParams, sizeof(vsParams));
    shader.setUniform(0, &fsParams, sizeof(fsParams));

    drawShapes();

    popShader();

    popMatrix();

    // =========================================================================
    // UI (top of screen) - must be drawn before shader usage
    // =========================================================================
    setColor(colors::white);
    drawBitmapString("UP/DOWN: Adjust strength (" + to_string((int)(effectStrength * 100)) + "%)", 10, winH - 20);
}

void tcApp::keyPressed(int key) {
    if (key == KEY_UP) {
        effectStrength = std::min(1.0f, effectStrength + 0.1f);
    } else if (key == KEY_DOWN) {
        effectStrength = std::max(0.0f, effectStrength - 0.1f);
    }
}
