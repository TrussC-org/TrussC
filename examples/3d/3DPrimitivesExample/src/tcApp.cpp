#include "tcApp.h"

using namespace std;

// ---------------------------------------------------------------------------
// setup
// ---------------------------------------------------------------------------
void tcApp::setup() {
    tcLogNotice("tcApp") << "05_3d_primitives: 3D Primitives Demo";
    tcLogNotice("tcApp") << "  - 1/2/3/4: Change resolution";
    tcLogNotice("tcApp") << "  - s: Fill ON/OFF";
    tcLogNotice("tcApp") << "  - w: Wireframe ON/OFF";
    tcLogNotice("tcApp") << "  - l: Lighting ON/OFF";
    tcLogNotice("tcApp") << "  - ESC: Exit";

    // Light settings (directional light from diagonal above)
    light_.setDirectional(Vec3(-1, -1, -1));
    light_.setAmbient(0.2f, 0.2f, 0.25f);
    light_.setDiffuse(1.0f, 1.0f, 0.95f);
    light_.setSpecular(1.0f, 1.0f, 1.0f);

    // Material for each primitive
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

    tcLogNotice("tcApp") << "Resolution mode: " << resolution;
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

    // Enable 3D drawing mode (perspective + depth test)
    enable3DPerspective(deg2rad(45.0f), 0.1f, 100.0f);

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
        float x, y;  // Position in 3D space (-1 to 1 range)
    };

    // Arrange in 3x2 grid (perspective space)
    PrimitiveInfo primitives[] = {
        { &plane,     "Plane",      -3.0f, 1.5f },
        { &box,       "Box",         0.0f, 1.5f },
        { &sphere,    "Sphere",      3.0f, 1.5f },
        { &icoSphere, "IcoSphere",  -3.0f, -1.5f },
        { &cylinder,  "Cylinder",    0.0f, -1.5f },
        { &cone,      "Cone",        3.0f, -1.5f },
    };

    // Lighting settings
    if (bLighting) {
        enableLighting();
        addLight(light_);
        // Set camera position (for specular calculation)
        setCameraPosition(0, 0, 0);
    }

    // Draw each primitive
    for (int i = 0; i < 6; i++) {
        auto& p = primitives[i];

        pushMatrix();
        translate(p.x, p.y, -8.0f);

        // 3D rotation (rotate on X and Y axes like oF)
        rotateY(spinX);
        rotateX(spinY);

        // Scale down (for perspective)
        scale(0.01f, 0.01f, 0.01f);

        // Fill
        if (bFill) {
            if (bLighting) {
                // When using lighting, set material
                setMaterial(materials_[i]);
                setColor(1.0f, 1.0f, 1.0f);  // Draw white (material determines color)
            } else {
                // Without lighting, use traditional color
                float hue = (float)i / 6.0f * TAU;
                setColor(
                    0.5f + 0.4f * cos(hue),
                    0.5f + 0.4f * cos(hue + TAU / 3),
                    0.5f + 0.4f * cos(hue + TAU * 2 / 3)
                );
            }
            p.mesh->draw();
        }

        // Wireframe (draw without lighting)
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

    // Return to 2D drawing
    disable3D();

    // Controls description (top-left)
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
        tcLogNotice("tcApp") << "Fill: " << (bFill ? "ON" : "OFF");
    }
    else if (key == 'w' || key == 'W') {
        bWireframe = !bWireframe;
        tcLogNotice("tcApp") << "Wireframe: " << (bWireframe ? "ON" : "OFF");
    }
    else if (key == 'l' || key == 'L') {
        bLighting = !bLighting;
        tcLogNotice("tcApp") << "Lighting: " << (bLighting ? "ON" : "OFF");
    }
}
