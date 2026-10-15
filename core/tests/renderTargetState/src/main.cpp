#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>

using namespace tc;

namespace {
int failures = 0;

void check(const char* label, bool passed) {
    std::printf("%s: %s\n", label, passed ? "PASS" : "FAIL");
    if (!passed) ++failures;
}

void checkTargetOwnership() {
    internal::WindowContext firstWindow;
    internal::WindowContext secondWindow;
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

void checkLegacyHandoff() {
    internal::RenderTarget windowTarget;
    internal::RenderTarget fboTarget;
    windowTarget.currentMatrix = Mat4::translate(4, 5, 6);
    windowTarget.matrixStack.push_back(Mat4::identity());
    windowTarget.currentBlendMode = BlendMode::Add;
    windowTarget.depthTestEnabled = true;
    windowTarget.currentScissor = {1, 2, 3, 4, true};
    windowTarget.scissorStack.push_back({0, 0, 8, 8, true});
    windowTarget.sglLayerNext = 7;
    fboTarget.inheritDrawState(windowTarget);
    check("Fbo inherits the legacy model, blend, depth and clipping scope",
          fboTarget.currentMatrix.m[3] == 4 && fboTarget.matrixStack.size() == 1 &&
          fboTarget.currentBlendMode == BlendMode::Add && fboTarget.depthTestEnabled &&
          fboTarget.currentScissor.w == 3 && fboTarget.scissorStack.size() == 1);
    fboTarget.currentMatrix = Mat4::translate(8, 9, 10);
    fboTarget.currentBlendMode = BlendMode::Screen;
    fboTarget.matrixStack.push_back(Mat4::identity());
    windowTarget.inheritDrawState(fboTarget);
    check("Fbo mutations still propagate to the outgoing window scope",
          windowTarget.currentMatrix.m[3] == 8 && windowTarget.matrixStack.size() == 2 &&
          windowTarget.currentBlendMode == BlendMode::Screen && windowTarget.sglLayerNext == 7);
}
}

TC_CORE_TEST_MAIN(int, char**) {
    checkTargetOwnership();
    checkLegacyHandoff();
    return failures ? 1 : 0;
}
