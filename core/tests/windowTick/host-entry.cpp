// Production-linked deterministic entry/coordinator probe; no native display.
#include <TrussC.h>
#include "sokol/util/sokol_window_event.h"
#include <cstdio>
#include <stdexcept>
#include <cmath>
#include <poll.h>
#include <dlfcn.h>
using namespace trussc;
namespace {
bool gpuValid = false;
sg_stats gpuStats{};
int checks = 0, failures = 0;
size_t eventAllocations = 0;
int customAppAllocations = 0, customAppDeallocations = 0;
void check(const char* label, bool ok) {
    ++checks;
    if (!ok) { ++failures; std::printf("host-entry: FAIL: %s\n", label); }
}
using Clock = std::chrono::steady_clock;
Clock::time_point now{};
Clock::time_point readClock() { return now; }
const internal::WindowTickPresentation presentation{[] {}, [] { return true; }, [] {}};
struct Native {
    sapp_tc_window_metadata metadata{};
    bool live = true;
    int reads = 0, acquisitions = 0;
    uint32_t expected = 0;
};
bool read(sapp_window handle, void* user, sapp_tc_window_metadata& metadata) {
    auto& native = *static_cast<Native*>(user);
    check("metadata uses the receiver identity", handle.id == native.expected);
    ++native.reads; metadata = native.metadata; return native.live;
}
sg_swapchain acquire(sapp_window, void* user) { ++static_cast<Native*>(user)->acquisitions; return {}; }
const internal::WindowNativeOperations operations{read, acquire, nullptr};
void bind(Window& window, Native& native, uint32_t id) {
    native.expected = id;
    auto& m = native.metadata;
    m.fb_width = 200; m.fb_height = 100; m.dpi_scale = 2;
    m.sample_count = 1; m.color_format = SAPP_PIXELFORMAT_RGB10A2;
    m.depth_format = SAPP_PIXELFORMAT_DEPTH_STENCIL;
    m.source_interval = .125; m.trust_hidden_signals = true;
    check("bind common operations", internal::bindWindow(window, {id}, operations, &native));
}
bool tick(Window& window) {
    now += std::chrono::milliseconds(125);
    return internal::enterWindowTick(window, internal::WindowRequestAccess::binding(window).handle,
                                     now, .5, &presentation);
}
struct Probe : App {
    int sizes = 0, updates = 0, setups = 0, exits = 0;
    bool seeded = true;
    std::function<void()> onExit;
    void windowResized(int w, int h) override { ++sizes; seeded &= w == 100 && h == 50; }
    void setup() override { ++setups; seeded &= getWidth() == 100 && getHeight() == 50; }
    void update() override { ++updates; }
    void exit() override { ++exits; if (onExit) onExit(); }
};
using TestDispatch = void (*)(const sapp_event*, sapp_window, Window*, internal::WindowContext&);
void deliver(const sapp_event* event, sapp_window handle, Window* window, TestDispatch dispatch = nullptr) {
    auto listener = window->events().rawEvent.listen([&](const sapp_event& e) {
        if (dispatch && event && e.type == event->type) dispatch(&e, handle, window, window->context());
    });
    internal::enterWindowEvent(event, handle, window);
}
void event(Window& window, TestDispatch dispatch = nullptr) {
    sapp_event e{}; e.type = SAPP_EVENTTYPE_KEY_DOWN;
    deliver(&e, internal::WindowRequestAccess::binding(window).handle, &window, dispatch);
}
void plainDraw(Window& window) {
    check("App draw binding exists after attachment", window.context().appEventListeners.size() >= 2);
    if (window.context().appEventListeners.size() < 2) return;
    window.context().appEventListeners.at(1) = internal::FrameworkEventAccess::listen(
        window.events().draw, [&window] { if (auto app = window.getApp()) app->draw(); }, EventPriority::App);
}
int bootstrapCallbacks = 0, dropGetterCalls = 0, peerEvents = 0;
bool bootstrapReady = false;
std::vector<std::string> nativeDropPaths;
std::shared_ptr<Probe> bootstrapApp;
std::function<void(sapp_event_type)> nativeSend;
std::function<bool()> queryEndSession;
void (*cancelNativeQuit)() = nullptr;
#include "native-admission.h"
void constructorSize(Window& window, double width, double height, bool) {
    auto& native = *static_cast<Native*>(internal::WindowRequestAccess::binding(window).user);
    native.metadata.fb_width = int(width * 2); native.metadata.fb_height = int(height * 2);
    nativeSend(SAPP_EVENTTYPE_RESIZED); // synchronous setContentSize -> windowDidResize seam
}
const internal::WindowNativeOperations bootstrapOperations{read, acquire, nullptr, nullptr, constructorSize};
struct BootstrapProbe : Probe {
    BootstrapProbe() { setWindowSize(120, 60); }
    void setup() override {
        ++setups;
        auto& window = *internal::mainWindow();
        check("ctx/getter equal authoritative hidden snapshot at setup",
              window.context().suspended && window.isOccluded() &&
              internal::WindowRequestAccess::binding(window).metadata.occluded);
    }
    void windowResized(int w, int h) override {
        ++sizes; seeded &= w == 120 && h == 60;
        nativeSend(SAPP_EVENTTYPE_FOCUSED); // even exit-boundary size seeding admits events
    }
};
void bootstrap() {
    using Run = void (*)(std::function<void()>);
    for (Run run : {admission_mac::run, admission_win32::run, admission_x11_glx::run, admission_x11_egl::run}) {
        auto& window = *internal::mainWindow();
        Native native; bind(window, native, 717);
        internal::WindowRequestAccess::binding(window).operations = &bootstrapOperations;
        Window other; Native peerNative; bind(other, peerNative, 716);
        bootstrapCallbacks = dropGetterCalls = 0; bootstrapReady = false;
        window.context().drawCount = 0;
        std::vector<sapp_event_type> delivered;
        std::vector<std::vector<std::string>> drops;
        std::vector<int> keys;
        int exitRequests = 0; peerEvents = 0;
        auto listener = window.events().rawEvent.listen([&](const sapp_event& event) {
            ++bootstrapCallbacks; delivered.push_back(event.type);
            check("every startup notification precedes first draw", window.context().drawCount == 0);
            if (bootstrapReady)
                check("post-init input sees attached seeded App", window.getApp() == bootstrapApp && bootstrapApp->sizes == 1 && bootstrapApp->setups == 0);
        });
        auto exit = window.events().exitRequested.listen([&](ExitRequestEventArgs& args) {
            ++exitRequests; args.cancel = true; args.reason = "startup veto";
        });
        auto dropped = window.events().filesDropped.listen([&](DragDropEventArgs& args) { drops.push_back(args.files); });
        auto pressed = window.events().keyPressed.listen([&](KeyEventArgs& args) { keys.push_back(args.key); });
        auto released = window.events().keyReleased.listen([&](KeyEventArgs& args) { keys.push_back(-args.key); });
        nativeDropPaths = {"/tmp/during-init.txt"}; native.metadata.occluded = true;
        std::vector<sapp_event_type> expected;
        run([&] {
            check("construction and queued-before-first-tick notifications are dropped", delivered.empty() && keys.empty());
            for (auto type : {SAPP_EVENTTYPE_FOCUSED, SAPP_EVENTTYPE_KEY_DOWN, SAPP_EVENTTYPE_CHAR,
                              SAPP_EVENTTYPE_KEY_UP, SAPP_EVENTTYPE_FILES_DROPPED,
                              SAPP_EVENTTYPE_MOUSE_MOVE, SAPP_EVENTTYPE_CLIPBOARD_PASTED}) {
                expected.push_back(type); nativeSend(type);
                check("during-init raw delivery is synchronous", delivered == expected);
            }
            sapp_event e{}; e.type=SAPP_EVENTTYPE_FOCUSED;
            deliver(&e, {716}, &other,
                [](const sapp_event*, sapp_window, Window*, internal::WindowContext&) { ++peerEvents; });
            check("secondary init-time event is admitted", peerEvents == 1);
            if (queryEndSession) {
                expected.push_back(SAPP_EVENTTYPE_QUIT_REQUESTED);
                check("WM_QUERYENDSESSION returns FALSE for exitRequested veto during init", queryEndSession() && exitRequests == 1);
            }
            expected.push_back(SAPP_EVENTTYPE_RESIZED);
            bootstrapApp = std::make_shared<BootstrapProbe>();
            check("constructor setWindowSize delivers raw RESIZED synchronously", delivered == expected && delivered.back() == SAPP_EVENTTYPE_RESIZED);
            window.setApp(bootstrapApp);
            expected.push_back(SAPP_EVENTTYPE_FOCUSED); // initial size callback at bootstrap exit
        });
        bootstrapReady = true;
        check("init notifications delivered once including size-seeding callback", delivered == expected);
        check("during-init input and native drop paths delivered", keys == std::vector<int>{SAPP_KEYCODE_B, -SAPP_KEYCODE_B} && drops == std::vector<std::vector<std::string>>{{"/tmp/during-init.txt"}});
        check("hidden seed leaves visibility pending for checkpoint 3", window.isOccluded() && window.context().suspended && !internal::WindowRequestAccess::binding(window).lastDeliveredHidden);
        check("visibility history belongs to each Window", !internal::WindowRequestAccess::binding(other).lastDeliveredHidden);
        check("initial App attached and size seeded before setup", window.getApp() == bootstrapApp && bootstrapApp->sizes == 1 && bootstrapApp->seeded && bootstrapApp->setups == 0);
        expected.push_back(SAPP_EVENTTYPE_SUSPENDED);
        for (auto type : {SAPP_EVENTTYPE_FOCUSED, SAPP_EVENTTYPE_KEY_DOWN, SAPP_EVENTTYPE_CHAR, SAPP_EVENTTYPE_KEY_UP}) {
            expected.push_back(type); nativeSend(type);
            check("post-init input before first draw delivered once in order", delivered == expected);
        }
        for (const auto& paths : {std::vector<std::string>{"/tmp/A.txt", "/tmp/A2.txt"}, std::vector<std::string>{"/tmp/B.txt"}}) {
            nativeDropPaths = paths; expected.push_back(SAPP_EVENTTYPE_FILES_DROPPED); nativeSend(SAPP_EVENTTYPE_FILES_DROPPED);
        }
        check("each post-init drop reads its own paths", drops == std::vector<std::vector<std::string>>{{"/tmp/during-init.txt"}, {"/tmp/A.txt", "/tmp/A2.txt"}, {"/tmp/B.txt"}});
        check("startup input arrives exactly once without replay", delivered == expected && bootstrapCallbacks == int(expected.size()) && window.context().keysPressed.empty());
        plainDraw(window); tick(window);
        check("setup ran with authoritative snapshot", bootstrapApp->setups == 1);
        check("ready entry delivers pending visibility once", internal::WindowRequestAccess::binding(window).lastDeliveredHidden);
        listener.disconnect(); dropped.disconnect(); pressed.disconnect(); released.disconnect(); exit.disconnect();
        sapp_event visibility{}; visibility.type = SAPP_EVENTTYPE_SUSPENDED;
        internal::mainWindowEvent(&visibility, {717}, &window);
        check("delivered aggregate records per-Window visibility", internal::WindowRequestAccess::binding(window).lastDeliveredHidden && !internal::WindowRequestAccess::binding(other).lastDeliveredHidden);
        native.metadata.occluded = false; internal::refreshWindowMetadata(window);
        check("snapshot refresh preserves last delivery", !window.isOccluded() && internal::WindowRequestAccess::binding(window).lastDeliveredHidden);
        visibility.type = SAPP_EVENTTYPE_RESUMED; internal::mainWindowEvent(&visibility, {717}, &window);
        check("delivered resume records visible state", !internal::WindowRequestAccess::binding(window).lastDeliveredHidden);
        internal::WindowRequestAccess::endApp(window); internal::WindowRequestAccess::unbind(window);
        internal::WindowRequestAccess::resetForLaunch(window); bootstrapApp.reset(); nativeDropPaths.clear();
        cancelNativeQuit = nullptr;
    }
}

int inputs = 0;
void input(const sapp_event*, sapp_window, Window* window, internal::WindowContext&) {
    ++inputs;
    auto app = std::static_pointer_cast<Probe>(window->getApp());
    check("size is seeded before first input", app && app->sizes == 1 && app->seeded && app->setups == 0);
    if (!app) return;
    check("same-window tick is rejected inside input", !tick(*window) && app->updates == 0);
    check("native deferred endpoints observe active host callbacks", internal::hostCallbacksActive());
}
void constructorsAndEpochs() {
    internal::beginRuntimeRun();
    const auto epoch = internal::runtimeEpoch();
    const auto firstRun = internal::appRunToken();
    check("run has a live host token", firstRun && firstRun->load());
    bool staleQueueRan = false, newQueueRan = false;
    std::thread staleWorker([&] { runOnMainThread([&] { staleQueueRan = true; }, firstRun); });
    staleWorker.join();
    internal::beginRuntimeRun();
    check("next run has a distinct epoch and invalidates old token",
        internal::runtimeEpoch() == epoch + 1 && !firstRun->load() && internal::appRunToken() != firstRun);
    std::thread newWorker([&] { runOnMainThread([&] {
        newQueueRan = true;
        check("Window0 queue delivery uses process context", !internal::currentWindow());
    }, internal::appRunToken()); });
    newWorker.join();
    { internal::HostTurnScope turn; }
    check("Window0 outer turn progresses queue and excludes old epoch work", !staleQueueRan && newQueueRan);
    auto* previous = internal::currentAppConstruction();
    const auto owner = mcp::internal::registrationOwner();
    int aOwner, bOwner;
    internal::AppConstructionTicket a{internal::appRunToken(), &aOwner};
    internal::AppConstructionTicket b{std::make_shared<std::atomic<bool>>(true), &bOwner};
    struct Derived : App {
        explicit Derived(internal::AppConstructionTicket& ticket) {
            const auto& attributed = internal::WindowRequestAccess::constructionTicket(*this);
            check("constructor scope precedes candidate attribution", internal::currentAppConstruction() == &ticket &&
                !ticket.app && !attributed.app && !attributed.runToken);
            check("derived constructor sees its own owner and token", mcp::internal::registrationOwner() == ticket.registrationOwner &&
                internal::appRunToken() == ticket.runToken && internal::constructorRunToken() == ticket.runToken && internal::hostCallbacksActive());
        }
    };
    {
        internal::AppConstructionScope outer(a);
        const auto before = internal::runtimeEpoch();
        bool rejected = false;
        try { internal::beginRuntimeRun(); } catch (const std::logic_error&) { rejected = true; }
        check("a nested run cannot replace the active epoch", rejected && internal::runtimeEpoch() == before &&
            internal::appRunToken() == a.runToken);
        const auto previousExitCode = internal::appExitCode();
        internal::appExitCode() = 71;
        rejected = false;
        try { (void)buildAppDescriptor<Probe>(); } catch (const std::logic_error&) { rejected = true; }
        check("nested startup rejects before changing active run state", rejected && internal::appExitCode() == 71 &&
            internal::runtimeEpoch() == before && internal::appRunToken() == a.runToken);
        internal::appExitCode() = previousExitCode;
        auto app = internal::makeConstructedApp<Derived>(a);
        check("completed candidate receives scope attribution", a.app == app.get() &&
            internal::WindowRequestAccess::constructionTicket(*app).runToken == a.runToken &&
            internal::WindowRequestAccess::constructionTicket(*app).epoch == internal::runtimeEpoch());
        {
            internal::AppConstructionScope inner(b);
            auto nested = internal::makeConstructedApp<Derived>(b);
            check("completed nested candidate receives B attribution", b.app == nested.get() &&
                internal::WindowRequestAccess::constructionTicket(*nested).registrationOwner == &bOwner);
            check("nested constructor selects B", internal::currentAppConstruction() == &b);
        }
        check("nested constructor restores A", internal::currentAppConstruction() == &a &&
            mcp::internal::registrationOwner() == &aOwner && internal::constructorRunToken() == a.runToken);
        internal::AppConstructionTicket failed{b.runToken, &bOwner};
        struct Throwing : App { Throwing() { throw std::runtime_error("derived constructor"); } };
        try {
            internal::AppConstructionScope inner(failed);
            (void)internal::makeConstructedApp<Throwing>();
        } catch (const std::runtime_error&) {}
        check("failed derived constructor unwinds claim and restores A", !failed.app &&
            internal::currentAppConstruction() == &a && mcp::internal::registrationOwner() == &aOwner);
        if (const char* path = std::getenv("TC_CONSTRUCTOR_GUEST")) {
            void* guest = dlopen(path, RTLD_NOW | RTLD_LOCAL);
            check("constructor guest links to host", guest != nullptr);
            if (guest) {
                using Probe = bool (*)(internal::AppConstructionTicket*, uint64_t, const void*, const void*);
                auto probe = reinterpret_cast<Probe>(dlsym(guest, "cp4_constructor_probe"));
                check("guest shares host ticket, epoch, Runtime and token storage", probe &&
                    probe(&a, internal::runtimeEpoch(), &internal::runtime(), &internal::appRunToken()));
                dlclose(guest);
            }
        }
    }
    check("constructor stack and owner restore on exit", internal::currentAppConstruction() == previous &&
        mcp::internal::registrationOwner() == owner && !internal::hostCallbacksActive());
    struct EarlierBase { App helper; };
    struct Root : EarlierBase, App {};
    struct VirtualRoot : EarlierBase, virtual App {};
    auto attribution = [&]<class T>() {
        internal::AppConstructionTicket ticket{internal::appRunToken(), &aOwner};
        internal::AppConstructionScope scope(ticket);
        auto candidate = internal::makeConstructedApp<T>();
        const auto& root = internal::WindowRequestAccess::constructionTicket(*candidate);
        const auto& helper = internal::WindowRequestAccess::constructionTicket(candidate->helper);
        check("requested App claims ticket after earlier incidental App", ticket.app == static_cast<App*>(candidate.get()) &&
            root.app == ticket.app && root.epoch == internal::runtimeEpoch() && root.registrationOwner == &aOwner &&
            root.runToken == ticket.runToken && !helper.app && !helper.runToken);
    };
    attribution.template operator()<Root>();
    attribution.template operator()<VirtualRoot>();
    {
        internal::AppConstructionTicket ticket{internal::appRunToken(), &aOwner};
        internal::AppConstructionScope scope(ticket);
        std::unique_ptr<Root> candidate(new Root());
        internal::completeAppConstruction(*candidate);
        check("guest allocation attributes requested App, not helper", ticket.app == static_cast<App*>(candidate.get()) &&
            !internal::WindowRequestAccess::constructionTicket(candidate->helper).app);
    }
    struct CustomRoot : EarlierBase, App {
        static void* operator new(size_t size) { ++customAppAllocations; return ::operator new(size); }
        static void operator delete(void* p) noexcept { ++customAppDeallocations; ::operator delete(p); }
    };
    {
        internal::AppConstructionTicket ticket{internal::appRunToken(), &aOwner};
        internal::AppConstructionScope scope(ticket);
        std::unique_ptr<CustomRoot> candidate(new CustomRoot());
        internal::completeAppConstruction(*candidate);
        check("raw guest attribution preserves class allocation", customAppAllocations == 1 &&
            ticket.app == static_cast<App*>(candidate.get()) && !internal::WindowRequestAccess::constructionTicket(candidate->helper).app);
    }
    check("raw guest attribution preserves class deallocation", customAppDeallocations == 1);
    internal::restoreWindowContext(&internal::mainWindowContext());
    setColor(1, 0, 0);
    std::thread styleWorker([] { runOnMainThread([] {
        check("service style uses process selection", !internal::currentWindow());
        setColor(0, 1, 0);
    }); });
    styleWorker.join();
    { internal::HostTurnScope turn; }
    const auto color = internal::getDefaultContext().getColor();
    check("process drawing style leaves main red", color.r == 1 && color.g == 0 && color.b == 0);
    internal::restoreWindowContext(nullptr);
    check("null restoration selects no Window", !internal::currentWindowCtx() && !internal::currentWindow() &&
        &internal::currentWindowContext() == &internal::nonWindowContext());
    check("process dimensions never fall back to a native main surface",
        !internal::usesNativeWindowDimensions(internal::currentWindowContext()));
}
void processToolSnapshots() {
    auto& window = *internal::mainWindow();
    Native native; bind(window, native, 141);
    auto& ctx = window.context();
    const auto updates = ctx.updateCount;
    const auto rates = ctx.rateCount;
    const auto duration = ctx.rateDurations[0], steps = ctx.rateSteps[0];
    ctx.updateCount = 42;
    ctx.rateCount = 1; ctx.rateDurations[0] = .5; ctx.rateSteps[0] = 3;
    internal::restoreWindowContext(nullptr);
    const auto level = mcp::getToolLevel();
    mcp::setToolLevel(mcp::ToolLevel::ReadOnly);
    auto call = [](const char* name) {
        auto reply = json::parse(mcp::internal::Server::instance().processMessage(
            json{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"},
                 {"params", {{"name", name}, {"arguments", json::object()}}}}.dump()));
        return json::parse(reply.at("result").at("content").at(0).at("text").get<std::string>());
    };
    const auto health = call("tc_get_health");
    const auto windows = call("tc_list_windows");
    check("process health reads explicit main metrics", health.at("width") == 100 && health.at("height") == 50 &&
        health.at("frameCount") == 42 && health.at("fps") == 6);
    check("process window listing preserves main dimensions", windows.at("windows").at(0).at("width") == 100 &&
        windows.at("windows").at(0).at("height") == 50);
    check("tool snapshots leave process context unselected", !internal::currentWindowCtx() && !internal::currentWindow());
    mcp::setToolLevel(level);
    internal::WindowRequestAccess::unbind(window);
    ctx.updateCount = updates; ctx.rateCount = rates;
    ctx.rateDurations[0] = duration; ctx.rateSteps[0] = steps;
}
int queues = 0, maintenance = 0, reloads = 0;
bool throwQueue = false;
void queueService() {
    ++queues;
    check("queues select no Window", internal::currentWindowCtx() == nullptr && !internal::currentWindow());
    internal::beforeHostTurn(); internal::afterHostTurn();
    check("nested service turn coalesces", queues == maintenance + 1);
    if (throwQueue) throw std::runtime_error("queue service fault");
}
void maintain() {
    ++maintenance;
    check("maintenance selects no Window", internal::currentWindowCtx() == nullptr && !internal::currentWindow());
    internal::HostTurnScope nested;
    check("maintenance cannot reenter services", queues == maintenance);
}
void reload() { ++reloads; }
void roots() {
    for (bool runner : {true, false}) {
        Window secondary, peer;
        auto& window = runner ? *internal::mainWindow() : secondary;
        Native native, peerNative;
        bind(window, native, runner ? 718u : 719u); bind(peer, peerNative, 720);
        auto app = std::make_shared<Probe>();
        window.setIndependentFps(VSYNC, EVENT_DRIVEN); window.context().loop.redrawCount = 0;
        window.setApp(app);
        auto* previous = internal::currentWindowCtx();
        internal::currentWindowCtx() = &peer.context();
        inputs = 0;
        event(window, input);
        check("entry delivers first input exactly once", inputs == 1);
        plainDraw(window);
        if (runner) {
            int rawEvents = 0;
            auto raw = window.events().rawEvent.listen([&](const sapp_event&) { ++rawEvents; });
            sapp_event e{}; e.type = SAPP_EVENTTYPE_FOCUSED;
            internal::mainWindowEvent(&e, {9999}, &window);
            check("launch event client rejects the delivered stale identity", rawEvents == 0);
            internal::mainWindowEvent(&e, {native.expected}, &window);
            check("launch event client uses the common binding and converter", rawEvents == 1);
        }
        check("event restores the actual peer caller", internal::currentWindowCtx() == &peer.context());
        check("entry acquires no drawable before draw admission", native.acquisitions == 0);
        window.setIndependentFps(VSYNC, EVENT_DRIVEN); window.context().loop.redrawCount = 0;
        const auto before = app->updates;
        native.metadata.source_interval = .25;
        check("EVENT_DRIVEN no-draw tick is accepted", !tick(window) && app->updates == before + 1);
        check("setup precedes first update with seeded dimensions", app->setups == 1 && app->seeded);
        check("tick uses metadata source interval", window.context().tickDuration == .25);
        check("tick restores the actual peer caller", internal::currentWindowCtx() == &peer.context());
        window.setIndependentFps(EVENT_DRIVEN, EVENT_DRIVEN); window.context().loop.redrawCount = 0;
        const auto idleUpdates = window.context().updateCount, idleResources = window.context().resourceTickCount;
        tick(window);
        check("idle independent admission has equal resource semantics for both roots", window.context().updateCount == idleUpdates && window.context().resourceTickCount == idleResources + 1);
        event(window, [](const sapp_event*, sapp_window, Window*, internal::WindowContext& ctx) {
            check("resource key follows common admitted tick count", internal::resourceFrameKey() == ctx.resourceTickCount);
        });
        const auto updates = window.context().updateCount;
        deliver(nullptr, {native.expected}, &window, input);
        sapp_event e{};
        deliver(&e, {9999}, &window, input);
        check("invalid events are rejected without delivery", inputs == 1);
        const int readsBefore = native.reads;
        check("stale tick identity is rejected", !internal::enterWindowTick(window, {9999}, now, .1, &presentation) && window.context().updateCount == updates && native.reads == readsBefore);
        native.live = false;
        event(window, [](const sapp_event*, sapp_window, Window*, internal::WindowContext&) { ++inputs; });
        check("failed metadata read rejects stale binding entry", inputs == 1 && internal::currentWindowCtx() == &peer.context());
        native.live = true;
        auto throwing = window.events().keyPressed.listen([](KeyEventArgs&) { throw 1; });
        event(window, [](const sapp_event*, sapp_window, Window* w, internal::WindowContext&) {
            /* Common converter performs typed delivery. */
        });
        check("throwing event restores caller and dispatch depth", internal::currentWindowCtx() == &peer.context() && window.dispatchDepth_ == 0 && !internal::WindowRequestAccess::dispatchActive(window));
        throwing.disconnect();
        // A -> B -> A cannot tick A while A's callback remains on the stack.
        auto peerInput = peer.events().keyPressed.listen([&](KeyEventArgs&) {
            check("A-B-A same-window tick reentry is rejected", !tick(window));
        });
        auto outerInput = window.events().keyPressed.listen([&](KeyEventArgs&) {
            event(peer, [](const sapp_event*, sapp_window, Window* w, internal::WindowContext&) {
                /* Common converter performs typed delivery. */
            });
            check("nested peer event restores receiving A", internal::currentWindowCtx() == &window.context());
        });
        event(window, [](const sapp_event*, sapp_window, Window* w, internal::WindowContext&) {
            /* Common converter performs typed delivery. */
        });
        outerInput.disconnect(); peerInput.disconnect();
        auto next = std::make_shared<Probe>();
        auto replace = window.events().keyPressed.listen([&](KeyEventArgs&) {
            window.setApp(next);
            check("attachment defers through callback", window.getApp() == app && app->exits == 0);
            tick(window);
            check("reentrant tick cannot apply pending attachment", window.getApp() == app && app->exits == 0);
        });
        event(window, [](const sapp_event*, sapp_window, Window* w, internal::WindowContext&) {
            /* Common converter performs typed delivery. */
        });
        check("attachment lands at outer entry exit", window.getApp() == next && app->exits == 1);
        replace.disconnect();
        auto committed = std::make_shared<Probe>(), later = std::make_shared<Probe>();
        next->onExit = [&] { window.setApp(later); };
        auto atExit = window.events().keyPressed.listen([&](KeyEventArgs&) { window.setApp(committed); });
        event(window, [](const sapp_event*, sapp_window, Window* w, internal::WindowContext&) {
            /* Common converter performs typed delivery. */
        });
        check("request created at exit boundary survives to next boundary", window.getApp() == committed && window.pendingApp_ == later && window.appRequested_);
        atExit.disconnect(); tick(window);
        check("next safe boundary applies the retained request", window.getApp() == later);
        next = later;
        plainDraw(window);
        // Capture existing policy as data; no ownership selector enters admission.
        native.metadata.occluded = true;
        next->seeded = true;
        window.setIndependentFps(VSYNC, EVENT_DRIVEN); window.context().loop.redrawCount = 0;
        const auto hiddenBefore = next->updates;
        tick(window);
        check("existing hidden policy survives common entry", next->updates == hiddenBefore + (window.context().pauseUpdatesWhenHidden ? 0 : 1));
        check("hidden entry acquires no drawable", native.acquisitions == 0);
        window.context().drawWhileHidden = false;
        window.context().pauseUpdatesWhenHidden = false;
        window.setFps(VSYNC);
        const auto noDrawCount = window.context().drawCount;
        check("hidden draw gate runs before fake presentation", !tick(window) && window.context().drawCount == noDrawCount);
        window.closeRequested_ = true;
        { internal::WindowEntryScope closedEntry(window, true); check("pending close rejects tick entry itself", !closedEntry); }
        auto refused = std::make_shared<Probe>(); window.pendingApp_ = refused; window.appRequested_ = true;
        const auto sizes = next->sizes, closingUpdates = next->updates;
        check("close outranks tick and attachment", !tick(window) && window.getApp() == next && next->sizes == sizes && next->updates == closingUpdates);
        int vetoes = 0;
        auto veto = window.events().exitRequested.listen([&](ExitRequestEventArgs& args) {
            ++vetoes; args.cancel = true; window.closeRequested_ = false;
        });
        sapp_event quit{}; quit.type = SAPP_EVENTTYPE_QUIT_REQUESTED;
        internal::enterWindowEvent(&quit, {native.expected}, &window);
        check("pending close still delivers the existing quit-veto endpoint", vetoes == 1 && !window.closeRequested_);
        veto.disconnect();
        window.context().drawWhileHidden = runner;
        window.context().pauseUpdatesWhenHidden = !runner;
        internal::currentWindowCtx() = previous;
        internal::WindowRequestAccess::unbind(window); internal::WindowRequestAccess::endApp(window);
        internal::WindowRequestAccess::unbind(peer); internal::WindowRequestAccess::endApp(peer);
    }
}
void retention() {
    Native native;
    bool destroyed = false, inside = false, destroyedInside = false;
    Window* raw = new Window;
    auto owner = std::shared_ptr<Window>(raw, [&](Window* w) {
        destroyed = true; destroyedInside |= inside;
        internal::WindowRequestAccess::unbind(*w);
        internal::WindowRequestAccess::endApp(*w);
        // Keep storage until the probe finishes, so a broken retainer fails assertions without UAF.
    });
    internal::WindowRequestAccess::setHostOwner(*raw, owner);
    bind(*raw, native, 830);
    auto listener = raw->events().keyPressed.listen([&](KeyEventArgs&) {
        inside = true; owner.reset();
        check("receiving Window stays owned throughout its callback", !destroyed);
        inside = false;
    });
    event(*raw, [](const sapp_event*, sapp_window, Window* w, internal::WindowContext&) {
        /* Common converter performs typed delivery. */
    });
    check("receiver release waits until safe entry unwind", destroyed && !destroyedInside && !internal::WindowRequestAccess::dispatchActive(*raw));
    listener.disconnect(); delete raw;

    // The common close bridge must retain through the existing ending endpoint too.
    destroyed = inside = destroyedInside = false;
    raw = internal::WindowRequestAccess::makeMain().release();
    owner = std::shared_ptr<Window>(raw, [&](Window*) {
        destroyed = true; destroyedInside |= inside;
        // Keep storage so a missing close retainer reports assertions without UAF.
    });
    internal::WindowRequestAccess::setHostOwner(*raw, owner);
    bind(*raw, native, 831);
    auto ending = raw->events().exit.listen([&] {
        inside = true; owner.reset();
        check("native close retains receiver throughout ending callbacks", !destroyed);
        inside = false;
    });
    internal::windowClosed({9999}, raw);
    check("common close bridge rejects stale identity", raw->isOpen() && !destroyed);
    internal::windowClosed({831}, raw);
    check("native close releases receiver only after safe unwind", destroyed && !destroyedInside && !internal::WindowRequestAccess::dispatchActive(*raw));
    ending.disconnect(); delete raw;
}
void turns() {
    const auto saved = internal::hostServiceOperationsForTests();
    internal::hostServiceOperationsForTests() = {queueService, maintain};
    internal::setHostReloadPoll(reload);
    auto* caller = internal::currentWindowCtx();
    internal::WindowContext peer;
    internal::currentWindowCtx() = &peer;
    for (int i = 1; i <= 3; ++i) {
        internal::beforeHostTurn();
        check("queues and reload run once per outer turn", queues == i && reloads == i);
        internal::beforeHostTurn(); internal::afterHostTurn();
        check("nested native turn does not duplicate process services", queues == i && maintenance == i - 1);
        internal::afterHostTurn();
        check("services and maintenance restore actual caller", internal::currentWindowCtx() == &peer);
        check("no-window no-draw turn still runs maintenance", maintenance == i);
    }
    Window window; Native native; bind(window, native, 921);
    event(window, [](const sapp_event*, sapp_window, Window*, internal::WindowContext&) {
        const int before = queues;
        internal::beforeHostTurn(); internal::afterHostTurn();
        check("process queues never run inside an App callback", queues == before);
    });
    internal::beforeHostTurn(); internal::afterHostTurn();
    check("nested callback turn coalesces to next outer boundary", queues == 4 && maintenance == 4 && reloads == 4);
    throwQueue = true;
    internal::beforeHostTurn(); internal::afterHostTurn();
    check("queue exception leaves maintenance and later turns serviceable", queues == 5 && maintenance == 5);
    throwQueue = false;
    auto ending = window.events().exit.listen([&] {
        const int before = queues;
        internal::beforeHostTurn(); internal::afterHostTurn();
        check("existing exit endpoint excludes process queue delivery", queues == before);
        check("existing exit endpoint excludes same-window ticks", !tick(window));
        check("native deferral sees ending callbacks after native unbind", internal::hostCallbacksActive());
    });
    internal::WindowRequestAccess::endApp(window);
    ending.disconnect();
    const auto queuesBeforeThrow = queues, maintenanceBeforeThrow = maintenance;
    try { internal::HostTurnScope turn; throw 1; } catch (...) {}
    check("throwing outer-loop work still completes the host turn", queues == queuesBeforeThrow + 1 && maintenance == maintenanceBeforeThrow + 1);
    internal::setHostReloadPoll(nullptr);
    internal::hostServiceOperationsForTests() = saved;
    internal::currentWindowCtx() = caller;
#ifndef __EMSCRIPTEN__
    mcp::internal::DeferredResponse response;
    response.id = 239; response.target = &window.context(); response.deadline = now;
    response.response = std::make_shared<std::promise<mcp::internal::ReplyThunk>>();
    auto expired = response.response->get_future();
    int produced = 0;
    response.makeEnvelope = [&] { ++produced; return [] { return std::string("unexpected capture"); }; };
    { std::lock_guard<std::mutex> lock(mcp::internal::deferredResponsesMutex());
      mcp::internal::deferredResponses().push_back(std::move(response)); }
    // No window tick, draw or afterFrame is needed for process-wide expiry.
    internal::beforeHostTurn(); internal::afterHostTurn();
    check("no-draw coordinator turn expires a peer capture without readback",
          expired.wait_for(std::chrono::seconds(0)) == std::future_status::ready && produced == 0);
#endif
    internal::WindowRequestAccess::unbind(window);
}
}
extern "C" void* __real__Znwm(size_t);
extern "C" void* __real__Znam(size_t);
extern "C" void* __wrap__Znwm(size_t size) { ++eventAllocations; return __real__Znwm(size); }
extern "C" void* __wrap__Znam(size_t size) { ++eventAllocations; return __real__Znam(size); }
// Model native process-owned drop storage, replaced before every notification.
extern "C" int __wrap_sapp_get_num_dropped_files(void) { ++dropGetterCalls; return static_cast<int>(nativeDropPaths.size()); }
extern "C" const char* __wrap_sapp_get_dropped_file_path(int index) { ++dropGetterCalls; return nativeDropPaths.at(index).c_str(); }
extern "C" void __wrap_sapp_cancel_quit(void) { if (cancelNativeQuit) cancelNativeQuit(); }
extern "C" bool __wrap_sg_isvalid(void) { return gpuValid; }
extern "C" sg_stats __wrap_sg_query_stats(void) { return gpuStats; }
int main() {
    getMainThreadId();
    internal::pixelPerfectMode() = false;
    internal::windowClockForTests() = readClock;
    constructorsAndEpochs();
    processToolSnapshots();
    WindowSettings settings; settings.pixelPerfect = false;
    const auto descriptor = internal::buildWindowDescriptor(settings);
    check("launch descriptor installs the common desktop tick client", descriptor._tc_tick_cb == internal::windowTick && descriptor._tc_bind_user == internal::mainWindow() && descriptor._tc_event_cb == internal::mainWindowEvent);
    check("launch descriptor installs process turn hooks", descriptor._tc_before_turn == internal::beforeHostTurn && descriptor._tc_after_turn == internal::afterHostTurn && descriptor._tc_callbacks_active == internal::hostCallbacksActive);
    bootstrap(); roots(); retention(); turns();
    auto* previous = internal::currentWindowCtx();
    auto& virtualContext = internal::nonWindowContext();
    internal::currentWindowCtx() = &virtualContext;
    virtualContext.updateCount = 17; virtualContext.resourceTickCount = 3;
    check("non-window headless resources retain their update key", internal::resourceFrameKey() == 17);
    internal::currentWindowCtx() = previous;
    gpuValid = true; gpuStats.prev_frame.frame_index = 23;
    check("GPU upload key is independent of a launch native tick", internal::deviceFrameCount() == 23);
    check("prefix work does not rotate the GPU upload key", internal::deviceFrameCount() == 23);
    gpuStats.prev_frame.frame_index = 24;
    check("no-draw GPU commit rotates upload admission", internal::deviceFrameCount() == 24);
    gpuValid = false;
    internal::windowClockForTests() = nullptr;
    std::printf("host-entry: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
