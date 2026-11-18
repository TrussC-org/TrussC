#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcWindowTest.h"
#include "../../common/tcInjectedWindowTest.h"
#include "../../common/tcDisplayTest.h"
#include <cstdio>
#include <cstring>
#include <limits>
using namespace tc;
namespace {
int checks = 0, failures = 0;
void check(const char* label, bool ok) {
    ++checks;
    if (!ok) { ++failures; std::printf("inputInjection: FAIL: %s\n", label); }
}
template<class T> void provenance(const T& e, const char* tag) {
    check("injected and destination tag", e.injected && e.sourceTag == tag);
}
template<class T> concept HasProvenance = requires(T e) { e.injected; e.sourceTag; };
static_assert(!HasProvenance<DragDropEventArgs> && !HasProvenance<ClipboardPastedEventArgs> &&
              !HasProvenance<BackPressedEventArgs> && !HasProvenance<ConsoleEventArgs>);
struct Observer {
    std::shared_ptr<App> app;
    internal::AppOwnerScope owner;
    explicit Observer(Window& w)
        : app(internal::makeConstructedAppAt<App>(&internal::WindowRequestAccess::context(w))),
          owner(internal::appOwner(*app)) {}
};
struct TextApp : App {
    std::vector<int>* order = nullptr;
    CharEventArgs character;
    DragDropEventArgs drop;
    int legacyCalls = 0;
    void charInput(const CharEventArgs& e) override { character = e; order->push_back(2); }
    void filesDropped(const DragDropEventArgs& e) override {
        drop = e;
        App::filesDropped(e);
    }
    void filesDropped(const std::vector<std::filesystem::path>& files) override {
        check("legacy callback receives listener edits", files.size() == 1 && files[0].u8string() == u8"編集済み😀.png");
        ++legacyCalls;
    }
};
void textAndDrop(bool pixels, float dpi) {
    tcCoreTest::WindowFixture fixture(711), peerFixture(712);
    auto& w = *fixture.window;
    tcCoreTest::InjectedWindow device(w, 711), peerDevice(*peerFixture.window, 712);
    device.metadata.dpi_scale = dpi;
    internal::pixelPerfectMode() = pixels;
    auto app = internal::makeConstructedAppAt<TextApp>(&internal::WindowRequestAccess::context(w));
    internal::attachCandidate(w, app);
    internal::applyPendingAppForTests(w);
    internal::AppOwnerScope owner(internal::appOwner(*app));
    std::vector<int> order;
    app->order = &order;
    CharEventArgs received;
    int peerChars = 0;
    auto peerChar = peerFixture.window->events().charInput.listen([&](CharEventArgs&) { ++peerChars; });
    auto before = w.events().charInput.listen([&](CharEventArgs& e) { order.push_back(1); received = e; });
    auto after = w.events().charInput.listen([&](CharEventArgs&) { order.push_back(3); }, EventPriority::AfterApp);
    check("character accepted", w.injectInput(InputEvent::charInput(U'é', {true, true, true, true}, true), "text"));
    check("character order and target", order == std::vector<int>{1, 2, 3} && peerChars == 0);
    check("character payload", received.codepoint == U'é' && received.isRepeat && received.shift && received.ctrl && received.alt && received.super);
    provenance(app->character, "text");
    received.consumed = true;
    order.clear();
    check("received character accepted", w.injectInput(InputEvent::charInput(received), "relay"));
    check("character resend reset and fresh tag", !app->character.consumed && app->character.sourceTag == "relay" && order.size() == 3);
    auto consume = w.events().charInput.listen([](CharEventArgs& e) { e.consumed = true; }, -20);
    order.clear();
    check("consumed character accepted", w.injectInput(InputEvent::charInput('A')));
    check("consumed before app", order.empty());
    consume.disconnect();
    sapp_event event{};
    event.type = SAPP_EVENTTYPE_CHAR;
    event.char_code = U'日';
    event.key_repeat = true;
    event.modifiers = SAPP_MODIFIER_SHIFT;
    check("device character accepted", device.send(event));
    check("device character metadata", !app->character.injected && app->character.sourceTag.empty() && app->character.isRepeat && app->character.shift);
    check("invalid scalar rejected", !w.injectInput(InputEvent::charInput(0xd800)) && !w.injectInput(InputEvent::charInput(0x110000)));
    int rawDrops = 0, peerDrops = 0;
    auto raw = w.events().rawEvent.listen([&](const sapp_event& e) { if (e.type == SAPP_EVENTTYPE_FILES_DROPPED) ++rawDrops; });
    auto peer = peerFixture.window->events().filesDropped.listen([&](DragDropEventArgs&) { ++peerDrops; });
    auto dropped = w.events().filesDropped.listen([&](DragDropEventArgs& e) {
        check("owned drop and exact position", e.files.size() == 2 && e.files[0].u8string() == u8"画像/日本語.png" && e.files[1].u8string() == u8"emoji/😀.png" && e.x == 20.25f && e.y == 30.5f);
        e.files = {fs::path(u8"編集済み😀.png")};
        e.x = 42;
    });
    DragDropEventArgs args;
    args.files = {fs::path(u8"画像/日本語.png"), fs::path(u8"emoji/😀.png")}; args.x = 20.25f; args.y = 30.5f;
    auto owned = InputEvent::filesDropped(args);
    args.files.clear();
    auto& ctx = internal::WindowRequestAccess::context(w);
    check("drop starts with nonzero cursor", w.injectInput(InputEvent::mouseMoved({7, 9})) &&
          Vec2(ctx.mouseX, ctx.mouseY) == Vec2(7, 9));
    const auto cursor = Vec2(ctx.mouseX, ctx.mouseY);
    check("owned drop accepted", w.injectInput(owned, "unexposed"));
    check("rich and legacy callbacks reached", app->legacyCalls == 1 && app->drop.x == 42);
    check("drop target and no raw event", rawDrops == 0 && peerDrops == 0);
    check("drop leaves cursor unchanged", cursor == Vec2(ctx.mouseX, ctx.mouseY));
    check("paths builder accepted", w.injectInput(InputEvent::filesDropped({fs::path(u8"画像/日本語.png"), fs::path(u8"emoji/😀.png")}, {20.25f, 30.5f})));
    check("invalid drop position rejected", !w.injectInput(InputEvent::filesDropped({}, {std::numeric_limits<float>::quiet_NaN(), 0})));
    internal::pixelPerfectMode() = false;
}
void exercise(bool pixels, float dpi) {
    tcCoreTest::WindowFixture fixture(701), peerFixture(702);
    auto& w = *fixture.window;
    auto& peer = *peerFixture.window;
    tcCoreTest::InjectedWindow native(w, 701), peerNative(peer, 702);
    native.metadata.dpi_scale = dpi;
    peerNative.metadata.dpi_scale = dpi;
    internal::pixelPerfectMode() = pixels;
    Observer observer(w);
    int presses = 0, drags = 0, moves = 0, releases = 0, keys = 0, scrolls = 0, touches = 0;
    MouseEventArgs received;
    auto press = w.events().mousePressed.listen([&](MouseEventArgs& e) {
        ++presses;
        provenance(e, "gesture");
        check("press coordinates and modifiers", e.pos == Vec2(20, 30) && e.globalPos == e.pos &&
              e.x == 20 && e.y == 30 && e.button == MOUSE_BUTTON_LEFT && e.shift && e.ctrl && e.alt && e.super);
        received = e;
        received.consumed = true;
        received.pos = Vec2(40, 50);
        received.x = -1; // builders use the canonical position
    });
    auto drag = w.events().mouseDragged.listen([&](MouseDragEventArgs& e) {
        ++drags;
        provenance(e, "gesture");
        check("held move becomes drag", e.pos == Vec2(80, 30) && e.delta == Vec2(60, 0) && e.button == MOUSE_BUTTON_LEFT && e.ctrl);
        auto raw = internal::toRaw(e);
        provenance(internal::toDragArgs(raw), "gesture");
    });
    auto release = w.events().mouseReleased.listen([&](MouseEventArgs& e) {
        ++releases;
        provenance(e, "gesture");
        check("release coordinates", e.pos == Vec2(80, 30) && e.ctrl);
    });
    auto move = w.events().mouseMoved.listen([&](MouseMoveEventArgs& e) {
        ++moves;
        provenance(e, "move");
        check("plain move coordinates and modifiers", e.pos == Vec2(90, 40) && e.delta == Vec2(10, 10) && e.alt);
        provenance(internal::toMoveArgs(internal::toRaw(e)), "move");
    });
    const InputModifiers mods{true, true, true, true};
    check("press accepted", w.injectInput(InputEvent::mousePressed({20, 30}, MouseButton::Left, mods), "gesture"));
    check("move accepted", w.injectInput(InputEvent::mouseMoved({80, 30}, {.ctrl = true}), "gesture"));
    check("release accepted", w.injectInput(InputEvent::mouseReleased({80, 30}, MouseButton::Left, {.ctrl = true}), "gesture"));
    MouseMoveEventArgs movement;
    movement.pos = {90, 40};
    movement.alt = true;
    check("received move accepted", w.injectInput(InputEvent::mouseMoved(movement), "move"));
    check("one notification each", presses == 1 && drags == 1 && releases == 1 && moves == 1);
    press.disconnect(); drag.disconnect(); release.disconnect(); move.disconnect();
    int braceMoves = 0;
    auto braceMove = w.events().mouseMoved.listen([&](MouseMoveEventArgs& e) {
        check("brace move position", e.pos == Vec2(80, 30));
        ++braceMoves;
    });
    check("brace move accepted", w.injectInput(InputEvent::mouseMoved({80, 30}), "move"));
    check("brace move delivered", braceMoves == 1);
    braceMove.disconnect();
    check("restore received move accepted", w.injectInput(InputEvent::mouseMoved(movement), "move"));

    {
        Observer peerObserver(peer);
        auto relay = peer.events().mousePressed.listen([&](MouseEventArgs& e) {
            provenance(e, "relay");
            check("relay has fresh consumption and canonical payload", !e.consumed && e.pos == Vec2(40, 50) && e.ctrl);
            ++presses;
        });
        check("relay accepted", peer.injectInput(InputEvent::mousePressed(received), "relay"));
        check("relay delivered", presses == 2);
        peer.injectInput(InputEvent::mouseReleased(received), "relay");
    }
    auto keyDown = w.events().keyPressed.listen([&](KeyEventArgs& e) {
        provenance(e, "key");
        check("key modifiers and repeat", e.key == 'A' && e.ctrl && e.isRepeat && !e.consumed);
        ++keys;
    });
    auto keyUp = w.events().keyReleased.listen([&](KeyEventArgs& e) {
        provenance(e, "key");
        check("key release modifiers", e.key == 'A' && e.ctrl && !e.isRepeat);
        ++keys;
    });
    KeyEventArgs key;
    key.key = 'A'; key.ctrl = true; key.isRepeat = true; key.consumed = true;
    check("received key press accepted", w.injectInput(InputEvent::keyPressed(key), "key"));
    check("received key release accepted", w.injectInput(InputEvent::keyReleased(key), "key"));
    check("key pair delivered", keys == 2);
    keyDown.disconnect(); keyUp.disconnect();
    auto scroll = w.events().mouseScrolled.listen([&](ScrollEventArgs& e) {
        provenance(e, "wheel");
        check("scroll units and cursor preserved", e.scroll == Vec2(2, -3) && e.pos == Vec2(90, 40) && e.super);
        ++scrolls;
    });
    ScrollEventArgs wheel;
    wheel.scroll = {2, -3}; wheel.super = true;
    check("received scroll accepted", w.injectInput(InputEvent::mouseScrolled(wheel), "wheel"));
    check("scroll delivered", scrolls == 1);
    scroll.disconnect();
    auto braceScroll = w.events().mouseScrolled.listen([&](ScrollEventArgs& e) {
        check("brace scroll amount and cursor", e.scroll == Vec2(0, -1) && e.pos == Vec2(90, 40));
        ++scrolls;
    });
    check("brace scroll accepted", w.injectInput(InputEvent::mouseScrolled({0, -1}), "wheel"));
    check("brace scroll delivered", scrolls == 2);
    auto touchCheck = [&](TouchEventArgs& e) {
        provenance(e, "touch");
        check("touch canonical payload", e.touches.size() == 1 && e.touches.front().id == 17 && e.touches.front().x == 25 &&
              e.touches.front().y == 35 && e.touches.front().changed && e.touches.front().pressure == 0.5f);
        ++touches;
    };
    auto touchDown = w.events().touchPressed.listen(touchCheck);
    auto touchMove = w.events().touchMoved.listen(touchCheck);
    auto touchUp = w.events().touchReleased.listen([&](TouchEventArgs& e) { touchCheck(e); check("touch cancellation", e.cancelled); });
    TouchEventArgs touch;
    touch.touches.push_back({17, 25, 35, 0.5f, true});
    check("touch press accepted", w.injectInput(InputEvent::touchPressed(touch), "touch"));
    check("touch move accepted", w.injectInput(InputEvent::touchMoved(touch), "touch"));
    touch.cancelled = true;
    check("touch release accepted", w.injectInput(InputEvent::touchReleased(touch), "touch"));
    check("touch sequence delivered", touches == 3);
    int nativeCount = 0;
    auto real = w.events().keyPressed.listen([&](KeyEventArgs& e) {
        check("native provenance defaults", !e.injected && e.sourceTag.empty()); ++nativeCount;
    });
    sapp_event event{};
    event.type = SAPP_EVENTTYPE_KEY_DOWN; event.key_code = SAPP_KEYCODE_B;
    check("native hook accepted", native.send(event));
    check("native hook delivered", nativeCount == 1);
    real.disconnect();
    int mcpCount = 0;
    auto mcpKey = w.events().keyPressed.listen([&](KeyEventArgs& e) { provenance(e, "mcp"); ++mcpCount; });
    auto mcpMove = w.events().mouseMoved.listen([&](MouseMoveEventArgs& e) { provenance(e, "mcp"); ++mcpCount; });
    mcp::setToolLevel(mcp::ToolLevel::Control);
    auto tool = [&](const char* name, json args) {
        args["window"] = 701;
        auto reply = json::parse(mcp::internal::Server::instance().processMessage(json{
            {"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"},
            {"params", {{"name", name}, {"arguments", args}}}}.dump()));
        check("MCP accepted", reply.contains("result") && !reply["result"].value("isError", false));
    };
    tool("tc_key_press", {{"key", 'C'}});
    tool("tc_key_release", {{"key", 'C'}});
    tool("tc_mouse_move", {{"x", 100}, {"y", 100}});
    check("MCP input delivered", mcpCount == 2);
    check("invalid key rejected", !w.injectInput(InputEvent::keyPressed(-1)));
    MouseEventArgs invalidButton;
    invalidButton.button = -1;
    check("invalid received button rejected", !w.injectInput(InputEvent::mousePressed(invalidButton)));
    check("invalid button rejected", !w.injectInput(InputEvent::mousePressed({0, 0}, MouseButton::None)));
    check("nonfinite coordinates rejected", !w.injectInput(InputEvent::mouseMoved({std::numeric_limits<float>::infinity(), 0})));
    touch.touches.resize(TouchEventArgs::MAX_TOUCHES + 1);
    check("oversized touch rejected", !w.injectInput(InputEvent::touchPressed(touch)));
    native.ready = false;
    check("unready native target rejected", !w.injectInput(InputEvent::keyPressed('D')));
    internal::WindowRequestAccess::unbind(w);
    check("closed window rejected", !w.injectInput(InputEvent::keyPressed('D')));
    internal::pixelPerfectMode() = false;
}
// Exercise both injected input and the platform-neutral sokol translation.
void activeTouches() {
    tcCoreTest::WindowFixture fixture(721);
    auto& w = *fixture.window;
    tcCoreTest::InjectedWindow native(w, 721);
    Observer observer(w);
    setTouchAsMouse(false);
    TouchEventArgs expected;
    int delivered = 0;
    auto inspect = [&](TouchEventArgs& e) {
        ++delivered;
        check("active touch count", e.touches.size() == expected.touches.size());
        std::size_t index = 0;
        for (const auto& point : e.touches) {
            if (index >= expected.touches.size()) {
                check("range-for excludes inactive slots", false);
                break;
            }
            const auto& wanted = expected.touches.at(index++);
            check("backend order and payload", point.id == wanted.id && point.x == wanted.x &&
                  point.y == wanted.y && point.changed == wanted.changed && point.pressure == wanted.pressure);
        }
        check("range-for visits exactly active points", index == expected.touches.size());
        check("front conveniences and empty defaults", e.x() == expected.x() && e.y() == expected.y() && e.id() == expected.id());
        if (e.touches.empty()) check("empty conveniences are zero", e.x() == 0 && e.y() == 0 && e.id() == 0);
        else {
            const auto& first = expected.touches.front();
            check("conveniences use first unchanged point", !first.changed && e.x() == first.x && e.y() == first.y && e.id() == first.id);
        }
    };
    auto pressed = w.events().touchPressed.listen(inspect);
    auto moved = w.events().touchMoved.listen(inspect);
    auto released = w.events().touchReleased.listen(inspect);
    for (int count : {0, 1, 3, TouchEventArgs::MAX_TOUCHES}) {
        expected.touches.clear();
        sapp_event packet{};
        packet.type = SAPP_EVENTTYPE_TOUCHES_BEGAN;
        packet.num_touches = count;
        for (int i = 0; i < count; ++i) {
            // Descending IDs make accidental sorting observable.
            expected.touches.push_back({100 - i, 10.0f + i, 20.0f + i, 1.0f, i != 0});
            packet.touches[i] = {static_cast<uintptr_t>(100 - i), 10.0f + i, 20.0f + i, SAPP_ANDROIDTOOLTYPE_FINGER, i != 0};
        }
        const int before = delivered;
        // The builder owns its vector independently of subsequent source edits.
        auto source = expected;
        auto input = InputEvent::touchPressed(source);
        source.touches.clear();
        check("active touch press injected", w.injectInput(input));
        check("active touch move injected", w.injectInput(InputEvent::touchMoved(expected)));
        check("active touch release injected", w.injectInput(InputEvent::touchReleased(expected)));
        check("backend touch translated", native.send(packet));
        check("every touch event delivered", delivered == before + 4);
    }
    sapp_event packet{};
    packet.type = SAPP_EVENTTYPE_TOUCHES_BEGAN;
    packet.num_touches = 3;
    expected.touches.resize(3);
    for (int i = 0; i < packet.num_touches; ++i) {
        auto& point = expected.touches[i];
        point.pressure = TouchPoint{}.pressure;
        packet.touches[i] = {static_cast<uintptr_t>(point.id), point.x, point.y, SAPP_ANDROIDTOOLTYPE_FINGER, point.changed};
    }
    expected.touches[0].pressure = 0.375f;
    TouchEventArgs pressureSource;
    pressureSource.touches.push_back(expected.touches[0]);
    const int before = delivered;
    check("short pressure source accepted", internal::injectWindowEvent(w, &packet, false, "touch", &pressureSource, nullptr));
    check("short pressure source delivers all native points", delivered == before + 1);
    setTouchAsMouse(true);
}
// Optional real-host check: desktop and sole-surface platforms must deliver
// synchronously inside setup(), entered on the first update before update().
bool nativeCompleted = false;
struct SetupInjectionApp : App {
    int presses = 0, mcpEvents = 0;
    std::shared_ptr<Window> target;
    EventListener mouse, key, moved, released, scrolled;
    void setup() override {
        target = internal::WindowRequestAccess::retainHostOwner(*getWindow());
        mouse = getWindow()->events().mousePressed.listen([&](MouseEventArgs& e) {
            check("native host injection provenance", e.injected && (e.sourceTag == "tuio" || e.sourceTag == "mcp"));
            if (e.sourceTag == "tuio") ++presses;
            else ++mcpEvents;
        });
        check("native setup injection accepted", getWindow()->injectInput(InputEvent::mousePressed({20, 30}, MouseButton::Left), "tuio"));
        check("native setup injection delivered synchronously", presses == 1);
        getWindow()->injectInput(InputEvent::mouseReleased({20, 30}, MouseButton::Left), "tuio");
        std::printf("inputInjection setup: %d press, %d failures\n", presses, failures);
    }
    void update() override {
        check("native first update injection accepted", getWindow()->injectInput(InputEvent::mousePressed({20, 30}, MouseButton::Left), "tuio"));
        check("native setup and update each deliver once", presses == 2);
        getWindow()->injectInput(InputEvent::mouseReleased({20, 30}, MouseButton::Left), "tuio");
        auto onMcp = [&](auto& e) { provenance(e, "mcp"); ++mcpEvents; };
        key = getWindow()->events().keyPressed.listen(onMcp);
        moved = getWindow()->events().mouseMoved.listen(onMcp);
        released = getWindow()->events().mouseReleased.listen(onMcp);
        scrolled = getWindow()->events().mouseScrolled.listen(onMcp);
        mcp::setToolLevel(mcp::ToolLevel::Control);
        auto tool = [&](const char* name, json args) {
            auto reply = json::parse(mcp::internal::Server::instance().processMessage(json{
                {"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"},
                {"params", {{"name", name}, {"arguments", args}}}}.dump()));
            check("native MCP accepted", reply.contains("result") && !reply["result"].value("isError", false));
        };
        tool("tc_key_press", {{"key", 'C'}});
        tool("tc_key_release", {{"key", 'C'}});
        tool("tc_mouse_move", {{"x", 40}, {"y", 50}});
        tool("tc_mouse_press", {{"x", 40}, {"y", 50}, {"button", 0}});
        tool("tc_mouse_release", {{"x", 40}, {"y", 50}, {"button", 0}});
        tool("tc_mouse_scroll", {{"dx", 0}, {"dy", 1}});
        check("native MCP key and mouse callbacks delivered", mcpEvents == 5);
        nativeCompleted = true;
        exitApp();
    }
    void cleanup() override {
        // App::getWindow() is detached before cleanup; keep the target for
        // checking rejection on the retained Window during teardown.
        check("native cleanup injection rejected", !target->injectInput(InputEvent::keyPressed('D')));
        target.reset();
    }
};
} // namespace
TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--native-check") == 0) {
        WindowSettings settings;
        settings.setSize(160, 100).setHighDpi(false).setSampleCount(1).setSwapInterval(0);
        return tcCoreTest::runDisplayTest<SetupInjectionApp>(settings, failures, [] {
            check("native input check completed", nativeCompleted);
            std::printf("inputInjection native: %d checks, %d failures\n", checks, failures);
        });
    }
    for (bool pixels : {false, true}) for (float dpi : {1.0f, 2.0f}) { exercise(pixels, dpi); textAndDrop(pixels, dpi); }
    activeTouches();
    Window unbound;
    check("unbound window rejected", !unbound.injectInput(InputEvent::keyPressed('A')));
    std::printf("inputInjection: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
