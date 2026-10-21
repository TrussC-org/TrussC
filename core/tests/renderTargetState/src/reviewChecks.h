// Returned logical blend is immediately effective for lazy and active parents.
bool reviewDrawn = false;
bool reviewCaptured = false;
EventListener reviewAfterFrame;

void captureReviewBlend() {
    if (!reviewDrawn || reviewCaptured) return;
    Pixels pixels;
    const bool captured = grabScreen(pixels) && pixels.isAllocated();
    check("review blend screen readback succeeds", captured);
    const float scale = internal::targetPixelScale();
    if (captured) {
        const auto alpha = pixels.getColor(int(16*scale), int(32*scale));
        const auto add = pixels.getColor(int(64*scale), int(32*scale));
        auto nearlyEqual = [](float a, float b) { return std::abs(a - b) < 0.015f; };
        check("Fbo end immediately reconciles the returned Add selection",
              nearlyEqual(alpha.r, 0.75f) && nearlyEqual(alpha.g, 0.25f) && nearlyEqual(alpha.b, 0.25f));
        check("repeated setBlendMode(Add) loads the Add pipeline after end (never a no-op)",
              nearlyEqual(add.r, 0.75f) && nearlyEqual(add.g, 0.25f) && nearlyEqual(add.b, 0.25f));
        std::printf("REVIEW blend alpha=%a,%a,%a add=%a,%a,%a\n",
                    double(alpha.r), double(alpha.g), double(alpha.b),
                    double(add.r), double(add.g), double(add.b));
    }
    if (captured) {
        const auto alpha = pixels.getColor(int(16*scale), int(112*scale));
        const auto add = pixels.getColor(int(64*scale), int(112*scale));
        check("active-pass return applies Add before and after explicit setter",
              std::abs(alpha.r - 0.75f) < 0.015f && std::abs(alpha.g - 0.25f) < 0.015f &&
              std::abs(add.r - 0.75f) < 0.015f && std::abs(add.g - 0.25f) < 0.015f);
    }
    reviewCaptured = true;
}

void runReviewChecks(Fbo& fbo) {
    const auto format = internal::swapchainTargetFormat(internal::currentWindowContext());
    check("swapchain pipeline sample count equals the real positive window count",
          format.sampleCount == sapp_sample_count() && format.sampleCount > 0 &&
          reviewSwapchainMetadataSamples() == sapp_sample_count() &&
          sg_query_desc().environment.defaults.sample_count == sapp_sample_count());
    std::printf("REVIEW swapchain samples=%d environment=%d\n", format.sampleCount,
                sg_query_desc().environment.defaults.sample_count);

    const auto depth = getDefaultContext().getMatrixStackDepth();
    pushMatrix(); translate(4, 5);
    const auto screenModel = getMatrix();
    const auto floor = getDefaultContext().setStackFloor(depth + 1, getDefaultContext().getStyleStackDepth(), &typeid(App));
    fbo.begin(0, 0, 0);
    check("Fbo starts with an independent model stack", getDefaultContext().getMatrixStackDepth() == 0 && sameMatrix(getMatrix(), Mat4::identity()));
    // The deliberately invalid empty-target pop is covered by the dummy
    // dispatch probe with an explicit warning count. Keep this GPU path clean.
    pushMatrix(); translate(2, 3); popMatrix();
    check("screen Node floor does not block Fbo-local pops", getDefaultContext().getMatrixStackDepth() == 0 && sameMatrix(getMatrix(), Mat4::identity()));
    fbo.end();
    check("screen model and stack survive return", getDefaultContext().getMatrixStackDepth() == depth + 1 && sameMatrix(getMatrix(), screenModel));
    getDefaultContext().setStackFloor(floor);
    popMatrix();

    resetScissor();
    pushScissor(1, 2, 7, 8);
    fbo.begin(0, 0, 0);
    check("Fbo starts with independent clipping", reviewDrawFields().scissorStack.empty() && !reviewDrawFields().currentScissor.active);
    pushScissor(1, 1, 3, 3); popScissor(); popScissor();
    check("Fbo clip reset uses its attachment extent", reviewDrawFields().scissorStack.empty() && !reviewDrawFields().currentScissor.active &&
          internal::currentTarget().width == 37 && internal::currentTarget().height == 23);
    Light shadowLight;
    shadowLight.setDirectional(0, -1, 0);
    shadowLight.enableShadow(32);
    addLight(shadowLight);
    auto* parentTarget = &internal::currentTarget();
    beginShadowPass(shadowLight);
    check("public shadow pass nests above Fbo", internal::currentWindowContext().shadow.inPass &&
          &internal::currentTarget() != parentTarget);
    endShadowPass();
    check("public shadow pass restores Fbo and its clip", &internal::currentTarget() == parentTarget &&
          internal::currentTarget().inPass && !internal::currentTarget().currentScissor.active);
    removeLight(shadowLight);
    fbo.end();
    check("screen scissor scope survives return", reviewDrawFields().scissorStack.size() == 1 && reviewDrawFields().currentScissor.active);
    popScissor();

    setupScreenOrtho();
    clear(0.25f, 0.25f, 0.25f);
    setBlendMode(BlendMode::Alpha);
    // First return without an active screen pass, then suspend/resume one.
    for (int row = 0; row < 2; ++row) {
        setBlendMode(BlendMode::Alpha);
        if (row) internal::ensureCurrentPass();
        fbo.begin(0, 0, 0);
        setBlendMode(BlendMode::Add);
        fbo.end();
        check("Fbo blend mutation carries back to getBlendMode", getBlendMode() == BlendMode::Add);
        setColor(1, 0, 0, 0.5f);
        drawRect(0, float(row * 80), 40, 64);
        setBlendMode(BlendMode::Add);
        drawRect(48, float(row * 80), 40, 64);
    }
    reviewDrawn = true;
}
