#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcWindowTest.h"
#include "../../common/tcInjectedWindowTest.h"
#include <array>
#include <cstdio>
#include <type_traits>

using namespace tc;
namespace {
template<class T> concept ComparesWithInt = requires(T button) { button == 0; };
static_assert(std::is_same_v<decltype(MouseEventArgs::button), MouseButton>);
static_assert(std::is_same_v<decltype(MouseDragEventArgs::button), MouseButton>);
static_assert(std::is_same_v<decltype(getMouseButton()), MouseButton>);
static_assert(!std::is_convertible_v<MouseButton, int> && !ComparesWithInt<MouseButton>);
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4996)
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
static_assert(std::is_same_v<std::remove_cv_t<decltype(MOUSE_BUTTON_LEFT)>, MouseButton>);
static_assert(MOUSE_BUTTON_LEFT == MouseButton::Left && MOUSE_BUTTON_RIGHT == MouseButton::Right &&
              MOUSE_BUTTON_MIDDLE == MouseButton::Middle);
#if defined(_MSC_VER)
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#endif

static_assert(std::is_same_v<decltype(std::declval<EasyCam>().getOrbitMouseButton()), MouseButton>);
static_assert(std::is_same_v<decltype(std::declval<EasyCam>().getPanMouseButton()), MouseButton>);
int failures = 0;
void check(const char* label, bool success) {
    std::printf("mouseButton: %s: %s\n", success ? "PASS" : "FAIL", label);
    failures += !success;
}
struct ShortApp : App {
    std::array<int, 3> buttons{-1, -1, -1};
    Vec2 position;
    void mousePressed(Vec2 pos, int button) override { buttons[0] = button; position = pos; }
    void mouseReleased(Vec2 pos, int button) override { buttons[1] = button; position = pos; }
    void mouseDragged(Vec2 pos, int button) override { buttons[2] = button; position = pos; }
};
struct ShortNode : Node {
    std::array<int, 3> buttons{-1, -1, -1};
    Vec2 position;
    bool onMousePress(Vec2 pos, int button) override { buttons[0] = button; position = pos; return true; }
    bool onMouseRelease(Vec2 pos, int button) override { buttons[1] = button; position = pos; return true; }
    bool onMouseDrag(Vec2 pos, int button) override { buttons[2] = button; position = pos; return true; }
};
// RectNode and App root overrides also have to pass through Node's bridge.
struct ShortRect : RectNode {
    std::array<int, 3> buttons{-1, -1, -1};
    bool onMousePress(Vec2, int button) override { buttons[0] = button; return true; }
    bool onMouseRelease(Vec2, int button) override { buttons[1] = button; return true; }
    bool onMouseDrag(Vec2, int button) override { buttons[2] = button; return true; }
};
struct ShortRoot : App {
    bool onMousePress(Vec2, int button) override { return button == 1; }
    bool onMouseRelease(Vec2, int button) override { return button == 1; }
    bool onMouseDrag(Vec2, int button) override { return button == 1; }
};
void appCallbacks(App& app, MouseButton button) {
    MouseEventArgs event;
    event.button = button;
    event.pos = {12, 34};
    app.mousePressed(event);
    app.mouseReleased(event);
    MouseDragEventArgs drag;
    drag.button = button;
    drag.pos = event.pos;
    app.mouseDragged(drag);
}
struct NodeCallbacks : Node {
    static constexpr auto press = static_cast<bool (Node::*)(const MouseEventArgs&)>(&NodeCallbacks::onMousePress);
    static constexpr auto release = static_cast<bool (Node::*)(const MouseEventArgs&)>(&NodeCallbacks::onMouseRelease);
    static constexpr auto drag = static_cast<bool (Node::*)(const MouseDragEventArgs&)>(&NodeCallbacks::onMouseDrag);
};
bool nodeCallbacks(Node& node, MouseButton button) {
    MouseEventArgs event;
    event.button = button;
    event.pos = {12, 34};
    MouseDragEventArgs drag;
    drag.button = button;
    drag.pos = event.pos;
    const bool press = (node.*NodeCallbacks::press)(event);
    const bool release = (node.*NodeCallbacks::release)(event);
    const bool dragged = (node.*NodeCallbacks::drag)(drag);
    return press && release && dragged;
}
}

TC_CORE_TEST_MAIN() {
    getMainThreadId();
    check("no context returns None", getMouseButton() == MouseButton::None);
    tcCoreTest::WindowFixture fixture(701);
    tcCoreTest::InjectedWindow device(*fixture.window, 701);
    auto ownerApp = internal::makeConstructedAppAt<App>(&internal::WindowRequestAccess::context(*fixture.window));
    internal::AppOwnerScope owner(internal::appOwner(*ownerApp));
    std::vector<std::string> warnings;
    auto listener = getLogger().onLog.listen([&](LogEventArgs& event) {
        if (event.level == LogLevel::Warning && event.message.find("(Vec2, int) is deprecated") != std::string::npos)
            warnings.push_back(event.message);
    });
    Node node;
    RectNode rect;
    appCallbacks(*ownerApp, MouseButton::Left);
    nodeCallbacks(node, MouseButton::Left);
    nodeCallbacks(rect, MouseButton::Left);
    nodeCallbacks(*ownerApp, MouseButton::Left);
    check("unmodified App, Node and RectNode defaults do not warn", warnings.empty());

    for (int instance = 0; instance < 2; ++instance) {
        auto app = internal::makeConstructedAppAt<ShortApp>(&internal::WindowRequestAccess::context(*fixture.window));
        ShortNode shortNode;
        for (auto button : {MouseButton::Left, MouseButton::Right, MouseButton::Middle}) {
            appCallbacks(*app, button);
            check("Node short form preserves consumption", nodeCallbacks(shortNode, button));
            const auto expected = std::array<int, 3>{static_cast<int>(button), static_cast<int>(button), static_cast<int>(button)};
            check("App forwards the same int values and position", app->buttons == expected && app->position == Vec2(12, 34));
            check("Node forwards the same int values and position", shortNode.buttons == expected && shortNode.position == Vec2(12, 34));
        }
    }
    const std::array<const char*, 6> kinds{
        "mousePressed", "mouseReleased", "mouseDragged", "onMousePress", "onMouseRelease", "onMouseDrag"};
    check("six warnings across repeated calls and objects", warnings.size() == kinds.size());
    for (int i = 0; i < 6; ++i) {
        const std::string name = kinds[i];
        const std::string args = i % 3 == 2 ? "MouseDragEventArgs" : "MouseEventArgs";
        const std::string expected = std::string(i < 3 ? "[App] " : "[Node] ") + name + "(Vec2, int) is deprecated and will be removed in v1.0. Use " + name + "(const " + args + "& e) and e.button (MouseButton).";
        check("each kind warns exactly once with its matching rich form",
              std::count(warnings.begin(), warnings.end(), expected) == 1);
    }
    ShortRect shortRect;
    auto shortRoot = internal::makeConstructedAppAt<ShortRoot>(&internal::WindowRequestAccess::context(*fixture.window));
    check("RectNode short overrides are still called", nodeCallbacks(shortRect, MouseButton::Right) &&
          shortRect.buttons == std::array<int, 3>{1, 1, 1});
    check("App root short overrides still consume", nodeCallbacks(*shortRoot, MouseButton::Right));
    check("Node warning gates are shared by subclasses", warnings.size() == 6);

    check("new context returns None", getMouseButton() == MouseButton::None);
    auto& window = *fixture.window;
    check("left press accepted", window.injectInput(InputEvent::mousePressed({10, 20}, MouseButton::Left)));
    check("getter is Left", getMouseButton() == MouseButton::Left);
    check("right press accepted", window.injectInput(InputEvent::mousePressed({10, 20}, MouseButton::Right)));
    check("getter keeps last-pressed meaning", getMouseButton() == MouseButton::Right);
    check("right release accepted", window.injectInput(InputEvent::mouseReleased({10, 20}, MouseButton::Right)));
    check("left release accepted", window.injectInput(InputEvent::mouseReleased({10, 20}, MouseButton::Left)));
    check("getter returns None after release", getMouseButton() == MouseButton::None);
    MouseEventArgs received;
    received.button = static_cast<MouseButton>(-1);
    check("received invalid enum still rejected", !window.injectInput(InputEvent::mousePressed(received)));

    EasyCam camera;
    check("typed camera defaults", camera.getOrbitMouseButton() == MouseButton::Left && camera.getPanMouseButton() == MouseButton::Middle);
    check("typed setters chain and store buttons", &camera.setOrbitButton(MouseButton::Right).setPanButton(MouseButton::Left) == &camera &&
          camera.getOrbitMouseButton() == MouseButton::Right && camera.getPanMouseButton() == MouseButton::Left);
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4996)
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    camera.setOrbitButton(1).setPanButton(2);
    check("deprecated int camera bindings remain usable", camera.getOrbitButton() == 1 && camera.getPanButton() == 2);
#if defined(_MSC_VER)
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#endif
    return failures ? 1 : 0;
}
