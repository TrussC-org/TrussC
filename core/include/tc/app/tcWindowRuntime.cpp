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
    ctx_.hostWindow = this;
    ctx_.isMain = mainWindow;
    ctx_.pauseUpdatesWhenHidden = !mainWindow;
    ctx_.drawWhileHidden = mainWindow;
    ctx_.deferredResponseTarget = mainWindow ? nullptr : &ctx_;
    clearColorSet_ = !mainWindow;
}

namespace internal {
namespace {
void processQueues();
void processMaintenance();
}
// One host instance, shared by ordinary apps, guests and headless runs. This
// substrate adds no Window registry or attachment/ending authority (C1).
struct Runtime {
    unsigned entries = 0, turns = 0;
    bool servicing = false, bootstrapping = false, servicedTurn = false;
    uint64_t epoch = 0;
    void (*reload)() = nullptr;
    HostServiceOperations services{processQueues, processMaintenance};
    std::shared_ptr<std::atomic<bool>> runToken;
    AppConstructionTicket* constructor = nullptr;
    WindowContext* selected = nullptr;
#ifndef __EMSCRIPTEN__
    ThreadChannel<std::function<void()>> mainQueue;
    std::atomic<size_t> mainQueuePending{0};
#endif
};
Runtime& runtime() {
    // Async completions and static App destructors may consult it at exit.
    static auto* state = new Runtime;
    return *state;
}
RuntimeActivityScope::RuntimeActivityScope() noexcept { ++runtime().entries; }
RuntimeActivityScope::~RuntimeActivityScope() noexcept { --runtime().entries; }
WindowContext*& currentWindowCtx() { return runtime().selected; }
std::shared_ptr<std::atomic<bool>>& appRunToken() {
    auto& state = runtime();
    return state.constructor ? state.constructor->runToken : state.runToken;
}
#ifndef __EMSCRIPTEN__
ThreadChannel<std::function<void()>>& mainThreadQueue() { return runtime().mainQueue; }
std::atomic<size_t>& mainThreadQueuePendingCount() { return runtime().mainQueuePending; }
#endif
uint64_t runtimeEpoch() noexcept { return runtime().epoch; }
void beginRuntimeRun() {
    auto& state = runtime();
    if (state.entries || state.turns || state.servicing || state.constructor)
        throw std::logic_error("a Runtime run cannot begin inside a host entry");
    if (state.epoch == UINT64_MAX) throw std::overflow_error("Runtime epochs exhausted");
    auto token = std::make_shared<std::atomic<bool>>(true);
    if (state.runToken) state.runToken->store(false);
    {
        RuntimeActivityScope starting;
        cancelResourceBackends();
    }
    state.runToken = std::move(token);
    ++state.epoch;
    state.servicedTurn = false;
}
AppConstructionTicket* currentAppConstruction() noexcept { return runtime().constructor; }
std::shared_ptr<std::atomic<bool>> constructorRunToken() {
    return appRunToken();
}
AppConstructionScope::AppConstructionScope(AppConstructionTicket& ticket)
    : ticket_(ticket), previous_(currentAppConstruction()), previousOwner_(mcp::internal::registrationOwner()),
      exceptions_(std::uncaught_exceptions()) {
    if (!ticket.runToken) ticket.runToken = appRunToken();
    if (!ticket.epoch) ticket.epoch = runtimeEpoch();
    runtime().constructor = &ticket;
    ++runtime().entries;
    mcp::internal::setRegistrationOwner(ticket.registrationOwner);
}
AppConstructionScope::~AppConstructionScope() noexcept {
    if (std::uncaught_exceptions() > exceptions_) ticket_.app = nullptr;
    mcp::internal::setRegistrationOwner(previousOwner_);
    runtime().constructor = previous_;
    --runtime().entries;
}
void completeAppConstruction(App& app) noexcept {
    if (auto* ticket = currentAppConstruction(); ticket && (!ticket->app || ticket->app == &app)) {
        ticket->app = &app;
        app.constructorTicket_ = *ticket;
    }
}
void WindowContext::admitMousePress(int button) noexcept {
    ++mousePressGenerations[button];
    // Scoped MCP owners cannot outlive a superseded click's stack frame.
    mousePressOwners[button] = nullptr;
    // Admission only invalidates older continuations. Delivery keeps the base
    // ledger, grab and touch identity until their ordinary conversion sites.
}
MouseDispatchScope::MouseDispatchScope(WindowContext& ctx, int pressButton, bool force) noexcept
    : ctx_(ctx), previous_(ctx.mouseDispatch), installed_(force || !previous_) {
    if (!installed_) return;
    if (pressButton >= 0 && pressButton < SAPP_MAX_MOUSEBUTTONS) {
        ctx_.admitMousePress(pressButton);
    }
    std::copy(std::begin(ctx_.mousePressGenerations), std::end(ctx_.mousePressGenerations), epochs_);
    trackMotionButtons(ctx_.mouseButton);
    ctx_.mouseDispatch = this;
}
MouseDispatchScope::~MouseDispatchScope() noexcept {
    if (installed_) ctx_.mouseDispatch = previous_;
}
bool MouseDispatchScope::current(int button) const noexcept {
    if (!installed_) return previous_->current(button);
    if (button >= 0 && button < SAPP_MAX_MOUSEBUTTONS)
        return epochs_[button] == ctx_.mousePressGenerations[button];
    for (int b = 0; b < SAPP_MAX_MOUSEBUTTONS; ++b)
        if ((motionButtons_ & (1u << b)) && epochs_[b] != ctx_.mousePressGenerations[b]) return false;
    return true;
}

void MouseDispatchScope::trackMotionButtons(int dragButton) noexcept {
    if (!installed_) { previous_->trackMotionButtons(dragButton); return; }
    // Explicit routing starts a fresh typed frame after its old epochs were
    // checked; -2 denotes no reported drag. Node continuation (-1) only adds
    // its current grab, never an unrelated legacy mouseButton from touch.
    if (dragButton != -1) motionButtons_ = 0;
    for (int button : {dragButton, ctx_.grabbedNode.expired() ? -1 : ctx_.grabbedButton}) {
        if (button >= 0 && button < SAPP_MAX_MOUSEBUTTONS) {
            if (!(motionButtons_ & (1u << button))) {
                epochs_[button] = ctx_.mousePressGenerations[button];
                motionButtons_ |= 1u << button;
            }
        }
    }
}

void MouseDispatchScope::beginRecovery(int button) noexcept {
    if (!installed_) { previous_->beginRecovery(button); return; }
    if (!(motionButtons_ & (1u << button))) epochs_[button] = ctx_.mousePressGenerations[button];
}

namespace {
constinit WindowContext* initializedNonWindow = nullptr;
struct NonWindowState {
    RenderContext processRender;
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
// Keep canonical prelaunch/headless references stable while isolating service
// drawing style (including its stack and entry bookkeeping) from main.
struct ProcessDrawingScope {
    WindowContext& context = nonWindowState().context;
    RenderContext* previous = context.render;
    ProcessDrawingScope() { context.render = &nonWindowState().processRender; }
    ~ProcessDrawingScope() { context.render = previous; }
};
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
bool injectEvent(sapp_window handle, void*, const sapp_event* event) { return sapp_window_inject_event(handle, event); }
bool closeRecorded(sapp_window handle, void*) { return sapp_tc_window_close_recorded(handle); }
bool readMetadata(sapp_window handle, void*, sapp_tc_window_metadata& out) {
    return sapp_tc_window_read_metadata(handle, &out);
}
sg_swapchain acquireSwapchain(sapp_window handle, void*) {
    sapp_tc_window_metadata metadata{};
    sg_swapchain sc{};
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
    // Acquiring a drawable can absorb an external resize (notably Retina).
    // Read its metadata afterwards, so the pass has one native size snapshot.
    if (!sapp_tc_window_read_metadata(handle, &metadata)) return {};
    sc.width = metadata.fb_width; sc.height = metadata.fb_height;
    sc.sample_count = metadata.sample_count;
    sc.color_format = pixelFormat(metadata.color_format);
    sc.depth_format = pixelFormat(metadata.depth_format);
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
    const auto swapchain = b.operations->acquireSwapchain(b.handle, b.user);
    if (swapchain.width > 0 && swapchain.height > 0) {
        auto metadata = b.metadata;
        if (b.operations->readMetadata && !b.operations->readMetadata(b.handle, b.user, metadata)) return {};
        metadata.fb_width = swapchain.width; metadata.fb_height = swapchain.height;
        publishWindowMetadata(*static_cast<Window*>(user), metadata);
    }
    return swapchain;
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
        nativeWindowSetFullscreen, nativeWindowFullscreenPhase, nativeWindowToggleFullscreen, closeRecorded, injectEvent};
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
    const bool hidden = b.bound && metadata.trust_hidden_signals && (metadata.iconified || metadata.occluded);
    if (ctx.suspended != hidden) ++b.visibilityRevision;
    ctx.suspended = hidden;
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
    if (b.handle.id != handle.id) {
        b.lastDeliveredHidden = false; b.visibilityReady = false;
        window.context().pointerValid = false; window.context().touchMapper.reset();
    }
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
Runtime& coordinator() { return runtime(); }
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
    return runtime().services;
}
void setHostReloadPoll(void (*poll)()) noexcept { coordinator().reload = poll; }
HostTurnScope::HostTurnScope() noexcept { beforeHostTurn(); }
HostTurnScope::~HostTurnScope() noexcept { afterHostTurn(); }
bool hostCallbacksActive() noexcept { return coordinator().entries || coordinator().servicing; }
void beforeHostTurn() noexcept {
    auto& state = coordinator();
    if (state.turns++ || state.entries || state.servicing) return;
    state.servicing = true;
    state.servicedTurn = true;
    auto* previous = currentWindowCtx();
    const auto native = sapp_tc_gl_save();
    const auto sgl = sg_isvalid() ? sgl_get_context() : sgl_context{};
    TeardownSteps steps;
    steps.run("host turn services", [&] {
        restoreWindowContext(nullptr);
        ProcessDrawingScope drawing;
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
    if (!state.turns || --state.turns || state.entries || state.servicing || !state.servicedTurn) return;
    state.servicedTurn = false;
    state.servicing = true;
    auto* previous = currentWindowCtx();
    const auto native = sapp_tc_gl_save();
    const auto sgl = sg_isvalid() ? sgl_get_context() : sgl_context{};
    TeardownSteps steps;
    steps.run("host turn maintenance", [&] {
        restoreWindowContext(nullptr);
        ProcessDrawingScope drawing;
        if (auto maintenance = hostServiceOperationsForTests().maintenance) maintenance();
    });
    steps.run("maintenance caller restoration", [&] { restoreWindowContext(previous); });
    currentWindowCtx() = previous;
    if (sg_isvalid() && sgl.id) sgl_set_context(sgl);
    sapp_tc_gl_restore(native);
    state.servicing = false;
}
bool currentWindowCloseRecorded() {
    auto* window = currentWindowContext().hostWindow;
    if (!window) return false;
    const auto& b = WindowRequestAccess::binding(*window);
    return !window->isOpen() || WindowRequestAccess::closeRequested(*window) ||
        (b.operations && b.operations->closeRecorded && b.operations->closeRecorded(b.handle, b.user));
}
void deliverPendingWindowVisibilityAtReadyEntry(Window& window) {
    auto& b = WindowRequestAccess::binding(window);
    if constexpr (supportsSecondaryWindows()) { if (b.visibilityReady) return; }
    b.visibilityReady = true;
    if (!b.metadata.trust_hidden_signals) { b.lastDeliveredHidden = false; return; }
    if (b.lastDeliveredHidden == window.isOccluded()) return;
    sapp_event event{};
    event.type = window.isOccluded() ? SAPP_EVENTTYPE_SUSPENDED : SAPP_EVENTTYPE_RESUMED;
    event.window_id = b.handle.id; event.dpi_scale = b.metadata.dpi_scale;
    event.frame_count = sapp_frame_count();
    event.window_width = b.metadata.width; event.window_height = b.metadata.height;
    event.framebuffer_width = b.metadata.fb_width; event.framebuffer_height = b.metadata.fb_height;
    convertWindowEvent(&event, window);
}
WindowEntryScope::WindowEntryScope(Window& window, bool tick, bool bootstrap, bool pendingVisibility)
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
        ++window.dispatchDepth_;
        dispatch_ = true;
        syncWindowAppSize(window, true);
        if (pendingVisibility && !coordinator().bootstrapping) deliverPendingWindowVisibilityAtReadyEntry(window);
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
    // The name an event entry point (#349) gives in its warning, from the
    // sapp event type: the App handler it reaches. `dragging`: a mouse move
    // with a button held, delivered as mouseDragged(). Text only; every event
    // shares AppEntry::Event's rate limit.
    const char* eventEntryName(const sapp_event* ev, bool dragging) {
        switch (ev->type) {
            case SAPP_EVENTTYPE_KEY_DOWN:          return "keyPressed()";
            case SAPP_EVENTTYPE_KEY_UP:            return "keyReleased()";
            case SAPP_EVENTTYPE_MOUSE_DOWN:        return "mousePressed()";
            case SAPP_EVENTTYPE_MOUSE_UP:          return "mouseReleased()";
            case SAPP_EVENTTYPE_MOUSE_ENTER:
            case SAPP_EVENTTYPE_MOUSE_MOVE:        return dragging ? "mouseDragged()" : "mouseMoved()";
            case SAPP_EVENTTYPE_MOUSE_SCROLL:      return "mouseScrolled()";
            case SAPP_EVENTTYPE_TOUCHES_BEGAN:     return "touchPressed()";
            case SAPP_EVENTTYPE_TOUCHES_MOVED:     return "touchMoved()";
            case SAPP_EVENTTYPE_TOUCHES_ENDED:
            case SAPP_EVENTTYPE_TOUCHES_CANCELLED: return "touchReleased()";
            case SAPP_EVENTTYPE_RESIZED:           return "windowResized()";
            case SAPP_EVENTTYPE_FILES_DROPPED:     return "filesDropped()";
            case SAPP_EVENTTYPE_CLIPBOARD_PASTED:  return "the clipboardPasted event";
            case SAPP_EVENTTYPE_TC_DEVICE_LOST:    return "the deviceLost event";
            case SAPP_EVENTTYPE_QUIT_REQUESTED:    return "the exitRequested event";
            case SAPP_EVENTTYPE_INVALID:
            case SAPP_EVENTTYPE_CHAR:
            case SAPP_EVENTTYPE_MOUSE_LEAVE:
            case SAPP_EVENTTYPE_ICONIFIED:
            case SAPP_EVENTTYPE_RESTORED:
            case SAPP_EVENTTYPE_FOCUSED:
            case SAPP_EVENTTYPE_UNFOCUSED:
            case SAPP_EVENTTYPE_SUSPENDED:
            case SAPP_EVENTTYPE_RESUMED:
            case _SAPP_EVENTTYPE_NUM:
            case _SAPP_EVENTTYPE_FORCE_U32: return "a rawEvent listener";
        }
        return "a rawEvent listener";
    }

    unsigned int mouseButtonBit(int button) {
        switch (button) {
            case SAPP_MOUSEBUTTON_LEFT:   return 1u << 0;
            case SAPP_MOUSEBUTTON_RIGHT:  return 1u << 1;
            case SAPP_MOUSEBUTTON_MIDDLE: return 1u << 2;
            default: return 0;
        }
    }

    bool mouseMoveHasReliableButtonBits(const sapp_event* ev) {
        return ev->reliable_button_bits && !ev->pointer_locked;
    }

    // Call only for moves whose reliability was captured before raw listeners.
    bool mouseButtonLostOnMove(const sapp_event* ev, int button) {
        unsigned int bit = 0;
        switch (button) {
            case SAPP_MOUSEBUTTON_LEFT:   bit = SAPP_MODIFIER_LMB; break;
            case SAPP_MOUSEBUTTON_RIGHT:  bit = SAPP_MODIFIER_RMB; break;
            case SAPP_MOUSEBUTTON_MIDDLE: bit = SAPP_MODIFIER_MMB; break;
            default: return false;
        }
        return (ev->modifiers & bit) == 0;
    }

enum class VisibilityDelivery { Drop, RawOnly, RawThenTyped };
VisibilityDelivery prepareWindowVisibilityDelivery(Window& window, sapp_event_type type) {
    if (type != SAPP_EVENTTYPE_SUSPENDED && type != SAPP_EVENTTYPE_RESUMED) return VisibilityDelivery::RawOnly;
    auto& binding = WindowRequestAccess::binding(window);
    if (!binding.metadata.trust_hidden_signals) {
        binding.lastDeliveredHidden = false; binding.visibilityReady = true;
        return supportsSecondaryWindows() ? VisibilityDelivery::Drop : VisibilityDelivery::RawOnly;
    }
    const bool hidden = window.isOccluded();
    // Single-window lifecycle notices remain raw even when visibility differs.
    // A matching visibility pair is delivered separately from that reason.
    if ((type == SAPP_EVENTTYPE_SUSPENDED) != hidden)
        return supportsSecondaryWindows() ? VisibilityDelivery::Drop : VisibilityDelivery::RawOnly;
    binding.visibilityReady = true;
    if (binding.lastDeliveredHidden == hidden)
        return supportsSecondaryWindows() ? VisibilityDelivery::Drop : VisibilityDelivery::RawOnly;
    // Commit the notification attempt before raw listeners can reenter or throw.
    binding.lastDeliveredHidden = hidden;
    return VisibilityDelivery::RawThenTyped;
}
    void convertWindowEvent(const sapp_event* ev, Window& window) {
        auto& ctx = window.context();
        const void* pressOwner = (ev->type == SAPP_EVENTTYPE_MOUSE_DOWN || ev->type == SAPP_EVENTTYPE_MOUSE_UP)
            ? std::exchange(ctx.pendingMousePressOwner, nullptr) : nullptr;
        const bool hidden = window.isOccluded();
        const auto visibility = prepareWindowVisibilityDelivery(window, ev->type);
        if (visibility == VisibilityDelivery::Drop) return;
        const bool visibilityChanged = visibility == VisibilityDelivery::RawThenTyped;
        const auto visibilityRevision = WindowRequestAccess::binding(window).visibilityRevision;
        auto currentVisibility = [&] { return WindowRequestAccess::binding(window).visibilityRevision == visibilityRevision; };
        WindowCallbackTimeScope callbackTime;
        CrashPhaseScope crashPhase("event dispatch");
        if (ev->type >= SAPP_EVENTTYPE_KEY_DOWN && ev->type <= SAPP_EVENTTYPE_TOUCHES_CANCELLED) {
            auto diagnosticCoordinate = [](float value) {
                // Bound (diagnostic): integer log coordinates saturate; non-finite values log as zero without changing input.
                if (!std::isfinite(value)) return 0;
                if (double(value) >= std::numeric_limits<int>::max()) return std::numeric_limits<int>::max();
                if (double(value) <= std::numeric_limits<int>::min()) return std::numeric_limits<int>::min();
                return static_cast<int>(value);
            };
            crashInput(static_cast<int>(ev->type), diagnosticCoordinate(ev->mouse_x),
                       diagnosticCoordinate(ev->mouse_y),
                       ev->type == SAPP_EVENTTYPE_KEY_DOWN || ev->type == SAPP_EVENTTYPE_KEY_UP
                           ? static_cast<int>(ev->key_code) : static_cast<int>(ev->mouse_button));
        }
        // Each event is an entry point (#349): the listeners, the App's
        // handler and the Node handlers it reaches leave the stacks as they
        // found them.
        EntryStackGuard guard(AppEntry::Event, eventEntryName(ev, ctx.mouseButton >= 0));

        // A raw listener can change pointer lock; keep this move's original
        // button-bit reliability for the entire dispatch.
        const bool recoverButtonsOnMove = ev->type == SAPP_EVENTTYPE_MOUSE_MOVE && !ev->pointer_locked &&
            (ev->button_snapshot || mouseMoveHasReliableButtonBits(ev));

        // Keep this event's coordinate basis through raw-listener reentry.
        const float dpi = ev->dpi_scale > 0 ? ev->dpi_scale : ctx.dpiScale;
        const double scale = pixelPerfectMode() ? 1.0 : 1.0 / double(dpi);
        const bool locked = ev->pointer_locked;
        const float basis = pixelPerfectMode() ? dpi : 1.0f;
        auto finitePointer = [&](double x, double y) {
            const double px = ctx.pointerValid ? (ctx.pointerBasis == basis ? ctx.lastPointerX : ctx.pointerLogicalX * basis) : x;
            const double py = ctx.pointerValid ? (ctx.pointerBasis == basis ? ctx.lastPointerY : ctx.pointerLogicalY * basis) : y;
            // Bound (abi): converted positions and re-expressed anchors must fit finite float fields before delivery or mutation.
            for (double value : {x, y, px, py})
                if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) return false;
            // Validate the difference of the actual rounded ABI positions, including a rebased anchor.
            const double dx = !ctx.pointerValid ? 0 : locked ? double(ev->mouse_dx) * scale : double(float(x)) - double(float(px));
            const double dy = !ctx.pointerValid ? 0 : locked ? double(ev->mouse_dy) * scale : double(float(y)) - double(float(py));
            // Bound (abi): actual emitted deltas must fit finite float fields before delivery or mutation.
            for (double value : {dx, dy})
                if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) return false;
            return true;
        };
        if (ev->type == SAPP_EVENTTYPE_MOUSE_DOWN || ev->type == SAPP_EVENTTYPE_MOUSE_UP ||
            ev->type == SAPP_EVENTTYPE_MOUSE_MOVE || ev->type == SAPP_EVENTTYPE_MOUSE_ENTER) {
            if (!finitePointer(double(ev->mouse_x) * scale, double(ev->mouse_y) * scale)) return;
        }
        const bool isTouch = ev->type >= SAPP_EVENTTYPE_TOUCHES_BEGAN && ev->type <= SAPP_EVENTTYPE_TOUCHES_CANCELLED;
        internal::TouchSample samples[TouchEventArgs::MAX_TOUCHES];
        const int touchCount = isTouch ? std::clamp<int>(ev->num_touches, 0, SAPP_MAX_TOUCHPOINTS) : 0;
        const auto phase = ev->type == SAPP_EVENTTYPE_TOUCHES_BEGAN ? internal::TouchPhase::Began :
            ev->type == SAPP_EVENTTYPE_TOUCHES_MOVED ? internal::TouchPhase::Moved :
            ev->type == SAPP_EVENTTYPE_TOUCHES_CANCELLED ? internal::TouchPhase::Cancelled : internal::TouchPhase::Ended;
        for (int i = 0; i < touchCount; ++i) {
            const auto& point = ev->touches[i];
            // Bound (abi): typed touch positions must fit finite float fields before any listener runs.
            for (double value : {double(point.pos_x) * scale, double(point.pos_y) * scale})
                if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) return;
            samples[i] = {point.identifier, double(point.pos_x) * scale, double(point.pos_y) * scale, point.changed};
        }
        auto mappedTouch = [&](internal::TouchMouseMapper& candidate) {
            if (!touchAsMouse()) { candidate.reset(); return internal::TouchMouseAction{}; }
            return candidate.update(phase, samples, touchCount);
        };
        auto finiteTouchAction = [&](const internal::TouchMouseAction& action) {
            return action.kind == internal::TouchMouseAction::Kind::None || finitePointer(action.x, action.y);
        };
        auto touchCandidate = ctx.touchMapper;
        const auto touchAction = isTouch ? mappedTouch(touchCandidate) : internal::TouchMouseAction{};
        if (isTouch && !finiteTouchAction(touchAction)) return;
        unsigned int recoveryButtonsAtEntry = ctx.pressedMouseButtons;
        // Snapshot-held input is also a press admission, even without a typed DOWN.
        if (ev->button_snapshot && ev->type == SAPP_EVENTTYPE_MOUSE_MOVE) {
            for (int button = 0; button < SAPP_MAX_MOUSEBUTTONS; ++button) {
                if (!mouseButtonLostOnMove(ev, button)) {
                    recoveryButtonsAtEntry |= mouseButtonBit(button);
                    if (!(ctx.pressedMouseButtons & mouseButtonBit(button))) ctx.admitMousePress(button);
                }
            }
        }
        const int pressButton = ev->type == SAPP_EVENTTYPE_MOUSE_DOWN ? int(ev->mouse_button) : -1;
        MouseDispatchScope dispatch(ctx, pressButton, true);
        // Raw or release listeners may reenter with a different DPI revision.
        auto rebasePointer = [&](int button = -1) {
            if (!dispatch.current(button)) return;
            if (ctx.pointerValid && ctx.pointerBasis != basis) {
                if (!finitePointer(ctx.pointerLogicalX * basis, ctx.pointerLogicalY * basis))
                    throw std::range_error("pointer anchor is out of range after callback");
                ctx.lastPointerX = ctx.pointerLogicalX * basis;
                ctx.lastPointerY = ctx.pointerLogicalY * basis;
                ctx.pointerBasis = basis;
            }
        };
        auto position = [&](double x, double y, int button = -1) {
            if (!dispatch.current(button)) return Vec2(0, 0);
            if (!finitePointer(x, y)) throw std::range_error("pointer position or delta is out of range after callback");
            rebasePointer(button);
            const Vec2 previous(ctx.lastPointerX, ctx.lastPointerY);
            const bool valid = ctx.pointerValid;
            if (!locked || !valid) {
                ctx.lastPointerX = x; ctx.lastPointerY = y;
                ctx.pointerLogicalX = double(x) / basis; ctx.pointerLogicalY = double(y) / basis;
                ctx.pointerBasis = basis;
                ctx.pointerValid = true;
            }
            ctx.mouseX = ctx.lastPointerX; ctx.mouseY = ctx.lastPointerY;
            if (locked) return valid ? Vec2(ev->mouse_dx * scale, ev->mouse_dy * scale) : Vec2(0, 0);
            return valid ? Vec2(x, y) - previous : Vec2(0, 0);
        };
        // Notify raw event listeners (used by addons like tcxImGui)
        window.events().rawEvent.notify(*ev);
        if (visibilityChanged && currentVisibility()) {
            logNotice("Window") << (hidden ? "Window is hidden" : "Window is visible");
            OccludedEventArgs args; args.occluded = hidden;
            GuardedEventDelivery<OccludedEventArgs>::notify(window.events().occludedChanged, args, currentVisibility);
        }

        // ev->mouse_x/y arrive in framebuffer coordinates
        // pixelPerfectMode() == true: use as-is (coords = framebuffer size)
        // pixelPerfectMode() == false: divide by DPI scale to get logical coords
        bool hasModShift = (ev->modifiers & SAPP_MODIFIER_SHIFT) != 0;
        bool hasModCtrl = (ev->modifiers & SAPP_MODIFIER_CTRL) != 0;
        bool hasModAlt = (ev->modifiers & SAPP_MODIFIER_ALT) != 0;
        bool hasModSuper = (ev->modifiers & SAPP_MODIFIER_SUPER) != 0;

        auto releaseMouse = [&](int button) {
            const unsigned int bit = mouseButtonBit(button);
            if (!dispatch.current(button) || (ctx.pressedMouseButtons & bit) == 0) return;
            rebasePointer(button);
            // Clear this press before notifying. Keep the base drag semantics:
            // any delivered release ends the current drag-button report.
            ctx.pressedMouseButtons &= ~bit;
            ctx.snapshotMouseButtons &= ~bit;
            ctx.mouseButton = -1;
            ctx.mousePressed = false;
            MouseEventArgs args;
            args.pos = args.globalPos = Vec2(ctx.lastPointerX, ctx.lastPointerY);
            args.button = button;
            args.shift = hasModShift;
            args.ctrl = hasModCtrl;
            args.alt = hasModAlt;
            args.super = hasModSuper;
            args.syncLegacy();
            window.events().mouseReleased.notify(args);
        };

        for (int button = SAPP_MOUSEBUTTON_LEFT; button <= SAPP_MOUSEBUTTON_MIDDLE; ++button) {
            if (recoverButtonsOnMove && (ctx.pressedMouseButtons & mouseButtonBit(button)) && mouseButtonLostOnMove(ev, button)) {
                // A button first held by raw reentry has no older recovery
                // gesture in this move. Start its base recovery now; retain
                // admission epochs for buttons this move already concerned.
                if (!(recoveryButtonsAtEntry & mouseButtonBit(button))) dispatch.beginRecovery(button);
                releaseMouse(button); // last real position, before updating the move
                if (dispatch.current(SAPP_MOUSEBUTTON_LEFT)) ctx.touchMapper.reset();
            }
        }

        switch (ev->type) {
            case SAPP_EVENTTYPE_UNFOCUSED:
                if (dispatch.current(SAPP_MOUSEBUTTON_LEFT)) ctx.touchMapper.reset();
                for (int button = SAPP_MOUSEBUTTON_LEFT; button <= SAPP_MOUSEBUTTON_MIDDLE; ++button)
                    releaseMouse(button);
                break;
            case SAPP_EVENTTYPE_KEY_DOWN: {
                // Notify event system
                KeyEventArgs args;
                args.key = ev->key_code;
                args.isRepeat = ev->key_repeat;
                args.shift = hasModShift;
                args.ctrl = hasModCtrl;
                args.alt = hasModAlt;
                args.super = hasModSuper;
                // Track key state (only on first press, not auto-repeat)
                if (!ev->key_repeat) {
                    ctx.keysPressed.insert(ev->key_code);
                }

                window.events().keyPressed.notify(args); // auto-repeat is delivered too

                break;
            }
            case SAPP_EVENTTYPE_KEY_UP: {
                KeyEventArgs args;
                args.key = ev->key_code;
                args.isRepeat = false;
                args.shift = hasModShift;
                args.ctrl = hasModCtrl;
                args.alt = hasModAlt;
                args.super = hasModSuper;
                // Track key state
                ctx.keysPressed.erase(ev->key_code);
                window.events().keyReleased.notify(args);
                break;
            }
            case SAPP_EVENTTYPE_MOUSE_DOWN: {
                if (!dispatch.current(ev->mouse_button)) break;
                position(ev->mouse_x * scale, ev->mouse_y * scale, ev->mouse_button);
                ctx.mousePressOwners[ev->mouse_button] = pressOwner;
                ctx.pressedMouseButtons |= mouseButtonBit(ev->mouse_button);
                ctx.snapshotMouseButtons &= ~mouseButtonBit(ev->mouse_button);
                ctx.mouseButton = ev->mouse_button;
                ctx.mousePressed = true;

                // App-level args: no node transform, so pos == globalPos.
                MouseEventArgs args;
                args.pos = args.globalPos = Vec2(ctx.mouseX, ctx.mouseY);
                args.button = ev->mouse_button;
                args.shift = hasModShift;
                args.ctrl = hasModCtrl;
                args.alt = hasModAlt;
                args.super = hasModSuper;
                args.syncLegacy();
                window.events().mousePressed.notify(args);
                break;
            }
            case SAPP_EVENTTYPE_MOUSE_UP: {
                if (!dispatch.current(ev->mouse_button)) break;
                if (pressOwner && ctx.mousePressOwners[ev->mouse_button] != pressOwner) break;
                position(ev->mouse_x * scale, ev->mouse_y * scale, ev->mouse_button);
                releaseMouse(ev->mouse_button);
                break;
            }
            case SAPP_EVENTTYPE_MOUSE_ENTER:
            case SAPP_EVENTTYPE_MOUSE_MOVE: {
                if (!dispatch.current()) break;
                const Vec2 delta = position(ev->mouse_x * scale, ev->mouse_y * scale);
                if (ev->button_snapshot) {
                    for (int button = 0; button < SAPP_MAX_MOUSEBUTTONS; ++button) {
                        if (!mouseButtonLostOnMove(ev, button)) {
                            const unsigned bit = mouseButtonBit(button);
                            if (!(ctx.pressedMouseButtons & bit)) { ctx.snapshotMouseButtons |= bit; ctx.mousePressOwners[button] = nullptr; }
                            if (button == SAPP_MOUSEBUTTON_LEFT && (ctx.snapshotMouseButtons & bit)) ctx.touchMapper.reset();
                            ctx.pressedMouseButtons |= bit;
                            ctx.mouseButton = button; ctx.mousePressed = true;
                        }
                    }
                }

                dispatch.trackMotionButtons(ctx.mouseButton >= 0 ? ctx.mouseButton : -2);

                // Rich carrier (internal::MouseEventRaw); the per-kind public type is
                // built from it at the boundary (internal::toDragArgs / internal::toMoveArgs).
                internal::MouseEventRaw args;
                args.pos = args.globalPos = Vec2(ctx.mouseX, ctx.mouseY);
                args.delta = args.globalDelta = delta;
                args.shift = hasModShift;
                args.ctrl = hasModCtrl;
                args.alt = hasModAlt;
                args.super = hasModSuper;

                if (ctx.mouseButton >= 0) {
                    args.button = ctx.mouseButton;
                    MouseDragEventArgs dragArgs = internal::toDragArgs(args);
                    window.events().mouseDragged.notify(dragArgs);
                } else {
                    MouseMoveEventArgs moveArgs = internal::toMoveArgs(args);
                    window.events().mouseMoved.notify(moveArgs);
                }
                break;
            }
            case SAPP_EVENTTYPE_MOUSE_LEAVE:
                if (!dispatch.current()) break;
                ctx.mouseX = ctx.mouseY = -1.0f;
                break;
            case SAPP_EVENTTYPE_MOUSE_SCROLL: {
                ScrollEventArgs args;
                args.pos = args.globalPos = Vec2(ctx.mouseX, ctx.mouseY);
                args.scroll = Vec2(ev->scroll_x, ev->scroll_y);
                args.shift = hasModShift;
                args.ctrl = hasModCtrl;
                args.alt = hasModAlt;
                args.super = hasModSuper;
                args.syncLegacy();
                window.events().mouseScrolled.notify(args);
                break;
            }
            // Touch events (Android/iOS)
            case SAPP_EVENTTYPE_TOUCHES_BEGAN:
            case SAPP_EVENTTYPE_TOUCHES_MOVED:
            case SAPP_EVENTTYPE_TOUCHES_ENDED:
            case SAPP_EVENTTYPE_TOUCHES_CANCELLED: {
                // Build TouchEventArgs from sokol touchpoints. The mapper below
                // keeps sokol's full uintptr_t identifier; TouchPoint::id is int.
                static_assert(TouchEventArgs::MAX_TOUCHES == SAPP_MAX_TOUCHPOINTS);
                TouchEventArgs touchArgs;
                touchArgs.numTouches = std::max(0, ev->num_touches);
                // Bound (platform): OS touch events must fit the SDK-sized event array. Overflow clamps the delivered touch count.
                if (touchArgs.numTouches > TouchEventArgs::MAX_TOUCHES)
                    touchArgs.numTouches = TouchEventArgs::MAX_TOUCHES;
                for (int i = 0; i < touchArgs.numTouches; i++) {
                    touchArgs.touches[i].id = (int)ev->touches[i].identifier;
                    touchArgs.touches[i].x = ev->touches[i].pos_x * scale;
                    touchArgs.touches[i].y = ev->touches[i].pos_y * scale;
                    touchArgs.touches[i].changed = ev->touches[i].changed;
                }

                if (phase == internal::TouchPhase::Began) window.events().touchPressed.notify(touchArgs);
                else if (phase == internal::TouchPhase::Moved) window.events().touchMoved.notify(touchArgs);
                else {
                    touchArgs.cancelled = phase == internal::TouchPhase::Cancelled;
                    window.events().touchReleased.notify(touchArgs);
                }

                // Touch-as-mouse: only the primary touch (the first finger
                // down) drives the mouse; see tc/events/tcTouchMouse.h.
                auto& mapper = ctx.touchMapper;
                if (!dispatch.current(SAPP_MOUSEBUTTON_LEFT)) break;
                auto candidate = mapper;
                const internal::TouchMouseAction action = mappedTouch(candidate);
                // Listeners can reenter input: validate again before committing the mapper or ledger.
                if (!finiteTouchAction(action)) throw std::range_error("touch action is out of range after callback");
                const double tx = action.x, ty = action.y;
                const Vec2 touchDelta = action.kind != internal::TouchMouseAction::Kind::None ? position(tx, ty, SAPP_MOUSEBUTTON_LEFT) : Vec2(0, 0);
                // Admit only an actual converted press, after callback-visible
                // setting and live-identity changes have been applied.
                MouseDispatchScope touchDispatch(ctx,
                    action.kind == internal::TouchMouseAction::Kind::Press ? SAPP_MOUSEBUTTON_LEFT : -1, true);
                touchDispatch.trackMotionButtons(SAPP_MOUSEBUTTON_LEFT);
                mapper = candidate;

                if (action.kind == internal::TouchMouseAction::Kind::Press) {
                    ctx.mousePressOwners[SAPP_MOUSEBUTTON_LEFT] = nullptr;
                    ctx.pressedMouseButtons |= mouseButtonBit(SAPP_MOUSEBUTTON_LEFT);
                    ctx.snapshotMouseButtons &= ~mouseButtonBit(SAPP_MOUSEBUTTON_LEFT);
                    ctx.mouseButton = 0;
                    ctx.mousePressed = true;

                    MouseEventArgs margs;
                    margs.pos = margs.globalPos = Vec2(ctx.mouseX, ctx.mouseY);
                    margs.button = MOUSE_BUTTON_LEFT;
                    margs.syncLegacy();
                    window.events().mousePressed.notify(margs);
                } else if (action.kind == internal::TouchMouseAction::Kind::Drag) {
                    const Vec2 delta = touchDelta;

                    internal::MouseEventRaw margs;
                    margs.pos = margs.globalPos = Vec2(ctx.mouseX, ctx.mouseY);
                    margs.delta = margs.globalDelta = delta;
                    margs.button = MOUSE_BUTTON_LEFT;
                    MouseDragEventArgs dragArgs = internal::toDragArgs(margs);
                    window.events().mouseDragged.notify(dragArgs);
                } else if (action.kind == internal::TouchMouseAction::Kind::Release) {
                    ctx.mousePressOwners[SAPP_MOUSEBUTTON_LEFT] = nullptr;
                    ctx.pressedMouseButtons &= ~mouseButtonBit(SAPP_MOUSEBUTTON_LEFT);
                    ctx.snapshotMouseButtons &= ~mouseButtonBit(SAPP_MOUSEBUTTON_LEFT);
                    ctx.mouseButton = -1;
                    ctx.mousePressed = false;

                    MouseEventArgs margs;
                    margs.pos = margs.globalPos = Vec2(ctx.mouseX, ctx.mouseY);
                    margs.button = MOUSE_BUTTON_LEFT;
                    margs.syncLegacy();
                    window.events().mouseReleased.notify(margs);
                }
                break;
            }
            case SAPP_EVENTTYPE_RESIZED: {
                // Publish the coherent native snapshot once through the common size routine.
                syncWindowAppSize(window);
                break;
            }
            case SAPP_EVENTTYPE_FILES_DROPPED: {
                DragDropEventArgs args;
                args.x = ctx.mouseX;  // Last mouse position
                args.y = ctx.mouseY;
                int numFiles = sapp_get_num_dropped_files();
                for (int i = 0; i < numFiles; i++) {
                    args.files.push_back(sapp_get_dropped_file_path(i));
                }
                window.events().filesDropped.notify(args);
                break;
            }
            case SAPP_EVENTTYPE_CLIPBOARD_PASTED: {
                // Read the clipboard now — on Web this is the only moment the
                // content is reliably available. App-level only (no Node dispatch).
                ClipboardPastedEventArgs args;
                args.text = getClipboardString();
                window.events().clipboardPasted.notify(args);
                break;
            }
            case SAPP_EVENTTYPE_TC_DEVICE_LOST: {
                DeviceLostEventArgs args;
                args.reason = ev->device_lost_reason;
                window.events().deviceLost.notify(args);
                if (!args.cancel) {
                    logError("D3D11") << "Device lost, GetDeviceRemovedReason=0x"
                        << std::hex << ev->device_lost_reason << "; exiting";
                    exitApp(1);
                    setExitReason("device-lost");
                }
                break;
            }
            case SAPP_EVENTTYPE_QUIT_REQUESTED: {
                // Notify exitRequested event - listeners can cancel by setting args.cancel = true
                setExitBlockReason("");
                ExitRequestEventArgs args;
                window.events().exitRequested.notify(args);
                if (args.cancel) {
                    sapp_cancel_quit();
                    cancelMainWindowClose();
                    cancelWindowExitSignal();
                    clearExitReason();
                    setExitBlockReason(args.reason);
                }
                break;
            }
            // These events have no typed TrussC input counterpart.
            case SAPP_EVENTTYPE_INVALID:
            case SAPP_EVENTTYPE_CHAR:
            case SAPP_EVENTTYPE_ICONIFIED:
            case SAPP_EVENTTYPE_RESTORED:
            case SAPP_EVENTTYPE_FOCUSED:
            case SAPP_EVENTTYPE_SUSPENDED:
            case SAPP_EVENTTYPE_RESUMED:
            case _SAPP_EVENTTYPE_NUM:
            case _SAPP_EVENTTYPE_FORCE_U32:
                break;
        }
    }
void dispatchWindowEventForTests(const sapp_event* input, Window& window) {
    if (!input) return;
    sapp_event event = *input;
    const auto& b = WindowRequestAccess::binding(window);
    if (!(event.dpi_scale > 0)) event.dpi_scale = window.context().dpiScale;
    event.window_id = b.handle.id;
#if defined(_WIN32) || defined(__EMSCRIPTEN__) || (defined(__linux__) && !defined(__ANDROID__))
    event.reliable_button_bits = true;
#endif
#if defined(__EMSCRIPTEN__) || (defined(__linux__) && !defined(SOKOL_GLCORE) && !defined(__ANDROID__))
    event.pointer_locked = event.pointer_locked || sapp_mouse_locked();
#endif
    enterWindowEvent(&event, b.handle, &window);
}
void dispatchWindowDisplayEventForTests(const sapp_event* event, Window& window) {
    sapp_window_inject_event(WindowRequestAccess::binding(window).handle, event);
}
void singleWindowEvent(const sapp_event* event) noexcept {
    if (!event) return;
    TeardownSteps steps;
    steps.run("main native event binding", [&] {
        auto* window = mainWindow();
        mainWindowEvent(event, WindowRequestAccess::binding(*window).handle, window);
    });
}
void mainWindowEvent(const sapp_event* event, sapp_window handle, void* user) noexcept {
    enterWindowEvent(event, handle, user);
}
void enterWindowEvent(const sapp_event* event, sapp_window handle, void* user) noexcept {
    // Admission belongs to the native init predicate. Desktop init callbacks
    // may synchronously deliver events, as they did through the legacy client.
    TeardownSteps steps;
    steps.run("native window event", [&] {
        auto* window = static_cast<Window*>(user);
        if (!event || !window || WindowRequestAccess::binding(*window).handle.id != handle.id) return;
        WindowEntryScope entry(*window, false, false, false);
        if (!entry) return;
        observeWindowVisibility(window->context(), event->type);
        restoreWindowContext(&window->context());
        if (!coordinator().bootstrapping && event->type != SAPP_EVENTTYPE_SUSPENDED && event->type != SAPP_EVENTTYPE_RESUMED &&
            event->type != SAPP_EVENTTYPE_ICONIFIED && event->type != SAPP_EVENTTYPE_RESTORED)
            deliverPendingWindowVisibilityAtReadyEntry(*window);
        convertWindowEvent(event, *window);
        if constexpr (!supportsSecondaryWindows()) {
            if (!coordinator().bootstrapping) deliverPendingWindowVisibilityAtReadyEntry(*window);
        }
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
