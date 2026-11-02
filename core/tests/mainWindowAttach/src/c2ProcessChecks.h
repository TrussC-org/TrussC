// C2 process notifications and addressed OS close, without a native device.
namespace c2 {
struct State {
    std::string order;
    int exits[3]{}, closes[3]{}, console[3]{};
    bool unsaved[3]{false, false, false};
    std::function<void(int, ExitRequestEventArgs&)> quit;
};
struct AppProbe : App {
    State& state;
    int id;
    std::vector<EventListener> listeners;
    AppProbe(State& s, int i) : state(s), id(i) {
        listeners.push_back(events().exitRequested.listen([this](ExitRequestEventArgs& e) {
            check("App quit subscriber token", internal::currentAppOwner() == internal::appOwner(*this));
            check("App quit begins fresh false and empty", !e.cancel && e.reason.empty());
            state.order += char('A' + id);
            if (state.quit) state.quit(id, e);
            if (state.unsaved[id]) { e.cancel = true; e.reason = "unsaved " + std::to_string(id); }
        }));
        listeners.push_back(events().closeRequested.listen([this](CloseRequestEventArgs& e) {
            ++state.closes[id];
            check("close subscriber token", internal::currentAppOwner() == internal::appOwner(*this));
            check("close starts fresh false", !e.cancel);
            e.cancel = state.unsaved[id];
        }));
        listeners.push_back(runtime().events().console.listen([this](ConsoleEventArgs&) {
            check("Runtime console restores subscriber token", internal::currentAppOwner() == internal::appOwner(*this));
            ++state.console[id];
        }));
    }
    void exit() override {
        ++state.exits[id];
        const auto code = internal::appExitCode();
        const auto reason = internal::exitReason();
        exitApp(99); requestExitApp();
        check("exitApp from App exit is inert", code == internal::appExitCode() && reason == internal::exitReason());
    }
};
std::shared_ptr<Window> make(State& state, int id) {
    auto w = std::make_shared<Window>();
    internal::WindowRequestAccess::bindForTests(*w, 801 + id);
    internal::retainRuntimeWindow(w);
    w->setApp<AppProbe>(state, id);
    internal::applyPendingAppForTests(*w);
    return w;
}
bool osClose(const std::shared_ptr<Window>& w) {
    auto handle = internal::WindowRequestAccess::binding(*w).handle;
    if (!internal::allowWindowClose(handle, w.get())) return false;
    internal::windowClosed(handle, w.get());
    return true;
}
void run() {
    internal::beginRuntimeRun(); internal::beginFrameworkLifetime();
    State state;
    auto a = make(state, 0), b = make(state, 1);
    int process = 0;
    auto root = runtime().events().exitRequested.listen([&](ExitRequestEventArgs& e) {
        check("Runtime quit first with process scope", state.order.empty() && !internal::currentAppOwner());
        check("Runtime fresh payload", !e.cancel && e.reason.empty());
        ++process; state.order += 'R';
    });
    for (int unsaved : {0, 1}) {
        state.unsaved[unsaved] = true;
        for (bool clearFirst : {false, true}) {
            // Both listener orders, including an explicit later false on the
            // same hub, must retain a veto and its first nonempty reason.
            auto clearer = (unsaved == 0 ? a : b)->events().exitRequested.listen([](ExitRequestEventArgs& e) {
                check("later false receives fresh false", !e.cancel && e.reason.empty());
                e.cancel = false;
            }, clearFirst ? -100 : 200);
            state.order.clear();
            check("unsaved App veto survives clean sibling and later false", !internal::dispatchExitRequested());
            check("Runtime first then reverse attachment", state.order == "RBA");
            check("first nonempty veto reason preserved", internal::exitBlockReason() == "unsaved " + std::to_string(unsaved));
        }
        state.unsaved[unsaved] = false;
    }
    auto earlyReason = runtime().events().exitRequested.listen([](ExitRequestEventArgs& e) { e.cancel = true; });
    auto reason = b->events().exitRequested.listen([](ExitRequestEventArgs& e) { e.cancel = true; e.reason = "first reason"; }, -1);
    auto otherReason = a->events().exitRequested.listen([](ExitRequestEventArgs& e) { e.cancel = true; e.reason = "later reason"; });
    state.order.clear(); check("empty earlier veto retains later first reason", !internal::dispatchExitRequested() && internal::exitBlockReason() == "first reason");
    earlyReason.disconnect(); reason.disconnect(); otherReason.disconnect();
    EventListener ownedRuntime;
    auto* render = trussc::internal::WindowRequestAccess::context(*a).render;
    const auto matrixDepth = render->getMatrixStackDepth(), styleDepth = render->getStyleStackDepth();
    {
        internal::AppOwnerScope owner(internal::appOwner(*a->getApp()));
        ownedRuntime = runtime().events().exitRequested.listen([](ExitRequestEventArgs& e) {
            pushMatrix(); pushStyle(); e.cancel = true;
        });
    }
    state.order.clear(); internal::dispatchExitRequested();
    check("Runtime subscriber guard restores its own App stacks", render->getMatrixStackDepth() == matrixDepth && render->getStyleStackDepth() == styleDepth);
    ownedRuntime.disconnect();
    auto throwing = runtime().events().exitRequested.listen([](ExitRequestEventArgs&) { throw std::runtime_error("C2 quit veto"); });
    state.order.clear(); check("throwing quit veto still visits both Apps", !internal::dispatchExitRequested() && state.order == "RBA");
    throwing.disconnect();
    // Device loss deliberately retains mutable last-value cancellation.
    std::string device;
    auto d0 = runtime().events().deviceLost.listen([&](DeviceLostEventArgs& e) { device += 'R'; e.cancel = true; });
    auto d1 = b->events().deviceLost.listen([&](DeviceLostEventArgs& e) { device += 'B'; check("device payload remains mutable", e.cancel); e.cancel = false; });
    auto d2 = a->events().deviceLost.listen([&](DeviceLostEventArgs& e) { device += 'A'; check("device later false is visible", !e.cancel); e.cancel = true; });
    internal::dispatchDeviceLost(0x887a0006);
    check("device Runtime first reverse Apps", device == "RBA");
    // Targeted generation reload excludes unrelated Apps.
    auto generation = internal::nextAppGeneration();
    internal::appOwner(*a->getApp())->generation = generation;
    std::string reload;
    auto h0 = runtime().events().hotReloadUnload.listen([&] { reload += 'R'; });
    auto h1 = a->events().hotReloadUnload.listen([&] { reload += 'A'; });
    auto h2 = b->events().hotReloadUnload.listen([&] { reload += 'B'; });
    internal::dispatchHotReloadUnload(generation);
    check("reload notifies Runtime and replacing App only", reload == "RA");
    // A sibling replacement during notification cannot receive this request.
    auto oldA = a->getApp();
    state.quit = [&](int id, ExitRequestEventArgs&) { if (id == 1) { a->setApp<AppProbe>(state, 2); internal::applyPendingAppForTests(*a); } };
    state.order.clear(); check("replacement during notification accepted", internal::dispatchExitRequested());
    check("no old or new sibling notification in same snapshot", state.order == "RB" && state.exits[0] == 1);
    state.quit = {};
    state.order.clear(); internal::dispatchExitRequested(); check("replacement joins next request", state.order == "RCB");
    // Closing sibling under notification is safe and does not duplicate exit.
    state.quit = [&](int id, ExitRequestEventArgs&) { if (id == 2) osClose(b); };
    state.order.clear(); internal::dispatchExitRequested();
    check("sibling close skips retired snapshot and exits once", state.order == "RC" && !b->isOpen() && state.exits[1] == 1);
    state.quit = {};
    root.disconnect();
    // Console once per slot, before drawing, after first close and at Window0.
    int unownedConsole = 0, queueCalls = 0, maintenanceCalls = 0;
    auto maintenance = internal::runtimeMaintenanceEvent().listen([&] { ++maintenanceCalls; });
    auto input = runtime().events().console.listen([&](ConsoleEventArgs&) { check("unowned console process token", !internal::currentAppOwner()); ++unownedConsole; });
    auto enqueue = [] { console::internal::getChannel().send(console::internal::parseLine("c2")); };
    console::internal::isRunning().store(true); // deterministic queue; no stdin reader
    enqueue(); internal::beforeHostTurn(); internal::afterHostTurn();
    check("console one surviving subscription with no draw", state.console[2] == 1 && state.console[0] == 0 && state.console[1] == 0 && unownedConsole == 1);
    auto veto = runtime().events().exitRequested.listen([](ExitRequestEventArgs& e) { e.cancel = true; });
    state.order.clear();
    check("last Window exit veto keeps Window open", !osClose(a) && a->isOpen() && state.order == "C");
    a->close();
    check("code last close veto keeps native Window open", a->isOpen() && !internal::WindowRequestAccess::closeRequested(*a));
    // Test-only disposal exercises an empty Runtime; the public close was vetoed.
    internal::WindowRequestAccess::teardown(*a);
    check("empty fixture Runtime keeps servicing", internal::resolveWindow() == nullptr);
    runOnMainThread([&] { ++queueCalls; });
    enqueue(); internal::beforeHostTurn(); internal::afterHostTurn();
    check("Window0 queues and maintenance continue", queueCalls == 1 && maintenanceCalls == 2);
    check("Window0 console services and retired subscribers", unownedConsole == 2 && state.console[2] == 1);
    check("App exit once through sibling close and shutdown", state.exits[0] == 1 && state.exits[1] == 1 && state.exits[2] == 1);
    console::stop();
    veto.disconnect();
    internal::shutdownRuntime(); internal::endFrameworkLifetime();

    // Both windows, both close-listener orders, later false, throw veto,
    // direct close bypass, opt-in and last-close follow-up requests.
    for (int target : {0, 1}) for (bool early : {false, true}) {
        internal::beginRuntimeRun(); internal::beginFrameworkLifetime();
        State closeState;
        auto x = make(closeState, 0), y = make(closeState, 1);
        auto w = target ? y : x;
        closeState.unsaved[target] = true;
        auto falseListener = w->events().closeRequested.listen([](CloseRequestEventArgs& e) { check("close false receives fresh payload", !e.cancel); e.cancel = false; }, early ? -100 : 200);
        check("OS close veto on either Window in both orders", !osClose(w) && w->isOpen());
        closeState.unsaved[target] = false;
        auto throwListener = w->events().closeRequested.listen([](CloseRequestEventArgs&) { throw std::runtime_error("C2 close veto"); }, early ? -200 : 300);
        check("throwing close listener veto", !osClose(w) && w->isOpen());
        throwListener.disconnect();
        internal::clearExitReason();
        check("OS close allow closes target only", osClose(w) && !w->isOpen() && (target ? x : y)->isOpen());
        check("default false nonlast close does not request quit", internal::exitReason().empty());
        auto remaining = target ? x : y;
        const int before = closeState.closes[1-target];
        remaining->close();
        check("direct close bypasses closeRequested", osClose(remaining) && closeState.closes[1-target] == before);
        check("last code close requests normal process ending", !remaining->isOpen() && internal::exitReason() == "last-window-close");
        internal::shutdownRuntime(); internal::endFrameworkLifetime();
    }
    internal::beginRuntimeRun(); internal::beginFrameworkLifetime();
    State optState; auto x = make(optState, 0), y = make(optState, 1);
    x->setQuitOnClose(true); internal::clearExitReason();
    optState.unsaved[1] = true;
    check("quitOnClose exit veto keeps both Windows open", !osClose(x) && x->isOpen() && y->isOpen());
    optState.unsaved[1] = false;
    check("quitOnClose accepted requests common process ending", !osClose(x) && x->isOpen() && y->isOpen() && internal::exitReason() == "last-window-close");
    internal::shutdownRuntime();
    check("accepted process close ends both Apps once", !x->isOpen() && !y->isOpen() && optState.exits[0] == 1 && optState.exits[1] == 1);
    internal::endFrameworkLifetime();
    internal::beginRuntimeRun(); internal::beginFrameworkLifetime();
    // Classic unsaved single-window listener, without a closeRequested listener.
    auto single = std::make_shared<Window>();
    internal::WindowRequestAccess::bindForTests(*single, 901); internal::retainRuntimeWindow(single);
    single->setApp<App>(); internal::applyPendingAppForTests(*single);
    auto unsaved = single->events().exitRequested.listen([](ExitRequestEventArgs& e) { e.cancel = true; });
    check("single-window exitRequested alone cancels OS close", !osClose(single) && single->isOpen());
    unsaved.disconnect();
    check("single-window accepted requests process ending", !osClose(single) && single->isOpen() && internal::exitReason() == "last-window-close");
    internal::shutdownRuntime(); internal::endFrameworkLifetime();
    internal::beginRuntimeRun(); internal::beginFrameworkLifetime();
    State manualState; auto manualWindow = make(manualState, 0), sibling = make(manualState, 1);
    EventListener manual;
    {
        internal::AppOwnerScope scope(internal::appOwner(*manualWindow->getApp()));
        manual = manualWindow->events().exit.listen([] { exitApp(); });
    }
    internal::clearExitReason();
    check("owned Window exit listener can request normal quit", osClose(manualWindow) && sibling->isOpen() && internal::exitReason() == "exit-app");
    internal::shutdownRuntime(); internal::endFrameworkLifetime();
    internal::beginRuntimeRun(); internal::beginFrameworkLifetime();
    State surfaceState;
    auto surface = std::make_shared<Window>();
    auto& binding = internal::WindowRequestAccess::binding(*surface);
    binding.bound = true; // a single-surface host has no native Window handle
    internal::retainRuntimeWindow(surface);
    surface->setApp<AppProbe>(surfaceState, 0); internal::applyPendingAppForTests(*surface);
    surface->close();
    check("surface code close waits for safe host boundary", surface->isOpen() && surfaceState.exits[0] == 0);
    internal::beforeHostTurn(); internal::afterHostTurn();
    check("surface host boundary retires App and closes once", !surface->isOpen() && surfaceState.exits[0] == 1);
    internal::shutdownRuntime(); internal::endFrameworkLifetime();
}
}
