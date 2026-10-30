#include <TrussC.h>
#include "../../common/tcInjectedWindowTest.h"
#include "../../common/tcWindowTest.h"
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <cstdio>
#include <map>
#include <thread>
using namespace tc;

namespace {
int failures = 0;
void check(const char* label, bool ok) {
    std::printf("appEventDelivery: %s: %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
std::map<std::string, std::string> order;
std::vector<EventListener> listeners;
bool treeConsumes = false, releaseConsumes = false, rootConsumes = false, dragConsumes = true;
int updates = 0, draws = 0, exits = 0, touches = 0;
bool tested = false;
int postClearFrames = 0;
int resizeExtent = 100;

// Stages are installed on both sides of the actual launcher binding.
void stages(bool before) {
    auto bind = [&](auto& event, const char* name) {
        auto add = [&](int priority, char stage) {
            listeners.push_back(event.listen([name, stage](auto&&...) { order[name] += stage; }, priority));
        };
        if (before) {
            add(-20, 'c');
            listeners.push_back(event.listen([name](auto&&...) { order[name] += 'd'; }));
            add(50, 'p'); add(100, 'b');
        }
        else { add(100, 'l'); add(EventPriority::AfterApp, 'z'); }
    };
#define STAGE(name) bind(events().name, #name)
    STAGE(update); STAGE(draw); STAGE(exit);
    STAGE(keyPressed); STAGE(keyReleased);
    STAGE(mousePressed); STAGE(mouseReleased); STAGE(mouseMoved); STAGE(mouseDragged); STAGE(mouseScrolled);
    STAGE(touchPressed); STAGE(touchMoved); STAGE(touchReleased);
    STAGE(windowResized); STAGE(filesDropped);
#undef STAGE
}
bool readResizeMetadata(sapp_window, void* user, sapp_tc_window_metadata& out) {
    out = *static_cast<sapp_tc_window_metadata*>(user); return true;
}
void feed(sapp_event_type type, float x = 20, float y = 20, unsigned int modifiers = 0) {
    sapp_event e{};
    e.type = type; e.key_code = SAPP_KEYCODE_A;
    e.mouse_button = SAPP_MOUSEBUTTON_LEFT;
    e.mouse_x = x; e.mouse_y = y; e.modifiers = modifiers;
    e.scroll_y = 1;
    auto& binding = internal::WindowRequestAccess::binding(*internal::resolveWindow());
    const auto* originalOperations = binding.operations;
    void* originalUser = binding.user;
    auto operations = originalOperations ? *originalOperations : internal::WindowNativeOperations{};
    auto metadata = binding.metadata;
    if (type == SAPP_EVENTTYPE_RESIZED) {
        // Real input refreshes the actual native extent between these injected
        // events. Alternate explicit revisions, independent of that refresh.
        resizeExtent = resizeExtent == 96 ? 100 : 96;
        metadata.fb_width = metadata.fb_height = resizeExtent;
        metadata.dpi_scale = internal::windowApiContext().dpiScale;
        if (metadata.sample_count <= 0) metadata.sample_count = internal::windowApiContext().swapchainTarget.sampleCount;
        if (metadata.color_format == SAPP_PIXELFORMAT_NONE) metadata.color_format = SAPP_PIXELFORMAT_RGBA8;
        if (metadata.depth_format == SAPP_PIXELFORMAT_NONE) metadata.depth_format = SAPP_PIXELFORMAT_DEPTH_STENCIL;
        operations.readMetadata = readResizeMetadata;
        binding.operations = &operations; binding.user = &metadata;
    }
    if (type >= SAPP_EVENTTYPE_TOUCHES_BEGAN && type <= SAPP_EVENTTYPE_TOUCHES_CANCELLED) {
        e.num_touches = 1; e.touches[0].identifier = 42;
        e.touches[0].pos_x = x; e.touches[0].pos_y = y; e.touches[0].changed = true;
    }
    tcCoreTest::dispatchMainEvent(&e);
    binding.operations = originalOperations; binding.user = originalUser;
}
void expect(const char* name, const char* expected) {
    if (order[name] != expected)
        std::printf("  %s: got %s expected %s\n", name, order[name].c_str(), expected);
    check(name, order[name] == expected);
    order[name].clear();
}
json tool(const char* name, const json& args) {
    auto reply = json::parse(mcp::internal::Server::instance().processMessage(json{
        {"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"},
        {"params", {{"name", name}, {"arguments", args}}}}.dump()));
    check("MCP tool accepted", reply.contains("result") && !reply["result"].value("isError", false));
    return reply;
}
struct Probe : RectNode {
    Probe() { setPos(10, 10); setSize(60, 60); enableEvents(); }
    void update() override { order["update"] += 't'; }
    void draw() override { order["draw"] += 't'; }
    bool onMousePress(const MouseEventArgs&) override { order["mousePressed"] += 't'; return treeConsumes; }
    bool onMouseRelease(const MouseEventArgs&) override { order["mouseReleased"] += 't'; return releaseConsumes; }
    bool onMouseDrag(const MouseDragEventArgs&) override { order["mouseDragged"] += 't'; return treeConsumes && dragConsumes; }
    bool onMouseMove(const MouseMoveEventArgs&) override { order[internal::currentWindowContext().mouseButton < 0 ? "mouseMoved" : "mouseDragged"] += 't'; return treeConsumes; }
    bool onMouseScroll(const ScrollEventArgs&) override { order["mouseScrolled"] += 't'; return treeConsumes; }
    bool onKeyPress(const KeyEventArgs&) override { order["keyPressed"] += 't'; return treeConsumes; }
    bool onKeyRelease(const KeyEventArgs&) override { order["keyReleased"] += 't'; return treeConsumes; }
};
void inputChecks(); void cameraChecks(); void clearChecks();
bool exitOrderMode = false;
struct DeliveryApp : App {
    DeliveryApp() {
        stages(true); stages(false);
        listeners.push_back(events().afterFrame.listen([] {
            if (exitOrderMode) { exitApp(); return; }
            if (!tested) {
                expect("update", "cdpatblz"); expect("draw", "cdpatblz");
                inputChecks(); cameraChecks(); clearChecks(); tested = true;
            } else {
                expect("update", "at"); expect("draw", "at");
                if (++postClearFrames == 2) exitApp();
            }
        }));
    }
    void setup() override { enableEvents(); addChild(std::make_shared<Probe>()); }
    void update() override { ++updates; order["update"] += 'a'; }
    void draw() override { ++draws; order["draw"] += 'a'; }
    void exit() override { ++exits; order["exit"] += 'a'; }
    void keyPressed(const KeyEventArgs& e) override { order["keyPressed"] += 'a'; check("App observes held key before press", isKeyPressed(e.key)); }
    void keyReleased(const KeyEventArgs& e) override { order["keyReleased"] += 'a'; check("App observes cleared key before release", !isKeyPressed(e.key)); }
    void mousePressed(const MouseEventArgs&) override { order["mousePressed"] += 'a'; }
    void mouseReleased(const MouseEventArgs&) override { order["mouseReleased"] += 'a'; }
    void mouseMoved(const MouseMoveEventArgs& e) override { order["mouseMoved"] += 'a'; lastMove = e; }
    void mouseDragged(const MouseDragEventArgs&) override { order["mouseDragged"] += 'a'; }
    void mouseScrolled(const ScrollEventArgs&) override { order["mouseScrolled"] += 'a'; }
    void touchPressed(const TouchEventArgs&) override { ++touches; order["touchPressed"] += 'a'; }
    void touchMoved(const TouchEventArgs&) override { ++touches; order["touchMoved"] += 'a'; }
    void touchReleased(const TouchEventArgs&) override { ++touches; order["touchReleased"] += 'a'; }
    void windowResized(int, int) override { order["windowResized"] += 'a'; }
    void filesDropped(const std::vector<std::string>&) override { order["filesDropped"] += 'a'; }
    using App::onMousePress;
    bool onMousePress(Vec2, int) override { return rootConsumes; }
    MouseMoveEventArgs lastMove;
};
void inputChecks() {
    order.clear();
    auto keyDown = events().keyPressed.listen([](auto& e) { check("listener observes held key", isKeyPressed(e.key)); });
    auto keyUp = events().keyReleased.listen([](auto& e) { check("listener observes released key", !isKeyPressed(e.key)); });
    feed(SAPP_EVENTTYPE_KEY_DOWN); expect("keyPressed", "cdpatblz");
    feed(SAPP_EVENTTYPE_KEY_UP); expect("keyReleased", "cdpatblz");
    feed(SAPP_EVENTTYPE_MOUSE_DOWN); expect("mousePressed", "cdpatblz");
    feed(SAPP_EVENTTYPE_MOUSE_MOVE, 30, 30, SAPP_MODIFIER_LMB); expect("mouseDragged", "cdpatblz");
    feed(SAPP_EVENTTYPE_MOUSE_UP); expect("mouseReleased", "cdpatblz");
    feed(SAPP_EVENTTYPE_MOUSE_MOVE); expect("mouseMoved", "cdpatblz");
    feed(SAPP_EVENTTYPE_MOUSE_SCROLL); expect("mouseScrolled", "cdpatblz");
    setTouchAsMouse(false);
    feed(SAPP_EVENTTYPE_TOUCHES_BEGAN); expect("touchPressed", "cdpablz");
    feed(SAPP_EVENTTYPE_TOUCHES_MOVED); expect("touchMoved", "cdpablz");
    feed(SAPP_EVENTTYPE_TOUCHES_ENDED); expect("touchReleased", "cdpablz");
    order["windowResized"].clear();
    feed(SAPP_EVENTTYPE_RESIZED); expect("windowResized", "cdpablz");
    feed(SAPP_EVENTTYPE_FILES_DROPPED); expect("filesDropped", "cdpablz");
    internal::windowApiContext().fbWidth = internal::windowApiContext().fbHeight = 128; internal::syncWindowAppSize(*internal::resolveWindow()); order.clear();
    for (int priority : {-20, 0, 50}) {
        auto consumer = events().mousePressed.listen([](auto& e) { e.consumed = true; }, priority);
        feed(SAPP_EVENTTYPE_MOUSE_DOWN);
        check("earlier consumer suppresses App, tree and AfterApp", order["mousePressed"].find('a') == std::string::npos && order["mousePressed"].find('z') == std::string::npos);
        order.clear(); feed(SAPP_EVENTTYPE_MOUSE_UP); order.clear();
    }
    treeConsumes = true;
    feed(SAPP_EVENTTYPE_MOUSE_DOWN); expect("mousePressed", "cdpat");
    dragConsumes = false;
    feed(SAPP_EVENTTYPE_MOUSE_MOVE, 30, 30, SAPP_MODIFIER_LMB); expect("mouseDragged", "cdpatt");
    treeConsumes = false;
    feed(SAPP_EVENTTYPE_MOUSE_MOVE, 35, 35, SAPP_MODIFIER_LMB); expect("mouseDragged", "cdpattblz");
    treeConsumes = dragConsumes = true;
    feed(SAPP_EVENTTYPE_MOUSE_MOVE, 200, 200, SAPP_MODIFIER_LMB); expect("mouseDragged", "cdpat");
    // A grabbed release returning false is delivered once and reaches AfterApp.
    feed(SAPP_EVENTTYPE_MOUSE_UP, 200, 200); expect("mouseReleased", "cdpatblz");
    feed(SAPP_EVENTTYPE_MOUSE_DOWN); order.clear(); releaseConsumes = true;
    feed(SAPP_EVENTTYPE_MOUSE_UP, 200, 200); expect("mouseReleased", "cdpat");
    feed(SAPP_EVENTTYPE_MOUSE_MOVE); expect("mouseMoved", "cdpat");
    feed(SAPP_EVENTTYPE_MOUSE_SCROLL); expect("mouseScrolled", "cdpat");
    feed(SAPP_EVENTTYPE_KEY_DOWN); expect("keyPressed", "cdpat");
    feed(SAPP_EVENTTYPE_KEY_UP); expect("keyReleased", "cdpat");
    treeConsumes = releaseConsumes = false;
    feed(SAPP_EVENTTYPE_MOUSE_DOWN, 90, 90); expect("mousePressed", "cdpablz");
    feed(SAPP_EVENTTYPE_MOUSE_MOVE, 95, 95, SAPP_MODIFIER_LMB); expect("mouseDragged", "cdpablz");
    feed(SAPP_EVENTTYPE_MOUSE_UP, 95, 95); expect("mouseReleased", "cdpablz");
    rootConsumes = true;
    feed(SAPP_EVENTTYPE_MOUSE_DOWN, 90, 90); expect("mousePressed", "cdpa");
    feed(SAPP_EVENTTYPE_MOUSE_MOVE, 95, 95, SAPP_MODIFIER_LMB); expect("mouseDragged", "cdpablz");
    feed(SAPP_EVENTTYPE_MOUSE_UP, 95, 95); expect("mouseReleased", "cdpablz");
    rootConsumes = false;
    auto root = std::static_pointer_cast<App>(internal::windowApiContext().rootNode.lock());
    auto rootConsumer = root->RectNode::mousePressed.listen([](auto& e) { e.consumed = true; });
    feed(SAPP_EVENTTYPE_MOUSE_DOWN, 90, 90); expect("mousePressed", "cdpa");
    feed(SAPP_EVENTTYPE_MOUSE_UP, 90, 90); order.clear(); rootConsumer.disconnect();
    auto edit = events().mouseMoved.listen([](auto& e) { e.pos.x = 31; e.delta.x = 7; });
    feed(SAPP_EVENTTYPE_MOUSE_MOVE);
    auto app = std::static_pointer_cast<DeliveryApp>(root);
    check("typed listener edits reach App", app->lastMove.pos.x == 31 && app->lastMove.delta.x == 7);
    edit.disconnect(); order.clear();
    auto resizeEdit = events().windowResized.listen([](auto& e) { e.width = 123; e.height = 124; });
    feed(SAPP_EVENTTYPE_RESIZED);
    check("resize listener edits reach App event path", app->getWidth() == 123 && app->getHeight() == 124);
    resizeEdit.disconnect(); internal::windowApiContext().fbWidth = internal::windowApiContext().fbHeight = 128; internal::syncWindowAppSize(*internal::resolveWindow()); order.clear();
    auto scrollConsumer = app->RectNode::mouseScrolled.listen([](auto& e) { e.consumed = true; });
    feed(SAPP_EVENTTYPE_MOUSE_MOVE, 90, 90); order.clear();
    feed(SAPP_EVENTTYPE_MOUSE_SCROLL); expect("mouseScrolled", "cdpa");
    scrollConsumer.disconnect(); order.clear();

    mcp::setToolLevel(mcp::ToolLevel::Control);
    tool("tc_key_press", {{"key", SAPP_KEYCODE_A}}); expect("keyPressed", "cdpatblz");
    tool("tc_key_release", {{"key", SAPP_KEYCODE_A}}); expect("keyReleased", "cdpatblz");
    {
        auto consumeDown = events().keyPressed.listen([](auto& e) { e.consumed = true; }, -20);
        auto consumeUp = events().keyReleased.listen([](auto& e) { e.consumed = true; }, -20);
        tool("tc_key_press", {{"key", SAPP_KEYCODE_A}}); expect("keyPressed", "c");
        check("consumed MCP key press still updates held state", isKeyPressed(SAPP_KEYCODE_A));
        tool("tc_key_release", {{"key", SAPP_KEYCODE_A}}); expect("keyReleased", "c");
        check("consumed MCP key release still clears held state", !isKeyPressed(SAPP_KEYCODE_A));
    }
    auto consumeMove = events().mouseMoved.listen([](auto& e) { e.consumed = true; }, -20);
    tool("tc_mouse_move", {{"x", 20}, {"y", 20}}); expect("mouseMoved", "c");
    consumeMove.disconnect();
    auto consumePress = events().mousePressed.listen([](auto& e) { e.consumed = true; }, -20);
    auto consumeRelease = events().mouseReleased.listen([](auto& e) { e.consumed = true; }, -20);
    tool("tc_mouse_click", {{"x", 20}, {"y", 20}});
    expect("mousePressed", "c"); expect("mouseReleased", "c");
    tool("tc_mouse_press", {{"x", 20}, {"y", 20}, {"button", 0}}); order.clear();
    auto consumeDrag = events().mouseDragged.listen([](auto& e) { e.consumed = true; }, -20);
    tool("tc_mouse_move", {{"x", 30}, {"y", 30}, {"button", 0}}); expect("mouseDragged", "c");
    tool("tc_mouse_release", {{"x", 30}, {"y", 30}, {"button", 0}}); order.clear();
}
void cameraChecks() {
    EasyCam cam;
    cam.setControlArea(Rect(0, 0, 100, 100));
    cam.enableMouseInput();
    auto drag = [&](float x) { feed(SAPP_EVENTTYPE_MOUSE_MOVE, x, 20, SAPP_MODIFIER_LMB); };
    auto position = cam.getPosition();
    treeConsumes = true;
    feed(SAPP_EVENTTYPE_MOUSE_DOWN); drag(40); feed(SAPP_EVENTTYPE_MOUSE_UP);
    check("node-owned press prevents orbit", cam.getPosition() == position);
    treeConsumes = false;
    feed(SAPP_EVENTTYPE_MOUSE_DOWN, 90, 20); drag(95);
    check("background press orbits", cam.getPosition() != position);
    releaseConsumes = true;
    feed(SAPP_EVENTTYPE_MOUSE_UP, 20, 20);
    position = cam.getPosition();
    // A later drag without a new accepted press must not resurrect the orbit.
    drag(70); check("release over consuming node ends camera gesture", cam.getPosition() == position);
    releaseConsumes = false;
    std::vector<sapp_event_type> endings{SAPP_EVENTTYPE_UNFOCUSED};
#if defined(_WIN32) || defined(__EMSCRIPTEN__) || (defined(__linux__) && !defined(__ANDROID__))
    endings.push_back(SAPP_EVENTTYPE_MOUSE_MOVE);
#endif
    for (auto end : endings) {
        feed(SAPP_EVENTTYPE_MOUSE_DOWN, 90, 20);
        feed(end, 95, 20); // move without held bits recovers a lost button
        position = cam.getPosition(); drag(70);
        check("synthetic release ends camera gesture", cam.getPosition() == position);
    }
    setTouchAsMouse(true);
    feed(SAPP_EVENTTYPE_TOUCHES_BEGAN, 90, 20);
    feed(SAPP_EVENTTYPE_TOUCHES_CANCELLED, 90, 20);
    position = cam.getPosition(); drag(70);
    check("touch cancel ends camera gesture", cam.getPosition() == position);
    setTouchAsMouse(false);
    treeConsumes = true; feed(SAPP_EVENTTYPE_MOUSE_MOVE); treeConsumes = false;
    float distance = cam.getDistance();
    ScrollEventArgs scroll; scroll.pos = scroll.globalPos = Vec2(200, 20); scroll.scroll.y = 1;
    events().mouseScrolled.notify(scroll);
    check("scroll routes using its own position outside control area", cam.getDistance() == distance && !scroll.consumed);
    scroll.pos = scroll.globalPos = Vec2(90, 20); events().mouseScrolled.notify(scroll);
    check("scroll inside control area zooms and consumes", cam.getDistance() != distance && scroll.consumed);
    scroll.consumed = false; scroll.scroll = Vec2(1, 0);
    events().mouseScrolled.notify(scroll);
    check("horizontal-only scroll is not consumed", !scroll.consumed);
    cam.setDragModifier(EasyCam::Modifier::Shift);
    feed(SAPP_EVENTTYPE_MOUSE_DOWN, 90, 20, SAPP_MODIFIER_SHIFT);
    drag(95); position = cam.getPosition();
    feed(SAPP_EVENTTYPE_MOUSE_UP, 90, 20); drag(70);
    check("modifier release does not strand camera gesture", cam.getPosition() == position);
    feed(SAPP_EVENTTYPE_MOUSE_DOWN, 90, 20); position = cam.getPosition(); drag(95);
    check("press without required modifier never starts gesture", cam.getPosition() == position);
    feed(SAPP_EVENTTYPE_MOUSE_UP, 90, 20);
    cam.setDragModifier(EasyCam::Modifier::None);
    auto buttonEvent = [](sapp_event_type type, sapp_mousebutton button) {
        sapp_event e{}; e.type = type; e.mouse_button = button;
        e.mouse_x = 90; e.mouse_y = 20;
        e.modifiers = SAPP_MODIFIER_LMB | SAPP_MODIFIER_MMB;
        tcCoreTest::dispatchMainEvent(&e);
    };
    buttonEvent(SAPP_EVENTTYPE_MOUSE_DOWN, SAPP_MOUSEBUTTON_LEFT);
    buttonEvent(SAPP_EVENTTYPE_MOUSE_DOWN, SAPP_MOUSEBUTTON_MIDDLE);
    buttonEvent(SAPP_EVENTTYPE_MOUSE_UP, SAPP_MOUSEBUTTON_LEFT);
    buttonEvent(SAPP_EVENTTYPE_MOUSE_UP, SAPP_MOUSEBUTTON_MIDDLE);
    position = cam.getPosition();
    cam.mouseDragged(95, 25, MOUSE_BUTTON_LEFT);
    cam.mouseDragged(95, 25, MOUSE_BUTTON_MIDDLE);
    check("each owned button release ends its gesture", cam.getPosition() == position);
    feed(SAPP_EVENTTYPE_MOUSE_DOWN, 90, 20);
    cam.disableMouseInput(); cam.enableMouseInput();
    position = cam.getPosition(); drag(95);
    check("disabling ends an owned gesture", cam.getPosition() == position);
    feed(SAPP_EVENTTYPE_MOUSE_UP, 95, 20);
    cam.disableMouseInput(); order.clear();
}
void clearChecks() {
    // Keep each clearer alive until its event actually fires.
    auto bind = [](auto& event) {
        listeners.push_back(event.listen([&event](auto&&...) { event.clear(); }, -100));
    };
#define CLEAR(name) bind(events().name)
    CLEAR(update); CLEAR(draw); CLEAR(exit); CLEAR(keyPressed); CLEAR(keyReleased);
    CLEAR(mousePressed); CLEAR(mouseReleased); CLEAR(mouseMoved); CLEAR(mouseDragged); CLEAR(mouseScrolled);
    CLEAR(touchPressed); CLEAR(touchMoved); CLEAR(touchReleased); CLEAR(windowResized); CLEAR(filesDropped);
#undef CLEAR
    order.clear();
    for (int pass = 0; pass < 2; ++pass) {
        feed(SAPP_EVENTTYPE_KEY_DOWN); expect("keyPressed", "at");
        feed(SAPP_EVENTTYPE_KEY_UP); expect("keyReleased", "at");
        feed(SAPP_EVENTTYPE_MOUSE_DOWN); expect("mousePressed", "at");
        feed(SAPP_EVENTTYPE_MOUSE_MOVE, 30, 30, SAPP_MODIFIER_LMB); expect("mouseDragged", "at");
        feed(SAPP_EVENTTYPE_MOUSE_UP); expect("mouseReleased", "at");
        feed(SAPP_EVENTTYPE_MOUSE_MOVE); expect("mouseMoved", "at");
        feed(SAPP_EVENTTYPE_MOUSE_SCROLL); expect("mouseScrolled", "at");
        feed(SAPP_EVENTTYPE_TOUCHES_BEGAN); expect("touchPressed", "a");
        feed(SAPP_EVENTTYPE_TOUCHES_MOVED); expect("touchMoved", "a");
        feed(SAPP_EVENTTYPE_TOUCHES_ENDED); expect("touchReleased", "a");
        feed(SAPP_EVENTTYPE_RESIZED); expect("windowResized", "a");
        feed(SAPP_EVENTTYPE_FILES_DROPPED); expect("filesDropped", "a");
    }
    const int before = touches;
    std::thread worker([] { TouchEventArgs e; events().touchPressed.notify(e); }); worker.join();
    check("worker notify never runs App code", touches == before);
}
void finalChecks() {
    check("test driver ran", tested);
    check("App exit survives clear during notify exactly once", exits == 1);
    expect("exit", "a");
    check("App keeps updating and drawing after clear", updates >= 3 && draws >= 3);
}
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
    internal::pixelPerfectMode() = true;

    WindowSettings settings; settings.width = settings.height = 128; settings.pixelPerfect = true;
    bool exitOrder = argc > 1 && std::string(argv[1]) == "--exit-order";
    exitOrderMode = exitOrder;
    bool display = exitOrder || (argc > 1 && std::string(argv[1]) == "--display");
    if (exitOrder) {
        return tcCoreTest::runDisplayTest<DeliveryApp>(settings, failures, [] {
            check("exit listener calls App exactly once", exits == 1);
            expect("exit", "cdpablz");
        });
    }
    if (display) return tcCoreTest::runDisplayTest<DeliveryApp>(settings, failures, finalChecks);
    // Core sweep also covers delivery without requiring an OS window.
    // Android creates its descriptor on the Activity thread, then runs App
    // callbacks on the native loop thread. Registration must not claim the
    // descriptor thread as the App thread; _setup_cb records that identity.
    Window* launch = nullptr;
    std::thread descriptorThread([settings, &launch] {
        auto descriptorSettings = settings;
        descriptorSettings.pixelPerfect = false; // no display query on this thread
        auto descriptor = buildAppDescriptor<DeliveryApp>(descriptorSettings);
        launch = static_cast<Window*>(descriptor._tc_bind_user);
    });
    descriptorThread.join();
    getMainThreadId(); // model _setup_cb on the actual App thread
    check("descriptor creation does not claim the App thread", isMainThread());
    internal::pixelPerfectMode() = true;
    tcCoreTest::InjectedWindow native(*launch);
    internal::currentWindowCtx() = &internal::windowApiContext();
    native.metadata.fb_width = native.metadata.fb_height = 128;
    internal::windowApiContext().fbWidth = internal::windowApiContext().fbHeight = 128;
    internal::appSetupFunc();
    internal::windowApiContext().fbWidth = internal::windowApiContext().fbHeight = 128; internal::syncWindowAppSize(*internal::resolveWindow());
    events().update.notify(); events().draw.notify();
    events().afterFrame.notify();
    for (int pass = 0; pass < 2; ++pass) {
        events().update.notify(); events().draw.notify();
        events().afterFrame.notify();
    }
    internal::shutdownRuntime(); finalChecks();
    return failures ? 1 : 0;
}
