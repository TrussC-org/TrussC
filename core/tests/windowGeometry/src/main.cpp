#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <cstdio>
#include <chrono>
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <climits>
#endif

namespace {
using namespace tc;
using tc::Window; // Avoid the global Xlib Window typedef on Linux.
int failures = 0;
void check(const char* label, bool ok) {
    std::printf("windowGeometry: %s: %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
struct Call { Window* window; internal::WindowControl op; int x, y; };
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
// The window can vanish between diagnostic queries; record X errors instead of
// letting Xlib's default handler exit the test.
bool x11QueryFailed = false;
int recordX11Error(Display*, XErrorEvent*) { x11QueryFailed = true; return 0; }
#endif
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
    Secondary() { native_ = &standIn; internal::WindowRequestAccess::bindForTests(*this, 202); }
    ~Secondary() { native_ = nullptr; internal::WindowRequestAccess::unbind(*this); internal::WindowRequestAccess::endApp(*this); }
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
// Store the last live poll: cleanup may run after native handles are detached.
struct GeometryObservation {
    std::chrono::steady_clock::time_point started;
    double elapsed = 0;
    int polls = 0, expectedWidth = 320, expectedHeight = 240;
    IVec2 position{}, handlePosition{};
    int width = 0, height = 0, handleWidth = 0, handleHeight = 0;
    Vec2 size{};
    bool hasHandle = false, pixelPerfect = false, printed = false;
    float dpi = 0;
    int framebufferWidth = 0, framebufferHeight = 0;
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
    bool clientValid = false, extentsValid = false;
    int clientX = 0, clientY = 0, clientWidth = 0, clientHeight = 0;
    unsigned long extents[4]{};
#endif
    void capture(Window* window, int phase, int pollCount) {
        elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        polls = pollCount;
        expectedWidth = phase ? 480 : 320;
        expectedHeight = phase ? 300 : 240;
        position = getWindowPosition();
        width = getWindowWidth(); height = getWindowHeight();
        size = getWindowSize();
        hasHandle = window != nullptr;
        if (window) {
            handlePosition = window->getPosition();
            handleWidth = window->getWidth(); handleHeight = window->getHeight();
        }
        pixelPerfect = internal::pixelPerfectMode();
        dpi = sapp_dpi_scale();
        framebufferWidth = sapp_width(); framebufferHeight = sapp_height();
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
        clientValid = extentsValid = false;
        auto* display = (Display*)sapp_x11_get_display();
        const auto native = (::Window)(uintptr_t)sapp_x11_get_window();
        if (!display || !native) return; // Also supports the headless KMS lane.
        XSync(display, False);
        const auto previousHandler = XSetErrorHandler(recordX11Error);
        x11QueryFailed = false;
        XWindowAttributes attributes{};
        ::Window child = 0;
        clientValid = XGetWindowAttributes(display, native, &attributes) &&
            XTranslateCoordinates(display, native, DefaultRootWindow(display),
                                  0, 0, &clientX, &clientY, &child);
        if (clientValid) { clientWidth = attributes.width; clientHeight = attributes.height; }
        const Atom property = XInternAtom(display, "_NET_FRAME_EXTENTS", True);
        Atom type = None;
        int format = 0;
        unsigned long count = 0, remaining = 0;
        unsigned char* data = nullptr;
        if (property != None &&
            XGetWindowProperty(display, native, property, 0, 4, False, XA_CARDINAL,
                               &type, &format, &count, &remaining, &data) == Success &&
            type == XA_CARDINAL && format == 32 && count == 4 && remaining == 0 && data) {
            const auto* values = reinterpret_cast<const unsigned long*>(data);
            extentsValid = true;
            for (int i = 0; i < 4; ++i) {
                extents[i] = values[i];
                // Bound (frame arithmetic stays in int, as the geometry helper's cap): reject decorations above SHRT_MAX.
                if (values[i] > SHRT_MAX) extentsValid = false;
            }
        }
        if (data) XFree(data);
        XSync(display, False);
        XSetErrorHandler(previousHandler);
        if (x11QueryFailed) clientValid = extentsValid = false;
#endif
    }
    void print() {
        if (printed) return;
        printed = true;
        std::printf("windowGeometry: observed geometry (elapsed=%.3fs, polls=%d)\n", elapsed, polls);
        std::printf("  expected: getWindowPosition=(80,70) size=(%d,%d) via getWindowWidth/Height (%s); requested size is logical\n",
                    expectedWidth, expectedHeight, pixelPerfect ? "pixels" : "logical");
        std::printf("  public: getWindowPosition=(%d,%d) getWindowWidth/Height=(%d,%d) getWindowSize=(%.0f,%.0f) pixelPerfect=%s\n",
                    position.x, position.y, width, height, double(size.x), double(size.y), pixelPerfect ? "true" : "false");
        if (hasHandle)
            std::printf("  Window: getPosition=(%d,%d) getWidth/Height=(%d,%d) logical\n",
                        handlePosition.x, handlePosition.y, handleWidth, handleHeight);
        else std::printf("  Window: unavailable\n");
        std::printf("  sapp: dpi_scale=%.3f framebuffer=(%d,%d) pixels\n", double(dpi), framebufferWidth, framebufferHeight);
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
        if (clientValid) {
            std::printf("  X11: client root position=(%d,%d) size=(%d,%d) pixels\n", clientX, clientY, clientWidth, clientHeight);
            if (extentsValid)
                std::printf("  X11: frame root position=(%d,%d) size=(%d,%d) pixels; extents L/R/T/B=(%lu,%lu,%lu,%lu)\n",
                            clientX - int(extents[0]), clientY - int(extents[2]),
                            clientWidth + int(extents[0] + extents[1]), clientHeight + int(extents[2] + extents[3]),
                            extents[0], extents[1], extents[2], extents[3]);
            else std::printf("  X11: frame unavailable (_NET_FRAME_EXTENTS absent, invalid or out of bounds)\n");
        } else std::printf("  X11: native geometry unavailable\n");
#endif
        std::fflush(stdout);
    }
};
struct GeometryDisplayApp : App {
    int phase = 0, frames = 0;
    GeometryObservation observation;
    void setup() override {
        observation.started = std::chrono::steady_clock::now();
        setWindowSizeLogical(320, 240);
        setWindowPosition(80, 70);
        observation.capture(getWindow(), phase, 0);
    }
    void update() override {
        const auto position = getWindowPosition();
        const bool observable = getWindowWidth() == (phase ? 480 : 320) &&
            getWindowHeight() == (phase ? 300 : 240) && position == IVec2(80, 70);
        observation.capture(getWindow(), phase, frames + 1);
        if (observable) {
            if (phase) observation.print();
            check("X11 main geometry round trip", true);
            if (phase++) { displayCompleted = true; requestExitApp(); }
            else setWindowSizeLogical(480, 300);
        }
        // Bound (test hang guard): stop a broken backend; success depends on
        // observed geometry, never on completing within a measured duration.
        if (++frames == 600) { observation.print(); check("main geometry became observable", false); requestExitApp(); }
    }
    void cleanup() override {
        // Covers early close as well, before the final completion assertion.
        observation.print();
        App::cleanup();
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
    auto mainWindow = std::make_unique<tc::Window>();
    internal::WindowRequestAccess::bindForTests(*mainWindow, 101);
    Secondary secondary;
    auto app = internal::makeConstructedApp<Probe>();
    internal::WindowRequestAccess::attachCandidate(secondary, app);
    internal::applyPendingAppForTests(secondary);
    // The stand-in has no native fullscreen state (especially on AppKit).
    internal::windowFullscreenHookForTests() = [](const tc::Window&) { return internal::WindowFullscreenPhase::Windowed; };
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
    { internal::AppOwnerScope owner(internal::appOwner(*app));
    check("global getter reports actual target position", getWindowPosition() == IVec2(-1730, 41));
    expect(secondary, internal::WindowControl::PositionQuery, "getter follows secondary context"); }
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
    const auto beforeAmbiguous = calls.size();
    setWindowPosition(50, 60);
    check("unowned ambiguous position changes no Window", calls.size() == beforeAmbiguous);
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
    internal::windowFullscreenHookForTests() = nullptr;
    internal::currentWindowCtx() = previous;
    return failures ? 1 : 0;
}
