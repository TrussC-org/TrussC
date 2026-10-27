// Production sizing statements injected by test_window_pixel_size.py.
#include "core/include/sokol/util/sokol_window_size.h"
#include <cassert>
#include <cmath>
#include <initializer_list>
#include <cstring>
#include <limits>
using LONG = int;
struct Size {
    int x, y;
    Size(int width = 0, int height = 0) : x(width), y(height) {}
    Size(bool accepted) : x(0), y(0) { assert(!accepted); }
};
struct Descriptor {
    int width = 0, height = 0, x = 0, y = 0, sample_count = 1;
    int _tc_pixel_width = 0, _tc_pixel_height = 0;
    bool high_dpi = true, no_high_dpi = false;
};
using sapp_desc = Descriptor;
using sapp_window_desc = Descriptor;
struct Settings { int width = 0, height = 0; bool pixelPerfect = false, highDpi = true; };
struct Platform { static constexpr bool isDesktop() { return true; } };
float displayScale = 1, renderScale = 1, preWindowScale = 1;
bool enabled = false;
bool pixelPerfectMode() { return enabled; }
// A pre-window/main-display query can differ from the target window.
float getDisplayScaleFactor() { return preWindowScale; }
int _sapp_tc_roundf_gzero(float value) { return int(value + 0.5f); }
struct State { Descriptor desc; float window_scale = 1, dpi_scale = 1; int xwin = 1, fb_width = 0, fb_height = 0, win_width = 0, win_height = 0; };
struct { void* display = nullptr; } _sapp_tc;
Size roundMac(double width, double height) { return {int(std::ceil(width) * renderScale), int(std::ceil(height) * renderScale)}; }
namespace internal {
Descriptor buildWindowDescriptor(const Settings& settings) {
    LAUNCHER
    return desc;
}
}
using internal::buildWindowDescriptor;
Descriptor mainDescriptor(const Settings& settings) { NORMAL_LAUNCHER }
Descriptor hotDescriptor(const Settings& settings) { HOT_LAUNCHER return desc; }
Descriptor secondaryDescriptor(const Settings& settings) {
    sapp_window_desc d{};
    SECONDARY
    return d;
}
Descriptor mainTransfer(Descriptor descriptor) {
    const Descriptor* d = &descriptor;
    State state; State* w = &state;
    TRANSFER
    return w->desc;
}
Size create(Descriptor descriptor, bool mainWindow) {
    State state{descriptor, displayScale, renderScale}; State* w = &state;
    [[maybe_unused]] const Descriptor* d = &descriptor;
    [[maybe_unused]] const int width_pt = descriptor.width, height_pt = descriptor.height;
#ifdef TEST_MAC
    const int width = descriptor.width, height = descriptor.height;
    const float scale = renderScale;
#endif
    (void)mainWindow; (void)w;
    CREATION_GUARD
    ARITHMETIC
}
#if defined(TEST_X11) || defined(TEST_EGL)
using _sapp_tc_window_t = State;
struct XWindowAttributes { int width, height; };
Size nativeSize;
void XGetWindowAttributes(void*, int, XWindowAttributes* attributes) {
    attributes->width = nativeSize.x; attributes->height = nativeSize.y;
}
X11_DIMENSIONS
#endif
int main() {
    for (bool pixels : {false, true}) for (bool highDpi : {true, false})
    for (float dpi : {1.5f, 2.0f}) for (Size requested : {Size{640,400}, Size{480,320}, Size{1001,667}, Size{1,3}})
    for (bool mainWindow : {false, true}) {
        displayScale = dpi;
        // 640x400 isolates the old integer truncation at the correct scale;
        // other requests also detect using a different pre-window display DPI.
        preWindowScale = requested.x == 640 ? dpi : 1.25f;
        renderScale = highDpi ? dpi : 1;
        enabled = pixels;
        const Settings settings{requested.x, requested.y, pixels, highDpi};
        const auto normal = mainDescriptor(settings), hot = hotDescriptor(settings);
        assert(normal.width == hot.width && normal.height == hot.height &&
               normal._tc_pixel_width == hot._tc_pixel_width && normal._tc_pixel_height == hot._tc_pixel_height);
        Descriptor d = mainWindow ? mainTransfer(mainDescriptor(settings)) : secondaryDescriptor(settings);
        const Size actual = create(d, mainWindow);
#if defined(TEST_X11) || defined(TEST_EGL)
        nativeSize = actual;
        State reported{d, displayScale, renderScale};
        assert(_sapp_tc_x11_update_dimensions(&reported));
        assert(reported.fb_width == actual.x && reported.fb_height == actual.y);
#endif
        // Pixel requests match setSize's fractional rule. With pixels disabled,
        // preserve creation's native rounding (X11 logical setSize truncates).
        const double pointsX = pixels ? double(requested.x) / renderScale : requested.x;
        const double pointsY = pixels ? double(requested.y) / renderScale : requested.y;
#ifdef TEST_MAC
        const Size expectedNative = roundMac(pointsX, pointsY);
#else
        const float nativeScale =
#if defined(TEST_X11) || defined(TEST_EGL)
            renderScale;
#else
            displayScale;
#endif
        const Size expectedNative{int(pointsX * nativeScale + 0.5), int(pointsY * nativeScale + 0.5)};
#endif
        assert(actual.x == expectedNative.x && actual.y == expectedNative.y);
        if (pixels) {
            assert(d._tc_pixel_width == requested.x && d._tc_pixel_height == requested.y);
#ifdef TEST_MAC
            // AppKit rounds content sizes to whole points; odd pixels may be unreachable.
            assert(pointsX == double(requested.x) / renderScale);
#else
            if (highDpi) assert(actual.x == requested.x && actual.y == requested.y);
#endif
        } else {
            assert(d._tc_pixel_width == 0 && d._tc_pixel_height == 0);
        }
    }
#ifdef TEST_X11
    // The real dimension reporter must never see the rejected secondary's pixels.
    // A high-DPI-disabled main can coexist with this secondary at the same Xft.dpi.
    enabled = true; displayScale = preWindowScale = 1e-8f; renderScale = 1;
    const Settings mainSettings{640,400,true,false}, secondarySettings{640,400,true,true};
    const auto main = mainTransfer(mainDescriptor(mainSettings));
    nativeSize = create(main, true);
    State mainState{main,displayScale,renderScale};
    _sapp_tc_x11_update_dimensions(&mainState);
    assert(mainState.win_width == 640 && mainState.win_height == 400);
    renderScale = displayScale;
    const auto secondary = secondaryDescriptor(secondarySettings);
    nativeSize = create(secondary, false);
    if (nativeSize.x > 0 && nativeSize.y > 0) {
        State secondaryState{secondary,displayScale,renderScale};
        _sapp_tc_x11_update_dimensions(&secondaryState);
    }
    assert(nativeSize.x == 0 && nativeSize.y == 0);
#endif
    enabled = true; renderScale = displayScale = 1e-8f;
    const Settings tiny{640,400,true,true};
    for (bool mainWindow : {false, true}) {
        const auto d = mainWindow ? mainTransfer(mainDescriptor(tiny)) : secondaryDescriptor(tiny);
        const Size result = create(d, mainWindow);
        assert(result.x == 0 && result.y == 0);
    }
    // Match the resize logical-getter limit, per dimension, on all native backends.
    assert(!_sapp_tc_window_pixels_representable(640,400,1e-8f));
    assert(!_sapp_tc_window_pixels_representable(640,400,1e-30f));
    assert(!_sapp_tc_window_pixels_representable(640,1,1e-8f));
    assert(!_sapp_tc_window_pixels_representable(1,400,1e-8f));
    assert(_sapp_tc_window_pixels_representable(0,-1,1e-8f));
    assert(_sapp_tc_window_pixels_representable(640,400,1));
    for (float invalid : {0.f, -1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
        assert(_sapp_tc_window_points(0, 1001, invalid) == 1001);
    for (double extent : {0., -1., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        const double actual = _sapp_tc_window_points(extent, 0, 2);
        assert(actual == extent || (std::isnan(actual) && std::isnan(extent)));
    }
    // Non-positive requests retain native default selection; no pixel override.
    enabled = true;
    for (int extent : {0, -1}) {
        const Settings settings{extent,extent,true,true};
        for (Descriptor d : {mainDescriptor(settings), secondaryDescriptor(settings)})
            assert(d._tc_pixel_width == 0 && d._tc_pixel_height == 0);
    }
}
