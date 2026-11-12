// =============================================================================
// tcWindowLinux.cpp - TrussC adapter for secondary windows (Linux / X11)
// =============================================================================
// The native windowing layer (X11 window + shared GL context with one
// GLXWindow or EGLSurface per window, timer-paced ticks at the measured refresh
// rate, sapp_event conversion with the full sokol_app XKB keytable) lives in
// sokol/sokol_app_tc.h and knows nothing about TrussC. This file maps its C
// API onto TrussC:
//   tick_cb   -> WindowContext switch + per-window dt + update/draw the tree
//   event_cb  -> CoreEvents + App hooks + Node-tree dispatch (same keycode
//                semantics on every Window: key == SAPP_KEYCODE_*)
//   close_cb  -> Window::disposeNativeBacking() (close button, or a close() request)
// All rendering shares the one sokol_gfx context; each window gets its own
// sokol_gl context (same pattern as Fbo). Mirror of platform/win/tcWindowWin.cpp.

#include "TrussC.h"

#if defined(__linux__) && !defined(__ANDROID__) && (defined(SOKOL_GLCORE) || defined(SOKOL_GLES3))
#include "tcWindowGeometryX11.h"
#include "sokol_app_tc.h"   // declarations only (impl lives in sokol_impl.cpp)
#include <unistd.h>
#include "stb/stb_image.h"

using namespace trussc;

namespace {

struct AdapterState {
    sapp_window win{0};
};

bool bindAdapter(uint32_t id, void* user) noexcept {
    auto* window = static_cast<trussc::Window*>(user);
    if (!window || !window->native_) return false;
    static_cast<AdapterState*>(window->native_)->win = {id};
    return internal::bindNativeWindow(id, window);
}

} // namespace

// ---------------------------------------------------------------------------
// Window methods (Linux implementations)
// ---------------------------------------------------------------------------
namespace trussc {

// A request (see tcWindow.h): the backend calls close_cb -> teardown() at its
// next safe point, outside this window's tick and events.
void Window::disposeNativeBacking() noexcept {
    if (internal::frameworkShutdown()) return;
    auto* st = static_cast<AdapterState*>(native_);
    if (!st) return;
    internal::TeardownSteps steps;
    native_ = nullptr;
    steps.run("native window destruction", [&] { sapp_destroy_window(st->win); });
    delete st;
}

namespace internal {
// Supplied by trussc_setup_icon() in the app target. Keep manual core-only
// linkers and hosts without the app helper usable as well.
const unsigned char* linuxAppIcon(int& size, const char*& name) __attribute__((weak));

namespace {
const std::vector<unsigned long>& windowIcon() {
    static const auto icon = [] {
        std::vector<unsigned long> data;
        if (!linuxAppIcon) return data;
        int size = 0;
        const char* name = nullptr;
        const auto* png = linuxAppIcon(size, name);
        int width = 0, height = 0, channels = 0;
        auto* rgba = stbi_load_from_memory(png, size, &width, &height, &channels, 4);
        if (!rgba) {
            logWarning("Window") << "Could not decode embedded window icon";
            return data;
        }
        Pixels scaled;
        const unsigned char* pixels = rgba;
        if (std::max(width, height) > 1024) {
            logNotice("Window") << name << " is " << width << "x" << height
                                << ", scaled to 1024 px";
            const int longest = std::max(width, height);
            const int targetWidth = std::max(1, width * 1024 / longest);
            const int targetHeight = std::max(1, height * 1024 / longest);
            scaled.setFromPixels(rgba, width, height, 4);
            stbi_image_free(rgba);
            rgba = nullptr;
            while (std::max(scaled.getWidth(), scaled.getHeight()) >= 2048) {
                scaled.halve();
            }
            scaled.resize(targetWidth, targetHeight);
            width = scaled.getWidth();
            height = scaled.getHeight();
            pixels = scaled.getData();
        }
        // Xlib format 32 takes native longs, including on 64-bit Linux.
        data.reserve(2 + static_cast<size_t>(width) * height);
        data.push_back(width);
        data.push_back(height);
        for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
            const auto* pixel = pixels + 4 * i;
            data.push_back((static_cast<unsigned long>(pixel[3]) << 24) |
                           (static_cast<unsigned long>(pixel[0]) << 16) |
                           (static_cast<unsigned long>(pixel[1]) << 8) | pixel[2]);
        }
        stbi_image_free(rgba);
        return data;
    }();
    return icon;
}
} // namespace

void nativeWindowSetClass(uint32_t id) {
    auto* display = (Display*)sapp_x11_get_display();
    const auto window = (::Window)(uintptr_t)sapp_window_x11_get_window({id});
    if (!display || !window) return;
    static std::string name = [] {
        // Bound (procfs): a path that fills PATH_MAX may be truncated, so fall back to "TrussC App".
        char path[PATH_MAX];
        const auto length = readlink("/proc/self/exe", path, sizeof(path));
        if (length <= 0 || static_cast<size_t>(length) >= sizeof(path)) return std::string("TrussC App");
        std::string executable(path, static_cast<size_t>(length));
        const auto slash = executable.find_last_of('/');
        auto basename = executable.substr(slash == std::string::npos ? 0 : slash + 1);
        return basename.empty() ? std::string("TrussC App") : basename;
    }();
    // ICCCM: install the class before the first map, including hidden startup.
    XClassHint hint{};
    hint.res_name = hint.res_class = name.data();
    XSetClassHint(display, window, &hint);
    const auto& icon = windowIcon();
    if (!icon.empty()) {
        XChangeProperty(display, window, XInternAtom(display, "_NET_WM_ICON", False),
            XA_CARDINAL, 32, PropModeReplace,
            reinterpret_cast<const unsigned char*>(icon.data()), static_cast<int>(icon.size()));
    }
}
namespace {
sapp_window handle(const Window& w) { return WindowRequestAccess::binding(w).handle; }
}
void nativeWindowSetTitle(Window& w, const char* title) { sapp_window_set_title(handle(w), title); }
void nativeWindowSetFullscreen(Window& w, bool full) { sapp_tc_window_set_fullscreen(handle(w), full); }
uint32_t nativeWindowFullscreenPhase(const Window& w) { return sapp_tc_window_fullscreen_phase(handle(w)); }
void nativeWindowToggleFullscreen(Window& w) { sapp_tc_window_toggle_fullscreen(handle(w)); }
void nativeWindowSetSize(Window& w, double width, double height, bool pixelSize) {
    x11Geometry((Display*)sapp_x11_get_display(),
        (::Window)(uintptr_t)sapp_window_x11_get_window(handle(w)), WindowRequestAccess::binding(w).geometry,
        false, 0, 0, width, height, trussc::internal::WindowRequestAccess::context(w).dpiScale, w.isFullscreen(), pixelSize);
}
void nativeWindowSetPosition(Window& w, int x, int y) {
    x11Geometry((Display*)sapp_x11_get_display(),
        (::Window)(uintptr_t)sapp_window_x11_get_window(handle(w)), WindowRequestAccess::binding(w).geometry,
        true, x, y, 0, 0, trussc::internal::WindowRequestAccess::context(w).dpiScale, w.isFullscreen());
}
IVec2 nativeWindowPosition(const Window& w) {
    return x11FramePosition((Display*)sapp_x11_get_display(), (::Window)(uintptr_t)sapp_window_x11_get_window(handle(w)),
        &WindowRequestAccess::binding(w).geometry);
}
// True when an EWMH window manager runs (it owns stacking and focus).
static bool hasEwmhWindowManager(Display* display, ::Window root) {
    const Atom check = XInternAtom(display, "_NET_SUPPORTING_WM_CHECK", True);
    if (check == None) return false;
    Atom type = None; int format = 0; unsigned long count = 0, after = 0; unsigned char* data = nullptr;
    const bool found = XGetWindowProperty(display, root, check, 0, 1, False, XA_WINDOW, &type, &format,
                                          &count, &after, &data) == Success && type == XA_WINDOW && count == 1;
    if (data) XFree(data);
    return found;
}
void nativeWindowBringToFront(Window& w) {
    Display* display = (Display*)sapp_x11_get_display();
    // ::Window = the X11 window id type (trussc::Window shadows it in this ns)
    ::Window window = (::Window)(uintptr_t)sapp_window_x11_get_window(handle(w));
    if (!display || !window) return;
    const ::Window root = DefaultRootWindow(display);
    if (hasEwmhWindowManager(display, root)) {
        // Ask the WM to activate the window (_NET_ACTIVE_WINDOW). Raising and
        // focusing it directly leaves the WM's active window out of date.
        // CurrentTime: raise now, like macOS activateIgnoringOtherApps.
        XEvent e{};
        e.xclient.type = ClientMessage;
        e.xclient.window = window;
        e.xclient.message_type = XInternAtom(display, "_NET_ACTIVE_WINDOW", False);
        e.xclient.format = 32;
        e.xclient.data.l[0] = 1;   // source: application
        e.xclient.data.l[1] = CurrentTime;
        XSendEvent(display, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &e);
    } else {
        // No window manager (e.g. bare Xvfb): map (un-minimise), raise and
        // focus directly. With a WM, _NET_ACTIVE_WINDOW also un-minimises.
        XMapRaised(display, window);
        XSetInputFocus(display, window, RevertToParent, CurrentTime);
    }
    XFlush(display);
}
}

std::shared_ptr<Window> internal::createSecondaryWindow(const WindowSettings& settings) {
    const bool noX11 = !sapp_x11_get_display();
#if defined(SOKOL_GLES3)
    const bool rejectHeadless = headless::isActive() && !noX11;
#else
    const bool rejectHeadless = headless::isActive();
#endif
    if (rejectHeadless) {
        logError("Window") << "createWindow(): not available in headless mode";
        return nullptr;
    }
    if (noX11) {
        logWarning("Window") << "createWindow(): secondary windows require X11 (unsupported on KMS or without an X11 display)";
        return nullptr;
    }
    auto win = std::shared_ptr<Window>(new Window());
    internal::WindowRequestAccess::setHostOwner(*win, win);
    win->title_ = mcp::internal::markedWindowTitle(settings.title);
    win->setQuitOnClose(settings.quitOnClose);
    internal::WindowRequestAccess::binding(*win).geometry = {double(settings.width), double(settings.height), pixelPerfectMode(), !settings.visible};
    auto* st = new AdapterState();

    sapp_window_desc d = internal::buildNativeWindowDescriptor(settings, pixelPerfectMode());
    d.title = win->title_.c_str();
    d.tick_cb = &internal::windowTick;
    d.event_cb = &internal::windowEvent;
    d.close_cb = &internal::windowClosed;
    d.close_request_cb = &internal::allowWindowClose;
    d.user_data = win.get();
    d.bind_cb = bindAdapter;
    d.parent = internal::resolveParentWindow(settings);
    win->native_ = st;
    {
        internal::WindowLifecycleScope construction(*win);
        st->win = sapp_create_window(&d);
    }
    if (st->win.id == 0) {
        win->native_ = nullptr;
        internal::WindowRequestAccess::unbind(*win);
        delete st;
        logError("Window") << "createWindow(): native window creation failed";
        return nullptr;
    }

    internal::refreshWindowMetadata(*win);
    internal::retainRuntimeWindow(win);
    return win;
}

} // namespace trussc

#endif // Linux X11 (GLCORE or GLES3), excluding Android
