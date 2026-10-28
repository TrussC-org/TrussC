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
    ctx_.pauseUpdatesWhenHidden = !mainWindow;
    ctx_.drawWhileHidden = mainWindow;
    ctx_.deferredResponseTarget = mainWindow ? nullptr : &ctx_;
    clearColorSet_ = !mainWindow;
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
    if (!user) return {};  // the adapter cleared its user during teardown
    auto& b = WindowRequestAccess::binding(*static_cast<Window*>(user));
    if (!b.bound || !b.operations || !b.operations->acquireSwapchain) return {};
    return b.operations->acquireSwapchain(b.handle, b.user);
}
void activateBinding(void* user) {
    if (!user) return;  // the adapter cleared its user during teardown
    auto& b = WindowRequestAccess::binding(*static_cast<Window*>(user));
    if (b.bound && b.operations && b.operations->activate)
        b.operations->activate(b.handle, b.user);
}
}
#if !defined(_WIN32) && !(defined(__APPLE__) && TARGET_OS_OSX) && !(defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__))
void nativeWindowSetTitle(Window&, const char*) {}
void nativeWindowSetSize(Window&, double, double, bool) {}
void nativeWindowSetPosition(Window&, int, int) {}
IVec2 nativeWindowPosition(const Window&) { return {-1, -1}; }
void nativeWindowSetFullscreen(Window&, bool) {}
uint32_t nativeWindowFullscreenPhase(const Window&) { return 0; }
void nativeWindowToggleFullscreen(Window&) {}
#endif
WindowContext& nonWindowContext() { return nonWindowState().context; }
WindowContext* nonWindowContextIfInitialized() noexcept { return initializedNonWindow; }
const WindowNativeOperations& productionWindowOperations() {
    static const WindowNativeOperations operations{readMetadata, acquireSwapchain, activate,
        nativeWindowSetTitle, nativeWindowSetSize, nativeWindowSetPosition, nativeWindowPosition,
        nativeWindowSetFullscreen, nativeWindowFullscreenPhase, nativeWindowToggleFullscreen};
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
bool refreshWindowMetadata(Window& window) {
    auto& b = WindowRequestAccess::binding(window);
    if (!b.bound) return false;
    if (!b.operations || !b.operations->readMetadata) return true;
    sapp_tc_window_metadata metadata{};
    if (!b.operations->readMetadata(b.handle, b.user, metadata)) return false;
    publishWindowMetadata(window, metadata);
    return true;
}
bool bindWindow(Window& window, sapp_window handle, const WindowNativeOperations& operations, void* user) {
    if (!handle.id || !operations.readMetadata) return false;
    sapp_tc_window_metadata metadata{};
    if (!operations.readMetadata(handle, user, metadata) || metadata.fb_width <= 0 || metadata.fb_height <= 0 ||
        !std::isfinite(metadata.dpi_scale) || metadata.dpi_scale <= 0) return false;
    auto& b = WindowRequestAccess::binding(window);
    b.handle = handle; b.operations = &operations; b.user = user; b.bound = true;
    publishWindowMetadata(window, metadata);
    if (!b.geometryInitialized) {
        auto& geometry = b.geometry;
        const double scale = geometry.pixelSize ? metadata.dpi_scale : 1.0;
        geometry.requestedWidth = geometry.requestedWidth > 0 ? geometry.requestedWidth / scale : metadata.width;
        geometry.requestedHeight = geometry.requestedHeight > 0 ? geometry.requestedHeight / scale : metadata.height;
        b.geometryInitialized = true;
    }
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
        metadata.source_interval = sapp_frame_duration();
        publishWindowMetadata(*window, metadata);
    }
}
bool refreshWindowEntryMetadata(Window& window) {
    if constexpr (supportsSecondaryWindows()) return refreshWindowMetadata(window);
    else { refreshMainWindowMetadata(); return true; }
}
void bindSingleWindowMain() {
    if constexpr (!supportsSecondaryWindows()) {
        if (!headless::isActive() && sapp_isvalid()) {
            // Single-window ports have a native canvas/surface, without sapp_window IDs.
            WindowRequestAccess::binding(*mainWindow()).bound = true;
        }
    }
}

namespace {
struct Coordinator {
    unsigned entries = 0, turns = 0;
    bool servicing = false;
    bool bootstrapping = false;
    void (*reload)() = nullptr;
};
Coordinator& coordinator() { static Coordinator state; return state; }
void processQueues() {
    auto loads = takeResourceLoadBatch();
    EntryStackGuard guard(AppEntry::Prelude);
    deliverResourceLoadBatch(std::move(loads));
    drainMainThreadQueue();
    pumpAudioDiagnostics();
    console::internal::processQueue();
#ifndef __EMSCRIPTEN__
    mcp::processHttpQueue();
#endif
}
void processMaintenance() {
#ifndef __EMSCRIPTEN__
    mcp::internal::drainDeferredResponses(nullptr, false, windowClockNow());
#endif
    if (int sig = pendingWindowExitSignal()) {
        setExitReason(sig == SIGTERM ? "sigterm" : "sigint");
        sapp_request_quit();
    }
}
}
WindowLifecycleScope::WindowLifecycleScope(Window& window) noexcept : window_(window) {
    ++window_.entryDepth_;
    ++coordinator().entries;
}
WindowLifecycleScope::~WindowLifecycleScope() noexcept {
    --coordinator().entries;
    --window_.entryDepth_;
}
HostServiceOperations& hostServiceOperationsForTests() {
    static HostServiceOperations operations{processQueues, processMaintenance};
    return operations;
}
void setHostReloadPoll(void (*poll)()) noexcept { coordinator().reload = poll; }
HostTurnScope::HostTurnScope() noexcept { beforeHostTurn(); }
HostTurnScope::~HostTurnScope() noexcept { afterHostTurn(); }
bool hostCallbacksActive() noexcept { return coordinator().entries || coordinator().servicing; }
void beforeHostTurn() noexcept {
    auto& state = coordinator();
    if (state.turns++ || state.entries || state.servicing) return;
    state.servicing = true;
    auto* previous = currentWindowCtx();
    const auto native = sapp_tc_gl_save();
    const auto sgl = sg_isvalid() ? sgl_get_context() : sgl_context{};
    TeardownSteps steps;
    steps.run("host turn services", [&] {
        // Headless services keep the explicit non-window root selected.
        if (!headless::isActive()) restoreWindowContext(nullptr);
        if (state.reload) state.reload();
        if (auto queues = hostServiceOperationsForTests().queues) queues();
        // Queue callbacks may upload/Fbo-render during a no-draw turn.
        if (sg_isvalid() && tc_internal_gpu_frame_has_work()) endGpuFrame();
    });
    steps.run("host turn caller restoration", [&] { restoreWindowContext(previous); });
    currentWindowCtx() = previous;
    if (sg_isvalid() && sgl.id) sgl_set_context(sgl);
    sapp_tc_gl_restore(native);
    state.servicing = false;
}
void afterHostTurn() noexcept {
    auto& state = coordinator();
    if (!state.turns || --state.turns || state.entries || state.servicing) return;
    state.servicing = true;
    TeardownSteps steps;
    steps.run("host turn maintenance", [&] {
        if (auto maintenance = hostServiceOperationsForTests().maintenance) maintenance();
    });
    state.servicing = false;
}
void deliverPendingWindowVisibilityAtReadyEntry(Window&) noexcept {
    // Checkpoint 3 hook: compare the authoritative snapshot to the binding's
    // lastDeliveredHidden (initially visible), then deliver raw aggregate before
    // typed occludedChanged once. Snapshot publication never consumes delivery;
    // listener subscription never replays it. No ready delivery in checkpoint 2.
}
WindowEntryScope::WindowEntryScope(Window& window, bool tick, bool bootstrap)
    : window_(window), keepAlive_(window.hostOwner_.lock()), previous_(currentWindowCtx()),
      previousSgl_(sg_isvalid() ? sgl_get_context() : sgl_context{}), native_(sapp_tc_gl_save()) {
    if (!window.isOpen() || (tick && (window.entryDepth_ || window.context().inCallbackClock ||
        window.context().inUpdate || coordinator().servicing || WindowRequestAccess::dispatchActive(window)))) return;
    ++coordinator().entries;
    ++window.entryDepth_;
    entered_ = true;
    try {
        // Park the actual caller before installing the receiver. No drawable is acquired here.
        if (&currentWindowContext() != &window.context() && sg_isvalid()) parkWindowPass();
        currentWindowCtx() = &window.context();
        if (!refreshWindowEntryMetadata(window)) return;
        requestsReady_ = true;
        restoreWindowContext(&window.context());
        if (window.entryDepth_ == 1) WindowRequestAccess::applyPendingApp(window);
        if (!window.isOpen() || (tick && WindowRequestAccess::closeRequested(window))) return;
        if (bootstrap) { admitted_ = true; return; }
        if (!coordinator().bootstrapping) deliverPendingWindowVisibilityAtReadyEntry(window);
        ++window.dispatchDepth_;
        dispatch_ = true;
        syncWindowAppSize(window, true);
        admitted_ = window.isOpen() && (!tick || !WindowRequestAccess::closeRequested(window));
    } catch (...) { leave(); throw; }
}
void WindowEntryScope::leave() noexcept {
    if (!entered_) return;
    admitted_ = false;
    TeardownSteps steps;
    if (dispatch_) { --window_.dispatchDepth_; dispatch_ = false; }
    if (requestsReady_ && window_.entryDepth_ == 1)
        steps.run("pending App at window entry exit", [&] { WindowRequestAccess::applyPendingApp(window_); });
    // Restoration is mandatory even if request callbacks or diagnostics failed.
    steps.run("window entry caller restoration", [&] { restoreWindowContext(previous_); });
    currentWindowCtx() = previous_;
    if (sg_isvalid() && !currentWindowContext().targetsClosed && previousSgl_.id) sgl_set_context(previousSgl_);
    sapp_tc_gl_restore(native_);
    --window_.entryDepth_;
    --coordinator().entries;
    entered_ = false;
}
WindowEntryScope::~WindowEntryScope() noexcept { leave(); }
void singleWindowEvent(const sapp_event* event) noexcept {
    if (!event) return;
    TeardownSteps steps;
    steps.run("main native event binding", [&] {
        auto* window = mainWindow();
        mainWindowEvent(event, WindowRequestAccess::binding(*window).handle, window);
    });
}
void mainWindowEvent(const sapp_event* event, sapp_window handle, void* user) noexcept {
    enterWindowEvent(event, handle, user,
        [](const sapp_event* ev, sapp_window, Window*, WindowContext&) { _event_cb_impl(ev); });
}
void enterWindowEvent(const sapp_event* event, sapp_window handle, void* user, WindowEventDispatch dispatch) noexcept {
    // Admission belongs to the native init predicate. Desktop init callbacks
    // may synchronously deliver events, as they did through the legacy client.
    TeardownSteps steps;
    steps.run("native window event", [&] {
        auto* window = static_cast<Window*>(user);
        if (!event || !window || WindowRequestAccess::binding(*window).handle.id != handle.id) return;
        WindowEntryScope entry(*window);
        if (!entry) return;
        observeWindowVisibility(window->context(), event->type);
        restoreWindowContext(&window->context());
        dispatch(event, handle, window, window->context());
        if (event->type == SAPP_EVENTTYPE_SUSPENDED || event->type == SAPP_EVENTTYPE_RESUMED)
            WindowRequestAccess::binding(*window).lastDeliveredHidden = window->isOccluded();
    });
}
bool enterWindowTick(Window& window, sapp_window handle, std::chrono::steady_clock::time_point now,
                     double duration, const WindowTickPresentation* presentation) {
    if (WindowRequestAccess::binding(window).handle.id != handle.id) return false;
    WindowEntryScope entry(window, true);
    if (!entry) return false;
    auto& ctx = window.context();
    syncWindowAppSize(window);
    const auto& metadata = WindowRequestAccess::binding(window).metadata;
    const bool gated = (window.isOccluded() && !ctx.drawWhileHidden) || ctx.fbWidth <= 0 || ctx.fbHeight <= 0;
    crashFrame(sapp_frame_count());
    CrashPhaseScope crashPhase("frame");
    bool forceDraw = !ctx.asyncScreenshots.pending.empty() || !ctx.pendingScreenshotPaths.empty();
#ifndef __EMSCRIPTEN__
    forceDraw = forceDraw || mcp::internal::hasDeferredResponsesForTarget(ctx.deferredResponseTarget);
#endif
    // Binding metadata supplies source timing without asking another window.
    const double source = metadata.source_interval > 0 ? metadata.source_interval : duration;
    const bool drawn = runWindowTick(now, source, forceDraw, presentation, gated);
    ctx.pmouseX = ctx.mouseX; ctx.pmouseY = ctx.mouseY;
    return drawn;
}
void windowTick(sapp_window handle, void* user) noexcept {
    TeardownSteps steps;
    bool drawn = false;
    steps.run("native window tick", [&] {
        if (auto* window = static_cast<Window*>(user)) {
            const double duration = supportsSecondaryWindows() ? sapp_window_frame_duration(handle) : sapp_frame_duration();
            drawn = enterWindowTick(*window, handle, windowClockNow(), duration);
        }
    });
    if (!drawn) {
        if constexpr (supportsSecondaryWindows()) sapp_tc_window_skip_present(handle);
        else sapp_skip_present();
    }
}
void mainWindowBootstrap(void (*bootstrap)()) {
    auto& state = coordinator();
    state.bootstrapping = true;
    try {
        bindSingleWindowMain();
        WindowEntryScope entry(*mainWindow(), false, true);
        if (entry) bootstrap();
    } catch (...) {
        state.bootstrapping = false;
        throw;
    }
    state.bootstrapping = false;
}
void windowClosed(sapp_window handle, void* user) noexcept {
    TeardownSteps steps;
    steps.run("native window close", [&] {
        auto* window = static_cast<Window*>(user);
        if (!window || !window->isOpen() || WindowRequestAccess::binding(*window).handle.id != handle.id) return;
        auto keepAlive = WindowRequestAccess::retainHostOwner(*window);
        WindowLifecycleScope callback(*window);
        WindowRequestAccess::teardown(*window);
    });
}
void singleWindowFrame() noexcept {
    beforeHostTurn();
    TeardownSteps steps;
    steps.run("single-window native tick binding", [&] {
        auto* window = mainWindow();
        windowTick(WindowRequestAccess::binding(*window).handle, window);
    });
    afterHostTurn();
}
}
}
