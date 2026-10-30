// Fitting creation sizes are checked strictly. On Linux X11, oversized sizes
// skip checks affected by the window manager constraining the first map.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <climits>
#endif
using namespace tc;
namespace {
int failures = 0;
bool pixels = true;
int requestWidth = 1001, requestHeight = 667;
void check(const char* label, bool passed) {
    std::printf("%s: %s\n", label, passed ? "PASS" : "FAIL");
    if (!passed) ++failures;
}
struct UsableArea {
    int width = 0, height = 0;
    const char* source = "X screen";
    bool exceededBy(int w, int h) const { return width > 0 && height > 0 && (w > width || h > height); }
};
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
bool readCardinals(Display* display, ::Window root, const char* name,
                   long offset, unsigned long* values, unsigned long length) {
    const Atom property = XInternAtom(display, name, True);
    Atom type = None;
    int format = 0;
    unsigned long count = 0, remaining = 0;
    unsigned char* data = nullptr;
    // Read a bounded prefix from zero: a malformed short property must not
    // turn the desktop offset into an X11 BadValue protocol error.
    const auto needed = static_cast<unsigned long>(offset) + length;
    // Bound (property shape): scalars must end here; work areas have complete four-CARDINAL records.
    const bool valid = property != None &&
        XGetWindowProperty(display, root, property, 0, long(needed), False, XA_CARDINAL,
                           &type, &format, &count, &remaining, &data) == Success &&
        type == XA_CARDINAL && format == 32 && count == needed && data &&
        (length == 1 ? remaining == 0 : remaining % 16 == 0);
    if (valid) std::memcpy(values, reinterpret_cast<const unsigned long*>(data) + offset, length * sizeof(unsigned long));
    if (data) XFree(data);
    return valid;
}
UsableArea usableArea() {
    auto* display = (Display*)sapp_x11_get_display();
    if (!display) return {}; // Without X11 metadata, retain strict checks.
    UsableArea result{DisplayWidth(display, DefaultScreen(display)), DisplayHeight(display, DefaultScreen(display)), "X screen"};
    unsigned long desktop = 0, area[4]{};
    // Bound (property allocation): read at most 1024 desktops, keeping prefix reads small and offsets representable.
    // Bound (dimension conversion): positive work-area dimensions must fit the int size comparisons.
    if (readCardinals(display, DefaultRootWindow(display), "_NET_CURRENT_DESKTOP", 0, &desktop, 1) &&
        desktop <= 1023 && readCardinals(display, DefaultRootWindow(display), "_NET_WORKAREA", long(desktop * 4), area, 4) &&
        area[2] > 0 && area[2] <= INT_MAX && area[3] > 0 && area[3] <= INT_MAX)
        result = {int(area[2]), int(area[3]), "EWMH work area"};
    return result;
}
#endif
struct SizeEvidence : App {
    int receivedWidth = 0, receivedHeight = 0, resizeCount = 0;
    float receivedDpi = 0, receivedScale = 0;
    void windowResized(int w, int h) override {
        receivedWidth = w; receivedHeight = h; ++resizeCount;
        receivedDpi = tc::getDpiScale();
        receivedScale = internal::appSizeDpi(receivedDpi);
        std::printf("resize role=%s frame=%llu received=%dx%d dpi=%.6g App-scale=%.6g\n",
            "window", (unsigned long long)sapp_frame_count(),
            w, h, receivedDpi, receivedScale);
    }
    void printSizeEvidence(const char* role) {
        const auto& ctx = getWindow()->context();
        std::printf("%s App=%gx%g received=%dx%d received-dpi=%.6g received-scale=%.6g resizes=%d "
            "context-dpi=%.6g context-fb=%dx%d target-fb=%dx%d\n",
            role, double(getWidth()), double(getHeight()), receivedWidth, receivedHeight,
            receivedDpi, receivedScale, resizeCount, ctx.dpiScale, ctx.fbWidth, ctx.fbHeight,
            ctx.swapchainTarget.width, ctx.swapchainTarget.height);
    }
};
struct Secondary : SizeEvidence { bool ready = false; int updates = 0; void setup() override { ready = true; } void update() override { ++updates; } };
struct CreationApp : SizeEvidence {
    std::shared_ptr<tc::Window> secondary;
    int mainWidth = 0, mainHeight = 0, secondWidth = 0, secondHeight = 0;
    int phase = 0, frames = 0, fullscreenUpdate = 0;
    UsableArea area;
    int expectedWidth = 0, expectedHeight = 0;
    bool skipMain = false, skipSecondary = false;
    void skip(const char* label, int w, int h) const {
        std::printf("SKIP: %s: requested native %dx%d exceeds usable area %dx%d (%s); a window manager may constrain the first map before main's setup snapshot or before/after phase 0. Rerun with a size that fits. snapshot=%dx%d live main=%dx%d secondary=%dx%d\n",
            label, expectedWidth, expectedHeight, area.width, area.height, area.source, w, h, sapp_width(), sapp_height(),
            secondary->context().fbWidth, secondary->context().fbHeight);
    }
    bool nonGrowing(int w, int h) const {
        return w > 0 && h > 0 && w <= expectedWidth && h <= expectedHeight;
    }
    bool complete = false;
    void setup() override {
        // Public mode comes from the main WindowSettings, also for secondaries.
        WindowSettings settings;
        settings.setSize(requestWidth, requestHeight).setPixelPerfect(!pixels).setTitle("Creation secondary");
        secondary = createWindow(settings);
        if (secondary) secondary->setPosition(32, 32);
        check("secondary created", bool(secondary));
        if (!secondary) { exitApp(1); return; }
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
        // Keep constructor dimensions before the secondary's first adapter tick.
        secondWidth = secondary->context().fbWidth; secondHeight = secondary->context().fbHeight;
#endif
        secondary->setApp<Secondary>();
        mainWidth = sapp_width(); mainHeight = sapp_height();
    }
    void update() override {
        if (!secondary) return;
        // Bound (hang guard): allow 300 event-pump frames for native/App synchronization; never a speed assertion.
        if (++frames > 300) { check("native size/app synchronization completed", false); exitApp(1); return; }
        auto app = std::static_pointer_cast<Secondary>(secondary->getApp());
        if (!app || !app->ready) return;
        const float scale = pixels ? 1 : sapp_dpi_scale();
        if (phase == 0) {
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
            area = usableArea();
            // Main's first native tick polls dimensions before App setup, so
            // its snapshot may already contain a WM first-map constraint.
            expectedWidth = pixels ? requestWidth : int(requestWidth * sapp_dpi_scale() + 0.5f);
            expectedHeight = pixels ? requestHeight : int(requestHeight * sapp_dpi_scale() + 0.5f);
            skipMain = skipSecondary = area.exceededBy(expectedWidth, expectedHeight);
#else
            secondWidth = secondary->context().fbWidth; secondHeight = secondary->context().fbHeight;
#endif
            std::printf("creation pixelPerfect=%d dpi=%.2f request=%dx%d main=%dx%d secondary=%dx%d\n",
                pixels, sapp_dpi_scale(), requestWidth, requestHeight, mainWidth, mainHeight, secondWidth, secondHeight);
            const bool mainSnapshotShrunk = skipMain && nonGrowing(mainWidth, mainHeight) &&
                (mainWidth < expectedWidth || mainHeight < expectedHeight);
            if (pixels && !Platform::isMacOS()) {
                if (mainSnapshotShrunk) skip("main creation uses requested pixels (first-map shrink before main setup)", mainWidth, mainHeight);
                else check("main creation uses requested pixels", mainWidth == requestWidth && mainHeight == requestHeight);
                check("secondary creation uses requested pixels", secondWidth == requestWidth && secondHeight == requestHeight);
            }
            if (!pixels) {
                const float dpi = sapp_dpi_scale();
                if (mainSnapshotShrunk) {
                    skip("main logical creation keeps DPI-scaled native size (first-map shrink before main setup)", mainWidth, mainHeight);
                    check("secondary logical creation keeps DPI-scaled native size",
                        secondWidth == expectedWidth && secondHeight == expectedHeight);
                } else check("logical creation keeps DPI-scaled native size",
                    mainWidth == int(requestWidth * dpi + 0.5f) && mainHeight == int(requestHeight * dpi + 0.5f) &&
                    secondWidth == mainWidth && secondHeight == mainHeight);
            }
            if (mainSnapshotShrunk && secondWidth == expectedWidth && secondHeight == expectedHeight)
                skip("main and secondary creation agree (first-map shrink before main setup)", mainWidth, mainHeight);
            else check("main and secondary creation agree", mainWidth == secondWidth && mainHeight == secondHeight);
            printSizeEvidence("main"); app->printSizeEvidence("secondary");
            std::printf("live sapp dpi=%.6g framebuffer=%dx%d creation-snapshot=%dx%d\n",
                sapp_dpi_scale(), sapp_width(), sapp_height(), mainWidth, mainHeight);
            if (skipMain) {
                check("main native size does not grow", nonGrowing(sapp_width(), sapp_height()));
                skip("main App receives creation size", mainWidth, mainHeight);
            } else {
                check("main App receives creation size", getWidth() == int(mainWidth / scale) && getHeight() == int(mainHeight / scale));
                if (getWidth() != int(mainWidth / scale) || getHeight() != int(mainHeight / scale))
                    std::printf("main App mismatch: actual=%gx%g expected=%dx%d snapshot=%dx%d scale=%.6g live=%dx%d\n",
                        double(getWidth()), double(getHeight()), int(mainWidth / scale), int(mainHeight / scale),
                        mainWidth, mainHeight, scale, sapp_width(), sapp_height());
            }
            if (skipSecondary) {
                check("secondary native size does not grow", nonGrowing(secondary->context().fbWidth, secondary->context().fbHeight));
                skip("secondary App receives creation size", secondWidth, secondHeight);
            } else {
                check("secondary App receives creation size", app->getWidth() == int(secondWidth / scale) && app->getHeight() == int(secondHeight / scale));
            }
#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__))
            // Exercise real backend intent and public admission on both windows.
            auto main = getWindow();
            for (auto* target : {main, secondary.get()}) {
                const auto* other = target == main ? secondary.get() : main;
                target->toggleFullscreen();
                check("target fullscreen intent is independent", target->isFullscreen() && !other->isFullscreen());
                int notices = 0;
                bool exact = true;
                auto listener = getLogger().onLog.listen([&](LogEventArgs& e) {
                    ++notices;
                    const std::string suffix = " on \"" + target->getTitle() + "\"";
                    const std::string prefix = "[Window] ";
                    const std::string reason = " ignored while fullscreen; call it once isFullscreen() returns false";
                    exact &= e.level == LogLevel::Notice &&
                        (e.message == prefix + "Window::setSize()" + suffix + reason ||
                         e.message == prefix + "Window::setPosition()" + suffix + reason ||
                         e.message == prefix + "App::setSize()" + suffix + reason);
                });
                target->setSize(213, 157);
                target->setPosition(27, 39);
                target->getApp()->setSize(215, 159);
                check("fullscreen geometry emits exact Notices", notices == 3 && exact && target->isFullscreen());
                target->toggleFullscreen();
                check("toggle reverses backend requested state", !target->isFullscreen());
            }
            fullscreenUpdate = app->updates;
            phase = 1;
#else
            setSize(300, 200); app->setSize(300, 200); phase = 2;
#endif
        } else if (phase == 1) {
            if (app->updates == fullscreenUpdate) return;
            if (skipMain || skipSecondary) {
                check("fullscreen native sizes do not grow",
                    nonGrowing(sapp_width(), sapp_height()) && nonGrowing(secondary->context().fbWidth, secondary->context().fbHeight));
                skip("ignored fullscreen requests preserve both native sizes", skipMain ? mainWidth : secondWidth, skipMain ? mainHeight : secondHeight);
            } else {
                check("ignored fullscreen requests preserve both native sizes",
                    sapp_width() == mainWidth && sapp_height() == mainHeight &&
                    secondary->context().fbWidth == secondWidth && secondary->context().fbHeight == secondHeight);
                if (sapp_width() != mainWidth || sapp_height() != mainHeight ||
                    secondary->context().fbWidth != secondWidth || secondary->context().fbHeight != secondHeight) {
                    std::printf("fullscreen size mismatch: main actual=%dx%d expected=%dx%d secondary actual=%dx%d expected=%dx%d\n",
                        sapp_width(), sapp_height(), mainWidth, mainHeight,
                        secondary->context().fbWidth, secondary->context().fbHeight, secondWidth, secondHeight);
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
                    for (auto* target : {getWindow(), secondary.get()}) {
                        const int liveWidth = target == getWindow() ? sapp_width() : secondary->context().fbWidth;
                        const int liveHeight = target == getWindow() ? sapp_height() : secondary->context().fbHeight;
                        const float targetScale = pixels ? 1 : target == getWindow() ? sapp_dpi_scale() : secondary->context().dpiScale;
                        struct RejectedSize { int width, height; };
                        for (const auto rejected : {RejectedSize{213, 157}, RejectedSize{215, 159}}) {
                            const int nativeWidth = int(rejected.width * targetScale), nativeHeight = int(rejected.height * targetScale);
                            std::printf("fullscreen %s live=%dx%d equals rejected setSize(%d,%d) native-target=%dx%d: %s\n",
                                target == getWindow() ? "main" : "secondary", liveWidth, liveHeight,
                                rejected.width, rejected.height, nativeWidth, nativeHeight,
                                liveWidth == nativeWidth && liveHeight == nativeHeight ? "YES" : "no");
                        }
                    }
#endif
                    printSizeEvidence("main"); app->printSizeEvidence("secondary");
                }
            }
            // Change away first, so the final comparison observes a real native resize.
            setSize(300, 200); app->setSize(300, 200); phase = 2;
        } else if (phase == 2) {
            if (getWidth() != 300 || app->getWidth() != 300) return;
            setSize(float(requestWidth), float(requestHeight));
            app->setSize(float(requestWidth), float(requestHeight));
            phase = 3;
        } else {
            // For logical odd requests, preserve the existing creation/resize
            // rounding difference on X11; exact equality is the pixel contract.
            if (pixels) {
                // Oversized main's snapshot may be constrained; explicit later
                // setSize must reach the request, as observed on Pi/labwc.
                if (sapp_width() != (skipMain ? expectedWidth : mainWidth) ||
                    sapp_height() != (skipMain ? expectedHeight : mainHeight) ||
                    secondary->context().fbWidth != (skipSecondary ? expectedWidth : secondWidth) ||
                    secondary->context().fbHeight != (skipSecondary ? expectedHeight : secondHeight)) return;
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
