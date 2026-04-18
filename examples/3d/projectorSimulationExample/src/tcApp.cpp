#include "tcApp.h"

void tcApp::setup() {
    setWindowTitle("projectorSimulation");

    cam.setDistance(800);
    cam.setTarget(0, 150, 0);  // look at wall center (Y-up)
    cam.enableMouseInput();

    // -------------------------------------------------------------------------
    // Scene: folded screen (byoubu) — 3 panels angled inward
    // -------------------------------------------------------------------------
    float panelW = 200.0f;
    float wingW  = 230.0f;  // slightly wider wings so they overlap at the fold
    float panelH = 300.0f;
    float foldAngle = 0.4f;  // radians, each wing folds inward

    screenCenter = createPlane(panelW, panelH, 4, 4);
    screenLeft   = createPlane(wingW, panelH, 4, 4);
    screenRight  = createPlane(wingW, panelH, 4, 4);

    // Floor
    floorMesh = createPlane(600, 600, 1, 1);

    // Materials (dark surfaces to avoid washout from projector)
    wallMat.setBaseColor(0.15f, 0.14f, 0.13f)
           .setMetallic(0.0f)
           .setRoughness(0.7f);

    floorMat.setBaseColor(0.12f, 0.11f, 0.10f)
            .setMetallic(0.0f)
            .setRoughness(0.9f);

    // -------------------------------------------------------------------------
    // Projector: on the ground (Y=0), pointing forward (-Z) toward the screen,
    // with lens shift pushing the image upward onto the wall.
    // -------------------------------------------------------------------------
    float projFov = 0.45f;  // ~26° half-angle → wide enough to cover the screen
    projector.setSpot(
        Vec3(projectorX, 10.0f, projectorZ),
        Vec3(0.0f, 0.0f, -1.0f),
        0.0f, projFov);
    projector.setDiffuse(1.0f, 1.0f, 1.0f);
    projector.setIntensity(10.0f);
    projector.setAttenuation(1.0f, 0.0f, 0.0f);
    projector.setLensShift(0.0f, 1.0f);   // full upward shift: image starts at projector height
    projector.setProjectorAspect(16.0f / 9.0f);
    projector.enableShadow(1024);

    // Dynamic gobo: FBO that we draw into every frame
    goboFbo.allocate(512, 288);  // 16:9
    projector.setProjectionTexture(&goboFbo.getTexture());

    // Ambient: soft fill so the room isn't pitch black
    ambientLight.setDirectional(Vec3(0.0f, -1.0f, 0.2f));
    ambientLight.setDiffuse(0.6f, 0.65f, 0.7f);
    ambientLight.setIntensity(0.15f);

    // IES downlight above the screen
    // Narrow spot IES: bright center, sharp falloff at ~25°
    iesProfile.loadFromString(
        "IESNA:LM-63-2002\n"
        "[TEST] Narrow spot\n"
        "TILT=NONE\n"
        "1 -1 1.0 19 1 1 2 0.0 0.0 0.0\n"
        "1.0 1 100\n"
        "0 5 10 15 20 25 30 35 40 45 50 55 60 65 70 75 80 85 90\n"
        "0\n"
        "1000 980 900 700 400 100 10 0 0 0 0 0 0 0 0 0 0 0 0\n"
    );
    downlight.setSpot(Vec3(0, 350, 100), Vec3(0, -1, 0), 0.0f, QUARTER_TAU);
    downlight.setDiffuse(1.0f, 0.95f, 0.85f);
    downlight.setIntensity(8.0f);
    downlight.setAttenuation(1.0f, 0.0f, 0.0f);
    downlight.setIesProfile(&iesProfile);

    // Simple procedural IBL for ambient reflections
    env.loadProcedural();
    setEnvironment(env);
}

void tcApp::update() {
    // Update projector position from mouse interaction
    projector.setSpot(
        Vec3(projectorX, 10.0f, projectorZ),
        Vec3(0.0f, 0.0f, -1.0f),
        0.0f, 0.45f);
    projector.setLensShift(0.0f, 1.0f);
    projector.setProjectorAspect(16.0f / 9.0f);
    projector.setProjectionTexture(&goboFbo.getTexture());
}

void tcApp::draw() {
    // -------------------------------------------------------------------------
    // 1. Render gobo content into FBO
    // -------------------------------------------------------------------------
    drawGoboContent();

    // -------------------------------------------------------------------------
    // 2. Render 3D scene with projector
    // -------------------------------------------------------------------------
    clear(0.03f, 0.03f, 0.05f);

    cam.begin();

    clearLights();
    addLight(projector);
    addLight(downlight);
    addLight(ambientLight);
    setCameraPosition(cam.getPosition());

    float foldAngle = 0.4f;

    // -------------------------------------------------------------------------
    // 2a. Shadow depth pass (render scene from projector's POV)
    // -------------------------------------------------------------------------
    beginShadowPass(projector);
    drawSceneShadow(foldAngle);
    endShadowPass();

    // -------------------------------------------------------------------------
    // 2b. Main PBR pass (shadow is applied automatically)
    // -------------------------------------------------------------------------
    drawScenePbr(foldAngle);

    clearMaterial();

    cam.end();

    // -------------------------------------------------------------------------
    // 3. 2D overlay
    // -------------------------------------------------------------------------
    setColor(1.0f);
    drawBitmapString(
        "projectorSimulation\n"
        "right-drag: move projector\n"
        "left-drag: rotate camera / scroll: zoom",
        20, 20);

    // Show gobo preview in corner
    setColor(1.0f);
    goboFbo.draw(getWidth() - 200, 10, 190, 107);
}

void tcApp::drawGoboContent() {
    goboFbo.begin(0.0f, 0.0f, 0.0f, 1.0f);

    // Animated color bars + grid
    float t = getElapsedTimef();
    int w = goboFbo.getWidth();
    int h = goboFbo.getHeight();

    // Background gradient
    for (int i = 0; i < 8; ++i) {
        float hue = fmod(float(i) / 8.0f + t * 0.05f, 1.0f);
        Color c = Color::fromHSB(hue, 0.7f, 0.9f);
        setColor(c);
        float x0 = float(i) * w / 8.0f;
        drawRect(x0, 0, float(w) / 8.0f, float(h));
    }

    // White grid overlay
    setColor(1.0f, 1.0f, 1.0f, 0.6f);
    for (int x = 0; x <= w; x += 64) {
        drawLine(float(x), 0, float(x), float(h));
    }
    for (int y = 0; y <= h; y += 36) {
        drawLine(0, float(y), float(w), float(y));
    }

    // Center crosshair
    setColor(1.0f, 1.0f, 1.0f, 0.8f);
    drawLine(float(w) / 2, 0, float(w) / 2, float(h));
    drawLine(0, float(h) / 2, float(w), float(h) / 2);

    goboFbo.end();
}

void tcApp::mousePressed(Vec2 pos, int button) {
    if (button == 1) {  // right button for projector drag
        isDraggingProjector = true;
    }
}

void tcApp::mouseReleased(Vec2 pos, int button) {
    if (button == 1) {
        isDraggingProjector = false;
    }
}

void tcApp::mouseDragged(Vec2 pos, int button) {
    if (isDraggingProjector && button == 1) {
        float normX = (pos.x / getWidth() - 0.5f) * 2.0f;
        float normY = (pos.y / getHeight() - 0.5f) * 2.0f;
        projectorX = normX * 300.0f;
        projectorZ = 200.0f + normY * 300.0f;
    }
}

void tcApp::drawSceneGeometry(float foldAngle, bool shadow) {
    // Floor (Y=0, horizontal)
    if (!shadow) setMaterial(floorMat);
    pushMatrix();
    rotateX(-QUARTER_TAU);
    shadow ? shadowDraw(floorMesh) : floorMesh.draw();
    popMatrix();

    // Screen center panel
    if (!shadow) setMaterial(wallMat);
    pushMatrix();
    translate(0, 150, 0);
    shadow ? shadowDraw(screenCenter) : screenCenter.draw();
    popMatrix();

    // Screen left wing (folded inward)
    pushMatrix();
    translate(-200, 150, 0);
    rotateY(foldAngle);
    shadow ? shadowDraw(screenLeft) : screenLeft.draw();
    popMatrix();

    // Screen right wing (folded inward)
    pushMatrix();
    translate(200, 150, 0);
    rotateY(-foldAngle);
    shadow ? shadowDraw(screenRight) : screenRight.draw();
    popMatrix();

}

void tcApp::drawSceneShadow(float foldAngle) {
    drawSceneGeometry(foldAngle, true);
}

void tcApp::drawScenePbr(float foldAngle) {
    drawSceneGeometry(foldAngle, false);

    // Projector body indicator
    Material projBodyMat = Material::iron();
    setMaterial(projBodyMat);
    pushMatrix();
    translate(projectorX, 10, projectorZ);
    drawBox(30, 20, 40);
    popMatrix();
}

Mesh tcApp::createPlane(float w, float h, int segsW, int segsH) {
    Mesh mesh;
    mesh.setMode(PrimitiveMode::Triangles);

    for (int iy = 0; iy <= segsH; ++iy) {
        for (int ix = 0; ix <= segsW; ++ix) {
            float u = float(ix) / segsW;
            float v = float(iy) / segsH;
            float px = (u - 0.5f) * w;
            float py = (v - 0.5f) * h;
            mesh.addVertex(px, py, 0.0f);
            mesh.addNormal(0, 0, 1);
            mesh.addTexCoord(u, v);
            mesh.addTangent(1, 0, 0, 1);
        }
    }

    for (int iy = 0; iy < segsH; ++iy) {
        for (int ix = 0; ix < segsW; ++ix) {
            int i0 = iy * (segsW + 1) + ix;
            int i1 = i0 + 1;
            int i2 = i0 + (segsW + 1);
            int i3 = i2 + 1;
            mesh.addTriangle(i0, i2, i1);
            mesh.addTriangle(i1, i2, i3);
        }
    }

    return mesh;
}
