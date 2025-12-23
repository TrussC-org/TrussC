#include "tcApp.h"
#include <sstream>

void tcApp::setup() {
    setWindowTitle("easyCamExample");

    // Camera initial settings
    cam.setDistance(600);
    cam.setTarget(0, 0, 0);

    // Generate meshes
    boxMesh = createBox(100);
    sphereMesh = createSphere(50, 24);
    coneMesh = createCone(50, 100, 24);
    cylinderMesh = createCylinder(50, 100, 24);

    // Light settings (strong ambient, light shading)
    light.setDirectional(Vec3(-0.7f, -1.0f, -0.4f));  // Asymmetric angle
    light.setAmbient(0.65f, 0.65f, 0.7f);
    light.setDiffuse(0.5f, 0.5f, 0.45f);
    light.setSpecular(0.5f, 0.5f, 0.5f);

    // Material settings
    matRed = Material::plastic(Color(0.9f, 0.2f, 0.2f));
    matOrange = Material::plastic(Color(1.0f, 0.6f, 0.2f));
    matBlue = Material::plastic(Color(0.2f, 0.4f, 0.9f));
    matCyan = Material::plastic(Color(0.2f, 0.8f, 0.8f));
    matYellow = Material::plastic(Color(1.0f, 0.9f, 0.2f));
    matMagenta = Material::plastic(Color(0.9f, 0.2f, 0.8f));
}

void tcApp::update() {
    // No processing needed
}

void tcApp::draw() {
    clear(20);

    // --- 3D drawing (camera enabled) ---
    cam.begin();

    // Enable lighting
    enableLighting();
    addLight(light);
    setCameraPosition(cam.getPosition());
    setColor(1.0f, 1.0f, 1.0f);

    // Right: Red cone
    pushMatrix();
    translate(150, 0, 0);
    setMaterial(matRed);
    coneMesh.draw();
    popMatrix();

    // Left: Orange sphere
    pushMatrix();
    translate(-150, 0, 0);
    setMaterial(matOrange);
    sphereMesh.draw();
    popMatrix();

    // Bottom: Blue box
    pushMatrix();
    translate(0, 150, 0);
    setMaterial(matBlue);
    boxMesh.draw();
    popMatrix();

    // Top: Cyan cylinder
    pushMatrix();
    translate(0, -150, 0);
    setMaterial(matCyan);
    cylinderMesh.draw();
    popMatrix();

    // Front: Yellow box
    pushMatrix();
    translate(0, 0, 150);
    setMaterial(matYellow);
    boxMesh.draw();
    popMatrix();

    // Back: Magenta box
    pushMatrix();
    translate(0, 0, -150);
    setMaterial(matMagenta);
    boxMesh.draw();
    popMatrix();

    // End lighting
    disableLighting();
    clearLights();

    // Draw grid
    setColor(0.4f, 0.4f, 0.4f);
    drawGrid(400, 10);

    cam.end();

    // --- 2D drawing (UI) ---
    setColor(1.0f);

    if (showHelp) {
        std::stringstream ss;
        ss << "FPS: " << (int)getFrameRate() << "\n\n";
        ss << "MOUSE INPUT: " << (cam.isMouseInputEnabled() ? "ON" : "OFF") << "\n";
        ss << "Distance: " << (int)cam.getDistance() << "\n";
        ss << "\n";
        ss << "Controls:\n";
        ss << "  LEFT DRAG: rotate camera\n";
        ss << "  MIDDLE DRAG: pan camera\n";
        ss << "  SCROLL: zoom in/out\n";
        ss << "\n";
        ss << "Keys:\n";
        ss << "  c: toggle mouse input\n";
        ss << "  r: reset camera\n";
        ss << "  f: toggle fullscreen\n";
        ss << "  h: toggle this help\n";

        drawBitmapString(ss.str(), 20, 20);
    }
}

void tcApp::drawGrid(float size, int divisions) {
    float step = size / divisions;
    float halfSize = size / 2.0f;

    sgl_begin_lines();
    auto col = getColor();
    sgl_c4f(col.r, col.g, col.b, col.a);

    // Lines parallel to X-axis (arranged in Z direction)
    for (int i = 0; i <= divisions; i++) {
        float z = -halfSize + i * step;
        sgl_v3f(-halfSize, 0, z);
        sgl_v3f(halfSize, 0, z);
    }

    // Lines parallel to Z-axis (arranged in X direction)
    for (int i = 0; i <= divisions; i++) {
        float x = -halfSize + i * step;
        sgl_v3f(x, 0, -halfSize);
        sgl_v3f(x, 0, halfSize);
    }

    sgl_end();
}

void tcApp::keyPressed(int key) {
    switch (key) {
        case 'c':
        case 'C':
            if (cam.isMouseInputEnabled()) {
                cam.disableMouseInput();
            } else {
                cam.enableMouseInput();
            }
            break;
        case 'r':
        case 'R':
            cam.reset();
            cam.setDistance(600);
            break;
        case 'f':
        case 'F':
            toggleFullscreen();
            break;
        case 'h':
        case 'H':
            showHelp = !showHelp;
            break;
    }
}

void tcApp::mousePressed(Vec2 pos, int button) {
    cam.mousePressed(pos.x, pos.y, button);
}

void tcApp::mouseReleased(Vec2 pos, int button) {
    cam.mouseReleased(pos.x, pos.y, button);
}

void tcApp::mouseDragged(Vec2 pos, int button) {
    cam.mouseDragged(pos.x, pos.y, button);
}

void tcApp::mouseScrolled(Vec2 delta) {
    cam.mouseScrolled(delta.x, delta.y);
}
