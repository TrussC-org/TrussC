// #915: style and Node drawing must use the camera's target-local depth flag.
void drawCameraDepthRect(Color color, float z) {
    setColor(color);
    pushMatrix(); translate(0, 0, z); drawRect(-24, -24, 48, 48); popMatrix();
}
class CameraDepthChild : public Node {
    void draw() override { drawCameraDepthRect(Color(0, 0, 1), -10); }
};

void runEasyCamDepthChecks() {
    const char* names[] = {"alpha", "popStyle", "resetStyle", "childNode", "disabled",
                           "restoreOff", "restoreOn", "nested", "fboInside", "add"};
    for (int variant = 0; variant < 10; ++variant) {
        Fbo target, innerTarget; target.allocate(96, 96);
        target.begin(0.125f, 0.125f, 0.125f);
        resetStyle(); resetMatrix(); disableDepthTest();
        const bool entryDepth = variant == 6;
        if (entryDepth) enableDepthTest();
        if (variant == 9) setBlendMode(BlendMode::Add);
        EasyCam camera; camera.setDistance(100); camera.enableOrtho();
        camera.begin();
        check("camera begin enables logical depth", isDepthTestEnabled());
        check("camera begin keeps current blend mode",
              internal::currentTarget().selectedPipeline == internal::fillSelection(getBlendMode(), true));
        drawCameraDepthRect(Color(1, 0, 0), 10);
        if (variant == 1) { pushStyle(); setBlendMode(BlendMode::Add); popStyle(); }
        if (variant == 2) resetStyle();
        if (variant == 3) {
            auto parent = internal::makeConstructedApp<App>();
            parent->addChild(std::make_shared<CameraDepthChild>());
            parent->handleDraw();
        } else {
            if (variant == 4 || variant == 6) disableDepthTest();
            if (variant == 4) {
                pushStyle(); popStyle(); resetStyle();
                check("explicit disable survives style changes until end", !isDepthTestEnabled());
            }
            if (variant == 7) {
                disableDepthTest();
                EasyCam inner; inner.setDistance(100); inner.enableOrtho(); inner.begin();
                check("nested camera enables depth", isDepthTestEnabled());
                drawCameraDepthRect(Color(0, 0, 1), -10);
                inner.end();
                check("nested camera restores outer disabled flag", !isDepthTestEnabled());
                enableDepthTest();
            }
            if (variant == 8) {
                innerTarget.allocate(32, 32); innerTarget.begin(0, 0, 0);
                check("Fbo inherits camera depth with its own camera stack",
                      isDepthTestEnabled() && internal::currentTarget().cameraStack.empty());
                EasyCam inner; inner.setDistance(100); inner.enableOrtho(); inner.begin();
                drawCameraDepthRect(Color(1, 0, 0), 10);
                disableDepthTest();
                drawCameraDepthRect(Color(0, 0, 1), -10);
                inner.end();
                check("Fbo camera restores its own saved depth", isDepthTestEnabled());
                innerTarget.end();
                check("outer camera remains active after Fbo", isDepthTestEnabled() &&
                      internal::currentTarget().cameraStack.back().owner == &camera);
            }
            drawCameraDepthRect(Color(0, 0, 1), variant == 9 ? 10 : -10);
        }
        const auto insideCamera = internal::currentTarget().camera;
        camera.end();
        check("camera end restores entry depth flag", isDepthTestEnabled() == entryDepth);
        // Reselect through the flag after end, then check effective GPU behavior.
        if (variant == 5 || variant == 6) {
            resetStyle();
            // Isolate the restored flag from the projection change at end().
            internal::currentTarget().camera = insideCamera;
            internal::applyTargetDrawingState();
            drawCameraDepthRect(Color(0, 1, 0), -20);
        }
        target.end();
        if (variant == 8)
            expectPixel(readTarget(innerTarget), 16, 16, Color(0, 0, 1),
                        "Fbo inside camera uses its own explicit depth setting");
        auto pixels = readTarget(target);
        Color expected(1, 0, 0);
        if (variant == 4) expected = Color(0, 0, 1);
        if (variant == 5) expected = Color(0, 1, 0);
        if (variant == 6) expected = Color(0, 0, 1); // disabled inside, restored on at exit
        if (variant == 9) expected = Color(1, 0.125f, 1);
        std::printf("EASYCAM DEPTH %s\n", names[variant]);
        expectPixel(pixels, 48, 48, expected, names[variant]);
        if (const char* directory = std::getenv("TC_EASYCAM_IMAGES")) {
            fs::create_directories(directory);
            check("save camera comparison image", pixels.save(fs::path(directory) / (std::string(names[variant]) + ".png")).ok());
        }
    }
    disableDepthTest(); resetStyle();
}
