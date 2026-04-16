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

    // Procedural normal map: overlapping sine bumps
    {
        const int S = 256;
        Pixels nmap;
        nmap.allocate(S, S, 4, PixelFormat::U8);
        auto* px = static_cast<unsigned char*>(nmap.getDataVoid());
        for (int iy = 0; iy < S; ++iy) {
            for (int ix = 0; ix < S; ++ix) {
                float u = float(ix) / S;
                float v = float(iy) / S;
                // Height from overlapping sine waves
                float h = std::sin(u * TAU * 6) * std::cos(v * TAU * 6) * 0.5f
                        + std::sin((u + v) * TAU * 4) * 0.3f;
                // Finite-difference partial derivatives
                float du = std::cos(u * TAU * 6) * TAU * 6 * std::cos(v * TAU * 6) * 0.5f
                         + std::cos((u + v) * TAU * 4) * TAU * 4 * 0.3f;
                float dv = std::sin(u * TAU * 6) * (-std::sin(v * TAU * 6)) * TAU * 6 * 0.5f
                         + std::cos((u + v) * TAU * 4) * TAU * 4 * 0.3f;
                (void)h;
                // Tangent-space normal from height derivatives (scale down for subtlety)
                float scale = 0.15f;
                float nx = -du * scale;
                float ny = -dv * scale;
                float nz = 1.0f;
                float len = std::sqrt(nx*nx + ny*ny + nz*nz);
                nx /= len; ny /= len; nz /= len;
                int idx = (iy * S + ix) * 4;
                px[idx + 0] = (unsigned char)((nx * 0.5f + 0.5f) * 255);
                px[idx + 1] = (unsigned char)((ny * 0.5f + 0.5f) * 255);
                px[idx + 2] = (unsigned char)((nz * 0.5f + 0.5f) * 255);
                px[idx + 3] = 255;
            }
        }
        normalMapTex.allocate(nmap, TextureUsage::Immutable);
    }

    // X axis = roughness, Y axis = metallic
    const Color baseColor(0.90f, 0.80f, 0.70f);
    for (int y = 0; y < GRID; ++y) {
        for (int x = 0; x < GRID; ++x) {
            materials[y][x]
                .setBaseColor(baseColor)
                .setRoughness(0.05f + (float(x) / (GRID - 1)) * 0.95f)
                .setMetallic(float(y) / (GRID - 1))
                .setNormalMap(&normalMapTex);
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
