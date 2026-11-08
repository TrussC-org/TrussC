// Count actual map requests, including requests made before App::setup().
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>
#include <unordered_map>
#if defined(SOKOL_GLES3)
#include <EGL/egl.h>
#endif
namespace {
auto& maps() { static std::unordered_map<unsigned long, int> counts; return counts; }
auto& invalidations() { static std::unordered_map<unsigned long, int> counts; return counts; }
auto& swapsAtLastConfigure() { static std::unordered_map<unsigned long, int> counts; return counts; }
auto& configuredSizes() { static std::unordered_map<unsigned long, std::pair<int, int>> sizes; return sizes; }
int nativeSwaps = 0;
int frameWidth = 0, frameHeight = 0, presentedWidth = 0, presentedHeight = 0;
void recordPresent() { ++nativeSwaps; presentedWidth = frameWidth; presentedHeight = frameHeight; }
}
extern "C" int __real_XMapWindow(Display*, ::Window);
extern "C" int __wrap_XMapWindow(Display* display, ::Window window) {
    ++maps()[window];
    return __real_XMapWindow(display, window);
}
int visibilityMapCount(unsigned long window) { return maps()[window]; }
extern "C" int __real_XNextEvent(Display*, XEvent*);
extern "C" int __wrap_XNextEvent(Display* display, XEvent* event) {
    const int result = __real_XNextEvent(display, event);
    if (event->type == MapNotify) ++invalidations()[event->xmap.window];
    if (event->type == ConfigureNotify) {
        swapsAtLastConfigure()[event->xconfigure.window] = nativeSwaps;
        auto& previous = configuredSizes()[event->xconfigure.window];
        const auto size = std::make_pair(event->xconfigure.width, event->xconfigure.height);
        if (previous != size) { previous = size; invalidations()[event->xconfigure.window] += 2; }
    }
    if (event->type == UnmapNotify) {
        std::printf("windowVisible: UnmapNotify client=0x%lx event=0x%lx synthetic=%d\n",
            event->xunmap.window, event->xunmap.event, event->xunmap.send_event);
    }
    return result;
}
extern "C" int __real_XMapRaised(Display*, ::Window);
extern "C" int __wrap_XMapRaised(Display* display, ::Window window) {
    ++maps()[window];
    return __real_XMapRaised(display, window);
}
#if defined(SOKOL_GLES3)
extern "C" EGLBoolean __real_eglSwapBuffers(EGLDisplay, EGLSurface);
extern "C" EGLBoolean __wrap_eglSwapBuffers(EGLDisplay display, EGLSurface surface) {
    const auto result = __real_eglSwapBuffers(display, surface);
    if (result == EGL_TRUE) recordPresent();
    return result;
}
#else
extern "C" void __real_glXSwapBuffers(Display*, unsigned long);
extern "C" void __wrap_glXSwapBuffers(Display* display, unsigned long window) {
    __real_glXSwapBuffers(display, window);
    recordPresent();
}
#endif
// The main window stays hidden during every swap-count assertion. Count surfaces
// together: GLXDrawable is a GLXWindow, not the X Window returned by the public API.
int visibilitySwapCount(unsigned long) { return nativeSwaps; }
int visibilityInvalidationCount(unsigned long window) { return invalidations()[window]; }
void visibilityFrameSize(int width, int height) { frameWidth = width; frameHeight = height; }
bool visibilityHasWindowManager(const void* rawDisplay) {
    auto* display = static_cast<Display*>(const_cast<void*>(rawDisplay));
    XWindowAttributes root{};
    return XGetWindowAttributes(display, DefaultRootWindow(display), &root) &&
        (root.all_event_masks & SubstructureRedirectMask) != 0;
}
bool visibilityFinalSizePresented(const void* rawDisplay, unsigned long window, bool fullscreen, int since) {
    auto* display = static_cast<Display*>(const_cast<void*>(rawDisplay));
    XWindowAttributes attrs{};
    if (!XGetWindowAttributes(display, window, &attrs) || attrs.map_state != IsViewable) return false;
    // Without a WM fullscreen has no native effect. With our single-monitor WM,
    // wait for the screen-sized client, then a completed present at that size.
    if (fullscreen && visibilityHasWindowManager(display) &&
        (attrs.width != DisplayWidth(display, DefaultScreen(display)) ||
         attrs.height != DisplayHeight(display, DefaultScreen(display)))) return false;
    // A matching pre-hide present cannot satisfy a later ConfigureNotify.
    return nativeSwaps > std::max(since, swapsAtLastConfigure()[window]) && presentedWidth == attrs.width && presentedHeight == attrs.height;
}
bool visibilityMapped(const void* display, unsigned long window) {
    XWindowAttributes attrs{};
    return XGetWindowAttributes(static_cast<Display*>(const_cast<void*>(display)), window, &attrs) && attrs.map_state != IsUnmapped;
}
// EWMH frame coordinates exclude Mutter's invisible resize borders.
bool visibilityFramePosition(const void* rawDisplay, unsigned long window, int& x, int& y) {
    auto* display = static_cast<Display*>(const_cast<void*>(rawDisplay));
    ::Window child;
    if (!XTranslateCoordinates(display, window, DefaultRootWindow(display), 0, 0, &x, &y, &child)) return false;
    Atom type; int format; unsigned long count, remaining; unsigned char* data = nullptr;
    const int result = XGetWindowProperty(display, window, XInternAtom(display, "_NET_FRAME_EXTENTS", False),
        0, 4, False, XA_CARDINAL, &type, &format, &count, &remaining, &data);
    const bool valid = result == Success && type == XA_CARDINAL && format == 32 && count == 4 && data;
    if (valid) {
        const auto* extents = reinterpret_cast<unsigned long*>(data);
        x -= int(extents[0]); y -= int(extents[2]);
    }
    if (data) XFree(data);
    return true; // Undecorated windows may have no extents property.
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
bool visibilityIconic(const void* rawDisplay, unsigned long window) {
    auto* display = static_cast<Display*>(const_cast<void*>(rawDisplay));
    const Atom wmState = XInternAtom(display, "WM_STATE", True);
    if (wmState == None) return false;
    Atom type; int format; unsigned long count, remaining; unsigned char* data = nullptr;
    const int result = XGetWindowProperty(display, window, wmState, 0, 2, False,
        wmState, &type, &format, &count, &remaining, &data);
    const bool iconic = result == Success && type == wmState && format == 32 && count >= 1 &&
        reinterpret_cast<unsigned long*>(data)[0] == IconicState;
    if (data) XFree(data);
    return iconic;
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
