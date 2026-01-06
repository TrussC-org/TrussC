#include "tcApp.h"
#include "TrussC.h"

using namespace std;

// ---------------------------------------------------------------------------
// setup
// ---------------------------------------------------------------------------
void tcApp::setup() {
    logNotice("tcApp") << "05_3d_primitives: 3D Primitives Demo";
    logNotice("tcApp") << "  - 1/2/3/4: Change resolution";
    logNotice("tcApp") << "  - s: Fill ON/OFF";
    logNotice("tcApp") << "  - w: Wireframe ON/OFF";
    logNotice("tcApp") << "  - l: Lighting ON/OFF";
    logNotice("tcApp") << "  - ESC: Exit";

    // Light settings (directional light from diagonal above-left)
    // Y+ is down in screen coords, so Y=+1 means "from above"
    light_.setDirectional(Vec3(-1, 1, -1));
    light_.setAmbient(0.2f, 0.2f, 0.25f);
    light_.setDiffuse(1.0f, 1.0f, 0.95f);
    light_.setSpecular(1.0f, 1.0f, 1.0f);

    // Material for each primitive (order matches primitives array)
    materials_[0] = Material::plastic(Color(0.8f, 0.2f, 0.2f));  // Plane: Red
    materials_[1] = Material::gold();                                  // Box: Gold
    materials_[2] = Material::plastic(Color(0.2f, 0.6f, 0.9f));  // Sphere: Blue
    materials_[3] = Material::emerald();                              // IcoSphere: Emerald
    materials_[4] = Material::silver();                               // Cylinder: Silver
    materials_[5] = Material::copper();                               // Cone: Copper

    rebuildPrimitives();
}

// ---------------------------------------------------------------------------
// Rebuild primitives
// ---------------------------------------------------------------------------
void tcApp::rebuildPrimitives() {
    float size = 120.0f;

    int planeRes, sphereRes, icoRes, cylRes, coneRes;

    switch (resolution) {
        case 1:
            planeRes = 2; sphereRes = 4; icoRes = 0; cylRes = 4; coneRes = 4;
            break;
        case 2:
            planeRes = 4; sphereRes = 8; icoRes = 1; cylRes = 8; coneRes = 8;
            break;
        case 3:
            planeRes = 8; sphereRes = 16; icoRes = 2; cylRes = 12; coneRes = 12;
            break;
        case 4:
        default:
            planeRes = 12; sphereRes = 32; icoRes = 3; cylRes = 20; coneRes = 20;
            break;
    }

    plane = createPlane(size * 1.5f, size * 1.5f, planeRes, planeRes);
    box = createBox(size);
    sphere = createSphere(size * 0.7f, sphereRes);
    icoSphere = createIcoSphere(size * 0.7f, icoRes);
    cylinder = createCylinder(size * 0.4f, size * 1.5f, cylRes);
    cone = createCone(size * 0.5f, size * 1.5f, coneRes);

    logNotice("tcApp") << "Resolution mode: " << resolution;
}

// ---------------------------------------------------------------------------
// update
// ---------------------------------------------------------------------------
void tcApp::update() {
}

// ---------------------------------------------------------------------------
// draw
// ---------------------------------------------------------------------------
void tcApp::draw() {
    clear(0.1f, 0.1f, 0.12f);

    // Now using default perspective from setupScreenFov (45° FOV)
    // Camera is at (screenW/2, screenH/2, dist) looking at Z=0

    float t = getElapsedTime();

    // Same rotation calculation as oF (stops when mouse is pressed)
    float spinX = 0, spinY = 0;
    if (!isMousePressed()) {
        spinX = sin(t * 0.35f);
        spinY = cos(t * 0.075f);
    }

    struct PrimitiveInfo {
        Mesh* mesh;
        const char* name;
        float x, y;  // Position offset from center
    };

    // Grid spacing based on window size (with padding for visibility)
    float baseSize = tc::min(getWindowWidth(), getWindowHeight());
    float spacingX = baseSize * 0.4f;   // Horizontal spacing
    float spacingY = baseSize * 0.4f;   // Vertical spacing

    // Arrange in 3x2 grid (screen space, centered)
    // Y+ is down in screen coordinates, so negative Y offset = top row
    PrimitiveInfo primitives[] = {
        { &plane,     "Plane",      -spacingX, -spacingY * 0.5f },
        { &box,       "Box",         0.0f,     -spacingY * 0.5f },
        { &sphere,    "Sphere",      spacingX, -spacingY * 0.5f },
        { &icoSphere, "IcoSphere",  -spacingX,  spacingY * 0.5f },
        { &cylinder,  "Cylinder",    0.0f,      spacingY * 0.5f },
        { &cone,      "Cone",        spacingX,  spacingY * 0.5f },
    };

    // Center of screen
    float cx = getWindowWidth() / 2.0f;
    float cy = getWindowHeight() / 2.0f;

    // Lighting settings
    if (bLighting) {
        enableLighting();
        addLight(light_);
        setCameraPosition(cx, cy, 1000);  // Approximate camera position
    }

    // Draw each primitive
    for (int i = 0; i < 6; i++) {
        auto& p = primitives[i];

        pushMatrix();
        // Position at screen center + offset, Z=0
        translate(cx + p.x, cy + p.y, 0.0f);

        // 3D rotation (rotate on X and Y axes like oF)
        rotateY(spinX);
        rotateX(spinY);

        // Fill
        if (bFill) {
            if (bLighting) {
                setMaterial(materials_[i]);
                setColor(1.0f, 1.0f, 1.0f);
            } else {
                float hue = (float)i / 6.0f * TAU;
                setColor(
                    0.5f + 0.4f * cos(hue),
                    0.5f + 0.4f * cos(hue + TAU / 3),
                    0.5f + 0.4f * cos(hue + TAU * 2 / 3)
                );
            }
            p.mesh->draw();
        }

        // Wireframe
        if (bWireframe) {
            disableLighting();
            setColor(0.0f, 0.0f, 0.0f);
            p.mesh->drawWireframe();
            if (bLighting) {
                enableLighting();
                addLight(light_);
            }
        }

        popMatrix();
    }

    // End lighting
    disableLighting();
    clearLights();

    // Controls description (top-left)
    // No need to call setupScreenOrtho() - 2D drawing works in perspective mode
    setColor(1.0f, 1.0f, 1.0f);
    float y = 20;
    drawBitmapString("3D Primitives Demo", 10, y); y += 16;
    drawBitmapString("1-4: Resolution (" + toString(resolution) + ")", 10, y); y += 16;
    drawBitmapString("s: Fill " + string(bFill ? "[ON]" : "[OFF]"), 10, y); y += 16;
    drawBitmapString("w: Wireframe " + string(bWireframe ? "[ON]" : "[OFF]"), 10, y); y += 16;
    drawBitmapString("l: Lighting " + string(bLighting ? "[ON]" : "[OFF]"), 10, y); y += 16;
    drawBitmapString("FPS: " + toString(getFrameRate(), 1), 10, y);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
void tcApp::keyPressed(int key) {
    if (key == KEY_ESCAPE) {
        sapp_request_quit();
    }
    else if (key == '1') {
        resolution = 1;
        rebuildPrimitives();
    }
    else if (key == '2') {
        resolution = 2;
        rebuildPrimitives();
    }
    else if (key == '3') {
        resolution = 3;
        rebuildPrimitives();
    }
    else if (key == '4') {
        resolution = 4;
        rebuildPrimitives();
    }
    else if (key == 's' || key == 'S') {
        bFill = !bFill;
        logNotice("tcApp") << "Fill: " << (bFill ? "ON" : "OFF");
    }
    else if (key == 'w' || key == 'W') {
        bWireframe = !bWireframe;
        logNotice("tcApp") << "Wireframe: " << (bWireframe ? "ON" : "OFF");
    }
    else if (key == 'l' || key == 'L') {
        bLighting = !bLighting;
        logNotice("tcApp") << "Lighting: " << (bLighting ? "ON" : "OFF");
    }
}
