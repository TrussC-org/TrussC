// Production sizing statements injected by test_window_pixel_size.py.
#include "core/include/sokol/util/sokol_window_size.h"
#include <cassert>
#include <cmath>
#include <initializer_list>
#include <cstring>
#include <string>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
using LONG = int;
struct Size {
    int x, y;
    Size(int width = 0, int height = 0) : x(width), y(height) {}
    Size(bool accepted) : x(0), y(0) { assert(!accepted); }
};
struct Descriptor {
    bool (*bind_cb)(uint32_t, void*) = nullptr;
    bool (*_tc_bind_cb)(uint32_t, void*) = nullptr;
    void* user_data = nullptr;
    void* _tc_bind_user = nullptr;
    int _tc_tick_cb = 0;
    int _tc_event_cb = 0;
    int width = 0, height = 0, x = 0, y = 0, sample_count = 1;
    int _tc_pixel_width = 0, _tc_pixel_height = 0;
    bool borderless=false, high_dpi = true, no_high_dpi = false;
    int swap_interval = 1, _tc_draw_while_hidden = 0;
    bool fullscreen = false;
    int event_cb = 0, tick_cb = 0;
    const void* mtl_device = nullptr;
    const char* title = nullptr;
    const char* window_title = nullptr;
};
using sapp_desc = Descriptor;
using sapp_window_desc = Descriptor;
struct Settings { int width = 0, height = 0; bool pixelPerfect = false, highDpi = true, decorated=true, fullscreen=false; int swapInterval=1,sampleCount=1; std::string title="probe"; };
struct Platform { static constexpr bool isDesktop() { return true; } };
float displayScale = 1, renderScale = 1, preWindowScale = 1;
bool enabled = false;
bool pixelPerfectMode() { return enabled; }
// A pre-window/main-display query can differ from the target window.
float getDisplayScaleFactor() { return preWindowScale; }
int _sapp_tc_roundf_gzero(float value) { return int(value + 0.5f); }
struct State {
    Descriptor desc;
    float window_scale = 1, dpi_scale = 1;
    int xwin = 1, fb_width = 0, fb_height = 0, win_width = 0, win_height = 0;
    bool surface_resize_pending=false;
    bool (*events_enabled)() = nullptr;
#ifdef TEST_WIN
    State* hwnd = nullptr;
    void* hmonitor = nullptr;
    int timing = 0;
    Size client;
#endif
};
#ifdef TEST_WIN
using _sapp_tc_window_t = State;
using DWORD = unsigned long;
struct RECT { LONG left, top, right, bottom; };
constexpr int CW_USEDEFAULT = -1, FALSE = 0, MONITOR_DEFAULTTONEAREST = 0;
constexpr int SWP_NOMOVE = 1, SWP_NOZORDER = 2, SWP_NOACTIVATE = 4, SAPP_LOGITEM_OK = 0;
struct {
    struct { struct { int logger = 0; } desc; } app;
    unsigned (*GetDpiForWindow)(State*) = nullptr;
    bool (*AdjustWindowRectExForDpi)(RECT*, DWORD, bool, DWORD, unsigned) = nullptr;
} _sapp_tc;
bool _sapp_tc_win32_ensure_wndclass() { return true; }
// Native stubs record client extents. Non-client frame adjustment is identity.
bool AdjustWindowRectEx(RECT*, DWORD, bool, DWORD) { return true; }
void* GetModuleHandleW(void*) { return nullptr; }
State* CreateWindowExW(DWORD, const wchar_t*, const wchar_t*, DWORD,
        int, int, int width, int height, void*, void*, void*, State* w) {
    w->client = {width, height};
    return w;
}
DWORD GetLastError() { return 0; }
DWORD HRESULT_FROM_WIN32(DWORD error) { return error; }
template<class... Args> void _sapp_tc_log(Args...) { assert(false); }
void* MonitorFromWindow(State*, int) { return nullptr; }
void _sapp_tc_win32_update_refresh(State*) {}
void _sapp_tc_win32_update_scale(State* w) {
    w->window_scale = displayScale;
    w->dpi_scale = w->desc.no_high_dpi ? 1 : displayScale;
}
bool GetClientRect(State* w, RECT* rect) {
    *rect = {0, 0, w->client.x, w->client.y};
    return true;
}
bool SetWindowPos(State* w, void*, int, int, int width, int height, unsigned) {
    w->client = {width, height};
    return true;
}
void _sapp_tc_timing_reset(int*) {}
WIN32_NATIVE_CREATOR
#else
struct { void* display = nullptr; struct { void* device = nullptr; } app; } _sapp_tc;
#endif
#if defined(TEST_WIN) || defined(TEST_MAC)
struct _sapp_tc_native_desc {
    Descriptor window;
    bool surface_resize_pending=false;
    bool (*events_enabled)() = nullptr;
    int swap_interval = 0, title_limit = 0, color_format = 0, depth_format = 0;
    bool fullscreen = false, draw_while_hidden = false, center_on_create = false;
};
WIN32_NORMALIZERS
#endif
#ifdef TEST_WIN
Descriptor commonTransfer(const _sapp_tc_native_desc* d) {
    State state; State* w = &state;
    WIN32_COMMON_TRANSFER
    return w->desc;
}
#endif
#ifdef TEST_MAC
bool _sapp_tc_app_events_enabled() { return true; }
struct _sapp_tc_mac_window_attrs {
    bool restorable = false, nearest_magnification = false, take_key_focus_on_create = false;
};
constexpr int _sapp_tc_main_event_tramp = 1, _sapp_tc_main_tick_tramp = 2;
constexpr int SAPP_PIXELFORMAT_RGB10A2 = 1, SAPP_PIXELFORMAT_BGRA8 = 2, SAPP_PIXELFORMAT_DEPTH_STENCIL = 3;
MAC_TRANSFERS
Descriptor macSecondaryTransfer(const Descriptor* desc) {
    WIN32_SECONDARY_TRANSFER
}
#endif
Size roundMac(double width, double height) { return {int(std::ceil(width) * renderScale), int(std::ceil(height) * renderScale)}; }
namespace internal {
SHARED_DESCRIPTOR
Descriptor buildWindowDescriptor(const Settings& settings) {
    LAUNCHER
    return desc;
}
}
using internal::buildWindowDescriptor;
Descriptor mainDescriptor(const Settings& settings) { NORMAL_LAUNCHER }
Descriptor hotDescriptor(const Settings& settings) { HOT_LAUNCHER return desc; }
Descriptor secondaryDescriptor(const Settings& settings) {
    using internal::buildNativeWindowDescriptor;
    SECONDARY
    return d;
}
Descriptor mainTransfer(Descriptor descriptor) {
    const Descriptor* d = &descriptor;
#ifdef TEST_WIN
    _sapp_tc_native_desc native{};
    TRANSFER
    return native.window;
#elif defined(TEST_MAC)
    TRANSFER
#else
    State state; State* w = &state;
    TRANSFER
    return w->desc;
#endif
}
Size create(Descriptor descriptor, bool mainWindow) {
#ifdef TEST_WIN
    _sapp_tc_native_desc native{descriptor};
    if (!mainWindow) {
        const Descriptor* desc = &descriptor;
        WIN32_SECONDARY_TRANSFER
    }
    State state;
    state.desc = commonTransfer(&native);
    if (!_sapp_tc_win32_create_native_window(&state, L"probe", 0, 0, 0, 0)) return false;
    return state.client;
#else
#ifdef TEST_MAC
    if (!mainWindow) descriptor = macSecondaryTransfer(&descriptor);
#endif
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
#endif
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
int main(int argc, char** argv) {
    for (bool pixels : {false, true}) for (bool highDpi : {true, false})
    for (float dpi : {1.0f, 1.25f, 1.5f, 1.75f, 2.0f}) for (Size requested : {Size{640,400}, Size{480,320}, Size{1001,667}, Size{1,3}})
    for (bool mainWindow : {false, true}) {
        if (argc > 1 && mainWindow != (std::strcmp(argv[1], "main") == 0)) continue;
        if (argc > 2 && pixels != (std::strcmp(argv[2], "pixels") == 0)) continue;
        if (argc > 3 && dpi != std::strtof(argv[3], nullptr)) continue;
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
        if (actual.x != expectedNative.x || actual.y != expectedNative.y) {
            std::fprintf(stderr, "%s pixels=%d highDpi=%d dpi=%.2f request=%dx%d: creation=%dx%d resize=%dx%d\n",
                mainWindow ? "main" : "secondary", pixels, highDpi, dpi, requested.x, requested.y,
                actual.x, actual.y, expectedNative.x, expectedNative.y);
        }
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
