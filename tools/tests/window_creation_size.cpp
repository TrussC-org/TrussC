// Production sizing statements injected by test_window_pixel_size.py.
#include "core/include/sokol/util/sokol_window_size.h"
#include <cassert>
#include <cmath>
#include <initializer_list>
#include <limits>
using LONG = int;
struct Size { int x, y; };
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
struct State { Descriptor desc; float window_scale = 1, dpi_scale = 1; };
struct EglState { Descriptor desc; int window_width = 0, window_height = 0; float dpi_scale = 1; int framebuffer_width = 0, framebuffer_height = 0; bool first_frame = true; struct { float dpi = 96; } x11; } _sapp_tc;
#ifdef TEST_EGL
#define _SOKOL_PRIVATE
constexpr int SAPP_EVENTTYPE_RESIZED = 1;
void _sapp_tc_x11_app_event(int) {}
EGL_DIMENSIONS
#endif
Size roundMac(double width, double height) { return {int(std::ceil(width) * renderScale), int(std::ceil(height) * renderScale)}; }
Descriptor mainDescriptor(const Settings& settings) {
    LAUNCHER
    return desc;
}
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
    _sapp_tc.desc = descriptor; _sapp_tc.window_width = descriptor.width; _sapp_tc.window_height = descriptor.height;
    _sapp_tc.dpi_scale = renderScale; _sapp_tc.x11.dpi = displayScale * 96;
    ARITHMETIC
}
int main() {
    for (bool pixels : {false, true}) for (bool highDpi : {true, false})
    for (float dpi : {1.5f, 2.0f}) for (Size requested : {Size{640,400}, Size{480,320}, Size{1001,667}, Size{1,3}})
    for (bool mainWindow : {false, true}) {
#ifdef TEST_EGL
        if (!mainWindow) continue; // secondary windows are unsupported on EGL
#endif
        displayScale = dpi;
        // 640x400 isolates the old integer truncation at the correct scale;
        // other requests also detect using a different pre-window display DPI.
        preWindowScale = requested.x == 640 ? dpi : 1.25f;
        renderScale = highDpi ? dpi : 1;
#ifdef TEST_EGL
        renderScale = dpi; // EGL's existing native scale reporting ignores high_dpi.
#endif
        enabled = pixels;
        const Settings settings{requested.x, requested.y, pixels, highDpi};
        Descriptor d = mainWindow ? mainTransfer(mainDescriptor(settings)) : secondaryDescriptor(settings);
        const Size actual = create(d, mainWindow);
#ifdef TEST_EGL
        _sapp_tc_x11_update_dimensions(actual.x, actual.y);
        assert(_sapp_tc.framebuffer_width == actual.x && _sapp_tc.framebuffer_height == actual.y);
#endif
        // Pixel requests match setSize's fractional rule. With pixels disabled,
        // preserve creation's native rounding (X11 logical setSize truncates).
        const double pointsX = pixels ? double(requested.x) / renderScale : requested.x;
        const double pointsY = pixels ? double(requested.y) / renderScale : requested.y;
#ifdef TEST_MAC
        const Size expectedNative = roundMac(pointsX, pointsY);
#else
        const float nativeScale =
#ifdef TEST_X11
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
    for (float invalid : {0.f, -1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
        assert(_sapp_tc_window_points(0, 1001, invalid) == 1001);
    // Non-positive requests retain native default selection; no pixel override.
    enabled = true;
    for (int extent : {0, -1}) {
        const Settings settings{extent,extent,true,true};
        for (Descriptor d : {mainDescriptor(settings), secondaryDescriptor(settings)})
            assert(d._tc_pixel_width == 0 && d._tc_pixel_height == 0);
    }
}
