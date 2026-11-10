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
bool visibilityHasWindowManager(const void*);
bool visibilityFramePosition(const void*, unsigned long, int&, int&);
bool visibilityWaitMapped(const void*, unsigned long, bool);
bool visibilityBluePixel(const void*, unsigned long);
bool visibilityIconic(const void*, unsigned long);
void visibilityFrameSize(int, int);
bool visibilityFinalSizePresented(const void*, unsigned long, bool, int);
#define VISIBILITY_X11 1
#endif
namespace {
int failures = 0;
bool startVisible = false, fullscreen = false, finished = false, decoratedMain = false;
bool windowManager = true;
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
#ifdef VISIBILITY_X11
struct PendingPositionRead { Window* window; IVec2 expected; };
std::vector<PendingPositionRead> pendingPositionReads;
#endif
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
void checkFirstLivePositions() {
    std::erase_if(pendingPositionReads, [](const PendingPositionRead& pending) {
        auto* window = pending.window;
        const auto& binding = internal::WindowRequestAccess::binding(*window);
        if (binding.geometry.hiddenPosition) return false;
        int x = 0, y = 0;
        const auto position = window->getPosition();
        const bool frame = visibilityFramePosition(sapp_x11_get_display(), xwindow(*window), x, y);
        std::printf("windowVisible: first live position %d,%d; native frame %d,%d\n", position.x, position.y, x, y);
        check("first live position follows completed map", sapp_tc_window_map_complete(binding.handle));
        // Fullscreen is requested again at MapNotify and can move the frame
        // between these two X11 samples. Check placement in the normal modes.
        if (!fullscreen)
            check("first live position matches native frame", frame && position.x == x && position.y == y);
        if (!fullscreen && (window == secondary.get() || decoratedMain))
            check("first live position equals requested placement", position.x == pending.expected.x && position.y == pending.expected.y);
        return true;
    });
}
#endif
void show(Window& window) {
#ifdef VISIBILITY_X11
    const auto before = window.getPosition();
    const bool snapshot = internal::WindowRequestAccess::binding(window).geometry.hiddenPosition;
#endif
    window.setVisible(true);
#ifdef VISIBILITY_X11
    if (snapshot) {
        const auto after = window.getPosition();
        check("show same-update position preserves snapshot", after.x == before.x && after.y == before.y);
        auto found = std::find_if(pendingPositionReads.begin(), pendingPositionReads.end(),
            [&](const auto& pending) { return pending.window == &window; });
        if (found == pendingPositionReads.end()) pendingPositionReads.push_back({&window, before});
        else found->expected = before;
    }
#endif
}
bool fullscreenSettled() {
#if defined(_WIN32) || (defined(__APPLE__) && TARGET_OS_OSX) || defined(VISIBILITY_X11)
    return !fullscreen || !windowManager || sapp_tc_window_fullscreen_phase(internal::WindowRequestAccess::binding(*secondary).handle)
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
#ifdef VISIBILITY_X11
        checkFirstLivePositions();
#endif
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
// Native smoke test: count callbacks while every window is explicitly hidden.
struct DrawHiddenProbe : App {
    int phase = 0, updates = 0, baselineUpdates = 0, baselineSecondaryUpdates = 0;
    int baselineMain = 0, baselineSecondary = 0;
    const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    void snapshot() {
        baselineMain = mainDraws; baselineSecondary = secondaryDraws;
        baselineUpdates = updates; baselineSecondaryUpdates = secondaryUpdates;
    }
    void setup() override {
        setDrawWhileHidden(true);
        secondary = createWindow(WindowSettings().setSize(100, 80).setVisible(false));
        check("draw override creates hidden secondary", secondary && secondary->isOpen());
        if (!secondary) { exitApp(1); return; }
        secondary->setApp<Secondary>();
        getWindow()->setDrawWhileHidden(false);
    }
    void update() override {
        ++updates;
        if (std::chrono::steady_clock::now() - started > std::chrono::seconds(30)) {
            check("draw override probe completes", false); exitApp(1); return;
        }
        if (!secondary) return;
        if (phase == 0 && updates >= 4 && secondaryUpdates >= 4 && secondaryDraws >= 3) {
            check("setup local false wins while new hidden window inherits global true", mainDraws == 0);
            setDrawWhileHidden(true); snapshot(); phase = 1;
        } else if (phase == 1 && mainDraws >= baselineMain + 3 && secondaryDraws >= baselineSecondary + 3) {
            check("later global true draws both hidden windows", true);
            secondary->setDrawWhileHidden(false); snapshot(); phase = 2;
        } else if (phase == 2 && mainDraws >= baselineMain + 3 && secondaryUpdates >= baselineSecondaryUpdates + 4) {
            check("later local false stops only secondary draw", secondaryDraws == baselineSecondary);
            setDrawWhileHidden(false); snapshot(); phase = 3;
        } else if (phase == 3 && updates >= baselineUpdates + 4 && secondaryUpdates >= baselineSecondaryUpdates + 4) {
            check("global false stops draw while every hidden window updates", mainDraws == baselineMain && secondaryDraws == baselineSecondary);
            secondary->setDrawWhileHidden(true); snapshot(); phase = 4;
        } else if (phase == 4 && secondaryDraws >= baselineSecondary + 3 && updates >= baselineUpdates + 4) {
            check("local true resumes only the hidden secondary", mainDraws == baselineMain);
            setDrawWhileHidden(false); finished = true; exitApp(failures ? 1 : 0);
        }
    }
    void draw() override { ++mainDraws; clear(0.1f); }
    void cleanup() override { secondary.reset(); }
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
            if (secondary->isVisible()) secondary->setVisible(false); else show(*secondary);
            --rapid; operationTime = now;
            return;
        }
        if (!sequenceDone && now - operationTime >= std::chrono::milliseconds(200)) {
            std::string op;
            if (operations >> op) {
                if (op == "H") secondary->setVisible(false);
                else if (op == "S") show(*secondary);
                else if (op == "SH") { show(*secondary); secondary->setVisible(false); }
                else if (op == "S/H") { show(*secondary); hideNext = true; }
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
        windowManager = visibilityHasWindowManager(sapp_x11_get_display());
        check("launch never mapped before first show", startVisible || visibilityMapCount(xwindow(*window)) == 0);
#endif
        window->setVisible(false);
        check("hide takes effect immediately", !window->isVisible() && window->isOccluded());
        if (!decoratedMain) { setWindowDecorated(true); setWindowDecorated(false); }
        checkHiddenControls(*window);
        window->setPosition(decoratedMain ? 200 : 10, decoratedMain ? 200 : 10);
        secondary = createWindow(WindowSettings().setSize(100, 80).setVisible(false).setFullscreen(fullscreen));
        check("hidden secondary created", secondary && secondary->isOpen());
        if (!secondary) { exitApp(1); return; }
        check("secondary starts hidden and occluded", !secondary->isVisible() && secondary->isOccluded());
        checkHiddenControls(*secondary, false);
#ifdef VISIBILITY_X11
        check("secondary never mapped at creation", visibilityMapCount(xwindow(*secondary)) == 0);
#endif
        if (!fullscreen) {
            secondary->setPosition(300, 100);
#ifdef VISIBILITY_X11
            check("start-hidden move reads requested position in same update", secondary->getPosition().x == 300 && secondary->getPosition().y == 100);
#endif
        }
        secondary->setApp<Secondary>();
    }
    void update() override {
        ++updates;
#ifdef VISIBILITY_X11
        checkFirstLivePositions();
#endif
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
            show(*getWindow()); show(*secondary);
            check("show reports requested state immediately", getWindow()->isVisible() && secondary->isVisible());
            phaseTime = std::chrono::steady_clock::now(); phase = 1;
        } else if (phase == 1 && fullscreenSettled() && secondaryDraws >= 3 && ((fullscreen && windowManager) || mainDraws >= 3)) {
#ifdef VISIBILITY_X11
            // Early drawable turns can precede MapNotify. Let both windows
            // reach their first live read before hiding them again.
            if (internal::WindowRequestAccess::binding(*getWindow()).geometry.hiddenPosition ||
                internal::WindowRequestAccess::binding(*secondary).geometry.hiddenPosition) return;
#endif
            check("shown fullscreen state follows native confirmation", secondary->isFullscreen() == (fullscreen && windowManager));
            check("shown windows resume drawing", secondaryDraws >= 3 && ((fullscreen && windowManager) || mainDraws >= 3));
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
            show(*secondary);
            check("second show reports requested state", secondary->isVisible());
            phaseTime = std::chrono::steady_clock::now(); phase = 3;
        } else if (phase == 3 && fullscreenSettled() && secondaryDraws >= savedSecondaryDraws + 3) {
            check("secondary redraws after repeated hide/show", true);
#ifdef VISIBILITY_X11
            check("completed show releases position snapshot", !internal::WindowRequestAccess::binding(*secondary).geometry.hiddenPosition);
#endif
            if (!fullscreen) {
                const auto position = secondary->getPosition();
                if ((std::abs(position.x - 400) > 4 || std::abs(position.y - 120) > 4) &&
                    std::chrono::steady_clock::now() - phaseTime < std::chrono::seconds(2)) return;
                std::printf("windowVisible: hidden-set position after show: %d,%d\n", position.x, position.y);
                check("hidden-set position honored on show", std::abs(position.x - 400) <= 4 && std::abs(position.y - 120) <= 4);
            }
            check("fullscreen state after hide/show follows native confirmation", secondary->isFullscreen() == (fullscreen && windowManager));
            hideInAppDraw = true; phase = 4;
        } else if (phase == 4 && drawHides == 1 && secondaryUpdates >= hiddenAtUpdate + 3) {
            checkDrawHide();
            secondary->setIndependentFps(VSYNC, VSYNC);
            show(*secondary);
            hideInDrawListener = true; phase = 5;
        } else if (phase == 5 && drawHides == 2 && secondaryUpdates >= hiddenAtUpdate + 3) {
            checkDrawHide();
            show(*secondary);
            hideInOnRender = true; phase = 6;
        } else if (phase == 6 && drawHides == 3 && secondaryUpdates >= hiddenAtUpdate + 3) {
            checkDrawHide(true);
            show(*secondary);
            hideInAfterFrame = true; phase = 7;
        } else if (phase == 7 && drawHides == 4 && secondaryUpdates >= hiddenAtUpdate + 3) {
            check("afterFrame hide takes effect immediately", !secondary->isVisible());
#ifdef VISIBILITY_X11
            check("afterFrame hide skips native present", visibilitySwapCount(xwindow(*secondary)) == swapsBeforeHide);
            checkUnmapped(*secondary);
#endif
            show(*secondary);
            armEventDrivenHide = true; phase = 8;
        } else if (phase == 8 && !armEventDrivenHide && secondaryUpdates >= hiddenAtUpdate + 3 &&
                   std::chrono::steady_clock::now() - eventDrivenHiddenSince >= std::chrono::milliseconds(500)) {
            savedSecondaryDraws = secondaryDraws;
#ifdef VISIBILITY_X11
            swapsBeforeHide = visibilitySwapCount(xwindow(*secondary));
            invalidationsBeforeShow = visibilityInvalidationCount(xwindow(*secondary));
#endif
            show(*secondary);
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
            show(*secondary); secondary->setVisible(false);
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
    bool display = false, drawHidden = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        display |= arg == "--display";
        drawHidden |= arg == "--draw-while-hidden";
        startVisible |= arg == "--start-visible";
        fullscreen |= arg == "--fullscreen";
        decoratedMain |= arg == "--decorated-main";
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
    if (drawHidden) return tcCoreTest::runDisplayTest<DrawHiddenProbe>(WindowSettings().setSize(160, 120).setVisible(false), failures, [] {
        check("draw override probe reached final phase", finished);
        std::printf("windowVisible drawWhileHidden verdict: %s\n", failures ? "FAIL" : "PASS");
    });
    return tcCoreTest::runDisplayTest<Probe>(WindowSettings().setSize(160, 120).setVisible(startVisible).setDecorated(decoratedMain), failures, [] {
        check("probe reached final phase", finished);
        std::printf("windowVisible verdict: %s\n", failures ? "FAIL" : "PASS");
    });
}
