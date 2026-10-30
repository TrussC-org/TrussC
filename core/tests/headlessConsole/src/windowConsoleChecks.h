// Real native host: console before draw, after first close, and at Window0.
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
    if (window->isOpen() || (waitForVeto && vetoes == 0)) {
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
            check(e.raw == "after-first-close" && !first->isOpen(), "native console after first closure");
            getWindow()->close();
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
int run() {
    if (!internal::supportsSecondaryWindows()) { std::puts("SKIP: windowed console needs desktop secondary windows"); return 0; }
    auto quit = runtime().events().exitRequested.listen([](ExitRequestEventArgs& e) {
        if (!readyToQuit) { e.cancel = true; ++vetoes; return; }
        check(phase == 3 && firstConsole == 1 && secondConsole == 1 && globalConsole == 3,
              "native Runtime console has no duplicated App fanout");
        check(exits == 2 && vetoes == 1 && firstDraws == 0,
              "native Window0 services after one process veto and exact-once ending");
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
            internal::mainThreadQueue().send([] { sendAfterClose(second, "zero-windows", true); });
        } else {
            ++phase;
            check(e.raw == "zero-windows" && !internal::resolveWindow(), "native console and queues progress with zero Windows");
            // The close request is vetoed at the end of the previous host turn.
            check(vetoes == 1, "native last-window process quit was vetoed");
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
