// Real native host: console before draw, after first close, and after a last-close veto.
namespace c2WindowConsole {
#ifndef __EMSCRIPTEN__
std::shared_ptr<Window> first, second;
int firstDraws = 0, firstConsole = 0, secondConsole = 0, globalConsole = 0;
int exits = 0, vetoes = 0, phase = 0;
bool readyToQuit = false;
void send(const char* text) {
    console::internal::getChannel().send(console::internal::parseLine(text));
}
void sendAfterClose(std::shared_ptr<Window> window, const char* line, bool waitForVeto = false) {
    if (waitForVeto ? vetoes == 0 : window->isOpen()) {
        internal::mainThreadQueue().send([window = std::move(window), line, waitForVeto] { sendAfterClose(window, line, waitForVeto); });
        return;
    }
    send(line);
}
struct Later : App {
    EventListener input, closeQuery;
    Later() {
        setIndependentFps(VSYNC, EVENT_DRIVEN);
        internal::findWindowApiContext()->loop.redrawCount = 0;
        input = runtime().events().console.listen([this](ConsoleEventArgs& e) {
            check(internal::currentAppOwner() == internal::appOwner(*this), "native sibling console uses subscriber owner");
            ++secondConsole;
            if (e.raw == "after-close-veto") {
                check(second->isOpen(), "native cancelled code close keeps last Window");
                return;
            }
            check(e.raw == "after-first-close" && !first->isOpen(), "native console after first closure");
            getWindow()->close(); // Last-window process veto leaves this Window open.
        });
        closeQuery = events().closeRequested.listen([](CloseRequestEventArgs&) { check(false, "native code close bypasses query on sibling"); });
    }
    void exit() override { ++exits; }
};
struct Initial : App {
    EventListener input, closeQuery;
    Initial() {
        setIndependentFps(VSYNC, EVENT_DRIVEN);
        // Configure before first admission; setup itself runs inside an admitted
        // update, after a synchronized tick may already have reserved drawing.
        internal::findWindowApiContext()->loop.redrawCount = 0;
        input = runtime().events().console.listen([this](ConsoleEventArgs& e) {
            check(internal::currentAppOwner() == internal::appOwner(*this), "native initial console uses subscriber owner");
            ++firstConsole;
            check(e.raw == "before-draw" && firstDraws == 0, "native console before first draw");
            first = internal::WindowRequestAccess::retainHostOwner(*getWindow());
            second = createWindow(WindowSettings{}.setSize(100, 100));
            check(bool(second), "native console creates sibling");
            if (second) { second->setPosition(450, 80); second->setApp<Later>(); }
            getWindow()->close();
        });
        closeQuery = events().closeRequested.listen([](CloseRequestEventArgs&) { check(false, "native code close bypasses initial query"); });
        console::internal::isRunning().store(true); // deterministic input; no reader
        send("before-draw");
    }
    void draw() override { ++firstDraws; }
    void exit() override { ++exits; exitApp(99); }
};
bool setupVeto = false;
int setupQueries = 0, setupInputs = 0, setupUpdates = 0;
struct SetupQuit : App {
    EventListener input;
    void setup() override {
        input = runtime().events().console.listen([](ConsoleEventArgs&) {
            ++setupInputs;
            exitApp();
        });
        console::internal::isRunning().store(true);
        send("queued-before-setup-returns");
        requestExitApp();
    }
    void update() override { ++setupUpdates; }
    void exit() override {
        check(setupQueries == (setupVeto ? 2 : 1), "windowed setup quit retains exitRequested veto");
        check(setupInputs == int(setupVeto), "accepted setup quit precedes queued console input");
        if (!setupVeto) check(setupUpdates == 0, "accepted setup quit precedes first update");
        if (failures) internal::appExitCode() = 1;
    }
};
int setupQuit(bool veto) {
    setupVeto = veto;
    auto query = runtime().events().exitRequested.listen([](ExitRequestEventArgs& e) {
        ++setupQueries;
        e.cancel = setupVeto && setupQueries == 1;
    });
    // runapp-verdict: returning-setup-quit -- #239 requires post-run counters on returning hosts; in-App exit checks remain for macOS.
    const int result = runApp<SetupQuit>(WindowSettings{}.setSize(160, 120));
    check(setupQueries == (veto ? 2 : 1), "post-run windowed setup quit retains veto");
    check(setupInputs == int(veto), "post-run setup quit has no late console delivery");
    if (!veto) check(setupUpdates == 0, "post-run setup quit has no late update");
    return failures ? 1 : result;
}

// A descendant's setup runs inside the parent's tree traversal. Ending there
// must wait until that traversal unwinds, before the next service callbacks.
// No verdict on macOS: these checks run after runApp, which never returns
// there. Exiting without a failure on macOS is not a pass; coverage is Linux.
bool childVeto = false, treeCleaned = false;
int childQueries = 0, treeExits = 0, parentCleanups = 0, childCleanups = 0;
int lateCallbacks = 0, childUpdates = 0, childMods = 0, childTimers = 0;
void treeCallback() { if (treeCleaned) ++lateCallbacks; }
struct ChildMod : Mod {
    void earlyUpdate() override { treeCallback(); ++childMods; }
    void update() override { treeCallback(); }
};
struct ChildQuit : App {
    void setup() override {
        treeCallback();
        addMod<ChildMod>();
        callAfter(0, [] { treeCallback(); ++childTimers; });
        requestExitApp();
    }
    void update() override {
        treeCallback(); ++childUpdates;
        if (childVeto && childQueries == 1) requestExitApp();
    }
    void cleanup() override { ++childCleanups; treeCleaned = true; }
};
struct ParentOfQuit : App {
    EventListener input;
    void setup() override {
        addChild(internal::makeConstructedApp<ChildQuit>());
        input = runtime().events().console.listen([](ConsoleEventArgs&) { treeCallback(); });
        console::internal::isRunning().store(true);
    }
    void update() override { treeCallback(); }
    void draw() override { treeCallback(); }
    void exit() override { ++treeExits; }
    void cleanup() override {
        ++parentCleanups; treeCleaned = true;
        send("queued-at-child-cleanup");
    }
};
int childSetupQuit(bool veto) {
    childVeto = veto;
    auto query = runtime().events().exitRequested.listen([](ExitRequestEventArgs& e) {
        ++childQueries;
        e.cancel = childVeto && childQueries == 1;
    });
    // runapp-verdict: returning-setup-quit -- #239 requires the child traversal's final counters after a returning runner has unwound.
    const int result = runApp<ParentOfQuit>(WindowSettings{}.setSize(160, 120));
    console::internal::processQueue();
    check(lateCallbacks == 0, "child setup quit has no parent/child/Mod/timer/console callback after cleanup");
    check(treeExits == 1 && parentCleanups == 1 && childCleanups == 1, "child setup quit ends the tree once");
    check(childQueries == (veto ? 2 : 1), "child setup quit retains exitRequested veto");
    check(childUpdates > 0 && childMods > 0 && childTimers == 1, "descendant setup unwinds before deferred ending");
    return failures ? 1 : result;
}
int run() {
    if (!internal::supportsSecondaryWindows()) { std::puts("SKIP: windowed console needs desktop secondary windows"); return 0; }
    auto quit = runtime().events().exitRequested.listen([](ExitRequestEventArgs& e) {
        if (!readyToQuit) { e.cancel = true; ++vetoes; return; }
        check(phase == 3 && firstConsole == 1 && secondConsole == 2 && globalConsole == 3,
              "native Runtime console has no duplicated App fanout");
        check(exits == 1 && vetoes == 1 && firstDraws == 0,
              "native console after one code-close veto and first App ending");
        if (failures) internal::appExitCode() = 1;
    });
    auto input = runtime().events().console.listen([](ConsoleEventArgs& e) {
        check(!internal::currentAppOwner(), "native unowned console stays process scoped");
        ++globalConsole;
        if (e.raw == "before-draw") {
            ++phase;
            internal::mainThreadQueue().send([] { sendAfterClose(first, "after-first-close"); });
        } else if (e.raw == "after-first-close") {
            ++phase;
            internal::mainThreadQueue().send([] { sendAfterClose(second, "after-close-veto", true); });
        } else {
            ++phase;
            check(e.raw == "after-close-veto" && second->isOpen(), "native console and queues progress after last-close veto");
            check(vetoes == 1, "native code-close process quit was vetoed once");
            readyToQuit = true;
            exitApp();
        }
    }, 200);
    // The Runtime quit listener publishes the verdict before native teardown,
    // including hosts whose runApp never returns.
    return runApp<Initial>(WindowSettings{}.setSize(160, 120));
}
#else
int run() { std::puts("SKIP: Web has no stdin console"); return 0; }
#endif
}
