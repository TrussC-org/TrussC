#include "tcApp.h"

static constexpr int GRID = 5;
static constexpr float SPACING = 140.0f;
static constexpr float SPHERE_R = 50.0f;

void tcApp::setup() {
    setWindowTitle("pbrSpheresExample");

    cam.setDistance(1200);
    cam.enableMouseInput();

    sphereMesh = createSphere(SPHERE_R, 32);

    // Procedural IBL: blue sky + sun, baked into irradiance / prefilter /
    // BRDF LUT at startup. The metals in the grid reflect this environment.
    env.loadProcedural();
    setEnvironment(env);

    keyLight.setDirectional(Vec3(-0.5f, -1.0f, -0.6f));
    keyLight.setDiffuse(1.0f, 0.98f, 0.92f);
    keyLight.setIntensity(3.0f);

    fillLight.setDirectional(Vec3(0.7f, -0.3f, 0.4f));
    fillLight.setDiffuse(0.4f, 0.5f, 0.7f);
    fillLight.setIntensity(0.6f);

    // X axis = roughness, Y axis = metallic
    const Color baseColor(0.90f, 0.80f, 0.70f);
    for (int y = 0; y < GRID; ++y) {
        for (int x = 0; x < GRID; ++x) {
            materials[y][x]
                .setBaseColor(baseColor)
                .setRoughness(0.05f + (float(x) / (GRID - 1)) * 0.95f)
                .setMetallic(float(y) / (GRID - 1));
        }
    }
}

void tcApp::update() {
}

void tcApp::draw() {
    clear(0.05f, 0.05f, 0.07f);

    cam.begin();

    clearLights();
    addLight(keyLight);
    addLight(fillLight);
    setCameraPosition(cam.getPosition());

    const float offset = -0.5f * (GRID - 1) * SPACING;
    for (int y = 0; y < GRID; ++y) {
        for (int x = 0; x < GRID; ++x) {
            setPbrMaterial(materials[y][x]);
            pushMatrix();
            translate(offset + x * SPACING, offset + y * SPACING, 0);
            sphereMesh.draw();
            popMatrix();
        }
    }

    setLightingMode(LightingMode::CpuPhong);
    clearPbrMaterial();

    cam.end();

    setColor(1.0f);
    drawBitmapString(
        "pbrSpheres\n"
        "X axis: roughness 0.05 -> 1.00\n"
        "Y axis: metallic  0.00 -> 1.00\n"
        "\n"
        "drag: rotate",
        20, 20);
}
