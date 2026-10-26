#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace tc;
namespace {
int failures = 0;
bool pixels = true;
int requestWidth = 1001, requestHeight = 667;
void check(const char* label, bool passed) {
    std::printf("%s: %s\n", label, passed ? "PASS" : "FAIL");
    if (!passed) ++failures;
}
struct Secondary : App { bool ready = false; void setup() override { ready = true; } };
struct CreationApp : App {
    std::shared_ptr<Window> secondary;
    int mainWidth = 0, mainHeight = 0, secondWidth = 0, secondHeight = 0;
    int phase = 0, frames = 0;
    bool complete = false;
    void setup() override {
        // Public mode comes from the main WindowSettings, also for secondaries.
        WindowSettings settings;
        settings.setSize(requestWidth, requestHeight).setPixelPerfect(!pixels);
        secondary = createWindow(settings);
        check("secondary created", bool(secondary));
        if (!secondary) { exitApp(1); return; }
        secondary->setApp(std::make_shared<Secondary>());
        mainWidth = sapp_width(); mainHeight = sapp_height();
    }
    void update() override {
        if (!secondary) return;
        // Bounded event pumping: frame count is a hang guard, never a speed assertion.
        if (++frames > 300) { check("native size/app synchronization completed", false); exitApp(1); return; }
        auto app = std::static_pointer_cast<Secondary>(secondary->getApp());
        if (!app || !app->ready) return;
        const float scale = pixels ? 1 : sapp_dpi_scale();
        if (phase == 0) {
            secondWidth = secondary->context().fbWidth; secondHeight = secondary->context().fbHeight;
            std::printf("creation pixelPerfect=%d dpi=%.2f request=%dx%d main=%dx%d secondary=%dx%d\n",
                pixels, sapp_dpi_scale(), requestWidth, requestHeight, mainWidth, mainHeight, secondWidth, secondHeight);
            if (pixels && !Platform::isMacOS()) {
                check("main creation uses requested pixels", mainWidth == requestWidth && mainHeight == requestHeight);
                check("secondary creation uses requested pixels", secondWidth == requestWidth && secondHeight == requestHeight);
            }
            if (!pixels) {
                const float dpi = sapp_dpi_scale();
                check("logical creation keeps DPI-scaled native size",
                    mainWidth == int(requestWidth * dpi + 0.5f) && mainHeight == int(requestHeight * dpi + 0.5f) &&
                    secondWidth == mainWidth && secondHeight == mainHeight);
            }
            check("main and secondary creation agree", mainWidth == secondWidth && mainHeight == secondHeight);
            check("main App receives creation size", getWidth() == int(mainWidth / scale) && getHeight() == int(mainHeight / scale));
            check("secondary App receives creation size", app->getWidth() == int(secondWidth / scale) && app->getHeight() == int(secondHeight / scale));
            // Change away first, so the final comparison observes a real native resize.
            setSize(300, 200); app->setSize(300, 200); phase = 1;
        } else if (phase == 1) {
            if (getWidth() != 300 || app->getWidth() != 300) return;
            setSize(float(requestWidth), float(requestHeight));
            app->setSize(float(requestWidth), float(requestHeight));
            phase = 2;
        } else {
            // For logical odd requests, preserve the existing creation/resize
            // rounding difference on X11; exact equality is the pixel contract.
            if (pixels) {
                if (sapp_width() != mainWidth || sapp_height() != mainHeight ||
                    secondary->context().fbWidth != secondWidth || secondary->context().fbHeight != secondHeight) return;
                check("creation equals setSize on both windows", true);
            } else {
                const float dpi = sapp_dpi_scale();
                const int expected = Platform::isLinux() ? int(int(float(requestWidth) * dpi) / dpi) : requestWidth;
                const int expectedHeight = Platform::isLinux() ? int(int(float(requestHeight) * dpi) / dpi) : requestHeight;
                if (getWidth() != expected || app->getWidth() != expected ||
                    getHeight() != expectedHeight || app->getHeight() != expectedHeight) return;
                check("logical requests retain App units", true);
            }
            complete = true; exitApp(failures ? 1 : 0);
        }
    }
    void cleanup() override { check("display probe completed", complete); }
};
}
TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc < 2 || std::strcmp(argv[1], "--display") != 0) {
        std::puts("SKIP: windowCreationSize --display [pixels|logical] [width height] requires a display");
        return 0;
    }
    pixels = argc < 3 || std::strcmp(argv[2], "logical") != 0;
    if (argc >= 5) { requestWidth = std::atoi(argv[3]); requestHeight = std::atoi(argv[4]); }
    WindowSettings settings;
    settings.setSize(requestWidth, requestHeight).setPixelPerfect(pixels);
    return tcCoreTest::runDisplayTest<CreationApp>(settings, failures);
}
