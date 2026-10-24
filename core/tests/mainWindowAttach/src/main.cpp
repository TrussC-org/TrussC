// Main/secondary shared admission, deferred boundaries and runner ownership.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
using namespace tc;
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__) || (defined(__APPLE__) && !TARGET_OS_OSX) || (defined(__linux__) && !defined(SOKOL_GLCORE))
static_assert(!internal::supportsSecondaryWindows(), "single-window platforms must reject secondary creation");
#endif
namespace {
int failures = 0;
void check(const char* label, bool ok) {
    std::printf("mainWindowAttach: %s: %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
int processExits = 0, processCleanups = 0;
void processVerdict() {
    const bool ok = processExits == 1 && processCleanups == 1;
    std::printf("mainWindowAttach: process teardown: %s\n", ok ? "PASS" : "FAIL");
    std::fflush(stdout);
    if (!ok) std::_Exit(1);
}
struct ProcessExitApp : App {
    void update() override {
        getWindow()->setApp(std::make_shared<App>());
        std::exit(0); // owner must outlive framework state even with a pending swap
    }
    void exit() override { ++processExits; }
    void cleanup() override { ++processCleanups; }
};
struct Probe : App {
    int exits = 0, cleanups = 0;
    void exit() override { ++exits; }
    void cleanup() override { ++cleanups; }
};
uint64_t standIn = 0;
struct OtherWindow : Window {
    OtherWindow() { native_ = &standIn; }
    ~OtherWindow() { finish(); }
    void finish() { native_ = nullptr; internal::WindowRequestAccess::endApp(*this); }
};
struct ThrowConstructor : App { ThrowConstructor() { throw std::runtime_error("constructor test"); } };
bool displayFinished = false;
void displayVerdict() {
    if (!displayFinished || failures) {
        std::printf("mainWindowAttach: display verdict: FAIL\n");
        std::fflush(stdout);
        std::_Exit(1);
    }
}
struct DisplayNext : App {
    void setup() override {
        check("replacement main handle", getWindow() && internal::currentWindow() == getWindow());
        check("replacement main size seeded before setup", getWidth() == getWindowWidth() && getHeight() == getWindowHeight());
        getWindow()->close();
    }
    void cleanup() override {
        auto* main = internal::mainWindow();
        check("main close reaches runner shutdown", main && !main->isOpen() && !main->getApp());
        displayFinished = true;
        if (failures) internal::appExitCode() = 1;
    }
};
struct DisplayFirst : App {
    int updates = 0;
    EventListener cancelClose;
    void setup() override {
        cancelClose = events().exitRequested.listen([](ExitRequestEventArgs& args) {
            args.cancel = true;
        });
    }
    void update() override {
        auto* window = getWindow();
        check("runner main handle", window && internal::currentWindow() == window);
        if (!window) { exitApp(1); return; }
        if (++updates == 1) { window->close(); return; }
        check("cancelled display close keeps main open", window->isOpen());
        cancelClose.disconnect();
        window->setApp(std::make_shared<DisplayNext>());
        check("self swap remains deferred", window->getApp().get() == this && getWindow() == window);
    }
};
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
    getMainThreadId();
    if (argc > 1 && std::string(argv[1]) == "--constructor-failure") {
        WindowSettings settings; settings.width = settings.height = 128;
        // runapp-verdict: atexit-verdict -- failure propagates through appExitCode.
        return runApp<ThrowConstructor>(settings);
    }
    if (argc > 1 && std::string(argv[1]) == "--exit-in-callback") {
        std::atexit(processVerdict);
        WindowSettings settings; settings.width = settings.height = 128;
        return runApp<ProcessExitApp>(settings);
    }
    if (argc > 1 && std::string(argv[1]) == "--process-exit") {
        std::atexit(processVerdict);
        internal::createMainWindow(WindowSettings{});
        internal::WindowRequestAccess::openMain(*internal::mainWindow());
        internal::attachMainApp(std::make_shared<ProcessExitApp>());
        internal::mainWindow()->setApp(std::make_shared<App>());
        std::exit(0);
    }
    if (argc > 1 && std::string(argv[1]) == "--display") {
        WindowSettings settings; settings.width = settings.height = 128;
        // The initial App is deliberately swapped out; verdict belongs to
        // the replacement's cleanup, with an atexit guard against early exit.
        std::atexit(displayVerdict);
        return runApp<DisplayFirst>(settings);
    }
    internal::createMainWindow(WindowSettings{});
    auto* window = internal::mainWindow();
    window->setApp(std::make_shared<Probe>());
    check("unopened main rejects attachment", !window->appRequested_ && !window->getApp());
    internal::WindowRequestAccess::openMain(*window); // no sapp in headless contract mode
    auto first = std::make_shared<Probe>();
    check("runner uses shared attachment", internal::attachMainApp(first));
    check("main currentWindow equals App handle", internal::currentWindow() == window && first->getWindow() == window);
    check("main context and events retained", &window->context() == &internal::mainWindowContext() && &window->events() == &events());
    const auto id = internal::WindowRequestAccess::creationId(*window);
    setWindowTitle("Main title");
    check("main title stored by global routing", window->getTitle() == (mcp::internal::isDevBuild() ? "Main title [MCP]" : "Main title") && &window->getTitle() == &mcp::internal::mainWindowTitleStorage());
    OtherWindow other;
    check("one non-reused identity sequence", id > 0 && internal::WindowRequestAccess::creationId(other) > id);
    auto next = std::make_shared<Probe>();
    {
        internal::MainWindowDispatchScope dispatch;
        check("main dispatch is active", internal::mainWindowDispatchActive());
        window->setApp(next);
        check("main setApp deferred", window->getApp() == first && first->getWindow() == window && !next->getWindow());
    }
    check("main dispatch ended", !internal::mainWindowDispatchActive());
    check("main swap at boundary", window->getApp() == next && next->getWindow() == window && first->exits == 1 && first->cleanups == 1);
    window->setApp(first);
    check("cleaned-up admission rejected at request", !window->appRequested_);
    other.setApp(next);
    check("main attachment blocks secondary admission", !other.appRequested_);
    auto foreignOwner = std::shared_ptr<App>(next.get(), [](App*) {});
    window->setApp(foreignOwner);
    check("foreign shared owner refused", !window->appRequested_);
    auto competing = std::make_shared<Probe>();
    window->setApp(competing); other.setApp(competing);
    internal::applyPendingAppForTests(other);
    internal::applyPendingAppForTests(*window);
    check("boundary repeats double-attach check", window->getApp() == next && other.getApp() == competing);
    other.finish();
    // A candidate can be ended between request and boundary.
    auto stale = std::make_shared<Probe>();
    window->setApp(stale);
    OtherWindow disposer; disposer.setApp(stale); internal::applyPendingAppForTests(disposer); disposer.finish();
    internal::applyPendingAppForTests(*window);
    check("boundary repeats lifecycle check", window->getApp() == next);
    headless::active = true; headless::running = true;
    auto cancelClose = events().exitRequested.listen([](ExitRequestEventArgs& args) {
        args.cancel = true;
    });
    // Drive the actual native quit handler without a display. Include a close
    // made inside another event callback, plus OS and direct soft-quit requests.
    for (int source = 0; source < 4; ++source) {
        headless::quitRequested = false;
        if (source == 0) window->close();
        if (source == 1) internal::setExitReason("window-close");
        if (source == 2) requestExitApp();
        if (source == 3) {
            auto callback = events().rawEvent.listen([&](const sapp_event&) { window->close(); });
            sapp_event focus{}; focus.type = SAPP_EVENTTYPE_FOCUSED;
            internal::_event_cb(&focus);
        }
        if (source != 1)
            check("main close uses cancellable quit", headless::running && headless::quitRequested);
        sapp_event quit{}; quit.type = SAPP_EVENTTYPE_QUIT_REQUESTED;
        internal::_event_cb(&quit);
        check("cancelled quit restores open main", window->isOpen() && !internal::WindowRequestAccess::closeRequested(*window));
        check("cancelled quit clears exit reason", internal::exitReason().empty());
        auto replacement = std::make_shared<Probe>();
        {
            internal::MainWindowDispatchScope dispatch;
            window->setApp(replacement);
            check("swap accepted after cancelled quit", window->appRequested_ && window->getApp() == next);
        }
        check("swap applied after cancelled quit", window->getApp() == replacement);
        if (window->getApp() == replacement) next = replacement;
    }
    cancelClose.disconnect();
    headless::quitRequested = false;
    window->setApp(std::make_shared<Probe>());
    window->close();
    check("main close requests quit again", headless::running && headless::quitRequested && window->isOpen());
    // Forced exitApp remains uncancellable, including after a soft request.
    exitApp();
    check("direct exitApp still forces quit", !headless::running);
    headless::quitRequested = false;
    headless::active = false;
    internal::applyPendingAppForTests(*window);
    check("main close wins pending swap", window->getApp() == next);
    internal::destroyMainWindow();
    check("main shutdown releases App once", !next->getWindow() && next->exits == 1 && next->cleanups == 1 && internal::attachedApps().empty());
    internal::createMainWindow(WindowSettings{});
    check("new creation never reuses main ID", internal::WindowRequestAccess::creationId(*internal::mainWindow()) > id);
    internal::destroyMainWindow();
    return failures ? 1 : 0;
}
