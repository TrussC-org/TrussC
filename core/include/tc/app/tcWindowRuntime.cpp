#include <TrussC.h>

namespace trussc {
Window::OwnedState::OwnedState() {
    context.render = &render;
    context.coreEvents = &events;
}
Window::Window()
    : owned_(std::make_unique<OwnedState>()), ctx_(owned_->context),
      events_(owned_->events), render_(owned_->render), title_(owned_->title) {
    ctx_.hostWindow = this;
    ctx_.pauseUpdatesWhenHidden = true;
    ctx_.drawWhileHidden = false;
    ctx_.deferredResponseTarget = &ctx_;
    clearColorSet_ = true;
    internal::WindowRequestAccess::prepareInitialBindings(*this);
}

namespace internal {
namespace {
void processQueues();
void processMaintenance();
}
// One host instance owns Window backings, attachment order and App lifetimes
// across ordinary apps, guests and headless runs.
class AttachmentLedger;
struct RetiringGeneration {
    const void* generation;
    RetiringGeneration* previous;
};
// Publication allocates each ledger record before attachment commit. Empty
// snapshots, moves, partitioning and release never allocate a container sentinel
// or debug proxy; teardown must keep working after every allocation fails.
class AttachmentLedger {
public:
    struct Entry {
        std::shared_ptr<App> app;
        Entry* previous = nullptr;
        std::unique_ptr<Entry> next;
        explicit Entry(const std::shared_ptr<App>& value) : app(value) {}
    };
    AttachmentLedger() noexcept = default;
    AttachmentLedger(AttachmentLedger&& other) noexcept
        : first_(std::move(other.first_)), last_(std::exchange(other.last_, nullptr)) {}
    AttachmentLedger(const AttachmentLedger&) = delete;
    ~AttachmentLedger() noexcept {
        while (first_) {
            auto retired = extract(first_.get());
            // Drop one record outside the chain, so deep ledgers do not recurse.
        }
    }
    bool empty() const noexcept { return !first_; }
    Entry* first() const noexcept { return first_.get(); }
    Entry* last() const noexcept { return last_; }
    void push_back(const std::shared_ptr<App>& app) {
        append(std::make_unique<Entry>(app));
    }
    void append(std::unique_ptr<Entry> entry) noexcept {
        auto* raw = entry.get();
        raw->previous = last_;
        if (last_) last_->next = std::move(entry);
        else first_ = std::move(entry);
        last_ = raw;
    }
    std::unique_ptr<Entry> extract(Entry* entry) noexcept {
        auto& position = entry->previous ? entry->previous->next : first_;
        auto result = std::move(position);
        position = std::move(result->next);
        if (position) position->previous = result->previous;
        else last_ = result->previous;
        result->previous = nullptr;
        return result;
    }
    void appendAll(AttachmentLedger& other) noexcept {
        if (other.empty()) return;
        other.first_->previous = last_;
        if (last_) last_->next = std::move(other.first_);
        else first_ = std::move(other.first_);
        last_ = std::exchange(other.last_, nullptr);
    }
    void erase(const App* app) noexcept {
        for (auto* entry = first(); entry; entry = entry->next.get()) {
            if (entry->app.get() != app) continue;
            auto retired = extract(entry);
            return; // Each App has one publication record; release after unlink.
        }
    }
private:
    std::unique_ptr<Entry> first_;
    Entry* last_ = nullptr;
};
struct Runtime {
    unsigned entries = 0, turns = 0, appExitCallbacks = 0;
    bool servicing = false, pollingReload = false, bootstrapping = false, servicedTurn = false;
    uint64_t epoch = 0;
    void (*reload)() = nullptr;
    std::string hotReloadType, hotReloadFile;
    HostServiceOperations services{processQueues, processMaintenance};
    std::mutex runTokenMutex;
    std::shared_ptr<std::atomic<bool>> runToken;
    AppConstructionTicket* constructor = nullptr;
    WindowContext* selected = nullptr;
    std::vector<std::shared_ptr<Window>> windows, endingWindows;
    AttachmentLedger attachments, endingAttachments;
    float logoutTimeout = 3.0f;
    bool (*nativeSessionActivity)() = nullptr;
    bool sessionQueried = false, sessionAllowed = true, sessionCommitted = false;
#ifdef _WIN32
    void* sessionAuthority = nullptr;
#endif
    RetiringGeneration* retiringGenerations = nullptr;
    std::vector<std::weak_ptr<AppOwner>> owners;
    bool stopping = false, notifyingQuit = false, quitPending = false;
    RuntimeEvents events;
    Event<void> maintenance;
    uintptr_t nextGeneration = 0;

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
namespace {
// Establish the subscriber's physical context before taking its stack guard.
// A Runtime subscription owned by App B must restore B's stacks, not those of
// the process or another Window which happened to receive the native callback.
template<class T, class Snapshot>
void notifyRequestListeners(const Snapshot& saved, T& aggregate, const char* name) {
    FrameworkEventAccess::forEachOwned(saved, [&](auto& callback, auto owner) {
        T fresh;
        try {
            AppCallbackScope subscriber(std::move(owner));
            EntryStackGuard guard(AppEntry::Event, name);
            callback(fresh);
        } catch (...) {
            fresh.cancel = true;
            reportTeardownFailure("request listener");
        }
        if (fresh.cancel) {
            aggregate.cancel = true;
            if constexpr (std::is_same_v<T, ExitRequestEventArgs>)
                if (aggregate.reason.empty() && !fresh.reason.empty()) aggregate.reason = std::move(fresh.reason);
        }
    });
}
}
void dispatchConsoleEvent(ConsoleEventArgs& args) {
    RuntimeActivityScope activity;
    AppOwnerScope process({});
    WindowContextScope context(nullptr);
    EntryStackGuard guard(AppEntry::Event, "console");
    TeardownSteps steps;
    auto saved = FrameworkEventAccess::snapshot(runtime().events.console);
    FrameworkEventAccess::forEachOwned(saved, [&](auto& callback, auto owner) {
        steps.run("Runtime console listener", [&] {
            AppCallbackScope subscriber(std::move(owner));
            EntryStackGuard listener(AppEntry::Event, "console");
            callback(args);
        });
    });
}
namespace {
// Capture all App event slots before any user code. A listener that replaces or
// closes a sibling cannot enroll a replacement in the current notification.
template<class T, class Invoke>
void notifyProcessEvent(Event<T>& processEvent, Event<T> CoreEvents::* member,
                        const char* name, Invoke invoke, const void* generation = nullptr, bool generationOnly = false) {
    using Snapshot = decltype(FrameworkEventAccess::snapshot(processEvent));
    struct Target {
        std::shared_ptr<App> app;
        std::shared_ptr<Window> window;
        WindowContext* context;
        Snapshot listeners;
    };
    RuntimeActivityScope activity;
    AppOwnerScope process({});
    std::vector<Target> targets;
    auto processListeners = FrameworkEventAccess::snapshot(processEvent);
    for (auto* entry = runtime().attachments.last(); entry; entry = entry->previous) {
        auto owner = appOwner(*entry->app);
        if (!owner || owner->ended || owner->retiring || !owner->context ||
                (generationOnly && (!generation || owner->generation != generation))) continue;
        auto window = owner->window ? WindowRequestAccess::retainHostOwner(*owner->window) : nullptr;
        targets.push_back({entry->app, std::move(window), owner->context,
            FrameworkEventAccess::snapshot(owner->context->coreEvents->*member)});
    }
    WindowContextScope processContext(nullptr);
    { EntryStackGuard guard(AppEntry::Event, name); invoke(processListeners); }
    for (auto& target : targets) {
        auto owner = appOwner(*target.app);
        if (!owner || owner->ended || owner->retiring || !owner->token->load()) continue;
        if (target.window) {
            WindowEntryScope entry(*target.window, false, false, false);
            if (!entry) continue;
            EntryStackGuard guard(AppEntry::Event, name);
            invoke(target.listeners);
        } else {
            WindowContextScope entry(target.context);
            EntryStackGuard guard(AppEntry::Event, name);
            invoke(target.listeners);
        }
    }
}
}
bool dispatchExitRequested() noexcept {
    auto& state = runtime();
    if (state.stopping || state.notifyingQuit) return false;
    state.notifyingQuit = true;
    struct Restore { bool& flag; ~Restore() { flag = false; } } restore{state.notifyingQuit};
    ExitRequestEventArgs aggregate;
    setExitBlockReason("");
    try {
        notifyProcessEvent(state.events.exitRequested, &CoreEvents::exitRequested, "exitRequested",
            [&](const auto& saved) { notifyRequestListeners<ExitRequestEventArgs>(saved, aggregate, "exitRequested"); });
    } catch (...) { aggregate.cancel = true; reportTeardownFailure("process quit notification"); }
    if (aggregate.cancel) {
        cancelWindowExitSignal(); clearExitReason();
        // The explanation is optional; allocation failure must retain the veto.
        try { setExitBlockReason(aggregate.reason); }
        catch (...) { setExitBlockReason(""); }
    }
    return !aggregate.cancel;
}
void dispatchDeviceLost(uint32_t reason) noexcept {
    DeviceLostEventArgs args;
    args.reason = reason;
    TeardownSteps steps;
    steps.run("device loss notification", [&] {
        notifyProcessEvent(runtime().events.deviceLost, &CoreEvents::deviceLost, "deviceLost",
            [&](const auto& saved) { FrameworkEventAccess::forEachOwned(saved, [&](auto& callback, auto owner) {
                steps.run("device loss listener", [&] {
                    AppCallbackScope subscriber(std::move(owner));
                    EntryStackGuard listener(AppEntry::Event, "deviceLost");
                    callback(args);
                });
            }); });
    });
    // Device loss retains its existing mutable cancel meaning, without OR.
    if (!args.cancel) {
        logError("D3D11") << "Device lost, GetDeviceRemovedReason=0x" << std::hex << reason << "; exiting";
        appExitCode() = 1;
        setExitReason("device-lost");
        if (headless::isActive()) headless::running = false;
        else sapp_quit();
    }
}
void dispatchHotReloadUnload(const void* generation) noexcept {
    TeardownSteps steps;
    steps.run("hot reload notification", [&] {
        notifyProcessEvent(runtime().events.hotReloadUnload, &CoreEvents::hotReloadUnload, "hotReloadUnload",
            [&](const auto& saved) { FrameworkEventAccess::forEachOwned(saved, [&](auto& callback, auto owner) {
                steps.run("hot reload listener", [&] {
                    AppCallbackScope subscriber(std::move(owner));
                    EntryStackGuard listener(AppEntry::Event, "hotReloadUnload");
                    callback();
                });
            }); }, generation, true);
    });
}
std::shared_ptr<std::atomic<bool>>& appRunToken() {
    auto& state = runtime();
    if (auto owner = currentAppOwner()) return owner->token;
    return state.runToken;
}
#ifndef __EMSCRIPTEN__
ThreadChannel<std::function<void()>>& mainThreadQueue() { return runtime().mainQueue; }
std::atomic<size_t>& mainThreadQueuePendingCount() { return runtime().mainQueuePending; }
#endif
uint64_t runtimeEpoch() noexcept { return runtime().epoch; }
void beginRuntimeRun() {
    auto& state = runtime();
    if (state.sessionCommitted || state.entries || state.turns || state.servicing || state.constructor || !state.windows.empty() || !state.attachments.empty())
        throw std::logic_error("a Runtime run cannot begin inside a host entry");
    if (state.epoch == UINT64_MAX) throw std::overflow_error("Runtime epochs exhausted");
    auto token = std::make_shared<std::atomic<bool>>(true);
    if (state.runToken) state.runToken->store(false);
    {
        RuntimeActivityScope starting;
        cancelResourceBackends();
    }
    {
        std::lock_guard lock(state.runTokenMutex);
        state.runToken = std::move(token);
    }
    ++state.epoch;
    state.servicedTurn = false;
    state.stopping = false;
    state.sessionQueried = state.sessionCommitted = false;
    state.quitPending = false;
    state.sessionAllowed = true;
    state.nativeSessionActivity = nullptr;
    state.logoutTimeout = 3.0f;

    prepareRuntimeExit();
    prepareLogoutWatchdog();
#ifdef _WIN32
    { RuntimeActivityScope construction; state.sessionAuthority = createSessionAuthority(); }
#endif
}
void requestNormalQuit(const char* reason, std::optional<int> code) {
#ifndef __EMSCRIPTEN__
    if (!isMainThread()) {
        // A process request outlives its App, but never its Runtime run. Read
        // only the published run token here; main owns all exit-state checks.
        std::shared_ptr<std::atomic<bool>> run;
        {
            auto& state = runtime();
            std::lock_guard lock(state.runTokenMutex);
            run = state.runToken;
        }
        if (!run || !run->load()) return;
        mainThreadQueue().send([run = std::move(run), reason = std::string(reason), code] {
            if (!run->load() || run != runtime().runToken || runtimeEnding()) return;
            AppOwnerScope process({});
            requestNormalQuit(reason.c_str(), code);
        });
        return;
    }
#endif
    if (runtimeEnding() || appEnding() || runtime().notifyingQuit) return;
    if (code) appExitCode() = *code;
    setExitReason(reason);
    if (headless::isActive()) headless::quitRequested = true;
    else {
        runtime().quitPending = true;
        sapp_request_quit();
    }
}
bool runtimeEnding() noexcept { return runtime().stopping; }
bool appEnding() noexcept {
    auto owner = currentAppOwner();
    return runtime().appExitCallbacks || (owner && (owner->retiring || owner->callbacksClosing));
}
void setHotReloadSource(const char* type, const char* file) {
    runtime().hotReloadType = type ? type : "";
    runtime().hotReloadFile = file ? file : "";
}
void retainRuntimeWindow(const std::shared_ptr<Window>& window) {
    if (runtime().stopping) throw std::logic_error("the Runtime is ending");
    runtime().windows.push_back(window);
    WindowRequestAccess::setHostOwner(*window, window);
    if (!runtime().hotReloadType.empty() && openWindows().size() >= 2) {
        logWarning() << "!! Remove \"TC_HOT_RELOAD(" << runtime().hotReloadType
            << ")\" from " << runtime().hotReloadFile << " !! Hot reload does not support multiple windows.";
    }
}
void releaseRuntimeWindow(Window& window) noexcept {
    auto& state = runtime();
    std::erase_if(state.windows, [&](const auto& owner) { return owner.get() == &window; });
}
void recordAppAttachment(const std::shared_ptr<App>& app, Window&) {
    runtime().attachments.push_back(app);
}
void recordHeadlessAttachment(const std::shared_ptr<App>& app, WindowContext& context) {
    auto owner = appOwner(*app);
    {
        std::lock_guard lock(owner->cancellationsMutex);
        owner->context = &context;
    }
    runtime().attachments.push_back(app);
}
void shutdownRuntime() noexcept {
    auto& state = runtime();
    if (state.stopping) return;
    if (!state.stopping) state.endingWindows.swap(state.windows);
    state.stopping = true;
    TeardownSteps steps;
#if defined(__APPLE__) && TARGET_OS_OSX
    cancelMacDialogSheets();
#endif
    // One retained snapshot of attachment order, including headless. Backings
    // are separately retained so user handle cycles cannot postpone disposal.
    auto& windows = state.endingWindows;
    state.endingAttachments.appendAll(state.attachments);
    auto& attachments = state.endingAttachments;
    for (auto* entry = attachments.last(); entry; entry = entry->previous) {
        if (auto app = entry->app) {
            auto owner = appOwner(*app);
            // Publication reserves a record before retiring the outgoing App.
            // A completed replacement still waiting on that stack is a candidate.
            if (!owner || (!owner->window && !owner->ended && owner->context != nonWindowContextIfInitialized()) ||
                    owner->ended) continue;
            if (auto* window = owner->window) WindowRequestAccess::endForShutdown(*window);
            else if (owner->context) {
                owner->context->rootNode.reset();
                endAppAttachment(app, *owner->context, true);
            }
        }
    }
    for (auto& window : windows) WindowRequestAccess::teardown(*window);
    // Unpublished candidates also belong to this run. They receive no invented
    // setup/exit, but their constructor-owned work must stop before services.
    while (!state.owners.empty()) {
        auto owner = state.owners.back().lock();
        if (owner) retireAppOwner(owner);
        else state.owners.pop_back();
    }
    if (state.runToken) state.runToken->store(false);
    if (sg_isvalid()) sapp_tc_gl_activate_idle();
    windows.clear();
    while (!attachments.empty()) { auto retired = attachments.extract(attachments.first()); }
    if (!state.sessionCommitted) cancelLogoutWatchdog();
#ifdef _WIN32
    if (state.sessionCommitted) {
        // Windows can end a logging-off process at the last top-level window's
        // destruction; process exit reclaims the receiver.
    } else destroySessionAuthority(std::exchange(state.sessionAuthority, nullptr));
#endif
}
bool sessionEndCommitted() noexcept { return runtime().sessionCommitted; }
void setSessionActivityProbe(bool (*probe)()) noexcept { runtime().nativeSessionActivity = probe; }
bool querySessionEnd() noexcept {
    auto& state = runtime();
    if (state.sessionCommitted || state.stopping) return true;
    if (state.sessionQueried) return state.sessionAllowed;
    state.sessionQueried = true;
    // A nested query cannot enter callbacks on an interrupted client stack.
    state.sessionAllowed = true;
    if (!hostCallbacksActive() && !state.constructor &&
            !(state.nativeSessionActivity && state.nativeSessionActivity())) {
        setExitReason("os-session-end");
        state.sessionAllowed = dispatchExitRequested();
    }
    return state.sessionAllowed;
}
void cancelSessionEnd() noexcept {
    if (runtime().sessionCommitted) return;
    runtime().sessionQueried = false;
    runtime().sessionAllowed = true;
}
bool endRuntimeForSession() noexcept {
    auto& state = runtime();
    if (state.sessionCommitted) return false;
    state.sessionCommitted = true;
    // Busy means App code on the main stack; unrelated workers do not count.
    if (hostCallbacksActive() || state.constructor ||
            (state.nativeSessionActivity && state.nativeSessionActivity())) return false;
    armLogoutWatchdog(state.logoutTimeout);
    setExitReason("os-session-end");
    _cleanup_cb();
    return true;
}
void commitSessionEnd() noexcept {
    if (runtime().sessionCommitted) return;
    if (!endRuntimeForSession()) {
#ifdef __APPLE__
        // No App callbacks or static destructors, and never block logout.
        _exit(0);
#endif
        return; // Returning WM_ENDSESSION lets Windows terminate the process.
    }
    // Match ordinary exit: run atexit handlers and static destructors after
    // cleanup. The armed, lock-free watchdog also bounds these user handlers.
    closeLogFile();
    std::fflush(stdout);
    std::fflush(stderr);
    std::exit(appExitCode());
}
Window* resolveWindow(bool report) {
    if (auto owner = currentAppOwner()) return !owner->ended && !owner->retiring ? owner->window : nullptr;
    Window* result = nullptr;
    for (auto* window : openWindows()) {
        if (result) {
            if (report) logError("Window") << "Specify a Window handle when more than one window is open";
            return nullptr;
        }
        result = window;
    }
    if (!result && report) logError("Window") << "No open window";
    return result;
}
WindowContext* findWindowApiContext() {
    if (auto owner = currentAppOwner()) {
        if (owner->context && !owner->retiring) return owner->context;
        logError("Window") << "This App has no attached window context";
        return nullptr;
    }
    if (auto* window = resolveWindow(true)) return &WindowRequestAccess::context(*window);
    return nullptr;
}
WindowContextScope::WindowContextScope(WindowContext* context) : previous_(currentWindowCtx()) {
    const auto native = sapp_tc_gl_save();
    display_ = native.display; context_ = native.context; draw_ = native.draw; read_ = native.read;
    drawWindow_ = native.draw_window; readWindow_ = native.read_window;
    sgl_ = sg_isvalid() ? sgl_get_context().id : 0;
    try { restoreWindowContext(context); }
    catch (...) {
        TeardownSteps steps;
        steps.run("explicit Window entry rollback", [&] { restoreWindowContext(previous_); });
        currentWindowCtx() = previous_;
        sapp_tc_gl_restore(native);
        if (sg_isvalid() && sgl_) sgl_set_context({sgl_});
        throw;
    }
}
WindowContextScope::~WindowContextScope() noexcept {
    TeardownSteps steps;
    steps.run("explicit Window context restoration", [&] { restoreWindowContext(previous_); });
    currentWindowCtx() = previous_;
    if (sg_isvalid() && !currentWindowContext().targetsClosed && sgl_) sgl_set_context({sgl_});
    sapp_tc_gl_restore({display_, context_, draw_, read_, drawWindow_, readWindow_});
}
WindowContext& windowApiContext() {
    if (auto* context = findWindowApiContext()) return *context;
    return nonWindowContext();
}
CoreEvents& unavailableWindowEvents() {
    struct UnavailableEvents {
        CoreEvents events;
        UnavailableEvents() {
            auto disable = [](auto&... event) { (FrameworkEventAccess::disableSubscriptions(event), ...); };
            disable(events.setup, events.update, events.draw, events.onRender, events.afterFrame,
                events.exit, events.hotReloadUnload, events.exitRequested, events.deviceLost,
                events.keyPressed, events.keyReleased, events.mousePressed, events.mouseReleased,
                events.mouseMoved, events.mouseDragged, events.mouseScrolled, events.windowResized,
                events.occludedChanged, events.filesDropped, events.clipboardPasted, events.closeRequested,
                events.touchPressed, events.touchMoved, events.touchReleased, events.rawEvent);
        }
    };
    static UnavailableEvents unavailable;
    return unavailable.events;
}
AppConstructionTicket* currentAppConstruction() noexcept { return runtime().constructor; }
std::shared_ptr<std::atomic<bool>> constructorRunToken() {
    return appRunToken();
}
namespace {
// Trivial TLS storage is still valid during late static App destruction.
thread_local AppOwnerScope* selectedAppOwner = nullptr;
}
std::shared_ptr<AppOwner> currentAppOwner() noexcept {
    return selectedAppOwner ? selectedAppOwner->owner_ : std::shared_ptr<AppOwner>{};
}
AppOwnerScope::AppOwnerScope(std::shared_ptr<AppOwner> owner) noexcept
    : previous_(selectedAppOwner), owner_(std::move(owner)), previousRegistration_(mcp::internal::registrationOwner()) {
    selectedAppOwner = this;
    mcp::internal::setRegistrationOwner(owner_ ? owner_->token.get() : nullptr);
}
AppOwnerScope::~AppOwnerScope() noexcept {
    mcp::internal::setRegistrationOwner(previousRegistration_);
    selectedAppOwner = previous_;
}
WorkerAppAdmission::WorkerAppAdmission(const std::shared_ptr<AppOwner>& owner, bool cleanup) noexcept {
    admitted_ = cleanup || isMainThread() || !owner || (owner->token->load() && !owner->callbacksClosing);
}
WorkerAppAdmission::~WorkerAppAdmission() noexcept = default;
AppCallbackScope::AppCallbackScope(std::shared_ptr<AppOwner> owner) : worker_(owner), owner_(owner) {
    if (!owner) return;
    if (!isMainThread()) {
        return;
    }
    ++runtime().entries;
    mainEntry_ = true;
    previous_ = currentWindowCtx();
    if (owner->context == previous_) return;
    const auto native = sapp_tc_gl_save();
    display_ = native.display; context_ = native.context; draw_ = native.draw; read_ = native.read;
    drawWindow_ = native.draw_window; readWindow_ = native.read_window;
    sgl_ = sg_isvalid() ? sgl_get_context().id : 0;
    entered_ = true;
    try { restoreWindowContext(owner->context); }
    catch (...) {
        TeardownSteps steps;
        steps.run("callback entry rollback", [&] { restoreWindowContext(previous_); });
        currentWindowCtx() = previous_;
        sapp_tc_gl_restore(native);
        --runtime().entries;
        mainEntry_ = false;
        entered_ = false;
        throw;
    }
}
AppCallbackScope::~AppCallbackScope() noexcept {
    if (!isMainThread()) return;
    if (!entered_) { if (mainEntry_) --runtime().entries; return; }
    TeardownSteps steps;
    steps.run("App callback caller restoration", [&] { restoreWindowContext(previous_); });
    currentWindowCtx() = previous_;
    if (sg_isvalid() && !currentWindowContext().targetsClosed && sgl_) sgl_set_context({sgl_});
    sapp_tc_gl_restore({display_, context_, draw_, read_, drawWindow_, readWindow_});
    --runtime().entries;
}
const void* nextAppGeneration() {
    auto& next = runtime().nextGeneration;
    if (next == UINTPTR_MAX) throw std::overflow_error("App generations exhausted");
    return reinterpret_cast<const void*>(++next);
}
void retireAppGeneration(const void* generation) noexcept {
    if (!generation) return;
    auto& state = runtime();
    if (state.stopping) return;
    for (auto* active = state.retiringGenerations; active; active = active->previous)
        if (active->generation == generation) return;
    AttachmentLedger snapshot;
    RetiringGeneration retiring{generation, state.retiringGenerations};
    state.retiringGenerations = &retiring;
    struct RestoreGeneration {
        Runtime& state;
        RetiringGeneration* previous;
        ~RestoreGeneration() { state.retiringGenerations = previous; }
    } restore{state, retiring.previous};
    RuntimeActivityScope activity;
    // Splice one retained snapshot without allocating, including under OOM.
    // Cohort retirement never consumes another App's ledger entry.
    for (auto* entry = state.attachments.first(); entry;) {
        auto* next = entry->next.get();
        if (auto owner = appOwner(*entry->app); owner && owner->generation == generation)
            snapshot.append(state.attachments.extract(entry));
        entry = next;
    }
    for (auto* entry = snapshot.last(); entry; entry = entry->previous) {
        auto owner = appOwner(*entry->app);
        if (owner->window) WindowRequestAccess::endApp(*owner->window);
        else if (owner->context) endAppAttachment(entry->app, *owner->context, true);
    }
    // Candidates were never published, so they retire without invented hooks.
    for (;;) {
        std::shared_ptr<AppOwner> candidate;
        for (auto it = state.owners.rbegin(); it != state.owners.rend(); ++it) {
            if (auto owner = it->lock(); owner && owner->generation == generation) {
                candidate = std::move(owner);
                break;
            }
        }
        if (!candidate) break;
        retireAppOwner(candidate);
    }
    mcp::internal::removeRegistrationsOwnedBy(generation);
}

std::unordered_set<const App*> attachedApps() {
    std::unordered_set<const App*> result;
    for (auto* entry = runtime().attachments.first(); entry; entry = entry->next.get()) if (auto& app = entry->app) {
        auto owner = appOwner(*app);
        if (owner && owner->window && !owner->ended && !owner->retiring) result.insert(app.get());
    }
    return result;
}
std::function<void()> attributeMainThreadWork(std::function<void()> callback) {
    auto owner = currentAppOwner();
    if (!owner || !callback) return callback;
    if (owner->retiring || owner->ended || !owner->token->load()) return {};
    // Queue storage owns the capture; successful guest modules remain mapped.
    // No per-submission cancellation record; discard releases captures too.
    struct Work {
        std::weak_ptr<AppOwner> weak;
        std::function<void()> callback;
        void operator()() const {
            auto live = weak.lock();
            if (!live || live->retiring || live->ended || !live->token->load()) return;
            AppCallbackScope scope(live);
            callback();
        }
    };
    return Work{owner, std::move(callback)};
}
void ownAppCancellation(std::function<void()> cancel) {
    if (auto owner = currentAppOwner()) {
        {
            std::lock_guard lock(owner->cancellationsMutex);
            if (!owner->retiring && !owner->ended && owner->token->load()) {
                owner->cancellations.push_back(std::move(cancel));
                return;
            }
        }
        cancel(); // Never invoke application/disconnect code under the owner lock.
    }
}
bool isAppExitEvent(const void* event) noexcept {
    auto owner = currentAppOwner();
    if (!owner) return false;
    // Event registration may run on a worker while main retires its context.
    std::lock_guard lock(owner->cancellationsMutex);
    return owner->context && event == &owner->context->coreEvents->exit;
}
void retireAppOwner(const std::shared_ptr<AppOwner>& owner) noexcept {
    if (!owner || owner->retiring.exchange(true)) return;
    runtime().attachments.erase(owner->app);
    std::erase_if(runtime().owners, [&](const auto& weak) { auto live = weak.lock(); return !live || live == owner; });
    owner->token->store(false, std::memory_order_release);
    {
        std::lock_guard lock(owner->cancellationsMutex);
        owner->window = nullptr;
        owner->context = nullptr;
    }
    AppOwnerScope scope(owner);
    TeardownSteps steps;
    steps.run("App MCP retirement", [&] { mcp::internal::removeRegistrationsOwnedBy(owner->token.get()); });
    // Admission and extraction share a lock; invocation and capture destruction
    // stay outside it. Extract in place so OOM teardown needs no container move.
    for (size_t index = 0;; ++index) {
        std::function<void()> cancel;
        {
            std::lock_guard lock(owner->cancellationsMutex);
            if (index == owner->cancellations.size()) {
                owner->cancellations.clear();
                break;
            }
            cancel = std::move(owner->cancellations[index]);
        }
        steps.run("App callback retirement", cancel);
    }
    owner->app = nullptr;
}
AppConstructionScope::AppConstructionScope(AppConstructionTicket& ticket)
    : ticket_(ticket), previous_(currentAppConstruction()) {
    ticket.owner = std::make_shared<AppOwner>();
    auto parent = currentAppOwner();
    ticket.owner->generation = ticket.registrationOwner ? ticket.registrationOwner :
        (parent ? parent->generation : nullptr);
    for (auto* retiring = runtime().retiringGenerations; retiring; retiring = retiring->previous)
        if (ticket.owner->generation == retiring->generation)
            throw std::logic_error("the guest generation is retiring");
    ticket.owner->epoch = runtimeEpoch();
    ticket.owner->context = ticket.context;
    runtime().owners.push_back(ticket.owner);
    ticket.runToken = ticket.owner->token;
    ticket.registrationOwner = ticket.runToken.get();
    ticket.epoch = ticket.owner->epoch;
    ownerScope_ = std::make_unique<AppOwnerScope>(ticket.owner);
    runtime().constructor = &ticket;
    ++runtime().entries;
}
AppConstructionScope::~AppConstructionScope() noexcept {
    if (!ticket_.app) retireAppOwner(ticket_.owner);
    runtime().constructor = previous_;
    --runtime().entries;
}
void requireAppConstruction(const App* app) {
    auto* ticket = currentAppConstruction();
    const auto address = reinterpret_cast<uintptr_t>(app);
    if (!ticket || ticket->claimed || !ticket->allocation ||
        address < reinterpret_cast<uintptr_t>(ticket->allocation) ||
        address - reinterpret_cast<uintptr_t>(ticket->allocation) >= ticket->allocationSize)
        throw std::logic_error("App construction requires Window::setApp<T>() or a launcher");
    // Known gap: an App subobject inside T other than T's own App base can
    // pass this range check and run its constructor body before the outer
    // construction fails. Such member/base composition is unsupported.
    ticket->claimed = true; // One base constructor per factory, including nested construction.
}
void completeAppConstruction(App& app) noexcept {
    if (auto* ticket = currentAppConstruction(); ticket && !ticket->app) {
        ticket->app = &app;
        ticket->owner->app = &app;
        app.constructorTicket_ = *ticket;
    }
}
std::shared_ptr<AppOwner> appOwner(const App& app) noexcept { return app.constructorTicket_.owner; }
std::shared_ptr<App> adoptConstructedApp(App* app, void (*destroy)(App*)) {
    if (!app) return {};
    auto owner = appOwner(*app);
    // The host owns the control block and final release. Even a retained App
    // handle destroyed after shutdown keeps its retired owner selected while
    // the entire derived destructor runs.
    return std::shared_ptr<App>(app, [owner, destroy](App* target) noexcept {
        WorkerAppAdmission worker(owner, true);
        AppOwnerScope scope(owner);
        retireAppOwner(owner);
        TeardownSteps steps;
        steps.run("App destruction", [&] {
            if (destroy) destroy(target);
            else delete target;
        });
    });
}

void deliverAppExit(App& app) {
    auto owner = appOwner(app);
    if (!owner || owner->exitDelivered) return;
    owner->exitDelivered = true;
    AppOwnerScope scope(owner);
    EntryStackGuard entry(AppEntry::Exit);
    ++runtime().appExitCallbacks;
    struct ExitScope { ~ExitScope() { --runtime().appExitCallbacks; } } exitScope;
    app.exit();
}
void resetAppNodeReferences(const App&) noexcept;
void endAppAttachment(const std::shared_ptr<App>& app, WindowContext& context, bool virtualOnly) noexcept {
    if (!app) return;
    auto owner = appOwner(*app);
    if (!owner || owner->ended) return;
    owner->ended = true;
    owner->token->store(false, std::memory_order_release);
    {
        std::lock_guard lock(owner->cancellationsMutex);
        owner->window = nullptr;
        owner->context = &context; // exit/cleanup retain their state without a Window handle.
    }
    AppOwnerScope scope(owner);
    TeardownSteps steps;
    auto* previous = currentWindowCtx();
    steps.run("ending context", [&] { restoreWindowContext(&context); });
    currentWindowCtx() = &context;
    if (owner->notifyWindowExit)
        FrameworkEventAccess::notifyShutdown(context.coreEvents->exit, steps);
    app->cleanupCalled_ = true;
    steps.run("App exit", [&] { deliverAppExit(*app); });
    owner->callbacksClosing = true;
    steps.run("App audio detach", [&] { detachAppAudio(*app); });
    resetAppNodeReferences(*app);
    if (virtualOnly) steps.run("App virtual cleanup", [&] { app->cleanup(); });
    else steps.run("App tree cleanup", [&] { app->cleanupTree(); });
    resetAppNodeReferences(*app);
    context.hoveredNode.reset();
    context.prevHoveredNode.reset();
    context.grabbedNode.reset();
    context.grabbedButton = -1;
    context.selectedNode.reset();
    retireAppOwner(owner);
    steps.run("ending caller restoration", [&] { restoreWindowContext(previous); });
    currentWindowCtx() = previous;
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
    RenderContext canonicalRender;
    CoreEvents events;
    WindowContext context;
    NonWindowState() {
        // Preserve prelaunch subscriptions and render references. Their storage
        // belongs to the canonical Window; this fallback is not a live Window.
        context.render = &canonicalRender;
        context.coreEvents = &events;
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
    auto& ctx = WindowRequestAccess::context(window);
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
    if (!WindowRequestAccess::acceptsBinding(window) || !handle.id || !operations.readMetadata) return false;
    sapp_tc_window_metadata metadata{};
    if (!operations.readMetadata(handle, user, metadata) || metadata.fb_width <= 0 || metadata.fb_height <= 0 ||
        !std::isfinite(metadata.dpi_scale) || metadata.dpi_scale <= 0) return false;
    auto& b = WindowRequestAccess::binding(window);
    if (b.handle.id != handle.id) {
        b.lastDeliveredHidden = false; b.visibilityReady = false;
        WindowRequestAccess::context(window).pointerValid = false; WindowRequestAccess::context(window).touchMapper.reset();
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
    WindowRequestAccess::context(window).acquireSwapchain = acquireBoundSwapchain;
    WindowRequestAccess::context(window).acquireSwapchainUser = &window;
    WindowRequestAccess::context(window).activateNative = activateBinding;
    return true;
}
bool bindNativeWindow(uint32_t id, void* user) noexcept {
    if (!id || !user) return false;
    try { return bindWindow(*static_cast<Window*>(user), {id}, productionWindowOperations(), nullptr); }
    catch (...) { reportTeardownFailure("native window binding"); return false; }
}
void syncWindowAppSize(Window& window, bool initial) {
    const auto app = window.getApp();
    const auto& ctx = WindowRequestAccess::context(window);
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
        AppCallbackScope owner(appOwner(*app));
        EntryStackGuard guard(AppEntry::Event, "windowResized()");
        WindowCallbackTimeScope callbackTime;
        try {
            // Initial attachment seeds only the App; subsequent changes use its events.
            const bool alreadySeeded = initial && app->getWidth() == size.width && app->getHeight() == size.height;
            if (!alreadySeeded) {
                if (initial && !WindowRequestAccess::appStarted(window)) app->handleWindowResized(size.width, size.height);
                else WindowRequestAccess::syncRootSize(window, float(size.width), float(size.height));
            }
        } catch (...) {
            // Contain at the native boundary and fail this Window; no replay.
            WindowRequestAccess::teardown(window);
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
void refreshSelectedWindowMetadata() {
    auto* selected = currentWindowCtx();
    auto* window = selected && selected->hostWindow ? selected->hostWindow : resolveWindow();
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
    else { refreshSelectedWindowMetadata(); return true; }
}

namespace {
Runtime& coordinator() { return runtime(); }
void processQueues() {
#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)) || defined(__EMSCRIPTEN_PTHREADS__)
    updateKeepScreenOn(headless::isActive());
#endif

    if (sg_isvalid()) sapp_tc_gl_ensure_context();
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
    TeardownSteps steps;
    auto listeners = FrameworkEventAccess::snapshot(runtime().maintenance);
    FrameworkEventAccess::forEachOwned(listeners, [&](auto& callback, auto owner) {
        steps.run("Runtime maintenance listener", [&] {
            AppCallbackScope subscriber(std::move(owner));
            EntryStackGuard guard(AppEntry::Event, "maintenance");
            callback();
        });
    });
    if (sg_isvalid()) sapp_tc_gl_ensure_context();
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
Event<void>& runtimeMaintenanceEvent() { return runtime().maintenance; }
HostServiceOperations& hostServiceOperationsForTests() {
    return runtime().services;
}
void setHostReloadPoll(void (*poll)()) noexcept { coordinator().reload = poll; }
HostTurnScope::HostTurnScope() noexcept { beforeHostTurn(); }
HostTurnScope::~HostTurnScope() noexcept { afterHostTurn(); }
bool hostCallbacksActive() noexcept { return coordinator().entries || coordinator().servicing; }
bool processPendingQuit() noexcept {
    auto& state = runtime();
    const bool headless = headless::isActive();
    const bool pending = headless ? headless::quitRequested.exchange(false) : std::exchange(state.quitPending, false);
    if (pending && !state.stopping) {
        if (dispatchExitRequested()) {
            if (headless) headless::running = false;
            else sapp_quit();
            shutdownRuntime();
        } else if (!headless) sapp_cancel_quit();
    }
    return state.stopping;
}
bool reloadSafePoint() noexcept {
    return isMainThread() && !runtime().stopping && runtime().pollingReload;
}
void beforeHostTurn() noexcept {
    auto& state = coordinator();
    if (state.turns++ || state.entries || state.servicing || state.stopping) return;
    if (processPendingQuit()) return;
#ifdef _WIN32
    pollSessionAuthority(state.sessionAuthority);
#endif
    state.servicing = true;
    state.servicedTurn = true;
    auto* previous = currentWindowCtx();
    const auto native = sapp_tc_gl_save();
    const auto sgl = sg_isvalid() ? sgl_get_context() : sgl_context{};
    TeardownSteps steps;
    steps.run("host turn services", [&] {
        restoreWindowContext(nullptr);
        ProcessDrawingScope drawing;
        if (state.reload) {
            state.pollingReload = true;
            struct RestorePoll { bool& flag; ~RestorePoll() { flag = false; } } restore{state.pollingReload};
            state.reload();
        }
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
    if (state.stopping) return;
    state.servicing = true;
    auto* previous = currentWindowCtx();
    const auto native = sapp_tc_gl_save();
    const auto sgl = sg_isvalid() ? sgl_get_context() : sgl_context{};
    TeardownSteps steps;
    steps.run("host turn maintenance", [&] {
        restoreWindowContext(nullptr);
        ProcessDrawingScope drawing;
        // Single-surface hosts have no native Window close callback. Retire the
        // addressed virtual Window only after all callbacks have returned.
        for (;;) {
            std::shared_ptr<Window> closing;
            for (const auto& window : state.windows) {
                if (window->isOpen() && !WindowRequestAccess::binding(*window).handle.id &&
                        WindowRequestAccess::closeRequested(*window)) { closing = window; break; }
            }
            if (!closing) break;
            WindowRequestAccess::teardown(*closing);
        }
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
      previousSgl_(sg_isvalid() ? sgl_get_context() : sgl_context{}),
      native_(sapp_tc_gl_save()) {
    if (runtimeEnding() || !window.isOpen() || (tick && (window.entryDepth_ || WindowRequestAccess::context(window).inCallbackClock ||
        WindowRequestAccess::context(window).inUpdate || coordinator().servicing || WindowRequestAccess::dispatchActive(window)))) return;
    ++coordinator().entries;
    ++window.entryDepth_;
    entered_ = true;
    try {
        // Park the actual caller before installing the receiver. No drawable is acquired here.
        if (&currentWindowContext() != &WindowRequestAccess::context(window) && sg_isvalid()) parkWindowPass();
        currentWindowCtx() = &WindowRequestAccess::context(window);
        if (!refreshWindowEntryMetadata(window)) return;
        requestsReady_ = true;
        restoreWindowContext(&WindowRequestAccess::context(window));
        if (window.entryDepth_ == 1) WindowRequestAccess::applyPendingApp(window);
        if (runtimeEnding() || !window.isOpen() || (tick && WindowRequestAccess::closeRequested(window))) return;
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
        auto& ctx = WindowRequestAccess::context(window);
        const void* pressOwner = (ev->type == SAPP_EVENTTYPE_MOUSE_DOWN || ev->type == SAPP_EVENTTYPE_MOUSE_UP)
            ? std::exchange(ctx.pendingMousePressOwner, nullptr) : nullptr;
        const bool hidden = window.isOccluded();
        const auto visibility = prepareWindowVisibilityDelivery(window, ev->type);
        if (visibility == VisibilityDelivery::Drop) return;
        const bool visibilityChanged = visibility == VisibilityDelivery::RawThenTyped;
        const auto visibilityRevision = WindowRequestAccess::binding(window).visibilityRevision;
        auto currentVisibility = [&] { return WindowRequestAccess::binding(window).visibilityRevision == visibilityRevision; };
        WindowCallbackTimeScope callbackTime;
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
            case SAPP_EVENTTYPE_TC_DEVICE_LOST:
                dispatchDeviceLost(ev->device_lost_reason);
                break;
            case SAPP_EVENTTYPE_QUIT_REQUESTED:
                if (!dispatchExitRequested()) sapp_cancel_quit();
                break;
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
    if (!(event.dpi_scale > 0)) event.dpi_scale = WindowRequestAccess::context(window).dpiScale;
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
    if (event->type == SAPP_EVENTTYPE_QUIT_REQUESTED || event->type == SAPP_EVENTTYPE_TC_DEVICE_LOST) {
        windowEvent(event, {0}, nullptr);
        return;
    }
    TeardownSteps steps;
    steps.run("single-window native event binding", [&] {
        auto* window = resolveWindow();
        if (window) windowEvent(event, WindowRequestAccess::binding(*window).handle, window);
    });
}
void windowEvent(const sapp_event* event, sapp_window handle, void* user) noexcept {
    if (event && (event->type == SAPP_EVENTTYPE_QUIT_REQUESTED || event->type == SAPP_EVENTTYPE_TC_DEVICE_LOST)) {
        if (event->type == SAPP_EVENTTYPE_QUIT_REQUESTED) {
            runtime().quitPending = false;
            if (!dispatchExitRequested()) sapp_cancel_quit();
        } else dispatchDeviceLost(event->device_lost_reason);
        return;
    }
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
        observeWindowVisibility(WindowRequestAccess::context(*window), event->type);
        restoreWindowContext(&WindowRequestAccess::context(*window));
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
    auto& ctx = WindowRequestAccess::context(window);
    syncWindowAppSize(window);
    const auto& metadata = WindowRequestAccess::binding(window).metadata;
    const bool gated = (window.isOccluded() && !ctx.drawWhileHidden) || ctx.fbWidth <= 0 || ctx.fbHeight <= 0;
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
    if (runtimeEnding()) return;
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
void bootstrapWindow(Window& target, void (*bootstrap)()) {
    auto& state = coordinator();
    state.bootstrapping = true;
    try {
        if constexpr (!supportsSecondaryWindows()) WindowRequestAccess::binding(target).bound = sapp_isvalid();
        WindowEntryScope entry(target, false, true);
        if (entry) bootstrap();
    } catch (...) {
        state.bootstrapping = false;
        throw;
    }
    state.bootstrapping = false;
}
bool codeCloseExitsRuntime(Window& window) {
    auto& state = runtime();
    if (state.stopping) return false;
    if (!WindowRequestAccess::quitOnClose(window) &&
        std::count_if(state.windows.begin(), state.windows.end(), [](const auto& w) { return w->isOpen() && !WindowRequestAccess::closeRequested(*w); }) != 1) return false;
    if (dispatchExitRequested()) {
        WindowRequestAccess::acceptCodeQuit(window);
        setExitReason("last-window-close");
        prepareRuntimeExit();
        if (headless::isActive()) headless::running = false;
        else sapp_quit();
    }
    return true;
}
bool allowWindowClose(sapp_window handle, void* user) noexcept {
    bool allow = false;
    TeardownSteps steps;
    steps.run("window close request", [&] {
        auto* window = static_cast<Window*>(user);
        if (!window || WindowRequestAccess::binding(*window).handle.id != handle.id) return;
        if (!window->isOpen() || runtimeEnding()) return;
        // A code close already made its decision; native replay must not query.
        if (WindowRequestAccess::closeRequested(*window)) { allow = true; return; }
        WindowEntryScope entry(*window, false, false, false);
        if (!entry) return;
        EntryStackGuard guard(AppEntry::Event, "closeRequested");
        CloseRequestEventArgs aggregate;
        auto saved = FrameworkEventAccess::snapshot(window->events().closeRequested);
        notifyRequestListeners<CloseRequestEventArgs>(saved, aggregate, "closeRequested");
        if (aggregate.cancel || !window->isOpen() || WindowRequestAccess::closeRequested(*window)) return;
        if (WindowRequestAccess::quitOnClose(*window) ||
                std::count_if(runtime().windows.begin(), runtime().windows.end(),
                    [](const auto& w) { return w->isOpen() && !WindowRequestAccess::closeRequested(*w); }) == 1) {
            if (!dispatchExitRequested()) return;
            setExitReason("last-window-close");
            prepareRuntimeExit();
            if (headless::isActive()) headless::running = false;
            else sapp_quit();
            return; // Common ending owns every Window; native must not close W first.
        }
        allow = true;
    });
    return allow;
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
        auto* window = resolveWindow();
        if (!window) return;
        windowTick(WindowRequestAccess::binding(*window).handle, window);
    });
    afterHostTurn();
}
}
}

namespace trussc {
Runtime& runtime() { static Runtime facade; return facade; }
RuntimeEvents& Runtime::events() { return internal::runtime().events; }
void Runtime::setLogoutTimeout(float seconds) {
    if (std::isnan(seconds) || seconds < 0) {
        logError("Runtime") << "setLogoutTimeout(): seconds must be nonnegative and not NaN";
        return;
    }
    internal::runtime().logoutTimeout = seconds;
}
}
