// Count actual map requests, including requests made before App::setup().
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <chrono>
#include <thread>
#include <unordered_map>
#if defined(SOKOL_GLES3)
#include <EGL/egl.h>
#endif
namespace {
auto& maps() { static std::unordered_map<unsigned long, int> counts; return counts; }
int nativeSwaps = 0;
}
extern "C" int __real_XMapWindow(Display*, ::Window);
extern "C" int __wrap_XMapWindow(Display* display, ::Window window) {
    ++maps()[window];
    return __real_XMapWindow(display, window);
}
int visibilityMapCount(unsigned long window) { return maps()[window]; }
extern "C" int __real_XMapRaised(Display*, ::Window);
extern "C" int __wrap_XMapRaised(Display* display, ::Window window) {
    ++maps()[window];
    return __real_XMapRaised(display, window);
}
#if defined(SOKOL_GLES3)
extern "C" EGLBoolean __real_eglSwapBuffers(EGLDisplay, EGLSurface);
extern "C" EGLBoolean __wrap_eglSwapBuffers(EGLDisplay display, EGLSurface surface) {
    ++nativeSwaps;
    return __real_eglSwapBuffers(display, surface);
}
#else
extern "C" void __real_glXSwapBuffers(Display*, unsigned long);
extern "C" void __wrap_glXSwapBuffers(Display* display, unsigned long window) {
    ++nativeSwaps;
    __real_glXSwapBuffers(display, window);
}
#endif
// The main window stays hidden during every swap-count assertion. Count surfaces
// together: GLXDrawable is a GLXWindow, not the X Window returned by the public API.
int visibilitySwapCount(unsigned long) { return nativeSwaps; }
bool visibilityMapped(const void* display, unsigned long window) {
    XWindowAttributes attrs{};
    return XGetWindowAttributes(static_cast<Display*>(const_cast<void*>(display)), window, &attrs) && attrs.map_state != IsUnmapped;
}
// Poll the server, not update counts: a WM handles MapRequest asynchronously.
bool visibilityWaitMapped(const void* display, unsigned long window, bool mapped) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    do {
        if (visibilityMapped(display, window) == mapped) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}
bool visibilityBluePixel(const void* rawDisplay, unsigned long window) {
    auto* display = static_cast<Display*>(const_cast<void*>(rawDisplay));
    XWindowAttributes attrs{};
    if (!XGetWindowAttributes(display, window, &attrs) || attrs.map_state != IsViewable) return false;
    XImage* image = XGetImage(display, window, attrs.width / 2, attrs.height / 2, 1, 1, AllPlanes, ZPixmap);
    if (!image) return false;
    const unsigned long pixel = XGetPixel(image, 0, 0);
    const auto channel = [pixel](unsigned long mask) {
        if (!mask) return 0ul;
        const unsigned long value = pixel & mask;
        const unsigned long lowBit = mask & (~mask + 1);
        return (value / lowBit) * 255 / (mask / lowBit);
    };
    const bool blue = channel(image->blue_mask) >= 240 &&
        channel(image->red_mask) < 110 && channel(image->green_mask) < 150;
    XDestroyImage(image);
    return blue;
}

#endif
