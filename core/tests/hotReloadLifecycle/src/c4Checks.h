// Runs in the own-binary host, loading the actual single-window guest.
#ifdef TC_C4_OBSERVE_SAPP_QUIT
int c4QuitCalls = 0;
extern "C" void __real_sapp_quit(void);
extern "C" void __wrap_sapp_quit(void) { ++c4QuitCalls; __real_sapp_quit(); }
#endif
namespace c4 {
using namespace tc;
using hot_reload::Host;
using hot_reload::GuestLibrary;
int failures = 0, attempts = 0;
Host* requesting = nullptr;
void check(const char* label, bool ok) {
    std::printf("hotReloadLifecycle: C4: %s: %s\n", label, ok ? "PASS" : "FAIL");
    std::fflush(stdout);
    if (!ok) ++failures;
}
std::shared_ptr<Window> window(uint32_t id) {
    auto w = std::make_shared<Window>();
    internal::WindowRequestAccess::bindForTests(*w, id);
    internal::retainRuntimeWindow(w);
    return w;
}
void poll() { requesting->poll(); }
void turn(Host& host) {
    requesting = &host;
    internal::setHostReloadPoll(poll);
    internal::beforeHostTurn(); internal::afterHostTurn();
    internal::setHostReloadPoll(nullptr);
}
struct Probe : App {
    Probe() { if (requesting) requesting->reload(); }
    void setup() override { if (requesting) requesting->reload(); }
    void exit() override { if (requesting) requesting->reload(); }
};
Window* constructorCloseTarget = nullptr;
struct CloseInConstructor : App { CloseInConstructor() { constructorCloseTarget->close(); } };
void* symbol(GuestLibrary& lib, const char* name) {
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(lib.handle, name));
#else
    return dlsym(lib.handle, name);
#endif
}
// Observe the loop exit request before fixture cleanup can cause App exits.
struct CloseExitObservation {
#ifndef TC_C4_OBSERVE_SAPP_QUIT
    bool wasActive = headless::active.exchange(true);
    bool wasRunning = headless::running.exchange(true);
    int calls = 0;
    ~CloseExitObservation() { headless::active = wasActive; headless::running = wasRunning; }
#endif
    int sample() {
#ifdef TC_C4_OBSERVE_SAPP_QUIT
        return c4QuitCalls;
#else
        if (!headless::running.exchange(true)) ++calls;
        return calls;
#endif
    }
};
void closeChecks() {
    CloseExitObservation exitRequest;
    for (bool sibling : {false, true}) {
        internal::beginRuntimeRun();
        auto a = window(701); auto b = sibling ? window(702) : nullptr;
        if (sibling) a->setQuitOnClose(true);
        int requests = 0, closes = 0, exits = 0;
        struct Closing : App { int* count; explicit Closing(int* p) : count(p) {} void exit() override { ++*count; } };
        a->setApp<Closing>(&exits); internal::applyPendingAppForTests(*a);
        auto veto = runtime().events().exitRequested.listen([&](ExitRequestEventArgs& e) { ++requests; e.cancel = true; });
        auto close = a->events().closeRequested.listen([&](CloseRequestEventArgs&) { ++closes; });
        const int beforeQuit = exitRequest.sample();
        a->close();
        check(sibling ? "code quitOnClose veto keeps both windows" : "code last close veto keeps window",
            exitRequest.sample() == beforeQuit && requests == 1 && closes == 0 && exits == 0 && a->isOpen() && !internal::WindowRequestAccess::closeRequested(*a) && (!b || b->isOpen()));
        veto.disconnect();
        auto accept = runtime().events().exitRequested.listen([&](ExitRequestEventArgs&) { ++requests; });
        a->close();
        check("accepted code close issues loop exit request", exitRequest.sample() == beforeQuit + 1);
        a->close();
        check("repeated accepted code close issues no second exit request", exitRequest.sample() == beforeQuit + 1);
        internal::shutdownRuntime();
        check("accepted code close exits once without closeRequested", requests == 2 && closes == 0 && exits == 1 && !a->isOpen() && (!b || !b->isOpen()));
    }
    internal::beginRuntimeRun();
    auto a = window(703), b = window(704); int requests = 0;
    auto listen = runtime().events().exitRequested.listen([&](ExitRequestEventArgs&) { ++requests; });
    a->close(); internal::WindowRequestAccess::teardown(*a);
    check("non-last plain code close asks no exitRequested", requests == 0 && !a->isOpen() && b->isOpen());
    internal::shutdownRuntime();
    for (bool cancel : {false, true}) {
        internal::beginRuntimeRun();
        auto first = window(705), last = window(706); int queries = 0;
        internal::clearExitReason();
        auto quit = runtime().events().exitRequested.listen([&](ExitRequestEventArgs& e) { ++queries; e.cancel = cancel; });
        const int beforeQuit = exitRequest.sample();
        first->close(); last->close();
        check("pending sibling decision issues loop exit request only on acceptance",
            exitRequest.sample() == beforeQuit + (cancel ? 0 : 1));
        if (!cancel) last->close();
        check("pending sibling repeated acceptance issues no second exit request",
            cancel || exitRequest.sample() == beforeQuit + 1);
        check(cancel ? "pending sibling close still asks last-window veto" : "pending sibling close still requests exit once",
            queries == 1 && (cancel ? !internal::WindowRequestAccess::closeRequested(*last) && internal::exitReason().empty()
                                  : internal::WindowRequestAccess::closeRequested(*last) && internal::exitReason() == "last-window-close"));
        internal::shutdownRuntime();
    }

}
int run() {
    getMainThreadId(); closeChecks(); internal::beginRuntimeRun();
    // Ordinary deferred reply liveness/closing checks, without reload interleavings.
    auto owner = std::make_shared<internal::AppOwner>(); int replies = 0;
    auto reply = mcp::internal::retainDeferredCode([&]() -> mcp::internal::ReplyThunk {
        return [&] { ++replies; return std::string("reply"); };
    }, owner, 239)();
    owner->callbacksClosing = true;
    std::thread closing([&] { reply(); }); closing.join();
    check("MCP deferred worker respects callbacksClosing", replies == 0);
    owner->callbacksClosing = false;
    std::thread live([&] { reply(); }); live.join();
    owner->token->store(false);
    std::thread retired([&] { reply(); }); retired.join();
    check("MCP deferred worker checks owner liveness", replies == 1);
    const auto dir = fs::temp_directory_path() / "tc-c4-safe-point";
    fs::create_directories(dir / "src");
    std::ofstream(dir / "CMakeCache.txt") << "CMAKE_COMMAND:INTERNAL=" << getExecutablePath().string() << '\n';
    std::ofstream(dir / "newer-core") << "stale host seam\n";
    fs::last_write_time(dir / "newer-core", fs::last_write_time(getExecutablePath()) + std::chrono::hours(1));
    Host host; host.projectDir = dir.string(); host.buildDir = dir.string(); host.srcDir = (dir / "src").string();
    host.coreLibPath = (dir / "newer-core").string(); host.initialized = true; host.watcher.init(host.srcDir);
    auto error = getLogger().onLog.listen([](LogEventArgs& e) {
        if (e.level == LogLevel::Error && e.message.find("host executable is older") != std::string::npos) ++attempts;
    });
    auto a = window(801); requesting = &host;
    a->setApp<Probe>(); internal::applyPendingAppForTests(*a);
    check("constructor request does no build", host.reloadRequested && attempts == 0);
    { internal::AppCallbackScope scope(internal::appOwner(*a->getApp()));
      a->getApp()->setup(); host.poll();
      internal::beforeHostTurn(); internal::afterHostTurn();
      check("setup and nested turn defer request", attempts == 0 && host.reloadRequested); }
    turn(host);
    check("next outer poll attempts once and preserves running App on build failure", attempts == 1 && !host.reloadRequested && a->getApp());
    host.reload();
    check("request outside callback only sets flag", attempts == 1 && host.reloadRequested);
    turn(host);
    check("next poll clears flag and attempts once", attempts == 2 && !host.reloadRequested);
    a->destroyApp(); internal::applyPendingAppForTests(*a);
    check("ordinary ending request defers", host.reloadRequested && attempts == 2);
    turn(host); check("ending completed before next attempt", attempts == 3);
    requesting = nullptr; error.disconnect(); internal::shutdownRuntime();

    internal::beginRuntimeRun(); a = window(811);
    Host actual; actual.projectDir = dir.string(); actual.buildDir = dir.string(); actual.srcDir = (dir / "src").string();
    actual.guestLibPath = findGuestLibrary(); actual.targetWindow = a; actual.initialized = true;
    actual.watcher.init(actual.srcDir); actual.lastCheck = hot_reload::Clock::now();
    check("single guest loads and attaches launch Window", actual.guest.load(actual.guestLibPath) && actual.guest.create(a));
    auto old = a->getApp();
    actual.coreLibPath = (dir / "newer-core").string();
    actual.reload(); turn(actual);
    check("build failure preserves actual running guest", actual.reloadCount == 0 && a->getApp() == old && mcp::hasTool("guest_probe"));
    actual.coreLibPath.clear();
    std::string order;
    auto runtimeUnload = runtime().events().hotReloadUnload.listen([&] { order += 'R'; });
    auto unload = a->events().hotReloadUnload.listen([&] { order += 'A'; });
    requesting = &actual;
    int unloadRequests = 0;
    auto again = a->events().hotReloadUnload.listen([&] { if (++unloadRequests == 1) actual.reload(); });
    auto setExit = reinterpret_cast<void (*)(App*, void (*)())>(symbol(actual.guest, "tcC4ExitRequest"));
    setExit(a->getApp().get(), +[] { requesting->reload(); });
    actual.reload();
    check("reload acceptance leaves running guest until poll", actual.reloadCount == 0 && a->getApp() == old);
    turn(actual);
    check("unload then attach replaces single target", actual.reloadCount == 1 && a->getApp() && a->getApp() != old && order == "RA");
    check("unload and exit requests survive first attempt", actual.reloadRequested && unloadRequests == 1);
    turn(actual);
    check("unload request causes second attempt on NEXT poll", actual.reloadCount == 2 && unloadRequests == 2 && !actual.reloadRequested);
    auto constructorRequest = +[] { requesting->reload(); };
    const auto address = std::to_string(reinterpret_cast<uintptr_t>(constructorRequest));
#ifdef _WIN32
    _putenv_s("TC_C4_CONSTRUCTOR_REQUEST", address.c_str());
#else
    setenv("TC_C4_CONSTRUCTOR_REQUEST", address.c_str(), 1);
#endif
    const auto beforeConstructor = actual.reloadCount;
    actual.reload(); turn(actual);
    check("constructor request during attempt defers to next poll", actual.reloadCount == beforeConstructor + 1 && actual.reloadRequested);
#ifdef _WIN32
    _putenv_s("TC_C4_CONSTRUCTOR_REQUEST", "");
#else
    unsetenv("TC_C4_CONSTRUCTOR_REQUEST");
#endif
    turn(actual);
    check("constructor request runs once on following poll", actual.reloadCount == beforeConstructor + 2 && !actual.reloadRequested);
    {
        // Release the Windows file handle before another build or remove_all.
        std::ifstream argv(dir / "build-command-argv"); std::string args((std::istreambuf_iterator<char>(argv)), {});
        check("build targets guest only", args.find("--target\nguest\n") != std::string::npos && args.find("host") == std::string::npos);
    }
    // A retained export remains callable after logical retirement.
    auto pinned = actual.guest.createApp;
    GuestLibrary pinnedModule; pinnedModule.handle = actual.guest.handle;
    int warnings = 0;
    const auto expected = std::string("!! Remove \"TC_HOT_RELOAD(tcApp)\" from ") +
        (fs::path(__FILE__).parent_path() / "tcApp.cpp").string() +
        " !! Hot reload does not support multiple windows.";
    auto warning = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.level == LogLevel::Warning && e.message == expected) ++warnings;
    });
    auto b = window(812);
    check("Q13(d) second window warns with real type and file", warnings == 1);
    auto c = window(813);
    check("Q13(d) third window warns again", warnings == 2);
    c->close(); internal::WindowRequestAccess::teardown(*c);
    c = window(814);
    check("Q13(d) close and recreate warns again", warnings == 3);
    c->close(); internal::WindowRequestAccess::teardown(*c);
    warning.disconnect(); again.disconnect(); unload.disconnect(); runtimeUnload.disconnect();
    // Request, close the launch target, then poll: no guest may be attached to it.
    auto before = actual.reloadCount;
    auto beforeCloseApp = a->getApp();
    actual.reload();
    const bool deferredUntilClose = actual.reloadCount == before && a->getApp() == beforeCloseApp;
    a->close(); internal::WindowRequestAccess::teardown(*a);
    turn(actual);
    check("closed launch Window skips attach at next poll", deferredUntilClose && actual.reloadCount == before + 1 && !a->isOpen() && !a->getApp() && actual.guest.app.expired() && !mcp::hasTool("guest_probe"));
    check("retired module remains mapped", pinned != nullptr && symbol(pinnedModule, "tcHotReloadCreateApp") == reinterpret_cast<void*>(pinned));
    check("closed target refuses direct candidate", !actual.guest.create(a) && !a->getApp());
    constructorCloseTarget = b.get();
    GuestLibrary candidate;
    candidate.createApp = +[]() -> App* { return internal::constructAppStorage<CloseInConstructor>(); };
    candidate.destroyApp = internal::destroyAppStorage<CloseInConstructor>;
    check("constructor close skips candidate attach", !candidate.create(b) && !b->getApp());
    candidate.unload();
    actual.guest.unload(); requesting = nullptr; internal::shutdownRuntime();
    internal::beginRuntimeRun();
    a = window(821); a->setApp<App>(); internal::applyPendingAppForTests(*a);
    int plainWarnings = 0;
    auto plainWarning = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.level == LogLevel::Warning) ++plainWarnings;
    });
    b = window(822); b->setApp<App>(); internal::applyPendingAppForTests(*b);
    check("plain two-window App emits no Warning", plainWarnings == 0);
    plainWarning.disconnect(); internal::shutdownRuntime();
    fs::remove_all(dir);
    std::printf("hotReloadLifecycle: C4 substrate: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
}
