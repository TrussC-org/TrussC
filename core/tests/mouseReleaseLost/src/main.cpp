// Lost OS releases must end the press through the normal App / node path.
// Headless: inject sapp events into the existing main-window callback.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
#include <string>
#include <vector>

// Private adapter seam, declared only here: no native window or OS input.
#if (defined(__APPLE__) && TARGET_OS_OSX) || defined(_WIN32) || \
    (defined(__linux__) && defined(SOKOL_GLCORE) && !defined(__ANDROID__))
#define TC_TEST_SECONDARY_DISPATCH
namespace trussc::internal {
void dispatchSecondaryWindowEventForTests(const sapp_event* ev, Window& win);
}
#endif

using namespace tc;

namespace {
int failures = 0;
void check(const char* label, bool ok) {
    std::printf("%-70s %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

struct Probe : RectNode {
    int presses = 0, releases = 0, moves = 0, drags = 0;
    MouseEventArgs lastRelease;
    std::vector<int> releaseButtons;
    Probe() { setSize(100, 100); enableEvents(); }
    bool onMousePress(const MouseEventArgs&) override { ++presses; return true; }
    bool onMouseRelease(const MouseEventArgs& e) override {
        ++releases; lastRelease = e; releaseButtons.push_back(e.button); return true;
    }
    bool onMouseMove(const MouseMoveEventArgs&) override { ++moves; return true; }
    bool onMouseDrag(const MouseDragEventArgs&) override { ++drags; return true; }
};
struct TestApp : App {
    int releases = 0, moves = 0, drags = 0;
    MouseEventArgs lastRelease;
    std::vector<int> releaseButtons, dragButtons;
    void mouseReleased(const MouseEventArgs& e) override { ++releases; lastRelease = e; releaseButtons.push_back(e.button); }
    void mouseMoved(const MouseMoveEventArgs&) override { ++moves; }
    void mouseDragged(const MouseDragEventArgs& e) override { ++drags; dragButtons.push_back(e.button); }
};
TestApp* activeApp = nullptr;

void feed(sapp_event_type type, int button = SAPP_MOUSEBUTTON_LEFT,
          unsigned int modifiers = 0, float x = 10, float y = 20) {
    sapp_event e{};
    e.type = type;
    e.mouse_button = static_cast<sapp_mousebutton>(button);
    e.modifiers = modifiers;
    e.mouse_x = x; e.mouse_y = y;
    internal::_event_cb(&e);
}

bool moveBitsSupported() {
#if defined(_WIN32) || defined(__EMSCRIPTEN__) || (defined(__linux__) && !defined(__ANDROID__))
    return true;
#else
    return false;
#endif
}

void printMoveBitsSkip() {
#if defined(__APPLE__) && TARGET_OS_OSX
    std::puts("SKIP: move-bit recovery (macOS moves carry no button bits)");
#elif defined(__ANDROID__)
    std::puts("SKIP: move-bit recovery (Android uses touch events)");
#elif defined(__APPLE__) && TARGET_OS_IPHONE
    std::puts("SKIP: move-bit recovery (iOS uses touch events)");
#else
    std::puts("SKIP: move-bit recovery (backend has no reliable move button bits)");
#endif
}

void feedTouch(sapp_event_type type) {
    sapp_event e{};
    e.type = type;
    e.num_touches = 1;
    e.touches[0].identifier = 42;
    e.touches[0].pos_x = 10; e.touches[0].pos_y = 20;
    e.touches[0].changed = true;
    internal::_event_cb(&e);
}

#ifdef TC_TEST_SECONDARY_DISPATCH
// Like windowSetApp's stand-in: permit real setApp() without creating an OS
// window, and remove the stand-in before the platform destructor runs.
struct HeadlessWindow : tc::Window {
    int standIn = 0; // setApp() needs an open Window; dispatch never reads native_
    HeadlessWindow() { native_ = &standIn; }
    ~HeadlessWindow() { native_ = nullptr; }
};
#endif

struct Fixture {
    std::shared_ptr<TestApp> app = std::make_shared<TestApp>();
    std::shared_ptr<Probe> probe = std::make_shared<Probe>();
    internal::WindowContext* previous = internal::currentWindowCtx();
#ifdef TC_TEST_SECONDARY_DISPATCH
    std::unique_ptr<HeadlessWindow> window;
#endif
    explicit Fixture(bool secondary) {
        app->RectNode::setSize(100, 100);
        app->addChild(probe);
#ifdef TC_TEST_SECONDARY_DISPATCH
        if (secondary) {
            window = std::make_unique<HeadlessWindow>();
            window->setApp(app);
            internal::applyPendingAppForTests(*window);
            internal::currentWindowCtx() = &window->context();
        }
#else
        (void)secondary;
#endif
        activeApp = app.get();
        auto& ctx = internal::currentWindowContext();
        ctx.rootNode = app;
        ctx.pressedMouseButtons = 0;
        internal::currentMouseButton = -1;
        ctx.mousePressed = false; ctx.mouseButton = -1;
    }
    ~Fixture() {
#ifdef TC_TEST_SECONDARY_DISPATCH
        if (window) internal::WindowRequestAccess::endApp(*window);
#endif
        internal::currentWindowCtx() = previous;
        activeApp = nullptr;
    }
    void send(sapp_event_type type, int button = SAPP_MOUSEBUTTON_LEFT,
              unsigned int modifiers = 0, float x = 10, float y = 20) {
#ifdef TC_TEST_SECONDARY_DISPATCH
        if (window) {
            sapp_event e{};
            e.type = type; e.mouse_button = static_cast<sapp_mousebutton>(button);
            e.modifiers = modifiers; e.mouse_x = x; e.mouse_y = y;
            internal::dispatchSecondaryWindowEventForTests(&e, *window);
            return;
        }
#endif
        feed(type, button, modifiers, x, y);
    }
};

void scenario(int kind, int button, unsigned int bit, bool secondary = false) {
    Fixture f(secondary);
    auto app = f.app;
    auto probe = f.probe;
    auto& ctx = internal::currentWindowContext();
    int releases = 0, moves = 0, drags = 0;
    bool stateClearAtRelease = false;
    MouseEventArgs lastRelease;
    std::string order;
    auto releaseListener = events().mouseReleased.listen([&](const MouseEventArgs& e) {
        ++releases; lastRelease = e; order += 'R';
        stateClearAtRelease = !isMousePressed() && ctx.mouseButton == -1 &&
            internal::currentMouseButton == -1;
    });
    auto moveListener = events().mouseMoved.listen([&](const MouseMoveEventArgs&) { ++moves; order += 'M'; });
    auto dragListener = events().mouseDragged.listen([&](const MouseDragEventArgs&) { ++drags; order += 'D'; });

    if (kind == 5) feedTouch(SAPP_EVENTTYPE_TOUCHES_BEGAN);
    else f.send(SAPP_EVENTTYPE_MOUSE_DOWN, button, bit);
    check("down reaches the node and sets pressed state", probe->presses == 1 && isMousePressed());
    Vec2 expectedRelease(10, 20);
    if (kind == 0) {
        f.send(SAPP_EVENTTYPE_UNFOCUSED, button, SAPP_MODIFIER_SHIFT, 90, 90);
        f.send(SAPP_EVENTTYPE_FOCUSED);
        f.send(SAPP_EVENTTYPE_MOUSE_MOVE, button, 0, 30, 40);
    } else if (kind == 1) {
        // Another held button must not hide the tracked button's release.
        const unsigned int otherBit = bit == SAPP_MODIFIER_LMB ? SAPP_MODIFIER_RMB : SAPP_MODIFIER_LMB;
        f.send(SAPP_EVENTTYPE_MOUSE_MOVE, button, otherBit | SAPP_MODIFIER_SHIFT, 30, 40);
        f.send(SAPP_EVENTTYPE_MOUSE_MOVE, button, 0, 35, 45);
    } else if (kind == 2) {
        f.send(SAPP_EVENTTYPE_MOUSE_MOVE, button, moveBitsSupported() ? bit : 0, 30, 40);
        check("a held button still drags and stays pressed", drags == 1 && isMousePressed());
        f.send(SAPP_EVENTTYPE_MOUSE_UP, button, 0, 35, 45);
        expectedRelease = Vec2(35, 45);
    } else if (kind == 3) {
        f.send(SAPP_EVENTTYPE_UNFOCUSED, button, SAPP_MODIFIER_SHIFT, 90, 90);
        f.send(SAPP_EVENTTYPE_UNFOCUSED);
        f.send(SAPP_EVENTTYPE_MOUSE_UP, button, 0, 80, 80);
        f.send(SAPP_EVENTTYPE_MOUSE_MOVE, button, 0, 30, 40);
    } else if (kind == 4) {
        f.send(SAPP_EVENTTYPE_MOUSE_UP, button, 0, 35, 45);
        expectedRelease = Vec2(35, 45);
    } else {
        f.send(SAPP_EVENTTYPE_UNFOCUSED, button, SAPP_MODIFIER_SHIFT);
        f.send(SAPP_EVENTTYPE_FOCUSED);
        feedTouch(SAPP_EVENTTYPE_TOUCHES_MOVED);
        feedTouch(SAPP_EVENTTYPE_TOUCHES_ENDED);
    }
    const int expectedMoves = kind == 1 ? 2 : (kind == 0 || kind == 3 ? 1 : 0);
    const int expectedDrags = kind == 2 ? 1 : 0;
    check("exactly one release reaches event listeners, App and node",
          releases == 1 && app->releases == 1 && probe->releases == 1);
    check("release retains button and last known position",
          lastRelease.button == button && lastRelease.pos == expectedRelease &&
          lastRelease.globalPos == expectedRelease && app->lastRelease.pos == expectedRelease &&
          probe->lastRelease.globalPos == expectedRelease);
    check("state is cleared before release notification", stateClearAtRelease && !isMousePressed());
    // Node dispatch also bubbles a move for a drag (the existing hover path).
    check("moves and drags preserve event, App and node dispatch",
          moves == expectedMoves && app->moves == expectedMoves &&
          probe->moves == expectedMoves + expectedDrags &&
          drags == expectedDrags && app->drags == expectedDrags && probe->drags == expectedDrags);
    check("release ordering is preserved",
          order == (kind == 1 ? "RMM" : kind == 2 ? "DR" : kind >= 4 ? "R" : "RM"));
    check("synthetic release carries keyboard modifiers", kind == 2 || kind == 4 || lastRelease.shift);
    check("node grab is cleared", ctx.grabbedNode.expired());
}
// Multi-button behavior must retain the base current-button reporting while
// release deduplication independently follows each outstanding press.
void multiScenario(int kind, bool secondary) {
    std::printf("secondary=%d multi=%d\n", secondary, kind);
    Fixture f(secondary);
    auto& ctx = internal::currentWindowContext();
    std::vector<int> released;
    auto listener = events().mouseReleased.listen([&](const MouseEventArgs& e) { released.push_back(e.button); });
    auto down = [&](int button) {
        f.send(SAPP_EVENTTYPE_MOUSE_DOWN, button, SAPP_MODIFIER_LMB | SAPP_MODIFIER_RMB);
    };
    auto up = [&](int button) { f.send(SAPP_EVENTTYPE_MOUSE_UP, button); };
    auto heldMove = [&] {
        f.send(SAPP_EVENTTYPE_MOUSE_MOVE, 0, moveBitsSupported() ? SAPP_MODIFIER_LMB | SAPP_MODIFIER_RMB : 0);
    };
    down(SAPP_MOUSEBUTTON_LEFT);
    if (kind == 1) {
        f.send(SAPP_EVENTTYPE_UNFOCUSED);
        f.send(SAPP_EVENTTYPE_FOCUSED);
    }
    down(SAPP_MOUSEBUTTON_RIGHT);
    check("right down takes over the node grab", ctx.grabbedNode.lock() == f.probe && ctx.grabbedButton == SAPP_MOUSEBUTTON_RIGHT);
    heldMove();
    check("held move reports the most recent down button", f.app->dragButtons == std::vector<int>{SAPP_MOUSEBUTTON_RIGHT});
    if (kind == 0 || kind == 1) {
        up(SAPP_MOUSEBUTTON_LEFT); // kind 1: late UP from the recovered press
        if (kind == 1) {
            check("late left up neither repeats release nor clears right drag state",
                  released == std::vector<int>{SAPP_MOUSEBUTTON_LEFT} && isMousePressed() && ctx.mouseButton == SAPP_MOUSEBUTTON_RIGHT);
            heldMove();
            check("right still drags after late left up", f.app->dragButtons == std::vector<int>({SAPP_MOUSEBUTTON_RIGHT, SAPP_MOUSEBUTTON_RIGHT}));
        } else {
            check("real left up retains base current-button clearing", !isMousePressed() && ctx.mouseButton == -1);
        }
        up(SAPP_MOUSEBUTTON_RIGHT);
    } else if (kind == 2) {
        f.send(SAPP_EVENTTYPE_UNFOCUSED);
        f.send(SAPP_EVENTTYPE_UNFOCUSED);
        up(SAPP_MOUSEBUTTON_LEFT);
        up(SAPP_MOUSEBUTTON_RIGHT);
    } else if (kind == 3) {
        f.send(SAPP_EVENTTYPE_MOUSE_MOVE, 0, 0, 30, 40);
        check("move recovers all absent buttons at the previous position",
              released.size() == 2 && f.app->lastRelease.pos == Vec2(10, 20) && f.probe->lastRelease.globalPos == Vec2(10, 20));
        up(SAPP_MOUSEBUTTON_LEFT);
        up(SAPP_MOUSEBUTTON_RIGHT);
    } else if (kind == 4) {
        f.send(SAPP_EVENTTYPE_MOUSE_MOVE, 0, SAPP_MODIFIER_RMB);
        check("move recovers only the absent left button", released == std::vector<int>{SAPP_MOUSEBUTTON_LEFT});
        up(SAPP_MOUSEBUTTON_LEFT); // duplicate while R remains outstanding
        up(SAPP_MOUSEBUTTON_RIGHT);
    } else {
        up(SAPP_MOUSEBUTTON_RIGHT);
        up(SAPP_MOUSEBUTTON_LEFT);
    }
    const std::vector<int> expected = kind == 5 ? std::vector<int>{1, 0} : std::vector<int>{0, 1};
    check("each button releases exactly once through events, App and node",
          released == expected && f.app->releaseButtons == expected && f.probe->releaseButtons == expected);
    check("all outstanding presses and the grab are cleared",
          ctx.pressedMouseButtons == 0 && !isMousePressed() && ctx.mouseButton == -1 && ctx.grabbedNode.expired());
    const int appDrags = f.app->drags, nodeDrags = f.probe->drags;
    const int appMoves = f.app->moves;
    f.send(SAPP_EVENTTYPE_MOUSE_MOVE, 0, 0, 35, 45);
    check("plain move after releases never drags the App or grabbed node",
          f.app->drags == appDrags && f.probe->drags == nodeDrags && f.app->moves == appMoves + 1);
}

#ifdef TC_TEST_SECONDARY_DISPATCH
void secondaryLeaveScenario(bool focusLoss) {
    Fixture f(true);
    auto& ctx = internal::currentWindowContext();
    MouseEventArgs released;
    int releases = 0;
    auto listener = events().mouseReleased.listen([&](const MouseEventArgs& e) { released = e; ++releases; });
    f.send(SAPP_EVENTTYPE_MOUSE_DOWN, SAPP_MOUSEBUTTON_LEFT, SAPP_MODIFIER_LMB);
    f.send(SAPP_EVENTTYPE_MOUSE_LEAVE, SAPP_MOUSEBUTTON_LEFT, SAPP_MODIFIER_LMB, 90, 95);
    check("secondary leave preserves hover-clearing sentinel", ctx.mouseX == -1 && ctx.mouseY == -1);
    if (focusLoss) f.send(SAPP_EVENTTYPE_UNFOCUSED);
    else f.send(SAPP_EVENTTYPE_MOUSE_MOVE, SAPP_MOUSEBUTTON_LEFT, 0, 30, 40);
    check("secondary recovery releases once at last real pointer position",
          releases == 1 && released.button == SAPP_MOUSEBUTTON_LEFT && released.pos == Vec2(10, 20) &&
          released.globalPos == Vec2(10, 20) && f.app->lastRelease.pos == Vec2(10, 20) && f.probe->lastRelease.globalPos == Vec2(10, 20));
    check("secondary recovery clears grabbed node", ctx.grabbedNode.expired() && !isMousePressed());
    if (focusLoss) check("focus recovery keeps hover offscreen", ctx.mouseX == -1 && ctx.mouseY == -1);
    f.send(SAPP_EVENTTYPE_MOUSE_UP);
    const int drags = f.probe->drags;
    f.send(SAPP_EVENTTYPE_MOUSE_MOVE, 0, 0, 35, 45);
    check("secondary late up is deduplicated and following move never drags",
          releases == 1 && f.app->releases == 1 && f.probe->releases == 1 && f.probe->drags == drags);
}
#endif
} // namespace

TC_CORE_TEST_MAIN() {
    internal::pixelPerfectMode() = true; // avoid querying DPI without a native window
    setTouchAsMouse(true);
    internal::appMousePressedFunc = [](const MouseEventArgs& e) { activeApp->handleMousePressed(e); };
    internal::appMouseReleasedFunc = [](const MouseEventArgs& e) { activeApp->handleMouseReleased(e); };
    internal::appMouseMovedFunc = [](const internal::MouseEventRaw& e) { activeApp->handleMouseMoved(e); };
    internal::appMouseDraggedFunc = [](const internal::MouseEventRaw& e) { activeApp->handleMouseDragged(e); };
    // One line covers all gated move-bit scenarios, in either dispatch path.
    if (!moveBitsSupported()) printMoveBitsSkip();
    const unsigned int bits[] = {SAPP_MODIFIER_LMB, SAPP_MODIFIER_RMB, SAPP_MODIFIER_MMB};
    for (int button = 0; button < 3; ++button) {
        for (int kind = 0; kind < 5; ++kind) {
            if (kind == 1 && !moveBitsSupported()) continue;
            std::printf("button=%d scenario=%d\n", button, kind);
            scenario(kind, button, bits[button]);
        }
    }
    scenario(5, SAPP_MOUSEBUTTON_LEFT, SAPP_MODIFIER_LMB);
    for (int kind = 0; kind < 6; ++kind) {
        if ((kind == 3 || kind == 4) && !moveBitsSupported()) continue;
        multiScenario(kind, false);
    }
#ifdef TC_TEST_SECONDARY_DISPATCH
    for (int button = 0; button < 3; ++button)
        for (int kind = 0; kind < 5; ++kind) {
            if (kind == 1 && !moveBitsSupported()) continue;
            scenario(kind, button, bits[button], true);
        }
    for (int kind = 0; kind < 6; ++kind) {
        if ((kind == 3 || kind == 4) && !moveBitsSupported()) continue;
        multiScenario(kind, true);
    }
    secondaryLeaveScenario(true);
    if (moveBitsSupported()) secondaryLeaveScenario(false);
#elif defined(__EMSCRIPTEN__)
    std::puts("SKIP: secondary-window dispatch (Web has no secondary-window adapter)");
#elif defined(__ANDROID__)
    std::puts("SKIP: secondary-window dispatch (Android has no secondary-window adapter)");
#elif defined(__APPLE__) && TARGET_OS_IPHONE
    std::puts("SKIP: secondary-window dispatch (iOS has no secondary-window adapter)");
#elif defined(__linux__)
    std::puts("SKIP: secondary-window dispatch (Linux backend requires GLCORE)");
#else
    std::puts("SKIP: secondary-window dispatch (no adapter for this platform/backend)");
#endif
    internal::appMousePressedFunc = nullptr;
    internal::appMouseReleasedFunc = nullptr;
    internal::appMouseMovedFunc = nullptr;
    internal::appMouseDraggedFunc = nullptr;
    return failures ? 1 : 0;
}
