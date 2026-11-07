#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <cstdio>
#include <string>
#include <chrono>
#include <sstream>
using namespace tc;
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
int visibilityMapCount(unsigned long);
int visibilitySwapCount(unsigned long);
int visibilityInvalidationCount(unsigned long);
bool visibilityMapped(const void*, unsigned long);
bool visibilityWaitMapped(const void*, unsigned long, bool);
bool visibilityBluePixel(const void*, unsigned long);
bool visibilityIconic(const void*, unsigned long);
void visibilityFrameSize(int, int);
bool visibilityFinalSizePresented(const void*, unsigned long, bool, int);
#define VISIBILITY_X11 1
#endif
namespace {
int failures = 0;
bool startVisible = false, fullscreen = false, finished = false;
int mainDraws = 0, secondaryDraws = 0, secondaryUpdates = 0;
bool hideInAppDraw = false, hideInDrawListener = false, hideInOnRender = false, hideInAfterFrame = false;
int drawHides = 0, hiddenAtUpdate = 0;
bool armEventDrivenHide = false, blueContent = false;
std::chrono::steady_clock::time_point eventDrivenHiddenSince;
uint64_t completedBeforeHide = 0;
#ifdef VISIBILITY_X11
int swapsBeforeHide = 0, invalidationsBeforeShow = 0;
#endif
std::shared_ptr<Window> secondary;
void check(const char* label, bool okay) {
    std::printf("windowVisible: %s: %s\n", label, okay ? "PASS" : "FAIL");
    std::fflush(stdout);
    if (!okay) ++failures;
}
#ifdef VISIBILITY_X11
unsigned long xwindow(Window& window) {
    return (unsigned long)(uintptr_t)sapp_window_x11_get_window(internal::WindowRequestAccess::binding(window).handle);
}
void checkUnmapped(Window& window) {
    check("X11 hidden window is unmapped", visibilityWaitMapped(sapp_x11_get_display(), xwindow(window), false));
}
#endif
bool fullscreenSettled() {
#if defined(_WIN32) || (defined(__APPLE__) && TARGET_OS_OSX) || defined(VISIBILITY_X11)
    return !fullscreen || sapp_tc_window_fullscreen_phase(internal::WindowRequestAccess::binding(*secondary).handle)
        == static_cast<uint32_t>(internal::WindowFullscreenPhase::Fullscreen);
#else
    return true;
#endif
}
void hideDuringDraw() {
    auto& ctx = internal::WindowRequestAccess::context(*secondary);
    completedBeforeHide = ctx.drawCount;
    secondary->setVisible(false);
    hiddenAtUpdate = secondaryUpdates;
    ++drawHides;
#ifdef VISIBILITY_X11
    swapsBeforeHide = visibilitySwapCount(xwindow(*secondary));
#endif
}
void checkDrawHide(bool onRender = false) {
    check(onRender ? "onRender hide skips frame completion" : "draw-time hide skips frame completion", !secondary->isVisible() &&
        internal::WindowRequestAccess::context(*secondary).drawCount == completedBeforeHide);
#ifdef VISIBILITY_X11
    check(onRender ? "onRender hide skips native present" : "draw-time hide skips native present",
        visibilitySwapCount(xwindow(*secondary)) == swapsBeforeHide);
    checkUnmapped(*secondary);
#endif
}
void checkHiddenControls(Window& window, bool move = true) {
#ifdef VISIBILITY_X11
    const int before = visibilityMapCount(xwindow(window));
#endif
    int notices = 0;
    auto log = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.level == LogLevel::Notice && e.message ==
            "[Window] bringToFront() ignored: window is hidden; call setVisible(true) first") ++notices;
    });
    window.bringToFront();
    window.bringToFront();
    check("hidden bringToFront logs Notice on every call", notices == 2);
    window.setVisible(false);
    window.setSize(120, 90);
    if (move) window.setPosition(20, 20);
    check("hidden controls preserve requested visibility", !window.isVisible());
#ifdef VISIBILITY_X11
    check("hidden bringToFront never maps", visibilityMapCount(xwindow(window)) == before);
#endif
}
struct Secondary : App {
    EventListener drawListener, renderListener, afterFrameListener;
    void setup() override {
        drawListener = events().draw.listen([] {
#ifdef VISIBILITY_X11
            if (!visibilityMapped(sapp_x11_get_display(), xwindow(*secondary))) return;
#endif
            if (hideInDrawListener) { hideInDrawListener = false; hideDuringDraw(); }
        });
        renderListener = events().onRender.listen([] {
#ifdef VISIBILITY_X11
            if (!visibilityMapped(sapp_x11_get_display(), xwindow(*secondary))) return;
#endif
            if (hideInOnRender) { hideInOnRender = false; hideDuringDraw(); }
        });
        afterFrameListener = events().afterFrame.listen([] {
#ifdef VISIBILITY_X11
            if (!visibilityMapped(sapp_x11_get_display(), xwindow(*secondary))) return;
#endif
            if (hideInAfterFrame) { hideInAfterFrame = false; hideDuringDraw(); }
        });
    }
    void update() override {
        ++secondaryUpdates;
        if (armEventDrivenHide) {
#ifdef VISIBILITY_X11
            if (!visibilityMapped(sapp_x11_get_display(), xwindow(*getWindow()))) return;
#endif
            armEventDrivenHide = false;
            getWindow()->setFps(EVENT_DRIVEN);
            blueContent = true;
            redraw(); // One request before hiding; showing must present it without another request.
            getWindow()->setVisible(false);
            eventDrivenHiddenSince = std::chrono::steady_clock::now();
            hiddenAtUpdate = secondaryUpdates;
        }
    }
    void draw() override {
        ++secondaryDraws;
#ifdef VISIBILITY_X11
        const auto& ctx = internal::WindowRequestAccess::context(*getWindow());
        visibilityFrameSize(ctx.fbWidth, ctx.fbHeight);
#endif
        if (blueContent) clear(0.1f, 0.2f, 1.0f); else clear(0.2f);
#ifdef VISIBILITY_X11
        // Exercise hiding a shown frame only after the WM has mapped it.
        if (!visibilityMapped(sapp_x11_get_display(), xwindow(*getWindow()))) return;
#endif
        if (hideInAppDraw) { hideInAppDraw = false; hideDuringDraw(); }
    }
};
struct Probe : App {
    int phase = 0, updates = 0, phaseUpdates = 0;
    const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point phaseTime;
#ifdef VISIBILITY_X11
    IVec2 preHideMainPosition{}, preHideSecondaryPosition{};
    const char* sequences[4] = {"H SH S H S", "H S/H S H S", "H R H S", "H R"};
    int sequence = 0, rapid = 0;
    bool hideNext = false, sequenceDone = false;
    std::istringstream operations;
    std::chrono::steady_clock::time_point operationTime, settledSince;
    void beginSequence() {
        std::printf("windowVisible: visSeq %s (R = 10 consecutive-update toggles)\n", sequences[sequence]);
        operations.clear(); operations.str(sequences[sequence]);
        operationTime = std::chrono::steady_clock::now() - std::chrono::seconds(1);
        sequenceDone = false;
    }
    void runSequence() {
        const auto now = std::chrono::steady_clock::now();
        if (hideNext) {
            secondary->setVisible(false); hideNext = false; operationTime = now;
            return;
        }
        if (rapid) {
            secondary->setVisible(!secondary->isVisible());
            --rapid; operationTime = now;
            return;
        }
        if (!sequenceDone && now - operationTime >= std::chrono::milliseconds(200)) {
            std::string op;
            if (operations >> op) {
                if (op == "H") secondary->setVisible(false);
                else if (op == "S") secondary->setVisible(true);
                else if (op == "SH") { secondary->setVisible(true); secondary->setVisible(false); }
                else if (op == "S/H") { secondary->setVisible(true); hideNext = true; }
                else if (op == "R") rapid = 10;
                operationTime = now;
                return;
            }
            sequenceDone = true; phaseTime = settledSince = now;
        }
        if (!sequenceDone) return;
        const bool matches = visibilityMapped(sapp_x11_get_display(), xwindow(*secondary)) == secondary->isVisible();
        const bool iconic = visibilityIconic(sapp_x11_get_display(), xwindow(*secondary));
        if (!matches || iconic) settledSince = now;
        // Poll through normal updates so MapNotify/UnmapNotify can complete the
        // queued operation. Require stability beyond Mutter's delayed iconify.
        if (now - settledSince < std::chrono::milliseconds(150) && now - phaseTime < std::chrono::seconds(2)) return;
        check("visSeq final native map state equals isVisible", matches);
        check("visSeq final WM_STATE is not IconicState", !iconic);
        if (++sequence < 4) beginSequence();
        else { finished = true; exitApp(failures ? 1 : 0); }
    }
#endif
    int savedMainDraws = 0, savedSecondaryDraws = 0, savedSecondaryUpdates = 0;
    void setup() override {
        auto* window = getWindow();
        check("launch requested visibility", window->isVisible() == startVisible);
#ifdef VISIBILITY_X11
        check("launch never mapped before first show", startVisible || visibilityMapCount(xwindow(*window)) == 0);
#endif
        window->setVisible(false);
        check("hide takes effect immediately", !window->isVisible() && window->isOccluded());
        setWindowDecorated(true); setWindowDecorated(false);
        checkHiddenControls(*window);
        window->setPosition(10, 10);
        secondary = createWindow(WindowSettings().setSize(100, 80).setVisible(false).setFullscreen(fullscreen));
        check("hidden secondary created", secondary && secondary->isOpen());
        if (!secondary) { exitApp(1); return; }
        check("secondary starts hidden and occluded", !secondary->isVisible() && secondary->isOccluded());
        checkHiddenControls(*secondary, false);
#ifdef VISIBILITY_X11
        check("secondary never mapped at creation", visibilityMapCount(xwindow(*secondary)) == 0);
#endif
        if (!fullscreen) secondary->setPosition(300, 100);
        secondary->setApp<Secondary>();
    }
    void update() override {
        ++updates;
        if (std::chrono::steady_clock::now() - started > std::chrono::seconds(30)) {
            check("probe completed within 30 seconds", false); exitApp(1); return;
        }
        if (!secondary) return;
        if (phase == 0 && secondaryUpdates >= 3) {
            check("all hidden windows keep updating", updates >= 3 && secondaryUpdates >= 3);
            check("start-hidden windows never draw", mainDraws == 0 && secondaryDraws == 0);
#ifdef VISIBILITY_X11
            // Startup hide can be queued behind its initial native map.
            if (visibilityMapped(sapp_x11_get_display(), xwindow(*getWindow())) ||
                visibilityMapped(sapp_x11_get_display(), xwindow(*secondary))) return;
            checkUnmapped(*getWindow()); checkUnmapped(*secondary);
#endif
            getWindow()->setVisible(true); secondary->setVisible(true);
            check("show reports requested state immediately", getWindow()->isVisible() && secondary->isVisible());
            phaseTime = std::chrono::steady_clock::now(); phase = 1;
        } else if (phase == 1 && fullscreenSettled() && secondaryDraws >= 3 && (fullscreen || mainDraws >= 3)) {
            check("shown windows resume drawing", secondaryDraws >= 3 && (fullscreen || mainDraws >= 3));
#ifdef VISIBILITY_X11
            check("X11 show maps both windows", visibilityWaitMapped(sapp_x11_get_display(), xwindow(*getWindow()), true) && visibilityWaitMapped(sapp_x11_get_display(), xwindow(*secondary), true));
#endif
            if (!fullscreen) {
                const auto position = secondary->getPosition();
                if ((std::abs(position.x - 300) > 4 || std::abs(position.y - 100) > 4) &&
                    std::chrono::steady_clock::now() - phaseTime < std::chrono::seconds(2)) return;
                std::printf("windowVisible: pre-first-show position after show: %d,%d\n", position.x, position.y);
                check("pre-first-show position honored on first show", std::abs(position.x - 300) <= 4 && std::abs(position.y - 100) <= 4);
            }
#ifdef VISIBILITY_X11
            preHideMainPosition = getWindow()->getPosition();
            preHideSecondaryPosition = secondary->getPosition();
#endif
            getWindow()->setVisible(false); secondary->setVisible(false);
            check("all windows hidden and occluded", !getWindow()->isVisible() && !secondary->isVisible() && getWindow()->isOccluded() && secondary->isOccluded());
            savedMainDraws = mainDraws; savedSecondaryDraws = secondaryDraws;
            savedSecondaryUpdates = secondaryUpdates; phaseUpdates = updates; phase = 2;
        } else if (phase == 2 && updates >= phaseUpdates + 4 && secondaryUpdates >= savedSecondaryUpdates + 4) {
            check("hiding every window does not quit", getWindow()->isOpen() && secondary->isOpen());
            check("hidden drawing stops while both Apps update", mainDraws == savedMainDraws && secondaryDraws == savedSecondaryDraws);
#ifdef VISIBILITY_X11
            checkUnmapped(*getWindow()); checkUnmapped(*secondary);
            check("main hidden position preserves pre-hide frame", getWindow()->getPosition().x == preHideMainPosition.x && getWindow()->getPosition().y == preHideMainPosition.y);
            check("secondary hidden position preserves pre-hide frame", secondary->getPosition().x == preHideSecondaryPosition.x && secondary->getPosition().y == preHideSecondaryPosition.y);
#endif
            checkHiddenControls(*getWindow()); checkHiddenControls(*secondary);
            if (!fullscreen) {
                secondary->setPosition(400, 120);
#ifdef VISIBILITY_X11
                check("hidden move reads requested frame position", secondary->getPosition().x == 400 && secondary->getPosition().y == 120);
#endif
            }
            secondary->setVisible(true);
            check("second show reports requested state", secondary->isVisible());
            phaseTime = std::chrono::steady_clock::now(); phase = 3;
        } else if (phase == 3 && fullscreenSettled() && secondaryDraws >= savedSecondaryDraws + 3) {
            check("secondary redraws after repeated hide/show", true);
            if (!fullscreen) {
                const auto position = secondary->getPosition();
                if ((std::abs(position.x - 400) > 4 || std::abs(position.y - 120) > 4) &&
                    std::chrono::steady_clock::now() - phaseTime < std::chrono::seconds(2)) return;
                std::printf("windowVisible: hidden-set position after show: %d,%d\n", position.x, position.y);
                check("hidden-set position honored on show", std::abs(position.x - 400) <= 4 && std::abs(position.y - 120) <= 4);
            }
            check("fullscreen request survives hide/show", secondary->isFullscreen() == fullscreen);
            hideInAppDraw = true; phase = 4;
        } else if (phase == 4 && drawHides == 1 && secondaryUpdates >= hiddenAtUpdate + 3) {
            checkDrawHide();
            secondary->setIndependentFps(VSYNC, VSYNC);
            secondary->setVisible(true);
            hideInDrawListener = true; phase = 5;
        } else if (phase == 5 && drawHides == 2 && secondaryUpdates >= hiddenAtUpdate + 3) {
            checkDrawHide();
            secondary->setVisible(true);
            hideInOnRender = true; phase = 6;
        } else if (phase == 6 && drawHides == 3 && secondaryUpdates >= hiddenAtUpdate + 3) {
            checkDrawHide(true);
            secondary->setVisible(true);
            hideInAfterFrame = true; phase = 7;
        } else if (phase == 7 && drawHides == 4 && secondaryUpdates >= hiddenAtUpdate + 3) {
            check("afterFrame hide takes effect immediately", !secondary->isVisible());
#ifdef VISIBILITY_X11
            check("afterFrame hide skips native present", visibilitySwapCount(xwindow(*secondary)) == swapsBeforeHide);
            checkUnmapped(*secondary);
#endif
            secondary->setVisible(true);
            armEventDrivenHide = true; phase = 8;
        } else if (phase == 8 && !armEventDrivenHide && secondaryUpdates >= hiddenAtUpdate + 3 &&
                   std::chrono::steady_clock::now() - eventDrivenHiddenSince >= std::chrono::milliseconds(500)) {
            savedSecondaryDraws = secondaryDraws;
#ifdef VISIBILITY_X11
            swapsBeforeHide = visibilitySwapCount(xwindow(*secondary));
            invalidationsBeforeShow = visibilityInvalidationCount(xwindow(*secondary));
#endif
            secondary->setVisible(true);
            phaseTime = std::chrono::steady_clock::now(); phase = 9;
        } else if (phase == 9) {
#ifdef VISIBILITY_X11
            // Keep ticking while the WM maps, allowing the early-present regression.
            const bool mapped = visibilityMapped(sapp_x11_get_display(), xwindow(*secondary));
            const bool finalPresent = visibilityFinalSizePresented(sapp_x11_get_display(), xwindow(*secondary), fullscreen, swapsBeforeHide);
            if ((!mapped || !finalPresent) && std::chrono::steady_clock::now() - phaseTime < std::chrono::seconds(2)) return;
            check("EVENT_DRIVEN show is mapped", mapped);
            check("EVENT_DRIVEN final size has a completed present", finalPresent);
#endif
            phaseTime = std::chrono::steady_clock::now(); phase = 10;
        } else if (phase == 10 && std::chrono::steady_clock::now() - phaseTime >= std::chrono::milliseconds(500)) {
#ifdef VISIBILITY_X11
            check("EVENT_DRIVEN shown pixels contain new blue content", visibilityBluePixel(sapp_x11_get_display(), xwindow(*secondary)));
            const int resumedPresents = visibilitySwapCount(xwindow(*secondary)) - swapsBeforeHide;
            std::printf("windowVisible: EVENT_DRIVEN presentations after show: %d\n", resumedPresents);
            // One pending redraw plus one per map and two per size change.
            const int invalidations = visibilityInvalidationCount(xwindow(*secondary)) - invalidationsBeforeShow;
            check("EVENT_DRIVEN show presents without a redraw storm", resumedPresents >= 1 && resumedPresents <= 1 + invalidations &&
                secondaryDraws == savedSecondaryDraws + resumedPresents &&
                internal::WindowRequestAccess::context(*secondary).loop.redrawCount == 0);
#else
            check("EVENT_DRIVEN show completes exactly one pending redraw", secondaryDraws == savedSecondaryDraws + 1 &&
                internal::WindowRequestAccess::context(*secondary).loop.redrawCount == 0);
#endif
            secondary->setVisible(false);
#ifdef VISIBILITY_X11
            checkUnmapped(*secondary);
#endif
            secondary->setVisible(true); secondary->setVisible(false);
            check("same-update hide wins requested state", !secondary->isVisible());
            phaseTime = std::chrono::steady_clock::now(); phase = 11;
        } else if (phase == 11) {
            if (std::chrono::steady_clock::now() - phaseTime < std::chrono::milliseconds(500)) return;
#ifdef VISIBILITY_X11
            checkUnmapped(*secondary);
            secondary->setFps(VSYNC);
            beginSequence(); phase = 12;
#else
            finished = true;
            exitApp(failures ? 1 : 0);
#endif
#ifdef VISIBILITY_X11
        } else if (phase == 12) {
            runSequence();
#endif
        }
    }
    void draw() override { ++mainDraws; clear(0.1f); }
    void cleanup() override { secondary.reset(); }
};
}
TC_CORE_TEST_MAIN(int argc, char** argv) {
    bool display = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        display |= arg == "--display";
        startVisible |= arg == "--start-visible";
        fullscreen |= arg == "--fullscreen";
    }
    WindowSettings settings;
    check("visible defaults to true", settings.visible);
    check("visibility settings chain", &settings.setVisible(false) == &settings && !settings.visible);
    Window closed;
    check("unbound window is not visible", !closed.isVisible());
    if (!display) {
#ifdef VISIBILITY_X11
        getMainThreadId();
        int notices = 0;
        auto log = getLogger().onLog.listen([&](LogEventArgs& e) {
            if (e.level == LogLevel::Notice && e.message.find("setVisible(): unsupported without X11") != std::string::npos) ++notices;
        });
        Window first, second;
        internal::WindowRequestAccess::bindForTests(first, 101);
        internal::WindowRequestAccess::bindForTests(second, 102);
        for (int i = 0; i < 3; ++i) { first.setVisible(false); second.setVisible(true); }
        check("unsupported visibility logs Notice on every call", notices == 6);
        internal::WindowRequestAccess::unbind(first); internal::WindowRequestAccess::unbind(second);
#endif
        return failures ? 1 : 0;
    }
    if (!internal::supportsSecondaryWindows()) { std::puts("SKIP: windowVisible requires desktop windows"); return 0; }
    return tcCoreTest::runDisplayTest<Probe>(WindowSettings().setSize(160, 120).setVisible(startVisible).setDecorated(false), failures, [] {
        check("probe reached final phase", finished);
        std::printf("windowVisible verdict: %s\n", failures ? "FAIL" : "PASS");
    });
}
