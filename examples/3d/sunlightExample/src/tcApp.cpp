#include "tcApp.h"

constexpr float tcApp::softnessPresets[3];

// Tiny deterministic hash so the city layout is the same on every run
static float hash01(int n) {
    n = (n << 13) ^ n;
    n = n * (n * n * 15731 + 789221) + 1376312589;
    return float(n & 0x7fffffff) / float(0x7fffffff);
}

void tcApp::setup() {
    setWindowTitle("sunlight");

    cam.setDistance(950);
    cam.setTarget(0, 40, 0);
    cam.setElevation(0.42f);
    cam.enableMouseInput();

    // -------------------------------------------------------------------------
    // Scene: a small city block. A 5x5 grid of buildings with hashed heights;
    // a couple of cells are left empty as plazas so the shadows have somewhere
    // to fall.
    // -------------------------------------------------------------------------
    floorMesh = createBox(1100, 4, 1100);

    floorMat.setBaseColor(0.42f, 0.42f, 0.44f)
            .setMetallic(0.0f)
            .setRoughness(0.95f);

    buildingMats[0].setBaseColor(0.78f, 0.74f, 0.68f).setRoughness(0.7f);
    buildingMats[1].setBaseColor(0.62f, 0.66f, 0.70f).setRoughness(0.6f);
    buildingMats[2].setBaseColor(0.80f, 0.62f, 0.50f).setRoughness(0.8f);

    const float spacing = 170.0f;
    for (int gx = -2; gx <= 2; gx++) {
        for (int gz = -2; gz <= 2; gz++) {
            int id = (gx + 2) * 5 + (gz + 2);

            // Leave a few plaza cells empty
            if (hash01(id * 7 + 3) < 0.18f) {
                continue;
            }

            float w = 70.0f + hash01(id * 3 + 1) * 50.0f;
            float d = 70.0f + hash01(id * 5 + 2) * 50.0f;
            float h = 60.0f + hash01(id) * hash01(id + 31) * 260.0f;

            buildingMeshes.push_back(createBox(w, h, d));
            buildingPositions.push_back(Vec3(
                gx * spacing + (hash01(id + 11) - 0.5f) * 40.0f,
                h * 0.5f - 0.5f,   // sink slightly to avoid z-fighting
                gz * spacing + (hash01(id + 17) - 0.5f) * 40.0f));
            buildingRotations.push_back((hash01(id + 23) - 0.5f) * 0.04f * TAU);
            buildingMatIndex.push_back(int(hash01(id + 41) * 3.0f) % 3);
        }
    }

    // -------------------------------------------------------------------------
    // The sun: one directional light with an orthographic shadow volume big
    // enough to hold the whole block. Softness 0.9 is the real sun (its 0.53
    // degree angular diameter widens the penumbra by ~0.9 units per 100 units
    // of distance between caster and receiver).
    // -------------------------------------------------------------------------
    sunLight.setDirectional(Vec3(0, -1, 0));
    sunLight.enableShadow(2048);
    sunLight.setShadowArea(Vec3(0, 0, 0), 900);
    sunLight.setShadowBias(2.0f);
    sunLight.setShadowSoftness(softnessPresets[softnessIndex]);
    sunLight.setShadowSamples(24);

    // Dim bluish sky fill so shadows stay readable, never pitch black
    skyLight.setDirectional(Vec3(0.2f, -1.0f, 0.1f));
    skyLight.setDiffuse(0.55f, 0.65f, 0.9f);

    lastTime = getElapsedTimef();
}

void tcApp::update() {
    float now = getElapsedTimef();
    float dt = now - lastTime;
    lastTime = now;

    // One sunrise-to-sunset pass takes 40 seconds, then the next day begins
    const float daySeconds = 40.0f;
    if (!paused) {
        sunPhase = fmod(sunPhase + dt / daySeconds, 1.0f);
    }

    // Sun path: azimuth sweeps east to west while elevation follows a half
    // sine -- low at both ends of the day, high at noon.
    float azimuth = (sunPhase - 0.5f) * 0.44f * TAU;      // -80deg .. +80deg
    float elevation = sin(sunPhase * TAU * 0.5f) * 0.18f * TAU;  // 0 .. 65deg
    Vec3 sunDir(-sin(azimuth) * cos(elevation),
                -sin(elevation),
                -cos(azimuth) * cos(elevation));
    sunLight.setDirectional(sunDir);

    // Warm at the horizon, near-white at noon; fade out near sunrise/sunset
    // so the day wrap is invisible.
    float heightFrac = sin(sunPhase * TAU * 0.5f);        // 0 at ends, 1 at noon
    float warmth = 1.0f - heightFrac;
    Color sunColor(1.0f,
                   0.95f - warmth * 0.40f,
                   0.88f - warmth * 0.58f);
    sunLight.setDiffuse(sunColor);
    sunLight.setIntensity(3.2f * min(heightFrac * 4.0f, 1.0f));

    // Sky fill brightens with the sun
    skyLight.setIntensity(0.10f + heightFrac * 0.16f);
}

void tcApp::draw() {
    // Sky color follows the day: dark warm dawn to pale blue noon
    float heightFrac = sin(sunPhase * TAU * 0.5f);
    clear(0.20f + heightFrac * 0.25f,
          0.16f + heightFrac * 0.42f,
          0.20f + heightFrac * 0.55f);

    cam.begin();

    clearLights();
    addLight(sunLight);
    addLight(skyLight);
    setCameraPosition(cam.getPosition());

    // Shadow depth pass for the sun (orthographic, parallel shadows)
    beginShadowPass(sunLight);
    drawScene(true);
    endShadowPass();

    // Main PBR pass
    drawScene(false);

    clearMaterial();
    cam.end();

    // 2D overlay
    setColor(1.0f);
    string softLabel = softnessIndex == 0 ? "0 (point sun, razor sharp)"
                     : softnessIndex == 1 ? "0.9 (the real sun)"
                     : "4 (huge emitter, very soft)";
    drawBitmapString(
        "sunlight - one directional light, orthographic shadow map\n"
        "s: sun softness = " + softLabel + "\n"
        "SPACE: " + string(paused ? "resume" : "pause") + " the day\n"
        "left-drag: rotate camera / scroll: zoom",
        20, 20);
}

void tcApp::keyPressed(int key) {
    if (key == 's') {
        softnessIndex = (softnessIndex + 1) % 3;
        sunLight.setShadowSoftness(softnessPresets[softnessIndex]);
    }
    if (key == ' ') {
        paused = !paused;
    }
}

void tcApp::drawScene(bool shadowPass) {
    // Floor
    if (!shadowPass) setMaterial(floorMat);
    pushMatrix();
    translate(0, -2, 0);
    shadowPass ? shadowDraw(floorMesh) : floorMesh.draw();
    popMatrix();

    // Buildings
    for (size_t i = 0; i < buildingMeshes.size(); i++) {
        if (!shadowPass) setMaterial(buildingMats[buildingMatIndex[i]]);
        pushMatrix();
        translate(buildingPositions[i]);
        rotateY(buildingRotations[i]);
        shadowPass ? shadowDraw(buildingMeshes[i]) : buildingMeshes[i].draw();
        popMatrix();
    }
}
