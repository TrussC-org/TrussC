// Adversarial review regressions: explicit selection versus saved-role restore.
int depthSetterCaptures = 0;
int depthSetterPending = -1;
EventListener depthSetterAfterFrame;

void captureDepthSetterFixture() {
    if (depthSetterPending < 0) return;
    const bool enabled = (depthSetterPending & 1) == 0;
    Pixels pixels;
    check("depth setter screen readback", grabScreen(pixels));
    if (pixels.isAllocated()) {
        const auto c = pixels.getColor(getFramebufferWidth() / 2, getFramebufferHeight() / 2);
        std::printf("DEPTH SETTER screen easycam=%d enabled=%d center=(%.0f,%.0f,%.0f)\n",
                    depthSetterPending / 2, int(enabled), c.r * 255, c.g * 255, c.b * 255);
        expectPixel(pixels, getFramebufferWidth() / 2, getFramebufferHeight() / 2,
                    enabled ? Color(1, 0, 0) : Color(0, 1, 0), "screen explicit depth setter controls overlap");
    }
    depthSetterPending = -1;
    ++depthSetterCaptures;
}

void drawDepthSetterOverlap(bool easycam, bool enabled) {
    setupScreenOrtho(); resetMatrix(); setBlendMode(BlendMode::Alpha);
    auto& target = internal::currentTarget();
    const auto entryRole = target.selectedPipeline;
    const auto entryCamera = target.camera.currentCameraContext;
    EasyCam camera;
    camera.setDistance(100);
    if (easycam) camera.begin();
    // Begin with the opposite variant, then change it without another blend
    // selection. In EasyCam this also replaces its always-depth-tested role.
    if (enabled) { disableDepthTest(); enableDepthTest(); }
    else { enableDepthTest(); disableDepthTest(); }
    check("explicit depth setter records blend/depth variant immediately",
          target.selectedPipeline == internal::fillSelection(BlendMode::Alpha, enabled) &&
          isDepthTestEnabled() == enabled);
    const float cx = easycam ? 0 : target.camera.currentViewW / 2;
    const float cy = easycam ? 0 : target.camera.currentViewH / 2;
    setColor(1, 0, 0);
    drawTriangle(Vec3(cx - 20, cy - 20, 10), Vec3(cx + 20, cy - 20, 10), Vec3(cx, cy + 20, 10));
    setColor(0, 1, 0);
    drawTriangle(Vec3(cx - 20, cy - 20, -10), Vec3(cx + 20, cy - 20, -10), Vec3(cx, cy + 20, -10));
    if (easycam) {
        camera.end();
        check("camera exit restores saved role while retaining explicit logical depth flag",
              target.selectedPipeline == entryRole && target.camera.currentCameraContext == entryCamera &&
              isDepthTestEnabled() == enabled);
    }
}

void drawDepthSetterFixture(int variant) {
    const bool easycam = variant >= 2;
    const bool enabled = (variant & 1) == 0;
    Fbo fbo; fbo.allocate(96, 64);
    const auto screenRole = internal::currentTarget().selectedPipeline;
    fbo.begin(0, 0, 0);
    drawDepthSetterOverlap(easycam, enabled);
    fbo.end();
    check("Fbo return restores saved screen role after explicit depth setter",
          internal::currentTarget().selectedPipeline == screenRole);
    auto pixels = readTarget(fbo);
    const auto c = pixels.getColor(48, 32);
    std::printf("DEPTH SETTER fbo easycam=%d enabled=%d center=(%.0f,%.0f,%.0f)\n",
                int(easycam), int(enabled), c.r * 255, c.g * 255, c.b * 255);
    expectPixel(pixels, 48, 32, enabled ? Color(1, 0, 0) : Color(0, 1, 0),
                "Fbo explicit depth setter controls overlap");
    clear(0, 0, 0);
    internal::ensureCurrentPass();
    drawDepthSetterOverlap(easycam, enabled);
    depthSetterPending = variant;
}

void runFractionalDpiChecks() {
    auto& window = internal::currentWindowContext();
    auto& target = internal::currentTarget();
    const bool pixel = internal::pixelPerfectMode();
    const float dpi = window.dpiScale;
    const int fbW = window.fbWidth, fbH = window.fbHeight;
    const int width = target.width, height = target.height;
    const auto camera = target.camera;
    const auto role = target.selectedPipeline;
    window.dpiScale = 1.5f;
    window.fbWidth = target.width = 1001; window.fbHeight = target.height = 751;
    for (bool pixelMode : {false, true}) {
        internal::pixelPerfectMode() = pixelMode;
        const float oldW = 1001 / (pixelMode ? 1.0f : 1.5f);
        const float oldH = 751 / (pixelMode ? 1.0f : 1.5f);
        const auto verify = [&](const char* name) {
            const auto& c = target.camera;
            const float aspect = std::abs(c.currentProjectionMatrix.m[5] / c.currentProjectionMatrix.m[0]);
            std::printf("FRACTIONAL DPI %s pixelPerfect=%d view=%.6f,%.6f old=%.6f,%.6f aspect=%.8f old=%.8f\n",
                        name, int(pixelMode), c.currentViewW, c.currentViewH, oldW, oldH, aspect, 1001.0f / 751);
            check("fractional DPI camera retains old float extent and context coordinates",
                  c.currentViewW == oldW && c.currentViewH == oldH &&
                  c.currentCameraContext->viewW == oldW && c.currentCameraContext->viewH == oldH);
            check("fractional DPI projection retains framebuffer aspect", std::abs(aspect - 1001.0f / 751) < 0.000001f);
        };
        EasyCam easy;
        for (bool ortho : {false, true}) {
            easy.setOrtho(ortho); easy.begin(); verify(ortho ? "EasyCam ortho" : "EasyCam perspective"); easy.end();
        }
        // Exercise the deprecated entry point because it has its own setup path.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
        enable3DPerspective();
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        verify("enable3DPerspective");
    }
    window.dpiScale = dpi; window.fbWidth = fbW; window.fbHeight = fbH;
    target.width = width; target.height = height; target.camera = camera; target.selectedPipeline = role;
    internal::pixelPerfectMode() = pixel;
    internal::applyTargetDrawingState();
}
