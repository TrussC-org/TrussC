#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>

namespace {
int checks = 0, failures = 0;
void check(const char* label, bool passed) {
    ++checks;
    if (!passed) ++failures;
    std::printf("%s: %s\n", label, passed ? "PASS" : "FAIL");
}
}

TC_CORE_TEST_MAIN() {
    using trussc::internal::windowClientExtentPixels;
    using trussc::internal::windowOuterExtentPixels;
    // Independent expected client rectangles, before adding the native frame.
    struct SizeCase { unsigned dpi; int physicalWidth, physicalHeight; };
    for (const auto sample : {SizeCase{96, 640, 480}, {144, 960, 720}, {192, 1280, 960}}) {
        const double windowScale = sample.dpi / 96.0;
        check("logical 640x480 maps to the physical client rect",
              windowClientExtentPixels(640, sample.dpi) == sample.physicalWidth &&
              windowClientExtentPixels(480, sample.dpi) == sample.physicalHeight);
        // highDpi=false reports render DPI 1 even on a scaled monitor. Logical
        // requests must still use native window DPI, as the check above does.
        for (bool highDpi : {false, true}) {
            const double renderScale = highDpi ? windowScale : 1.0;
            const int expectedWidth = highDpi ? 640 : sample.physicalWidth;
            const int expectedHeight = highDpi ? 480 : sample.physicalHeight;
            check("pixel-perfect conversion uses render scale then native DPI",
                  windowClientExtentPixels(640 / renderScale, sample.dpi) == expectedWidth &&
                  windowClientExtentPixels(480 / renderScale, sample.dpi) == expectedHeight);
        }
        check("pixel-perfect odd extents survive fractional logical conversion",
              windowClientExtentPixels(641 / windowScale, sample.dpi) == 641 &&
              windowClientExtentPixels(479 / windowScale, sample.dpi) == 479);
    }
    check("fractional client pixels round instead of truncating",
          windowClientExtentPixels(641, 144) == 962 && windowClientExtentPixels(479, 144) == 719);
    check("missing DPI falls back to 96", windowClientExtentPixels(641, 0) == 641);
    // Re-querying the window DPI on every resize must allow 96 -> 192 -> 144.
    check("monitor changes use the new window DPI",
          windowClientExtentPixels(320, 96) == 320 &&
          windowClientExtentPixels(320, 192) == 640 &&
          windowClientExtentPixels(320, 144) == 480);
    const int intLimit = (std::numeric_limits<int>::max)();
    check("zero extents are rejected", windowClientExtentPixels(0, 192) == 0);
    check("negative extents are rejected", windowClientExtentPixels(-1, 192) == 0);
    check("huge extents are rejected",
          windowClientExtentPixels((std::numeric_limits<double>::max)(), 192) == 0);
    check("non-finite extents are rejected",
          windowClientExtentPixels(std::numeric_limits<double>::infinity(), 192) == 0 &&
          windowClientExtentPixels(std::numeric_limits<double>::quiet_NaN(), 192) == 0);
    check("rounded zero pixels are rejected", windowClientExtentPixels(0.1, 96) == 0);
    check("representable client boundary is accepted", windowClientExtentPixels(intLimit, 96) == intLimit);
    check("unrepresentable client boundary is rejected", windowClientExtentPixels(double(intLimit) + 1, 96) == 0);
    check("scaled client boundary is accepted", windowClientExtentPixels(intLimit / 2.0, 192) == intLimit);
    check("scaled client boundary is rejected", windowClientExtentPixels((double(intLimit) + 1) / 2, 192) == 0);
    check("normal frame is added", windowOuterExtentPixels(1280, 16) == 1296);
    check("frame boundary is accepted", windowOuterExtentPixels(intLimit - 16, 16) == intLimit);
    check("frame boundary is rejected", windowOuterExtentPixels(intLimit - 15, 16) == 0);
    check("invalid client cannot become valid through frame addition", windowOuterExtentPixels(0, 16) == 0);
    std::printf("windowDpiSize: %d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
