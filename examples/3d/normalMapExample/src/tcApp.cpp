#include "tcApp.h"

/*
 * normalMapExample - Normal Map Demo
 *
 * Normal maps add surface detail (bumps, dents, grooves) without changing
 * the actual mesh geometry. They work by perturbing the surface normal at
 * each pixel, which changes how light reflects off the surface.
 *
 * A normal map is an RGB texture where each pixel encodes a direction:
 *   R = X component (-1..1 mapped to 0..255)
 *   G = Y component (-1..1 mapped to 0..255)
 *   B = Z component ( 0..1 mapped to 128..255, always points "outward")
 *
 * This example generates a procedural normal map from overlapping sine
 * waves and shows two identical spheres side by side:
 *   Left:  flat surface (no normal map)
 *   Right: same material with normal map applied
 *
 * The normal map is set on the Material with setNormalMap(&texture).
 */

void tcApp::setup() {
    setWindowTitle("normalMapExample");

    cam.setDistance(500);
    cam.enableMouseInput();

    // High-resolution sphere so the normal map detail is visible
    sphereMesh = createSphere(90, 48);

    // IBL environment for ambient reflections
    env.loadProcedural();
    setEnvironment(env);

    // Key light: warm directional from front-above
    keyLight.setDirectional(Vec3(-0.5f, -1.0f, -0.8f));
    keyLight.setDiffuse(1.0f, 0.95f, 0.85f);
    keyLight.setIntensity(3.0f);

    // Fill light: cool from opposite side
    fillLight.setDirectional(Vec3(0.7f, -0.3f, 0.4f));
    fillLight.setDiffuse(0.4f, 0.5f, 0.7f);
    fillLight.setIntensity(0.6f);

    // -------------------------------------------------------------------------
    // Generate procedural normal map from overlapping sine waves.
    // We compute height-field partial derivatives (du, dv) and convert them
    // to tangent-space normals: N = normalize(-du, -dv, 1).
    // -------------------------------------------------------------------------
    const int S = 256;
    Pixels nmap;
    nmap.allocate(S, S, 4, PixelFormat::U8);
    auto* px = static_cast<unsigned char*>(nmap.getDataVoid());
    for (int iy = 0; iy < S; ++iy) {
        for (int ix = 0; ix < S; ++ix) {
            float u = float(ix) / S;
            float v = float(iy) / S;

            // Height field: h(u,v) = sin(6u) * cos(6v) * 0.5 + sin(4(u+v)) * 0.3
            // Partial derivatives of h with respect to u and v:
            float du = cos(u * TAU * 6) * TAU * 6 * cos(v * TAU * 6) * 0.5f
                     + cos((u + v) * TAU * 4) * TAU * 4 * 0.3f;
            float dv = sin(u * TAU * 6) * (-sin(v * TAU * 6)) * TAU * 6 * 0.5f
                     + cos((u + v) * TAU * 4) * TAU * 4 * 0.3f;

            // Convert to tangent-space normal (scale controls bump strength)
            float scale = 0.15f;
            float nx = -du * scale;
            float ny = -dv * scale;
            float nz = 1.0f;
            float len = sqrt(nx*nx + ny*ny + nz*nz);
            nx /= len; ny /= len; nz /= len;

            // Encode normal to RGBA8: [-1,1] -> [0,255]
            int idx = (iy * S + ix) * 4;
            px[idx + 0] = (unsigned char)((nx * 0.5f + 0.5f) * 255);
            px[idx + 1] = (unsigned char)((ny * 0.5f + 0.5f) * 255);
            px[idx + 2] = (unsigned char)((nz * 0.5f + 0.5f) * 255);
            px[idx + 3] = 255;
        }
    }
    normalMapTex.allocate(nmap, TextureUsage::Immutable);

    // Base material: orange-ish dielectric
    matFlat.setBaseColor(0.9f, 0.45f, 0.2f)
           .setMetallic(0.0f)
           .setRoughness(0.35f);

    // Same material but with normal map attached
    matBumped = matFlat;
    matBumped.setNormalMap(&normalMapTex);
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

    // Left sphere: flat (no normal map)
    setMaterial(matFlat);
    pushMatrix();
    translate(-130, 0, 0);
    sphereMesh.draw();
    popMatrix();

    // Right sphere: with normal map — same geometry, but light reacts to
    // the per-pixel normals creating the appearance of surface bumps.
    setMaterial(matBumped);
    pushMatrix();
    translate(130, 0, 0);
    sphereMesh.draw();
    popMatrix();

    clearMaterial();
    cam.end();

    setColor(1.0f);
    drawBitmapString(
        "normalMapExample\n"
        "Left: flat   Right: normal mapped\n"
        "\n"
        "drag: rotate",
        20, 20);
}
