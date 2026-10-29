// =============================================================================
// tcWindowWin.cpp - TrussC adapter for secondary windows (Windows)
// =============================================================================
// The native windowing layer (HWND + D3D11 flip swapchain + per-window frame
// latency waitable ticks, occlusion-tested presents, sapp_event conversion
// with the full sokol_app scancode table) lives in sokol/sokol_app_tc.h and
// knows nothing about TrussC. This file maps its C API onto TrussC:
//   tick_cb   -> WindowContext switch + per-window dt + update/draw the tree
//   event_cb  -> CoreEvents + App hooks + Node-tree dispatch (same keycode
//                semantics as the main window: key == SAPP_KEYCODE_*)
//   close_cb  -> Window::teardownSecondary() (close button, or a close() request)
// All rendering shares the one sokol_gfx context; each window gets its own
// sokol_gl context (same pattern as Fbo). Mirror of platform/mac/tcWindowMac.mm.

#include "TrussC.h"

#if defined(_WIN32)
#include <windows.h>        // HWND / SetWindowPos (per-window setSize)
#include "sokol_app_tc.h"   // declarations only (impl lives in sokol_impl.cpp)

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
// Window methods (Windows implementations)
// ---------------------------------------------------------------------------
namespace trussc {

// A request (see tcWindow.h): the backend calls close_cb -> teardown() at its
// next safe point, outside this window's tick and events.
void Window::closeSecondary() {
    auto* st = static_cast<AdapterState*>(native_);
    if (!st || closeRequested_) return;
    closeRequested_ = true;
    sapp_window_request_close(st->win);
}

void Window::teardownSecondary() noexcept {
    auto* st = static_cast<AdapterState*>(native_);
    if (!st) return;
    internal::TeardownSteps steps;
    internal::WindowRequestAccess::unbind(*this);
    native_ = nullptr;             // guard before entry diagnostics can reenter
    closeRequested_ = false;
    ctx_.targetsClosed = true;
    // Stack restoration must not prevent any native/App teardown step.
    std::optional<internal::EntryStackGuard> guard;
    steps.run("exit entry guard", [&] { guard.emplace(internal::AppEntry::Exit); });
    steps.run("native target cleanup", [&] { ctx_.teardownTargets(); });
    ctx_.acquireSwapchain = nullptr;
    ctx_.acquireSwapchainUser = nullptr;
    ctx_.activateNative = nullptr;
    steps.run("native window destruction", [&] { sapp_destroy_window(st->win); });
    // Frees this window's sgl pipelines and its sgl context (#317).
    steps.run("native GPU resource release", [&] { ctx_.swapchainTarget.resources().release(); });
    delete st;
    // The window's own exit event: per-window resource holders (e.g. the
    // tcxImGui per-window manager) tear down here, while this window's
    // CoreEvents is still alive.
    endApp();
}

namespace internal {
namespace {
sapp_window handle(const Window& w) { return WindowRequestAccess::binding(w).handle; }
}
void nativeWindowSetTitle(Window& w, const char* title) { sapp_window_set_title(handle(w), title); }
void nativeWindowSetFullscreen(Window& w, bool full) { sapp_tc_window_set_fullscreen(handle(w), full); }
uint32_t nativeWindowFullscreenPhase(const Window& w) { return sapp_tc_window_fullscreen_phase(handle(w)); }
void nativeWindowToggleFullscreen(Window& w) { sapp_tc_window_toggle_fullscreen(handle(w)); }
void nativeWindowSetSize(Window& w, double width, double height, bool) {
    setWindowSizeWin32(sapp_window_win32_get_hwnd(handle(w)), width, height);
}
void nativeWindowSetPosition(Window& w, int x, int y) { setWindowPositionNative(sapp_window_win32_get_hwnd(handle(w)), x, y); }
IVec2 nativeWindowPosition(const Window& w) { return windowPositionNative(sapp_window_win32_get_hwnd(handle(w))); }
}

std::shared_ptr<Window> internal::createSecondaryWindow(const WindowSettings& settings) {
    if (headless::isActive()) {
        logError("Window") << "createWindow(): not available in headless mode";
        return nullptr;
    }
    auto win = std::shared_ptr<Window>(new Window());
    internal::WindowRequestAccess::setHostOwner(*win, win);
    win->title_ = settings.title;
    internal::WindowRequestAccess::binding(*win).geometry = {double(settings.width), double(settings.height), pixelPerfectMode()};
    auto* st = new AdapterState();

    sapp_window_desc d = {};
    d.x = -1;
    d.y = -1;
    d.width = settings.width;
    d.height = settings.height;
    if (pixelPerfectMode()) {
        d._tc_pixel_width = settings.width > 0 ? settings.width : 0;
        d._tc_pixel_height = settings.height > 0 ? settings.height : 0;
    }
    d.title = settings.title.c_str();
    d.borderless = !settings.decorated;
    d.no_high_dpi = !settings.highDpi;
    d.sample_count = settings.sampleCount;
    // (the shared D3D11 device is owned by sokol_app_tc.h; nothing to pass)
    d.tick_cb = &internal::windowTick;
    d.event_cb = &internal::mainWindowEvent;
    d.close_cb = &internal::windowClosed;
    d.user_data = win.get();
    d.bind_cb = bindAdapter;
    win->native_ = st;
    st->win = sapp_create_window(&d);
    if (st->win.id == 0) {
        win->native_ = nullptr;
        internal::WindowRequestAccess::unbind(*win);
        delete st;
        logError("Window") << "createWindow(): native window creation failed";
        return nullptr;
    }

    internal::refreshWindowMetadata(*win);
    return win;
}

} // namespace trussc

#endif // _WIN32
