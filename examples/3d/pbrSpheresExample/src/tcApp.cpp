#include "tcApp.h"
#include <sstream>

static constexpr int GRID = 5;
static constexpr float SPACING = 140.0f;
static constexpr float SPHERE_R = 50.0f;

void tcApp::setup() {
    setWindowTitle("pbrSpheresExample");

    cam.setDistance(1200);
    cam.setTarget(0, 0, 0);
    cam.enableMouseInput();

    // Sphere mesh shared across all grid cells. createSphere() produces
    // vertices + normals (required by the GpuPbr path).
    sphereMesh = createSphere(SPHERE_R, 32);

    // Key light: directional, warm white, top-front-right
    keyLight.setDirectional(Vec3(-0.5f, -1.0f, -0.6f));
    keyLight.setDiffuse(1.0f, 0.98f, 0.92f);
    keyLight.setIntensity(3.0f);

    // Fill light: directional, cool, from the opposite side so we can see the
    // shadow side of metallic spheres
    fillLight.setDirectional(Vec3(0.7f, -0.3f, 0.4f));
    fillLight.setDiffuse(0.4f, 0.5f, 0.7f);
    fillLight.setIntensity(0.6f);

    // Build material grid. X axis = roughness 0.05 → 1.0, Y axis = metallic 0 → 1.
    // Base color: a slightly warm off-white that works well for both metals and
    // dielectrics.
    const Color baseColor(0.90f, 0.80f, 0.70f);
    for (int y = 0; y < GRID; ++y) {
        for (int x = 0; x < GRID; ++x) {
            PbrMaterial& m = materials[y][x];
            m.setBaseColor(baseColor);
            float roughness = 0.05f + (float(x) / (GRID - 1)) * 0.95f;
            float metallic  = float(y) / (GRID - 1);
            m.setRoughness(roughness);
            m.setMetallic(metallic);
        }
    }
}

void tcApp::update() {
}

void tcApp::draw() {
    clear(0.05f, 0.05f, 0.07f);

    cam.begin();

    // --- Lighting state shared by all PBR draws this frame -----------------
    clearLights();
    addLight(keyLight);
    addLight(fillLight);
    setCameraPosition(cam.getPosition());

    // --- Draw sphere grid ---------------------------------------------------
    const float offset = -0.5f * (GRID - 1) * SPACING;
    for (int y = 0; y < GRID; ++y) {
        for (int x = 0; x < GRID; ++x) {
            setPbrMaterial(materials[y][x]);  // also switches to LightingMode::GpuPbr

            pushMatrix();
            translate(offset + x * SPACING,
                      offset + y * SPACING,
                      0.0f);
            sphereMesh.draw();
            popMatrix();
        }
    }

    // Restore defaults so any subsequent 2D drawing still uses sokol_gl.
    setLightingMode(LightingMode::CpuPhong);
    clearPbrMaterial();

    cam.end();

    // --- 2D overlay ---------------------------------------------------------
    setColor(1.0f);
    if (showHelp) {
        std::stringstream ss;
        ss << "FPS: " << (int)getFrameRate() << "\n\n";
        ss << "Grid: 5x5\n";
        ss << "X axis: roughness 0.05 -> 1.00\n";
        ss << "Y axis: metallic  0.00 -> 1.00\n";
        ss << "\n";
        ss << "LMB drag: rotate  |  MMB drag: pan  |  scroll: zoom\n";
        ss << "h: toggle help\n";
        drawBitmapString(ss.str(), 20, 20);
    }
}

void tcApp::keyPressed(int key) {
    switch (key) {
        case 'h': case 'H':
            showHelp = !showHelp;
            break;
        case 'r': case 'R':
            cam.reset();
            cam.setDistance(1200);
            break;
        case 'f': case 'F':
            toggleFullscreen();
            break;
    }
}
