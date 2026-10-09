// #653: --gpu-check needs a display (Linux/Xvfb or macOS Debug).
#include <TrussC.h>
#include "../../common/tcCoreTest.h"

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

    fbo.begin(0, 0, 0);
    setColor(1, 1, 1);
    drawCircle(8, 8, 4);
    fbo.end();
    check(main ? "main FBO restores its window context" :
                 "secondary FBO restores its window context",
          sgl_get_context().id == internal::currentWindowContext().swapchainTarget.context.id);

    setColor(main ? Color(1, 0, 1) : Color(0, 1, 0));
    drawCircle(48, 32, 8);
}

void captureFrame(bool main) {
    Pixels pixels;
    const bool captured = grabScreen(pixels) && pixels.isAllocated();
    check(main ? "main window readback succeeds" : "secondary window readback succeeds", captured);
    if (!captured) return;

    const int w = pixels.getWidth();
    const int h = pixels.getHeight();
    const Color before = pixels.getColor(w / 4, h / 2);
    const Color after = pixels.getColor(3 * w / 4, h / 2);
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
        drawFrame(fbo_, true);
    }

private:
    Fbo fbo_;
    EventListener after_;
    std::shared_ptr<Window> secondary_;
};
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc <= 1 || std::strcmp(argv[1], "--gpu-check") != 0) {
        std::printf("SKIP: fboWindowContext needs --gpu-check and a display\n");
        return 0;
    }
    WindowSettings settings;
    settings.setSize(64, 64);
    settings.setHighDpi(false);
    settings.sampleCount = 1;
    runApp<MainApp>(settings);
    check("both window readbacks completed", secondaryCaptured && mainCaptured);
    return failures ? 1 : 0;
}
