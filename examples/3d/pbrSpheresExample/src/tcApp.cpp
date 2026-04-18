#include "tcApp.h"

/*
 * pbrSpheresExample - PBR Material Parameter Grid
 *
 * This example demonstrates physically-based rendering (PBR) using the
 * metallic-roughness workflow. Instead of the traditional Phong shading
 * model, PBR simulates how light interacts with surfaces based on
 * physical properties:
 *
 *   - baseColor:  The surface color (albedo for dielectrics, reflectance
 *                 tint for metals)
 *   - metallic:   0 = dielectric (plastic, wood, etc.), 1 = metal (gold,
 *                 silver, etc.). Metals have no diffuse, only colored
 *                 specular reflection.
 *   - roughness:  0 = mirror-smooth, 1 = fully diffuse/matte.
 *
 * The appearance is controlled by two classes:
 *   - Light:     Light source (directional, point, spot, or projector)
 *   - Material:  Surface properties (baseColor, metallic, roughness, etc.)
 *
 * A 5x5 grid of spheres shows the full range of these parameters:
 *   X axis = roughness (smooth -> rough)
 *   Y axis = metallic  (dielectric -> metal)
 */

static constexpr int GRID = 5;
static constexpr float SPACING = 140.0f;
static constexpr float SPHERE_R = 50.0f;

void tcApp::setup() {
    setWindowTitle("pbrSpheresExample");

    cam.setDistance(1200);
    cam.enableMouseInput();

    sphereMesh = createSphere(SPHERE_R, 32);

    // Load a procedural sky/ground environment for image-based lighting (IBL).
    // Without IBL, metallic surfaces appear black because they have no diffuse
    // component — their appearance comes entirely from reflecting the environment.
    env.loadProcedural();
    setEnvironment(env);

    // Key light: warm directional light from the front-above.
    // This provides the primary illumination and specular highlights.
    keyLight.setDirectional(Vec3(-0.5f, -1.0f, -0.8f));
    keyLight.setDiffuse(1.0f, 0.95f, 0.85f);  // slightly warm white
    keyLight.setIntensity(3.0f);

    // Fill light: cool directional light from the opposite side.
    // Prevents the shadowed side from going completely black.
    fillLight.setDirectional(Vec3(0.7f, -0.3f, 0.4f));
    fillLight.setDiffuse(0.4f, 0.5f, 0.7f);  // cool blue tint
    fillLight.setIntensity(0.4f);

    // Build the 5x5 material grid.
    // All spheres share the same baseColor so the visual differences come
    // purely from the roughness and metallic parameters.
    // Values in linear space (equivalent to sRGB ~0.75, 0.65, 0.55)
    const Color baseColor(0.53f, 0.39f, 0.27f);
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

    // EasyCam: drag to rotate the view
    cam.begin();

    // IBL only (no direct lights) — test to see pure environment lighting
    clearLights();
    setCameraPosition(cam.getPosition());

    // Draw the sphere grid
    const float offset = -0.5f * (GRID - 1) * SPACING;
    for (int y = 0; y < GRID; ++y) {
        for (int x = 0; x < GRID; ++x) {
            // setMaterial() activates PBR rendering for subsequent mesh draws.
            setMaterial(materials[y][x]);
            pushMatrix();
            translate(offset + x * SPACING, offset + y * SPACING, 0);
            sphereMesh.draw();
            popMatrix();
        }
    }

    // clearMaterial() returns to default (unlit) rendering.
    clearMaterial();

    cam.end();

    // 2D overlay text
    setColor(1.0f);
    drawBitmapString(
        "pbrSpheres\n"
        "X axis: roughness 0.05 -> 1.00\n"
        "Y axis: metallic  0.00 -> 1.00\n"
        "\n"
        "drag: rotate",
        20, 20);
}
