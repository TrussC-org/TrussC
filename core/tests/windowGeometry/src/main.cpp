#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <cstdio>

namespace {
using namespace tc;
int failures = 0;
void check(const char* label, bool ok) {
    std::printf("windowGeometry: %s: %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
struct Call { Window* window; internal::WindowControl op; int x, y; };
std::vector<Call> calls;
bool throws = false;
internal::WindowContext* callerContext = nullptr;
bool record(Window& window, internal::WindowControl op, int x, int y, const std::string*, IVec2& result) {
    check("adapter preserves caller context", internal::currentWindowCtx() == callerContext);
    calls.push_back({&window, op, x, y});
    result = {-1730, 41}; // a WM's actual (possibly clamped) frame position
    if (throws) throw std::runtime_error("control fault");
    return true;
}
struct Secondary : Window {
    int standIn = 0;
    Secondary() { native_ = &standIn; }
    ~Secondary() { native_ = nullptr; internal::WindowRequestAccess::endApp(*this); }
};
struct Probe : App {
    void setup() override { setWindowPosition(-1920, 0); setFullscreen(true); setWindowTitle("Secondary"); }
    void update() override { setWindowSize(600, 400); setWindowSizeLogical(620, 420); }
    void keyPressed(const KeyEventArgs&) override { setWindowPosition(-1200, 30); }
};
void expect(Window& target, internal::WindowControl op, const char* label) {
    check(label, !calls.empty() && calls.back().window == &target && calls.back().op == op);
}
bool displayCompleted = false;
struct GeometryDisplayApp : App {
    int phase = 0, frames = 0;
    void setup() override {
        setWindowSizeLogical(320, 240);
        setWindowPosition(80, 70);
    }
    void update() override {
        const auto position = getWindowPosition();
        if (getWindowWidth() == (phase ? 480 : 320) &&
            getWindowHeight() == (phase ? 300 : 240) && position == IVec2(80, 70)) {
            check("X11 main geometry round trip", true);
            if (phase++) { displayCompleted = true; requestExitApp(); }
            else setWindowSizeLogical(480, 300);
        }
        // Bound (test hang guard): stop a broken backend; success depends on
        // observed geometry, never on completing within a measured duration.
        if (++frames == 600) { check("main geometry became observable", false); requestExitApp(); }
    }
};

}
TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc > 1 && std::string_view(argv[1]) == "--display") {
        WindowSettings settings;
        settings.width = 128; settings.height = 96;
        return tcCoreTest::runDisplayTest<GeometryDisplayApp>(settings, failures, [] {
            check("main geometry display completed", displayCompleted);
        });
    }
    auto* previous = internal::currentWindowCtx();
    auto mainWindow = internal::WindowRequestAccess::makeMain();
    internal::WindowRequestAccess::openMain(*mainWindow);
    Secondary secondary;
    auto app = std::make_shared<Probe>();
    secondary.setApp(app);
    internal::applyPendingAppForTests(secondary);
    internal::windowControlHookForTests() = record;
    internal::currentWindowCtx() = callerContext = &secondary.context();
    secondary.tickTree(); // real App setup/update binding without GPU drawing
    check("setup moves current secondary before fullscreen", calls.size() >= 3 &&
        calls[0].window == &secondary && calls[0].op == internal::WindowControl::Position && calls[0].x == -1920 &&
        calls[1].window == &secondary && calls[1].op == internal::WindowControl::Fullscreen);
    expect(secondary, internal::WindowControl::Size, "update logical size targets secondary");
    auto fromSecondaryEvent = secondary.events().keyPressed.listen([&](const KeyEventArgs&) {
        mainWindow->setPosition(10, 20);
        expect(*mainWindow, internal::WindowControl::Position, "main handle inside secondary event");
        check("event restores secondary context", internal::currentWindowCtx() == &secondary.context());
    });
    KeyEventArgs key{};
    secondary.events().keyPressed.notify(key);
    expect(secondary, internal::WindowControl::Position, "event position targets secondary");
    check("global getter reports actual target position", getWindowPosition() == IVec2(-1730, 41));
    expect(secondary, internal::WindowControl::PositionQuery, "getter follows secondary context");
    mainWindow->setPosition(10, 20);
    expect(*mainWindow, internal::WindowControl::Position, "main handle from secondary callback context");
    check("explicit main restores secondary context", internal::currentWindowCtx() == &secondary.context());
    mainWindow->setTitle("Main");
    expect(*mainWindow, internal::WindowControl::Title, "title uses same adapter");
    internal::currentWindowCtx() = callerContext = &mainWindow->context();
    auto fromMainUpdate = mainWindow->events().update.listen([&] { app->setSize(640, 480); });
    mainWindow->events().update.notify();
    expect(secondary, internal::WindowControl::Size, "App::setSize targets own window from main");
    check("App::setSize restores main", internal::currentWindowCtx() == &mainWindow->context());
    setWindowPosition(50, 60);
    expect(*mainWindow, internal::WindowControl::Position, "main global position");
    secondary.setPosition(-100, -200);
    expect(secondary, internal::WindowControl::Position, "explicit secondary from main");
    throws = true;
    try { secondary.setSize(12, 13); check("fault injected", false); } catch (const std::runtime_error&) {}
    check("throw restores context", internal::currentWindowCtx() == &mainWindow->context());
    throws = false;
    internal::currentWindowCtx() = callerContext = nullptr;
    secondary.setFullscreen(false);
    check("explicit handle restores null context", internal::currentWindowCtx() == nullptr);
    const auto before = calls.size();
    app->setSize(std::numeric_limits<float>::quiet_NaN(), 10);
    app->setSize(std::numeric_limits<float>::infinity(), 10);
    app->setSize(-1, 10);
    check("invalid App sizes never reach adapter", calls.size() == before);
    internal::windowControlHookForTests() = nullptr;
    internal::currentWindowCtx() = previous;
    return failures ? 1 : 0;
}
