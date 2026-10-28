#include <TrussC.h>

namespace trussc {
Window::OwnedState::OwnedState() {
    context.render = &render;
    context.coreEvents = &events;
}
Window::Window() : Window(false) {}
Window::Window(bool mainWindow)
    : owned_(std::make_unique<OwnedState>()), ctx_(owned_->context),
      events_(owned_->events), render_(owned_->render), title_(owned_->title) {
    ctx_.isMain = mainWindow;
}

namespace internal {
namespace {
constinit WindowContext* initializedNonWindow = nullptr;
struct NonWindowState {
    WindowContext context;
    NonWindowState() {
        // Preserve prelaunch subscriptions and render references. Their storage
        // belongs to the canonical Window; this fallback is not a live Window.
        context.render = &mainRenderContext();
        context.coreEvents = &mainCoreEvents();
        initializedNonWindow = &context;
    }
    ~NonWindowState() { initializedNonWindow = nullptr; }
};
NonWindowState& nonWindowState() {
    static NonWindowState state;
    return state;
}
sg_pixel_format pixelFormat(sapp_pixel_format format) {
    switch (format) {
        case SAPP_PIXELFORMAT_RGBA8: return SG_PIXELFORMAT_RGBA8;
        case SAPP_PIXELFORMAT_BGRA8: return SG_PIXELFORMAT_BGRA8;
        case SAPP_PIXELFORMAT_RGB10A2: return SG_PIXELFORMAT_RGB10A2;
        case SAPP_PIXELFORMAT_DEPTH: return SG_PIXELFORMAT_DEPTH;
        case SAPP_PIXELFORMAT_DEPTH_STENCIL: return SG_PIXELFORMAT_DEPTH_STENCIL;
        default: return SG_PIXELFORMAT_NONE;
    }
}
bool readMetadata(sapp_window handle, void*, sapp_tc_window_metadata& out) {
    return sapp_tc_window_read_metadata(handle, &out);
}
sg_swapchain acquireSwapchain(sapp_window handle, void*) {
    sapp_tc_window_metadata metadata{};
    sg_swapchain sc{};
    if (!sapp_tc_window_read_metadata(handle, &metadata)) return sc;
    sc.width = metadata.fb_width; sc.height = metadata.fb_height;
    sc.sample_count = metadata.sample_count;
    sc.color_format = pixelFormat(metadata.color_format);
    sc.depth_format = pixelFormat(metadata.depth_format);
#if defined(SOKOL_METAL)
    sc.metal.current_drawable = sapp_window_metal_current_drawable(handle);
    if (!sc.metal.current_drawable) return {};
    sc.metal.depth_stencil_texture = sapp_window_metal_depth_stencil_texture(handle);
    sc.metal.msaa_color_texture = sapp_window_metal_msaa_color_texture(handle);
#elif defined(SOKOL_D3D11)
    sc.d3d11.render_view = sapp_window_d3d11_render_view(handle);
    if (!sc.d3d11.render_view) return {};
    sc.d3d11.resolve_view = sapp_window_d3d11_resolve_view(handle);
    sc.d3d11.depth_stencil_view = sapp_window_d3d11_depth_stencil_view(handle);
#else
    sc.gl.framebuffer = sapp_window_gl_framebuffer(handle);
#endif
    return sc;
}
void activate(sapp_window handle, void*) {
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
    sapp_tc_gl_make_current(handle);
#else
    (void)handle;
#endif
}
sg_swapchain acquireBoundSwapchain(void* user) {
    auto& b = WindowRequestAccess::binding(*static_cast<Window*>(user));
    if (!b.bound || !b.operations || !b.operations->acquireSwapchain) return {};
    return b.operations->acquireSwapchain(b.handle, b.user);
}
void activateBinding(void* user) {
    auto& b = WindowRequestAccess::binding(*static_cast<Window*>(user));
    if (b.bound && b.operations && b.operations->activate)
        b.operations->activate(b.handle, b.user);
}
}
WindowContext& nonWindowContext() { return nonWindowState().context; }
WindowContext* nonWindowContextIfInitialized() noexcept { return initializedNonWindow; }
const WindowNativeOperations& productionWindowOperations() {
    static const WindowNativeOperations operations{readMetadata, acquireSwapchain, activate};
    return operations;
}
void publishWindowMetadata(Window& window, const sapp_tc_window_metadata& metadata) {
    auto& b = WindowRequestAccess::binding(window);
    auto& ctx = window.context();
    b.metadata = metadata;
    // Minimized zero extents describe availability, not a new App coordinate size.
    if (metadata.fb_width > 0 && metadata.fb_height > 0 &&
        std::isfinite(metadata.dpi_scale) && metadata.dpi_scale > 0) {
        ctx.fbWidth = metadata.fb_width; ctx.fbHeight = metadata.fb_height;
        ctx.dpiScale = metadata.dpi_scale;
        if (ctx.swapchainTarget.width == 0 && ctx.swapchainTarget.height == 0) {
            ctx.swapchainTarget.width = ctx.fbWidth; ctx.swapchainTarget.height = ctx.fbHeight;
        }
        ctx.swapchainTarget.colorFormat = pixelFormat(metadata.color_format);
        ctx.swapchainTarget.depthFormat = pixelFormat(metadata.depth_format);
        ctx.swapchainTarget.sampleCount = metadata.sample_count;
    }
    ctx.suspended = b.bound && metadata.trust_hidden_signals && (metadata.iconified || metadata.occluded);
}
void refreshWindowMetadata(Window& window) {
    auto& b = WindowRequestAccess::binding(window);
    if (!b.bound || !b.operations || !b.operations->readMetadata) return;
    sapp_tc_window_metadata metadata{};
    if (b.operations->readMetadata(b.handle, b.user, metadata)) publishWindowMetadata(window, metadata);
}
bool bindWindow(Window& window, sapp_window handle, const WindowNativeOperations& operations, void* user) {
    if (!handle.id || !operations.readMetadata) return false;
    sapp_tc_window_metadata metadata{};
    if (!operations.readMetadata(handle, user, metadata) || metadata.fb_width <= 0 || metadata.fb_height <= 0 ||
        !std::isfinite(metadata.dpi_scale) || metadata.dpi_scale <= 0) return false;
    auto& b = WindowRequestAccess::binding(window);
    b.handle = handle; b.operations = &operations; b.user = user; b.bound = true;
    publishWindowMetadata(window, metadata);
    window.context().acquireSwapchain = acquireBoundSwapchain;
    window.context().acquireSwapchainUser = &window;
    window.context().activateNative = activateBinding;
    return true;
}
bool bindNativeWindow(uint32_t id, void* user) noexcept {
    if (!id || !user) return false;
    try { return bindWindow(*static_cast<Window*>(user), {id}, productionWindowOperations(), nullptr); }
    catch (...) { reportTeardownFailure("native window binding"); return false; }
}
void syncWindowAppSize(Window& window, bool initial) {
    const auto app = window.getApp();
    const auto& ctx = window.context();
    if (!app || window.appSizeDelivering_ || (initial && window.appSizeSeeded_)) return;
    window.appSizeDelivering_ = true;
    struct DeliveryScope {
        bool& active;
        ~DeliveryScope() { active = false; }
    } delivery{window.appSizeDelivering_};
    // Bound (resize feedback): three deliveries let the host return; the latest context extent waits for the next tick.
    for (int deliveryCount = 0; deliveryCount < 3; ++deliveryCount) {
        if (ctx.fbWidth <= 0 || ctx.fbHeight <= 0) return;
        const auto size = framebufferResizeArgs(ctx.fbWidth, ctx.fbHeight, appSizeDpi(ctx.dpiScale));
        if (window.appSizeSeeded_ && window.appSizeWidth_ == size.width && window.appSizeHeight_ == size.height) return;
        try {
            EntryStackGuard guard(AppEntry::Event, "windowResized()");
            WindowCallbackTimeScope callbackTime;
            // Initial attachment seeds only the App; subsequent changes use its events.
            const bool alreadySeeded = initial && app->getWidth() == size.width && app->getHeight() == size.height;
            if (!alreadySeeded) {
                if (initial && !WindowRequestAccess::appStarted(window)) app->handleWindowResized(size.width, size.height);
                else window.syncRootSize(float(size.width), float(size.height));
            }
        } catch (...) {
            // A listener may throw before OR after the App callback. Even the
            // previous delivered extent may now disagree with the App. Clear
            // the delivered dimensions and unwind; never retry in this loop.
            // Retain initial-seed completion so an unstarted App's subsequent
            // event retry cannot bypass CoreEvents as a new attachment seed.
            if (window.getApp() == app) window.appSizeWidth_ = window.appSizeHeight_ = -1;
            throw;
        }
        if (window.getApp() != app || !window.isOpen()) return;
        window.appSizeWidth_ = size.width; window.appSizeHeight_ = size.height;
        window.appSizeSeeded_ = true;
        initial = false;
        // A listener can pump newer native observations. Their nested syncs
        // defer here so an older outer App callback cannot overwrite a newer
        // delivery/claim. Re-read the context, never the listener-edited args.
    }
}
void refreshMainWindowMetadata() {
    auto* window = mainWindow();
    if (!window) return;
    if constexpr (supportsSecondaryWindows()) refreshWindowMetadata(*window);
    else if (sapp_isvalid()) {
        sapp_tc_window_metadata metadata{};
        metadata.fb_width = sapp_width(); metadata.fb_height = sapp_height();
        metadata.dpi_scale = sapp_dpi_scale(); metadata.sample_count = sapp_sample_count();
        metadata.color_format = sapp_color_format(); metadata.depth_format = sapp_depth_format();
        metadata.trust_hidden_signals = true; metadata.occluded = sapp_window_occluded({0});
        metadata.drawable_available = metadata.fb_width > 0 && metadata.fb_height > 0;
        publishWindowMetadata(*window, metadata);
    }
}
void bindSingleWindowMain() {
    if constexpr (!supportsSecondaryWindows()) {
        if (!headless::isActive() && sapp_isvalid()) {
            // Single-window ports have a native canvas/surface, without sapp_window IDs.
            WindowRequestAccess::binding(*mainWindow()).bound = true;
        }
    }
}
}
}
