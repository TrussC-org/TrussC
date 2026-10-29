// =============================================================================
// tcWindowMac.mm - TrussC adapter for secondary windows (macOS)
// =============================================================================
// The native windowing layer (NSWindow + CAMetalLayer + per-window
// CADisplayLink, occlusion-gated ticks, sapp_event conversion with the full
// sokol_app keycode table) lives in sokol/sokol_app_tc.h and knows nothing
// about TrussC. This file maps its C API onto TrussC:
//   tick_cb   -> WindowContext switch + per-window dt + update/draw the tree
//   event_cb  -> CoreEvents + App hooks + Node-tree dispatch (same keycode
//                semantics as the main window: key == SAPP_KEYCODE_*)
//   close_cb  -> Window::teardownSecondary() (close button, or a close() request)
// All rendering shares the one sokol_gfx context; each window gets its own
// sokol_gl context (same pattern as Fbo).

#include "TrussC.h"

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>      // NSWindow (per-window setSize)
#include "sokol_app_tc.h"   // declarations only (impl lives in sokol_impl.mm)

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
// Window methods (macOS implementations)
// ---------------------------------------------------------------------------
namespace trussc {

// A request (see tcWindow.h): the backend calls close_cb -> teardown() at its
// next safe point, outside this window's tick and events.
void Window::closeSecondary() {
    if (internal::frameworkShutdown()) return;
    auto* st = static_cast<AdapterState*>(native_);
    if (!st || closeRequested_) return;
    closeRequested_ = true;
    sapp_window_request_close(st->win);
}

void Window::teardownSecondary() noexcept {
    if (internal::frameworkShutdown()) return;
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
    NSWindow* native = (__bridge NSWindow*)(void*)sapp_window_macos_get_window(handle(w));
    if (native) [native setContentSize:NSMakeSize((CGFloat)width, (CGFloat)height)];
}
void nativeWindowSetPosition(Window& w, int x, int y) { setWindowPositionNative(sapp_window_macos_get_window(handle(w)), x, y); }
IVec2 nativeWindowPosition(const Window& w) { return windowPositionNative(sapp_window_macos_get_window(handle(w))); }
}

std::shared_ptr<Window> internal::createSecondaryWindow(const WindowSettings& settings) {
    if (headless::isActive()) {
        logError("Window") << "createWindow(): not available in headless mode";
        return nullptr;
    }
    if (sessionEndExit()) {
        // Busy session end (frozen until _exit): no native window is created.
        static bool reported = false;
        if (!reported) {
            reported = true;
            logError("Window") << "createWindow(): refused, the session is ending";
        }
        return nullptr;
    }
    // While a busy session end is frozen, the last release of a Window does not
    // tear it down: it stays alive (and its context valid) until the process
    // _exits. Otherwise the release destroys it as usual.
    auto win = std::shared_ptr<Window>(new Window(), [](Window* w) {
        if (!frameworkShutdown() && sessionEndExit()) return;
        delete w;
    });
    internal::WindowRequestAccess::setHostOwner(*win, win);
    win->title_ = settings.title;
    internal::WindowRequestAccess::binding(*win).geometry = {double(settings.width), double(settings.height), pixelPerfectMode()};
    auto* st = new AdapterState();

    sapp_window_desc d = internal::buildNativeWindowDescriptor(settings, pixelPerfectMode());
    d.mtl_device = sglue_environment().metal.device;
    d.tick_cb = &internal::windowTick;
    d.event_cb = &internal::mainWindowEvent;
    d.close_cb = &internal::windowClosed;
    d.user_data = win.get();
    d.bind_cb = bindAdapter;
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
    return win;
}

} // namespace trussc

#endif // __APPLE__

#if defined(__APPLE__) && !TARGET_OS_IPHONE
namespace trussc::internal {
WindowFullscreenPhase macWindowFullscreenPhase(uint32_t id) noexcept {
    return static_cast<WindowFullscreenPhase>(sapp_tc_window_fullscreen_phase({id}));
}
void setMacWindowFullscreen(uint32_t id, bool full) noexcept {
    sapp_tc_window_set_fullscreen({id}, full);
}
void toggleMacWindowFullscreen(uint32_t id) noexcept {
    sapp_tc_window_toggle_fullscreen({id});
}
}
#endif
