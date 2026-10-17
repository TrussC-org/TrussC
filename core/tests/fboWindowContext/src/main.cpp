// #653: --gpu-check needs a display (Linux/Xvfb or macOS Debug).
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"

#include <cstdio>
#include <cstring>

using namespace tc;

namespace {
int failures = 0;
bool secondaryCaptured = false;
bool mainCaptured = false;

void check(const char* name, bool ok) {
    std::printf("%s: %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

bool matches(const Color& color, float r, float g, float b) {
    return std::abs(color.r - r) < 0.1f &&
           std::abs(color.g - g) < 0.1f &&
           std::abs(color.b - b) < 0.1f;
}

void drawFrame(Fbo& fbo, bool main) {
    clear(0, 0, 0);
    setColor(main ? Color(0, 1, 1) : Color(1, 0, 0));
    drawRect(8, 16, 16, 32);

    auto& window = internal::currentWindowContext();
    auto* screenTarget = window.currentTarget;
    check(main ? "main target size matches its framebuffer inside draw" :
                 "secondary target size matches its framebuffer inside draw",
          screenTarget->getWidth() == getFramebufferWidth() &&
          screenTarget->getHeight() == getFramebufferHeight() &&
          screenTarget->getWidth() > 0 && screenTarget->getHeight() > 0);
    fbo.begin(0, 0, 0);
    auto* fboTarget = window.currentTarget;
    auto* borrowedResources = &fboTarget->resources();
    check("Fbo owns its dimensions, format, samples and pass state",
          fboTarget != screenTarget && fboTarget->isFbo && fboTarget->inPass &&
          fboTarget->getWidth() == 16 && fboTarget->getHeight() == 16 &&
          fboTarget->colorFormat == SG_PIXELFORMAT_RGBA8 && fboTarget->sampleCount == 1 &&
          fboTarget->hasDepth);
    setColor(1, 1, 1);
    drawCircle(8, 8, 4);
    fbo.end();
    check(main ? "main FBO restores its window context" :
                 "secondary FBO restores its window context",
          sgl_get_context().id == internal::currentWindowContext().swapchainTarget.resources().context.id);

    Fbo other;
    other.allocate(5, 7);
    other.begin(0, 0, 0);
    check("sequential same-format Fbos reuse the released resource lease",
          window.currentTarget != fboTarget &&
          &window.currentTarget->resources() == borrowedResources &&
          window.currentTarget->getWidth() == 5 && fboTarget->getWidth() == 16);
    other.end();
    check("both Fbos return to this window's target", window.currentTarget == screenTarget);

    setColor(main ? Color(1, 0, 1) : Color(0, 1, 0));
    drawCircle(48, 32, 8);
}

void captureFrame(bool main) {
    Pixels pixels;
    const bool captured = grabScreen(pixels) && pixels.isAllocated();
    check(main ? "main window readback succeeds" : "secondary window readback succeeds", captured);
    if (!captured) return;

    // Sample the centers of the shapes drawFrame() draws (rect at 8,16 16x32;
    // circle at 48,32), not fractions of the readback: Windows may widen a
    // small window's client area (64 → 120), which moved those fractions off
    // the shapes.
    const int w = pixels.getWidth();
    const int h = pixels.getHeight();
    check(main ? "main readback covers both shapes" : "secondary readback covers both shapes",
          w > 48 && h > 32);
    if (w <= 48 || h <= 32) return;
    const Color before = pixels.getColor(16, 32);
    const Color after = pixels.getColor(48, 32);
    check(main ? "main 2D before FBO remains visible" : "secondary 2D before FBO remains visible",
          main ? matches(before, 0, 1, 1) : matches(before, 1, 0, 0));
    check(main ? "main 2D after FBO remains visible" : "secondary 2D after FBO remains visible",
          main ? matches(after, 1, 0, 1) : matches(after, 0, 1, 0));

    if (main) {
        // Main shapes are cyan/magenta, so any red/green pixel belongs to
        // the secondary window. Scan the whole frame, including its background.
        bool leaked = false;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const Color c = pixels.getColor(x, y);
                leaked |= matches(c, 1, 0, 0) || matches(c, 0, 1, 0);
            }
        }
        check("secondary shapes do not leak into main window", !leaked);
    }
}

class SecondaryApp : public App {
public:
    void setup() override {
        fbo_.allocate(16, 16);
        after_ = events().afterFrame.listen([] {
            if (secondaryCaptured) return;
            captureFrame(false);
            secondaryCaptured = true;
        });
    }

    void draw() override {
        if (!secondaryCaptured) drawFrame(fbo_, false);
    }

private:
    Fbo fbo_;
    EventListener after_;
};

class MainApp : public App {
public:
    void setup() override {
        fbo_.allocate(16, 16);
        after_ = events().afterFrame.listen([] {
            // Check a main frame after the secondary window has presented.
            if (!secondaryCaptured || mainCaptured) return;
            captureFrame(true);
            mainCaptured = true;
        });
        WindowSettings settings;
        settings.setSize(64, 64);
        settings.setHighDpi(false);
        settings.sampleCount = 1;
        secondary_ = createWindow(settings);
        check("secondary window created", bool(secondary_));
        if (secondary_) secondary_->setApp(std::make_shared<SecondaryApp>());
    }

    void draw() override {
        if (mainCaptured || !secondary_) {
            exitApp();
            return;
        }
        // Count main draws: an occluded secondary may never receive a tick.
        if (++frames_ >= 300) {
            check("secondary window did not present within 300 main frames", false);
            exitApp();
            return;
        }
        drawFrame(fbo_, true);
    }

private:
    Fbo fbo_;
    EventListener after_;
    std::shared_ptr<Window> secondary_;
    int frames_ = 0;
};
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (!internal::supportsSecondaryWindows()) {
        std::printf("SKIP: fboWindowContext single-window platform\n");
        return 0;
    }
    if (argc <= 1 || std::strcmp(argv[1], "--gpu-check") != 0) {
        std::printf("SKIP: fboWindowContext needs --gpu-check and a display\n");
        return 0;
    }
    WindowSettings settings;
    settings.setSize(64, 64);
    settings.setHighDpi(false);
    settings.sampleCount = 1;
    return tcCoreTest::runDisplayTest<MainApp>(settings, failures, [] {
        check("both window readbacks completed", secondaryCaptured && mainCaptured);
    });
}
