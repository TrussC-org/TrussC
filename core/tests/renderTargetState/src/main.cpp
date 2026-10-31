#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <cstdio>
#include <cstring>
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
#include <X11/Xlib.h>
#endif

#include "state.glsl.h"
#include "../../../../addons/tcxLut/src/tcLut.h"
#include "../../../../addons/tcxHap/src/impl/tcxHapDrawData.h"
#include "ycocg.glsl.h"
#if defined(__linux__) && !defined(__ANDROID__)
#include "../../../platform/linux/tcVideoNV12Shader.h"
#endif

using namespace tc;

namespace {
int failures = 0;

void check(const char* label, bool passed) {
    std::printf("%s: %s\n", label, passed ? "PASS" : "FAIL");
    if (!passed) ++failures;
}

bool isStackTargetWarning(const LogEventArgs& e) {
    if (e.level != LogLevel::Warning) return false;
    for (const char* source : {"[RenderContext]", "[Fbo]", "[EasyCam]", "[RenderTarget]", "[sokol_gl]", "[sgl]"})
        if (e.message.rfind(source, 0) == 0) return true;
    return false;
}

struct GpuWarningChecks {
    static constexpr const char* unmatched = "[Fbo] end() discarded unmatched matrix, camera or scissor pushes";
    bool expectingUnmatched = false;
    int expected = 0, unexpected = 0;
    void observe(const LogEventArgs& e) {
        if (!isStackTargetWarning(e)) return;
        if (expectingUnmatched && e.message == unmatched) ++expected;
        else {
            ++unexpected;
            internal::appExitCode() = 1;
        }
    }
} gpuWarnings;

// Survive returning Web/Android loops, App destruction, the Exit stack
// guard and framework graphics cleanup. Late warnings must also raise the
// in-app exit code on platforms whose runApp() never returns at shutdown.
EventListener gpuWarningLog, resizeWarningLog;

void checkTargetOwnership() {
    internal::WindowRequestAccess::HostState firstWindowState; internal::WindowContext& firstWindow = firstWindowState.context;
    internal::WindowRequestAccess::HostState secondWindowState; internal::WindowContext& secondWindow = secondWindowState.context;
    internal::RenderTarget firstFbo;
    internal::RenderTarget secondFbo;
    internal::RenderTargetResources pooled;
    firstFbo.pooledResources = &pooled;
    secondFbo.pooledResources = &pooled;
    firstFbo.width = 13;
    firstFbo.height = 17;
    firstFbo.sglLayerNext = 3;
    firstFbo.deferredPbrDraws.emplace_back();
    secondFbo.width = 29;
    secondFbo.height = 31;
    secondFbo.deferredPointDraws.emplace_back();
    firstWindow.swapchainTarget.deferredShaderDraws.emplace_back();
    check("Fbo targets share only GPU resources",
          &firstFbo.resources() == &secondFbo.resources() &&
          firstFbo.getWidth() == 13 && firstFbo.getHeight() == 17 &&
          secondFbo.getWidth() == 29 && secondFbo.getHeight() == 31 &&
          secondFbo.sglLayerNext == 0 && secondFbo.deferredPbrDraws.empty() &&
          firstFbo.deferredPointDraws.empty());
    check("swapchain queues remain isolated from Fbo and other windows",
          firstWindow.swapchainTarget.deferredShaderDraws.size() == 1 &&
          secondWindow.swapchainTarget.deferredShaderDraws.empty() &&
          firstFbo.deferredShaderDraws.empty());
    check("each window routes to its own swapchain target",
          firstWindow.currentTarget == &firstWindow.swapchainTarget &&
          secondWindow.currentTarget == &secondWindow.swapchainTarget &&
          &firstWindow.swapchainTarget.resources() != &secondWindow.swapchainTarget.resources());
    firstFbo.camera.currentViewW = 13;
    firstFbo.clearAction.colors[0].clear_value = {1, 0, 0, 0.5f};
    firstFbo.currentScissor = {1, 2, 3, 4, true};
    check("camera, clear and scissor state belong to each target",
          secondFbo.camera.currentViewW == 0 && !secondFbo.currentScissor.active &&
          secondFbo.clearAction.colors[0].clear_value.r == 0 &&
          secondFbo.clearAction.colors[0].clear_value.a == 1);
}

void checkLogicalHandoff() {
    internal::RenderTarget windowTarget;
    internal::RenderTarget fboTarget;
    windowTarget.currentMatrix = Mat4::translate(4, 5, 6);
    windowTarget.matrixStack.push_back(Mat4::identity());
    windowTarget.currentBlendMode = BlendMode::Add;
    windowTarget.depthTestEnabled = true;
    windowTarget.currentScissor = {1, 2, 3, 4, true};
    windowTarget.scissorStack.push_back({0, 0, 8, 8, true});
    windowTarget.camera.currentViewMatrix = Mat4::translate(1, 2, 3);
    windowTarget.camera.currentProjectionMatrix = Mat4::scale(2, 3, 4);
    windowTarget.camera.currentScreenFov = TAU / 6;
    windowTarget.camera.currentViewW = 96;
    windowTarget.camera.currentViewH = 64;
    windowTarget.camera.currentCameraDist = 173;
    windowTarget.camera.currentCameraContext = std::make_shared<CameraContext>();
    windowTarget.sglLayerNext = 7;
    fboTarget.inheritLogicalDrawFlags(windowTarget);
    check("only blend/depth flags cross target activation",
          fboTarget.currentBlendMode == BlendMode::Add && fboTarget.depthTestEnabled &&
          fboTarget.matrixStack.empty() && fboTarget.scissorStack.empty() &&
          !fboTarget.currentScissor.active && fboTarget.currentMatrix.m[3] == 0 &&
          fboTarget.camera.currentViewW == 0 && !fboTarget.camera.currentCameraContext &&
          windowTarget.matrixStack.size() == 1 && windowTarget.scissorStack.size() == 1);
    fboTarget.camera.currentViewW = 37;
    fboTarget.currentMatrix = Mat4::translate(8, 9, 10);
    fboTarget.currentBlendMode = BlendMode::Screen;
    fboTarget.matrixStack.push_back(Mat4::identity());
    windowTarget.inheritLogicalDrawFlags(fboTarget);
    check("return handoff retains suspended matrices, stacks and camera",
          windowTarget.currentMatrix.m[3] == 4 && windowTarget.matrixStack.size() == 1 &&
          windowTarget.currentBlendMode == BlendMode::Screen && windowTarget.sglLayerNext == 7 &&
          windowTarget.camera.currentViewW == 96 && windowTarget.scissorStack.size() == 1);

}
void checkCpuCoordinates() {
    tc::Window fixture; internal::WindowRequestAccess::bindForTests(fixture, 101);
    auto& window = fixture.context();
    window.fbWidth = 512; window.fbHeight = 384;
    auto* previous = internal::currentWindowCtx();
    internal::currentWindowCtx() = &window;
    internal::RenderTarget fbo;
    fbo.isFbo = true; fbo.width = 37; fbo.height = 23;
    fbo.camera.currentViewMatrix = Mat4::translate(1, 2, 3);
    fbo.camera.currentProjectionMatrix = Mat4::scale(2, 3, 4);
    fbo.currentMatrix = Mat4::translate(7, 8, 9);
    fbo.selectedPipeline = {internal::PipelineRole::Geometry3D, BlendMode::Alpha, true};
    fbo.cameraStack.push_back({fbo.camera, internal::fillSelection(BlendMode::Alpha, true), &fbo});
    const bool previousMode = internal::pixelPerfectMode();
    for (float dpi : {1.0f, 2.0f}) for (bool pixel : {false, true}) {
        window.dpiScale = dpi; internal::pixelPerfectMode() = pixel;
        window.currentTarget = &fbo;
        RectNode object; object.setSize(11, 13);
        check("headless offscreen getters ignore host DPI and preserve object/window dimensions",
              tc::getWidth() == 37 && tc::getHeight() == 23 && tc::getSize().y == 23 &&
              getViewMatrix().m[3] == 1 && getProjectionMatrix().m[0] == 2 && getMatrix().m[3] == 7 &&
              getFramebufferWidth() == 512 && getWindowWidth() == int(512 / (pixel ? 1 : dpi)) && object.getWidth() == 11);
        window.currentTarget = &window.swapchainTarget;
        check("headless target switching isolates camera/model scopes and selected role",
              getMatrix().m[3] == 0 && window.swapchainTarget.cameraStack.empty() && window.swapchainTarget.selectedPipeline == internal::fillSelection(BlendMode::Alpha) &&
              fbo.cameraStack.size() == 1 && fbo.selectedPipeline == internal::ImmediateSelection{internal::PipelineRole::Geometry3D, BlendMode::Alpha, true} && fbo.cameraStack.back().selectedPipeline == internal::fillSelection(BlendMode::Alpha, true));
    }
    internal::pixelPerfectMode() = previousMode;
    internal::currentWindowCtx() = previous;
    // Finish the synthetic Runtime before disposing its last addressed Window.
    // Its close must not leave a native quit request for the later display run.
    internal::shutdownRuntime();
}
auto& cameraFields() { return internal::currentWindowContext().currentTarget->camera; }
auto& reviewDrawFields() { return *internal::currentWindowContext().currentTarget; }
bool reviewSwapchainInPass() { return internal::currentWindowContext().swapchainTarget.inPass; }
int reviewSwapchainMetadataSamples() { return internal::currentWindowContext().swapchainTarget.sampleCount; }
bool sameMatrix(const Mat4&, const Mat4&);
#include "reviewChecks.h"
#include "matrixChecks.h"
#include "depthSetterChecks.h"
#include "cameraChecks.h"
#include "resizeChecks.h"
}

TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--resize-check") == 0) {
        int samples = 1;
        bool pixelPerfect = false;
        for (int i = 2; i < argc; ++i) {
            if (std::strcmp(argv[i], "--msaa4") == 0) samples = 4;
            if (std::strcmp(argv[i], "--pixel-perfect") == 0) pixelPerfect = true;
        }
        const int result = runResizeStateChecks(samples, pixelPerfect);
        return ResizeStateApp::stackWarnings ? 1 : result;
    }
    checkTargetOwnership();
    checkLogicalHandoff();
    checkCpuCoordinates();
    if (argc > 1 && std::strcmp(argv[1], "--gpu-check") == 0) {
        const int result = runCameraChecks(argc > 2 && std::strcmp(argv[2], "--msaa4") == 0 ? 4 : 1);
        // Blocking loops return after teardown, later than the final callback.
        // Async loops return before drawing: exact-once belongs in that callback.
        return gpuWarnings.unexpected ? 1 : result;
    }
    return failures ? 1 : 0;
}
