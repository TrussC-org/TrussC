// Optional X server screen-saver suspension on sokol's borrowed Display.
#if defined(__linux__) && !defined(__ANDROID__)
#include "TrussC.h"
#include <X11/Xlib.h>
#include <dlfcn.h>

namespace trussc {
namespace {
struct IdleX11 {
    Display* display = nullptr; // Host main thread only; released before cleanup closes it.
    void* (*openLibrary)(const char*, int) = &dlopen;
    void* (*findSymbol)(void*, const char*) = &dlsym;
    void* library = nullptr;
    // Private libXss ABI: Xlib Bool and Status are int; no development header required.
    Bool (*queryExtension)(Display*, int*, int*) = nullptr;
    Status (*queryVersion)(Display*, int*, int*) = nullptr;
    void (*suspendSaver)(Display*, Bool) = nullptr;
    bool suspended = false;
    bool reported = false;

    bool acquire(Display* host) {
        if (suspended) return true;
        if (!host) return false;
        // Xlib keeps extension callbacks into this library, even after release.
        // Retain its code for process lifetime; never dlclose a live Xlib callback.
        if (!library) library = openLibrary("libXss.so.1", RTLD_NOW | RTLD_LOCAL);
        if (!library) return false;
        queryExtension = reinterpret_cast<decltype(queryExtension)>(findSymbol(library, "XScreenSaverQueryExtension"));
        queryVersion = reinterpret_cast<decltype(queryVersion)>(findSymbol(library, "XScreenSaverQueryVersion"));
        suspendSaver = reinterpret_cast<decltype(suspendSaver)>(findSymbol(library, "XScreenSaverSuspend"));
        int event = 0, error = 0, major = 0, minor = 0;
        if (!queryExtension || !queryVersion || !suspendSaver ||
            !queryExtension(host, &event, &error) ||
            !queryVersion(host, &major, &minor) || major < 1 || (major == 1 && minor < 1))
            return false;
        display = host;
        suspendSaver(display, True);
        suspended = true;
        return true;
    }
    void release() {
        if (suspended) suspendSaver(display, False);
        suspended = false;
        display = nullptr;
    }
    void apply(bool enabled, Display* host) {
        if (!enabled) { release(); return; }
        if (!acquire(host) && !reported) {
            reported = true;
            logNotice("Platform") << "Keep-screen-on unavailable; disable screen blanking in your OS or desktop settings.";
        }
    }
};
IdleX11& idleX11() {
    static IdleX11 backend;
    return backend;
}
} // namespace
namespace internal {
void applyKeepScreenOn(bool enabled, bool headless) {
    Display* display = nullptr;
    if (enabled && !headless)
        display = static_cast<Display*>(const_cast<void*>(sapp_x11_get_display()));
    idleX11().apply(enabled, display);
}
} // namespace internal
} // namespace trussc
#endif
