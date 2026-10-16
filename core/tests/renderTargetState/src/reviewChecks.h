// Shared verbatim with the 56467c6b probe. Only reviewDrawFields() and
// reviewSwapchainInPass()/reviewSwapchainMetadataSamples() adapt internal fields.
bool reviewDrawn = false;
bool reviewCaptured = false;
EventListener reviewAfterFrame;

void captureReviewBlend() {
    if (!reviewDrawn || reviewCaptured) return;
    Pixels pixels;
    const bool captured = grabScreen(pixels) && pixels.isAllocated();
    check("review blend screen readback succeeds", captured);
    if (captured) {
        const auto alpha = pixels.getColor(16, 32);
        const auto add = pixels.getColor(64, 32);
        auto nearlyEqual = [](float a, float b) { return std::abs(a - b) < 0.015f; };
        check("Fbo end retains the old screen Alpha pipeline despite logical Add (base behavior)",
              nearlyEqual(alpha.r, 0.625f) && nearlyEqual(alpha.g, 0.125f) && nearlyEqual(alpha.b, 0.125f));
        check("repeated setBlendMode(Add) loads the Add pipeline after end (never a no-op)",
              nearlyEqual(add.r, 0.75f) && nearlyEqual(add.g, 0.25f) && nearlyEqual(add.b, 0.25f));
        std::printf("REVIEW blend alpha=%a,%a,%a add=%a,%a,%a\n",
                    double(alpha.r), double(alpha.g), double(alpha.b),
                    double(add.r), double(add.g), double(add.b));
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

    // The CPU stack was window-wide on base while sgl stacks are context-local.
    const auto depth = getDefaultContext().getMatrixStackDepth();
    pushMatrix();
    fbo.begin(0, 0, 0);
    check("CPU push before Fbo begin survives the context switch",
          getDefaultContext().getMatrixStackDepth() == depth + 1);
    popMatrix();
    check("cross-target pop consumes CPU entry and underflows the reset sgl stack (base behavior)",
          getDefaultContext().getMatrixStackDepth() == depth && sgl_error().stack_underflow);
    fbo.end();
    // The public pop consumed the CPU entry in the Fbo context. Remove its
    // matching GPU-only entry left in the screen context to isolate the probe.
    sgl_pop_matrix();

    resetScissor();
    pushScissor(1, 2, 7, 8);
    fbo.begin(0, 0, 0);
    check("scissor push before Fbo begin survives the context switch",
          reviewDrawFields().scissorStack.size() == 1 && reviewDrawFields().currentScissor.active);
    const int commands = sgl_num_commands();
    popScissor();
    check("popScissor inside Fbo consumes the window scope and records full-window reset (base behavior)",
          reviewDrawFields().scissorStack.empty() && !reviewDrawFields().currentScissor.active &&
          sgl_num_commands() == commands + 1 &&
          // The reset rectangle is the window's framebuffer, not the Fbo's.
          getFramebufferWidth() > fbo.getWidth() && getFramebufferHeight() > fbo.getHeight() &&
          fbo.getWidth() == 37 && fbo.getHeight() == 23);
    // beginShadowPass uses only the swapchain flag as its suspension predicate;
    // do not execute its known-invalid nested sg_begin_pass in this check.
    check("active Fbo leaves shadow swapchain suspension predicate false (base behavior)",
          !reviewSwapchainInPass() && internal::currentWindowContext().currentTarget->isFbo);
    fbo.end();
    resetScissor();

    setupScreenOrtho();
    clear(0.25f, 0.25f, 0.25f);
    setBlendMode(BlendMode::Alpha);
    // Exercise both pass states: the preceding checks began with no screen
    // pass, while this Fbo suspends/resumes an active one.
    ensureSwapchainPass();
    fbo.begin(0, 0, 0);
    setBlendMode(BlendMode::Add);
    fbo.end();
    check("Fbo blend mutation carries back to getBlendMode", getBlendMode() == BlendMode::Add);
    setColor(1, 0, 0, 0.5f);
    drawRect(0, 0, 40, 64);
    setBlendMode(BlendMode::Add);
    drawRect(48, 0, 40, 64);
    reviewDrawn = true;
}
