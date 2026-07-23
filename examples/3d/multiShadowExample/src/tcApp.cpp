#include "tcApp.h"

void tcApp::setup() {
    setWindowTitle("multiShadow");

    cam.setDistance(750);
    cam.setTarget(0, 60, 0);
    cam.setElevation(0.45f);
    cam.enableMouseInput();

    // -------------------------------------------------------------------------
    // Scene: ground plane + a few boxes and a sphere
    // -------------------------------------------------------------------------
    floorMesh   = createBox(900, 4, 900);
    boxMesh     = createBox(90, 90, 90);
    tallBoxMesh = createBox(60, 190, 60);
    sphereMesh  = createSphere(55, 32);

    floorMat.setBaseColor(0.55f, 0.55f, 0.55f)
            .setMetallic(0.0f)
            .setRoughness(0.9f);

    boxMat.setBaseColor(0.75f, 0.72f, 0.68f)
          .setMetallic(0.0f)
          .setRoughness(0.6f);

    sphereMat.setBaseColor(0.7f, 0.73f, 0.78f)
             .setMetallic(0.1f)
             .setRoughness(0.35f);

    // -------------------------------------------------------------------------
    // Two shadow-casting spot lights from clearly different directions:
    // warm from the left, cool from the right. Both use enableShadow(), so
    // both cast their own shadow every frame (up to 4 lights are supported).
    // -------------------------------------------------------------------------
    warmLight.setSpot(Vec3(-450, 420, 220), Vec3(1.0f, -1.0f, -0.5f),
                      0.15f, 0.55f);
    warmLight.setDiffuse(1.0f, 0.75f, 0.5f);
    warmLight.setIntensity(9.0f);
    warmLight.setAttenuation(1.0f, 0.0f, 0.0f);
    warmLight.enableShadow(1024);
    warmLight.setShadowBias(2.0f);

    coolLight.setSpot(Vec3(450, 420, 220), Vec3(-1.0f, -1.0f, -0.5f),
                      0.15f, 0.55f);
    coolLight.setDiffuse(0.5f, 0.7f, 1.0f);
    coolLight.setIntensity(9.0f);
    coolLight.setAttenuation(1.0f, 0.0f, 0.0f);
    coolLight.enableShadow(1024);
    coolLight.setShadowBias(2.0f);

    // Soft directional fill so shadowed areas aren't pitch black
    fillLight.setDirectional(Vec3(0.0f, -1.0f, -0.3f));
    fillLight.setDiffuse(0.9f, 0.9f, 0.95f);
    fillLight.setIntensity(0.12f);
}

void tcApp::update() {
    // Slight light movement: both lights sway slowly so the two shadows
    // visibly move independently.
    float t = getElapsedTimef();
    float swayWarm = sin(t * 0.4f) * 120.0f;
    float swayCool = sin(t * 0.4f + 0.25f * TAU) * 120.0f;

    Vec3 warmPos(-450 + swayWarm, 420, 220);
    Vec3 coolPos(450 + swayCool, 420, 220);

    // Aim both lights at the scene center
    warmLight.setSpot(warmPos, Vec3(0, 60, 0) - warmPos, 0.15f, 0.55f);
    coolLight.setSpot(coolPos, Vec3(0, 60, 0) - coolPos, 0.15f, 0.55f);
}

void tcApp::draw() {
    clear(0.04f, 0.04f, 0.06f);

    cam.begin();

    clearLights();
    addLight(warmLight);
    addLight(coolLight);
    addLight(fillLight);
    setCameraPosition(cam.getPosition());

    // -------------------------------------------------------------------------
    // Shadow depth passes — one per shadow-casting light. Each pass renders
    // into its own layer of the shared shadow map array.
    // -------------------------------------------------------------------------
    if (warmShadowOn) {
        beginShadowPass(warmLight);
        drawScene(true);
        endShadowPass();
    }
    if (coolShadowOn) {
        beginShadowPass(coolLight);
        drawScene(true);
        endShadowPass();
    }

    // -------------------------------------------------------------------------
    // Main PBR pass (both shadows are applied automatically)
    // -------------------------------------------------------------------------
    drawScene(false);

    clearMaterial();
    cam.end();

    // 2D overlay
    setColor(1.0f);
    drawBitmapString(
        "multiShadow - two spot lights, two shadow maps\n"
        "1: toggle warm light shadow (" + string(warmShadowOn ? "on" : "off") + ")\n"
        "2: toggle cool light shadow (" + string(coolShadowOn ? "on" : "off") + ")\n"
        "left-drag: rotate camera / scroll: zoom",
        20, 20);
}

void tcApp::keyPressed(int key) {
    if (key == '1') warmShadowOn = !warmShadowOn;
    if (key == '2') coolShadowOn = !coolShadowOn;
}

void tcApp::drawScene(bool shadowPass) {
    // Floor
    if (!shadowPass) setMaterial(floorMat);
    pushMatrix();
    translate(0, -2, 0);
    shadowPass ? shadowDraw(floorMesh) : floorMesh.draw();
    popMatrix();

    // Center tall box
    if (!shadowPass) setMaterial(boxMat);
    pushMatrix();
    translate(0, 94.5f, 0);
    shadowPass ? shadowDraw(tallBoxMesh) : tallBoxMesh.draw();
    popMatrix();

    // Left cube
    pushMatrix();
    translate(-170, 44.5f, 60);
    rotateY(0.1f * TAU);
    shadowPass ? shadowDraw(boxMesh) : boxMesh.draw();
    popMatrix();

    // Right cube
    pushMatrix();
    translate(180, 44.5f, -40);
    rotateY(-0.05f * TAU);
    shadowPass ? shadowDraw(boxMesh) : boxMesh.draw();
    popMatrix();

    // Sphere in front
    if (!shadowPass) setMaterial(sphereMat);
    pushMatrix();
    translate(30, 54.5f, 190);
    shadowPass ? shadowDraw(sphereMesh) : sphereMesh.draw();
    popMatrix();
}
