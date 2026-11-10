#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcInjectedWindowTest.h"
#include <cstdio>
#include <limits>
#include <thread>
#include <type_traits>

using namespace tc;
namespace {
int failures = 0;
void check(const char* label, bool ok) {
    std::printf("inputInjection: %s: %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
std::string order;
struct Probe : RectNode {
    InputSource source = InputSource::Real;
    Vec2 local;
    Probe() { enableEvents(); setPos(10, 10); setSize(200, 200); }
    bool onMousePress(const MouseEventArgs& e) override {
        order += 't'; source = e.source; local = e.pos; return false;
    }
};
struct InputApp : App {
    std::shared_ptr<Probe> probe = std::make_shared<Probe>();
    InputSource source = InputSource::Real;
    bool held = false;
    InputApp() { enableEvents(); addChild(probe); }
    void mousePressed(const MouseEventArgs& e) override {
        order += 'a'; source = e.source;
        held = isMousePressed() && getMouseX() == e.pos.x;
    }
};
static_assert(std::is_aggregate_v<KeyEventArgs> && std::is_aggregate_v<MouseEventArgs> &&
    std::is_aggregate_v<MouseMoveEventArgs> && std::is_aggregate_v<MouseDragEventArgs> &&
    std::is_aggregate_v<ScrollEventArgs> && std::is_aggregate_v<TouchEventArgs>);
// Existing prefix aggregate initialization remains valid.
const KeyEventArgs legacyKey{SAPP_KEYCODE_A, false, true, false, false, false, false};
}

TC_CORE_TEST_MAIN() {
    if constexpr (!internal::supportsSecondaryWindows()) {
        std::puts("SKIP: inputInjection uses the virtual desktop native producer");
        return 0;
    }
    getMainThreadId();
    check("legacy aggregate defaults to Real", legacyKey.source == InputSource::Real);
    Window window, other;
    tcCoreTest::InjectedWindow native(window, 1398), otherNative(other, 1399);
    auto& ctx = internal::WindowRequestAccess::context(window);
    auto* previous = internal::currentWindowCtx();
    internal::currentWindowCtx() = &internal::WindowRequestAccess::context(other);
    auto app = internal::makeConstructedAppAt<InputApp>(&ctx);
    internal::attachCandidate(window, app);
    native.metadata.dpi_scale = 2;
    native.metadata.fb_width = 1280; native.metadata.fb_height = 960;
    auto& hub = window.events();
    InputSource seen = InputSource::Real;
    sapp_event rawEvent{};
    auto raw = hub.rawEvent.listen([&](const sapp_event& e) { order += 'r'; rawEvent = e; });
    auto capture = hub.mousePressed.listen([&](const auto& e) { order += 'i'; seen = e.source; }, -20);
    auto before = hub.mousePressed.listen([](const auto&) { order += 'b'; });
    auto after = hub.mousePressed.listen([](const auto&) { order += 'z'; }, EventPriority::AfterApp);
    MouseMoveEventArgs move;
    MouseDragEventArgs drag;
    MouseEventArgs release;
    ScrollEventArgs scroll;
    KeyEventArgs key;
    TouchEventArgs touch;
    auto moving = hub.mouseMoved.listen([&](const auto& e) { move = e; });
    auto released = hub.mouseReleased.listen([&](const auto& e) { release = e; });
    auto dragging = hub.mouseDragged.listen([&](const auto& e) { drag = e; });
    auto scrolling = hub.mouseScrolled.listen([&](const auto& e) { scroll = e; });
    auto pressingKey = hub.keyPressed.listen([&](const auto& e) {
        internal::AppOwnerScope owner(internal::appOwner(*app));
        key = e; check("key state updated before listeners", isKeyPressed(e.key));
    });
    auto releasingKey = hub.keyReleased.listen([&](const auto& e) {
        internal::AppOwnerScope owner(internal::appOwner(*app));
        key = e; check("key release clears held state", !isKeyPressed(e.key));
    });
    auto touching = hub.touchPressed.listen([&](const auto& e) { touch = e; });
    auto touchMoving = hub.touchMoved.listen([&](const auto& e) { touch = e; });
    auto touchEnding = hub.touchReleased.listen([&](const auto& e) { touch = e; });
    for (bool pixelPerfect : {false, true}) {
        internal::pixelPerfectMode() = pixelPerfect;
        const float basis = pixelPerfect ? 2 : 1;
        check("logical move accepted", window.injectInput(InputEvent::mouseMove({20, 30})));
        check("DPI conversion and injected move source", move.pos == Vec2(20 * basis, 30 * basis) &&
            move.source == InputSource::Injected && rawEvent.mouse_x == 40 && rawEvent.mouse_y == 60);
        order.clear();
        check("replay press accepted", window.injectInput(InputEvent::mousePress({20, 30}), InputSource::Replayed));
        check("raw, priority -20, BeforeApp, App, tree, AfterApp order", order == "ribatz");
        check("source reaches listeners, App and localized tree", seen == InputSource::Replayed &&
            app->source == seen && app->probe->source == seen &&
            app->probe->local == Vec2(20 * basis - 10, 30 * basis - 10));
        check("App sees current pointer and button", app->held);
        check("drag accepted", window.injectInput(InputEvent::mouseMove({25, 35}), InputSource::Replayed));
        check("held button, delta and source survive move converters", drag.source == InputSource::Replayed &&
            drag.delta == Vec2(5 * basis, 5 * basis) && drag.button == MOUSE_BUTTON_LEFT);
        check("wheel accepted", window.injectInput(InputEvent::mouseScroll({1, -2}), InputSource::Replayed));
        check("wheel uses current cursor and unchanged units", scroll.pos == drag.pos &&
            scroll.scroll == Vec2(1, -2) && scroll.source == InputSource::Replayed);
        check("release accepted", window.injectInput(InputEvent::mouseRelease({25, 35})));
        check("mouse release preserves source", release.source == InputSource::Injected);
        check("receiver context restored and other window unchanged", internal::currentWindowCtx() ==
            &internal::WindowRequestAccess::context(other) &&
            !internal::WindowRequestAccess::context(other).mousePressed && internal::WindowRequestAccess::context(other).mouseX == 0);
        {
            internal::AppOwnerScope owner(internal::appOwner(*app));
            check("release clears target held state", !isMousePressed());
        }
    }
    internal::pixelPerfectMode() = false;
    check("key with flags and repeat accepted", window.injectInput(InputEvent::keyPress(SAPP_KEYCODE_A,
        SAPP_MODIFIER_SHIFT | SAPP_MODIFIER_CTRL | SAPP_MODIFIER_ALT | SAPP_MODIFIER_SUPER, true), InputSource::Replayed));
    check("key source, flags and repeat delivered", key.source == InputSource::Replayed && key.isRepeat &&
        key.shift && key.ctrl && key.alt && key.super);
    check("key release accepted", window.injectInput(InputEvent::keyRelease(SAPP_KEYCODE_A)));
    check("key release source delivered", key.source == InputSource::Injected && !key.isRepeat);
    window.injectInput(InputEvent::keyPress(KEY_LEFT_SHIFT));
    check("modifier key press derives its event flag", key.shift && ctx.keysPressed.contains(KEY_LEFT_SHIFT));
    window.injectInput(InputEvent::keyPress(SAPP_KEYCODE_A));
    check("ordinary key inherits target held modifiers", key.shift);
    window.injectInput(InputEvent::mouseMove({20, 30}));
    check("pointer inherits target held modifiers", move.shift);
    window.injectInput(InputEvent::keyRelease(SAPP_KEYCODE_A));
    window.injectInput(InputEvent::keyRelease(KEY_LEFT_SHIFT));
    check("modifier release clears flag and held state", !key.shift && !ctx.keysPressed.contains(KEY_LEFT_SHIFT));
    {
        auto consumer = hub.mousePressed.listen([](auto& e) { e.consumed = true; }, -20);
        order.clear();
        check("consumption still reports accepted", window.injectInput(InputEvent::mousePress({20, 30})));
        check("consumption stops later typed delivery", order == "ri");
        check("consumption still updates held state", ctx.mousePressed);
        window.injectInput(InputEvent::mouseRelease({20, 30}));
    }
    setTouchAsMouse(true);
    TouchEventArgs points;
    points.numTouches = 1; points.touches[0] = {42, 20, 30, 1, true};
    check("touch press accepted", window.injectInput(InputEvent::touchPress(points), InputSource::Replayed));
    check("touch source and logical coordinates", touch.source == InputSource::Replayed && touch.x() == 20 &&
        touch.id() == 42 && app->probe->source == InputSource::Replayed);
    points.touches[0].x = 25;
    check("touch move accepted", window.injectInput(InputEvent::touchMove(points), InputSource::Replayed));
    check("touch-derived drag preserves source", drag.source == InputSource::Replayed && touch.x() == 25);
    points.cancelled = true;
    check("touch cancellation accepted", window.injectInput(InputEvent::touchRelease(points), InputSource::Replayed));
    check("touch cancellation clears mouse state", touch.cancelled && touch.source == InputSource::Replayed && !ctx.mousePressed);
    check("empty touch packets rejected", !window.injectInput(InputEvent::touchPress(TouchEventArgs{})) &&
        !window.injectInput(InputEvent::touchMove(TouchEventArgs{})) &&
        !window.injectInput(InputEvent::touchRelease(TouchEventArgs{})));
    native.metadata.dpi_scale = 2;
    setTouchAsMouse(false);
    // Native delivery still defaults to Real.
    sapp_event physical{}; physical.type = SAPP_EVENTTYPE_MOUSE_MOVE; physical.mouse_x = 40; physical.mouse_y = 60;
    native.send(physical);
    check("real input remains Real", move.source == InputSource::Real);
    const auto eventCountBefore = order.size();
    bool workerAccepted = true;
    std::thread worker([&] { workerAccepted = window.injectInput(InputEvent::keyPress(SAPP_KEYCODE_B)); }); worker.join();
    check("worker call rejected without delivery", !workerAccepted && order.size() == eventCountBefore);
    check("native source cannot be forged", !window.injectInput(InputEvent::keyPress(SAPP_KEYCODE_B), InputSource::Real));
    check("invalid key rejected", !window.injectInput(InputEvent::keyPress(-1)));
    check("oversized key rejected before enum conversion", !window.injectInput(InputEvent::keyPress(std::numeric_limits<int>::max())));
    check("oversized button rejected before enum conversion", !window.injectInput(InputEvent::mousePress({20, 30}, std::numeric_limits<int>::max())));
    check("invalid pointer rejected", !window.injectInput(InputEvent::mouseMove({std::numeric_limits<float>::infinity(), 0})));
    points.numTouches = TouchEventArgs::MAX_TOUCHES + 1;
    check("invalid touch count rejected", !window.injectInput(InputEvent::touchPress(points)));
    mcp::setToolLevel(mcp::ToolLevel::Control);
    const auto reply = json::parse(mcp::internal::Server::instance().processMessage(json{
        {"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"},
        {"params", {{"name", "tc_key_press"}, {"arguments", {{"window", 1398}, {"key", SAPP_KEYCODE_B}}}}}}.dump()));
    check("MCP uses shared input route", reply.contains("result") && !reply["result"].value("isError", false) &&
        key.key == SAPP_KEYCODE_B && key.source == InputSource::Injected && ctx.keysPressed.contains(SAPP_KEYCODE_B));
    window.injectInput(InputEvent::keyRelease(SAPP_KEYCODE_B));
    std::string nested;
    auto nesting = hub.keyPressed.listen([&](const KeyEventArgs& e) {
        nested += e.key == SAPP_KEYCODE_A ? 'A' : 'B';
        if (e.key == SAPP_KEYCODE_A) window.injectInput(InputEvent::keyPress(SAPP_KEYCODE_B));
    });
    auto remaining = hub.keyPressed.listen([&](const KeyEventArgs& e) { nested += e.key == SAPP_KEYCODE_A ? 'a' : 'b'; });
    window.injectInput(InputEvent::keyPress(SAPP_KEYCODE_A));
    check("nested injection completes before outer listeners resume", nested == "ABba");
    window.injectInput(InputEvent::keyRelease(SAPP_KEYCODE_A)); window.injectInput(InputEvent::keyRelease(SAPP_KEYCODE_B));
    window.close();
    check("closing target rejects input", !window.injectInput(InputEvent::keyPress(SAPP_KEYCODE_B)));
    internal::currentWindowCtx() = previous;
    return failures ? 1 : 0;
}
