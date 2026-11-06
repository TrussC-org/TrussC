// Focus loss ends held keys through the ordinary per-window event path.
#include <TrussC.h>
#include "../../common/tcWindowTest.h"
#include "../../common/tcCoreTest.h"
#include <cstdio>
#include <vector>

#if (defined(__APPLE__) && TARGET_OS_OSX) || defined(_WIN32) || \
    (defined(__linux__) && (defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)) && !defined(__ANDROID__))
#define TC_TEST_SECONDARY_DISPATCH
namespace trussc::internal {
void dispatchWindowEventForTests(const sapp_event*, Window&);
}
#endif
using namespace tc;
namespace {
int failures = 0;
void check(const char* label, bool ok) {
    std::printf("%s: %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
struct TestApp : App {
    std::vector<int> pressed;
    std::vector<int> released;
    bool stateCleared = true;
    void keyPressed(const KeyEventArgs& e) override {
        pressed.push_back(e.key);
    }
    void keyReleased(const KeyEventArgs& e) override {
        released.push_back(e.key);
        stateCleared &= !isKeyPressed(e.key) && !isShiftPressed() &&
            !isControlPressed() && !isAltPressed() && !isSuperPressed();
    }
};
#ifdef TC_TEST_SECONDARY_DISPATCH
struct HeadlessWindow : Window {
    int standIn = 0;
    HeadlessWindow() {
        native_ = &standIn;
        internal::WindowRequestAccess::bindForTests(*this, 202);
    }
    ~HeadlessWindow() {
        native_ = nullptr;
        internal::WindowRequestAccess::unbind(*this);
    }
};
#endif
void scenario(Window& window, bool secondary) {
    auto app = internal::makeConstructedApp<TestApp>();
    internal::WindowRequestAccess::attachCandidate(window, app);
    internal::applyPendingAppForTests(window);
    auto& ctx = internal::WindowRequestAccess::context(window);
    auto* previous = internal::currentWindowCtx();
    internal::currentWindowCtx() = &ctx;
    {
        internal::AppOwnerScope ownership(internal::appOwner(*app));
        auto send = [&](sapp_event_type type, int key = SAPP_KEYCODE_INVALID, bool repeat = false) {
            sapp_event e{};
            e.type = type;
            e.key_code = static_cast<sapp_keycode>(key);
            e.key_repeat = repeat;
            if (type == SAPP_EVENTTYPE_KEY_DOWN || type == SAPP_EVENTTYPE_UNFOCUSED)
                e.modifiers = SAPP_MODIFIER_SHIFT | SAPP_MODIFIER_CTRL | SAPP_MODIFIER_ALT | SAPP_MODIFIER_SUPER;
#ifdef TC_TEST_SECONDARY_DISPATCH
            if (secondary) internal::dispatchWindowEventForTests(&e, window);
            else
#else
            (void)secondary;
#endif
                tcCoreTest::dispatchMainEvent(&e);
        };
        // Both sides of every modifier are keys, including Cmd on macOS.
        const std::vector<int> expected = {'A', 'H', KEY_LEFT_SHIFT, KEY_LEFT_CONTROL,
            KEY_LEFT_ALT, KEY_LEFT_SUPER, KEY_RIGHT_SHIFT, KEY_RIGHT_CONTROL,
            KEY_RIGHT_ALT, KEY_RIGHT_SUPER};
        std::vector<int> notified;
        bool clearAtNotification = true;
        auto listener = events().keyReleased.listen([&](const KeyEventArgs& e) {
            notified.push_back(e.key);
            clearAtNotification &= ctx.keysPressed.empty() && !e.isRepeat &&
                !e.shift && !e.ctrl && !e.alt && !e.super;
        });
        for (auto it = expected.rbegin(); it != expected.rend(); ++it) send(SAPP_EVENTTYPE_KEY_DOWN, *it);
        send(SAPP_EVENTTYPE_KEY_DOWN, 'A', true);
        check("down and repeat record held keys and modifiers", isKeyPressed('A') &&
            isShiftPressed() && isControlPressed() && isAltPressed() && isSuperPressed());
        send(SAPP_EVENTTYPE_UNFOCUSED);
        check("focus loss releases every key in key-code order to listeners and App",
            notified == expected && app->released == expected);
        check("keys and modifiers clear before release callbacks",
            clearAtNotification && app->stateCleared && ctx.keysPressed.empty() &&
            !isKeyPressed('A') && !isShiftPressed() && !isControlPressed() &&
            !isAltPressed() && !isSuperPressed());
        send(SAPP_EVENTTYPE_UNFOCUSED);
        send(SAPP_EVENTTYPE_FOCUSED);
        for (int key : expected) send(SAPP_EVENTTYPE_KEY_UP, key);
        check("repeated focus loss and late OS key-ups do not duplicate releases",
            notified == expected && app->released == expected);
        send(SAPP_EVENTTYPE_KEY_DOWN, 'A');
        send(SAPP_EVENTTYPE_KEY_UP, 'A');
        check("a new press still receives its ordinary release", notified.size() == expected.size() + 1 &&
            app->released.size() == notified.size() && notified.back() == 'A' && !isKeyPressed('A'));

        send(SAPP_EVENTTYPE_KEY_DOWN, 'C');
        send(SAPP_EVENTTYPE_UNFOCUSED);
        send(SAPP_EVENTTYPE_FOCUSED);
        app->pressed.clear();
        app->released.clear();
        notified.clear();
        send(SAPP_EVENTTYPE_KEY_DOWN, 'C', true);
        check("repeat after refocus delivers and records the press",
            app->pressed == std::vector<int>{'C'} && isKeyPressed('C'));
        send(SAPP_EVENTTYPE_KEY_UP, 'C');
        check("repeat after refocus receives exactly one ordinary release",
            notified == std::vector<int>{'C'} && app->released == notified && !isKeyPressed('C'));
        send(SAPP_EVENTTYPE_KEY_UP, 'C');
        check("late duplicate after a refocused repeat is ignored",
            notified == std::vector<int>{'C'} && app->released == notified);
    }
    window.destroyApp();
    internal::applyPendingAppForTests(window);
    internal::currentWindowCtx() = previous;
}
} // namespace
TC_CORE_TEST_MAIN() {
    internal::pixelPerfectMode() = true;
    getMainThreadId();
    auto launch = internal::prepareLaunchWindow(WindowSettings{});
    internal::WindowRequestAccess::bindForTests(*launch, 101);
#ifdef TC_TEST_SECONDARY_DISPATCH
    HeadlessWindow other;
    auto& otherCtx = internal::WindowRequestAccess::context(other);
    otherCtx.keysPressed.insert('Z');
    otherCtx.keysPressed.insert(KEY_LEFT_SUPER);
#endif
    scenario(*launch, false);
#ifdef TC_TEST_SECONDARY_DISPATCH
    check("main focus loss leaves another window's keys and modifiers held",
        otherCtx.keysPressed.count('Z') && otherCtx.keysPressed.count(KEY_LEFT_SUPER));
    otherCtx.keysPressed.clear();
    auto& mainCtx = internal::WindowRequestAccess::context(*launch);
    mainCtx.keysPressed.insert('Z');
    mainCtx.keysPressed.insert(KEY_LEFT_SUPER);
    scenario(other, true);
    check("secondary focus loss leaves main window's keys and modifiers held",
        mainCtx.keysPressed.count('Z') && mainCtx.keysPressed.count(KEY_LEFT_SUPER));
#else
    std::puts("SKIP: secondary-window dispatch unavailable on this backend");
#endif
    internal::shutdownRuntime();
    return failures ? 1 : 0;
}
