#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <cstdio>
#include <map>
#include <thread>

#if (defined(__APPLE__) && TARGET_OS_OSX) || defined(_WIN32) || (defined(__linux__) && (defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)) && !defined(__ANDROID__))
namespace trussc::internal {
void dispatchWindowDisplayEventForTests(const sapp_event*, Window&);
}
using namespace tc;
namespace {
int failures = 0;
void check(const char* label, bool ok) {
    std::printf("secondaryEventDelivery: %s: %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
std::shared_ptr<Window> secondary;
std::shared_ptr<App> observerOwner;
CoreEvents& source() { return secondary->events(); }
std::map<std::string, std::string> order;
std::vector<EventListener> listeners;
bool treeConsumes = false, releaseConsumes = false, rootConsumes = false, dragConsumes = true;
int exits = 0, framesAfterClear = 0, closeEvents = 0;
bool inputsDone = false, completed = false;
bool secondaryTicked = false, skippedNoTick = false;
EventListener firstUpdate, firstDraw;
void expect(const char* name, const char* expected) {
    if (order[name] != expected) std::printf("  %s: got %s expected %s\n", name, order[name].c_str(), expected);
    check(name, order[name] == expected); order[name].clear();
}
void stages(bool before) {
    auto bind = [&](auto& event, const char* name) {
        auto add = [&](int priority, char stage) {
            listeners.push_back(event.listen([name, stage](auto&&...) { order[name] += stage; }, priority));
        };
        if (before) {
            add(-20, 'c');
            listeners.push_back(event.listen([name](auto&&...) { order[name] += 'd'; }));
            add(50, 'p'); add(100, 'b');
        } else { add(100, 'l'); add(EventPriority::AfterApp, 'z'); }
    };
#define STAGE(name) bind(source().name, #name)
    STAGE(update); STAGE(draw); STAGE(exit); STAGE(keyPressed); STAGE(keyReleased);
    STAGE(mousePressed); STAGE(mouseReleased); STAGE(mouseMoved); STAGE(mouseDragged); STAGE(mouseScrolled);
    STAGE(touchPressed); STAGE(touchMoved); STAGE(touchReleased); STAGE(windowResized); STAGE(filesDropped);
#undef STAGE
}
void feed(sapp_event_type type, float x = 20, float y = 20, unsigned int modifiers = 0) {
    sapp_event e{}; e.type = type; e.key_code = SAPP_KEYCODE_A;
    e.mouse_button = SAPP_MOUSEBUTTON_LEFT;
    const float dpi = secondary->context().dpiScale;
    e.mouse_x = x * dpi; e.mouse_y = y * dpi; e.modifiers = modifiers; e.scroll_y = 1;
    internal::dispatchWindowDisplayEventForTests(&e, *secondary);
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
struct DeliveryApp : App {
    std::shared_ptr<Probe> probe;
    MouseMoveEventArgs lastMove;
    bool hoverPhase = false;
    void setup() override { enableEvents(); probe = std::make_shared<Probe>(); addChild(probe); }
    void update() override {
        order["update"] += 'a';
        if (hoverPhase) {
            // Seed after native input, in the same tick as the hover traversal.
            auto& ctx = getWindow()->context();
            ctx.mouseX = ctx.mouseY = 20.5f;
        }
    }
    void draw() override { order["draw"] += 'a'; }
    void exit() override { ++exits; order["exit"] += 'a'; }
    void keyPressed(const KeyEventArgs& e) override { order["keyPressed"] += 'a'; check("App sees held key", isKeyPressed(e.key)); }
    void keyReleased(const KeyEventArgs& e) override { order["keyReleased"] += 'a'; check("App sees released key", !isKeyPressed(e.key)); }
    void mousePressed(const MouseEventArgs&) override { order["mousePressed"] += 'a'; }
    void mouseReleased(const MouseEventArgs&) override { order["mouseReleased"] += 'a'; }
    void mouseMoved(const MouseMoveEventArgs& e) override { order["mouseMoved"] += 'a'; lastMove = e; }
    void mouseDragged(const MouseDragEventArgs&) override { order["mouseDragged"] += 'a'; }
    void mouseScrolled(const ScrollEventArgs&) override { order["mouseScrolled"] += 'a'; }
    void touchPressed(const TouchEventArgs&) override { order["touchPressed"] += 'a'; }
    void touchMoved(const TouchEventArgs&) override { order["touchMoved"] += 'a'; }
    void touchReleased(const TouchEventArgs&) override { order["touchReleased"] += 'a'; }
    void windowResized(int, int) override { order["windowResized"] += 'a'; }
    void filesDropped(const std::vector<std::string>&) override { order["filesDropped"] += 'a'; }
    using App::onMousePress;
    bool onMousePress(Vec2, int) override { return rootConsumes; }
};
void extraEvents() {
    // Secondary OS touch/file delivery is outside this PR. Check the typed slots.
    TouchEventArgs touch; DragDropEventArgs files;
    source().touchPressed.notify(touch); source().touchMoved.notify(touch); source().touchReleased.notify(touch);
    source().filesDropped.notify(files);
    secondary->syncRootSize(secondary->getApp()->getWidth() + 1, secondary->getApp()->getHeight() + 1);
}
void inputChecks() {
    order.clear();
    auto down = source().keyPressed.listen([](auto& e) { check("listener sees held key", isKeyPressed(e.key)); });
    auto up = source().keyReleased.listen([](auto& e) { check("listener sees released key", !isKeyPressed(e.key)); });
    feed(SAPP_EVENTTYPE_KEY_DOWN); expect("keyPressed", "cdpatblz");
    feed(SAPP_EVENTTYPE_KEY_UP); expect("keyReleased", "cdpatblz");
    feed(SAPP_EVENTTYPE_MOUSE_DOWN); expect("mousePressed", "cdpatblz");
    feed(SAPP_EVENTTYPE_MOUSE_MOVE, 30, 30, SAPP_MODIFIER_LMB); expect("mouseDragged", "cdpatblz");
    feed(SAPP_EVENTTYPE_MOUSE_UP); expect("mouseReleased", "cdpatblz");
    feed(SAPP_EVENTTYPE_MOUSE_MOVE); expect("mouseMoved", "cdpatblz");
    feed(SAPP_EVENTTYPE_MOUSE_SCROLL); expect("mouseScrolled", "cdpatblz");
    extraEvents();
    for (auto name : {"touchPressed", "touchMoved", "touchReleased", "filesDropped", "windowResized"}) expect(name, "cdpablz");
    for (int priority : {-20, 0, 50}) {
        auto consumer = source().mousePressed.listen([](auto& e) { e.consumed = true; }, priority);
        feed(SAPP_EVENTTYPE_MOUSE_DOWN);
        check("early consumption suppresses App/tree/AfterApp", order["mousePressed"].find('a') == std::string::npos && order["mousePressed"].find('z') == std::string::npos);
        order.clear(); feed(SAPP_EVENTTYPE_MOUSE_UP); order.clear();
    }
    {
        auto consumeDown = source().keyPressed.listen([](auto& e) { e.consumed = true; }, -20);
        auto consumeUp = source().keyReleased.listen([](auto& e) { e.consumed = true; }, -20);
        feed(SAPP_EVENTTYPE_KEY_DOWN); expect("keyPressed", "c");
        check("consumed key still updates held state", isKeyPressed(SAPP_KEYCODE_A));
        feed(SAPP_EVENTTYPE_KEY_UP); expect("keyReleased", "c");
        check("consumed release still clears held state", !isKeyPressed(SAPP_KEYCODE_A));
    }
    treeConsumes = true;
    feed(SAPP_EVENTTYPE_MOUSE_DOWN); expect("mousePressed", "cdpat");
    dragConsumes = false;
    feed(SAPP_EVENTTYPE_MOUSE_MOVE, 30, 30, SAPP_MODIFIER_LMB); expect("mouseDragged", "cdpatt");
    treeConsumes = false;
    feed(SAPP_EVENTTYPE_MOUSE_MOVE, 35, 35, SAPP_MODIFIER_LMB); expect("mouseDragged", "cdpattblz");
    treeConsumes = dragConsumes = true;
    feed(SAPP_EVENTTYPE_MOUSE_MOVE, 200, 200, SAPP_MODIFIER_LMB); expect("mouseDragged", "cdpat");
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
    feed(SAPP_EVENTTYPE_MOUSE_UP, 90, 90); order.clear(); rootConsumes = false;
    auto app = std::static_pointer_cast<DeliveryApp>(secondary->getApp());
    auto rootConsumer = app->RectNode::mousePressed.listen([](auto& e) { e.consumed = true; });
    feed(SAPP_EVENTTYPE_MOUSE_DOWN, 90, 90); expect("mousePressed", "cdpa");
    feed(SAPP_EVENTTYPE_MOUSE_UP, 90, 90); order.clear(); rootConsumer.disconnect();
    auto edit = source().mouseMoved.listen([](auto& e) { e.pos.x = 31; e.delta.x = 7; });
    feed(SAPP_EVENTTYPE_MOUSE_MOVE);
    check("typed edits reach App", app->lastMove.pos.x == 31 && app->lastMove.delta.x == 7); edit.disconnect();
    auto resizeEdit = source().windowResized.listen([](auto& e) { e.width = 125; });
    secondary->syncRootSize(121, 122); check("resize listener edits reach App", app->getWidth() == 125); resizeEdit.disconnect();
    order.clear();
    auto bind = [](auto& event) { listeners.push_back(event.listen([&event](auto&&...) { event.clear(); }, -100)); };
#define CLEAR(name) bind(source().name)
    CLEAR(update); CLEAR(draw); CLEAR(exit); CLEAR(keyPressed); CLEAR(keyReleased);
    CLEAR(mousePressed); CLEAR(mouseReleased); CLEAR(mouseMoved); CLEAR(mouseDragged); CLEAR(mouseScrolled);
    CLEAR(touchPressed); CLEAR(touchMoved); CLEAR(touchReleased); CLEAR(windowResized); CLEAR(filesDropped);
#undef CLEAR
    for (int pass = 0; pass < 2; ++pass) {
        feed(SAPP_EVENTTYPE_KEY_DOWN); expect("keyPressed", "at");
        feed(SAPP_EVENTTYPE_KEY_UP); expect("keyReleased", "at");
        feed(SAPP_EVENTTYPE_MOUSE_DOWN); expect("mousePressed", "at");
        feed(SAPP_EVENTTYPE_MOUSE_MOVE, 30, 30, SAPP_MODIFIER_LMB); expect("mouseDragged", "at");
        feed(SAPP_EVENTTYPE_MOUSE_UP); expect("mouseReleased", "at");
        feed(SAPP_EVENTTYPE_MOUSE_MOVE); expect("mouseMoved", "at");
        feed(SAPP_EVENTTYPE_MOUSE_SCROLL); expect("mouseScrolled", "at");
        extraEvents();
        for (auto name : {"touchPressed", "touchMoved", "touchReleased", "filesDropped", "windowResized"}) expect(name, "a");
    }
    std::thread worker([] { TouchEventArgs e; source().touchPressed.notify(e); }); worker.join();
    check("worker does not enter App", order["touchPressed"].empty());
    // Hover uses logical floats even at fractional framebuffer-to-point scales.
    app->probe->setPos(20.25f, 10); app->probe->setSize(0.5f, 60);
    app->hoverPhase = true;
    order.clear();
}
struct Counts { int updates = 0, draws = 0, exits = 0, cleanups = 0; } counts[4];
int phase = 0;
struct SwapApp : App {
    int index;
    explicit SwapApp(int value) : index(value) {}
    void update() override {
        ++counts[index].updates;
        if (index == 1 && counts[1].updates == 1) {
            feed(SAPP_EVENTTYPE_KEY_DOWN); feed(SAPP_EVENTTYPE_KEY_UP);
            check("nested input keeps outgoing App for tick", secondary->getApp().get() == this);
        }
        if (index == 0 && counts[0].updates == 2) {
            secondary->setApp<SwapApp>(1);
            check("update swap remains pending", secondary->getApp().get() == this && getWindow() == secondary.get());
        }
    }
    void draw() override { ++counts[index].draws; }
    void keyPressed(const KeyEventArgs&) override {
        if (index == 1) {
            secondary->setApp<SwapApp>(2);
            check("input swap remains pending", secondary->getApp().get() == this);
        }
    }
    void exit() override {
        ++counts[index].exits;
        if (index == 0) {
            // Outgoing teardown must not erase the replacement's bindings.
            source().keyPressed.clear(); source().update.clear();
        }
        if (index == 3) {
            check("close detaches outgoing App before exit", !getWindow() && !secondary->getApp() && !secondary->isOpen());
            source().exit.clear();
        }
    }
    void cleanup() override { ++counts[index].cleanups; }
};
void closeOrderChecks() {
    secondary = createWindow(); check("close-order secondary created", bool(secondary));
    if (!secondary) return;
    auto observer = internal::makeConstructedAppAt<App>(&secondary->context());
    internal::AppOwnerScope observation(internal::appOwner(*observer));
    order.clear(); stages(true);
    struct ExitOrderApp : App {
        void exit() override { order["exit"] += 'a'; check("close uses secondary context", internal::currentWindowContext().hostWindow && !getWindow()); }
    };
    auto app = internal::makeConstructedAppAt<ExitOrderApp>(&secondary->context()); std::weak_ptr<App> outgoing = app;
    internal::WindowRequestAccess::attachCandidate(*secondary, app); internal::applyPendingAppForTests(*secondary); stages(false); app.reset();
    std::string quitOrder;
    auto processQuit = runtime().events().exitRequested.listen([&](ExitRequestEventArgs& e) {
        quitOrder += 'R'; e.cancel = true; e.reason = "secondary unsaved";
    });
    auto appQuit = secondary->events().exitRequested.listen([&](ExitRequestEventArgs& e) {
        check("secondary quit fresh payload after Runtime veto", !e.cancel && e.reason.empty());
        quitOrder += 'S'; e.cancel = false;
    });
    check("secondary process veto retains Runtime reason", !internal::dispatchExitRequested() && internal::exitBlockReason() == "secondary unsaved");
    check("secondary process Runtime then App slot", quitOrder == "RS");
    auto closeVeto = secondary->events().closeRequested.listen([](CloseRequestEventArgs& e) { e.cancel = true; });
    auto closeFalse = secondary->events().closeRequested.listen([](CloseRequestEventArgs& e) {
        check("secondary close later false begins fresh", !e.cancel); e.cancel = false;
    });
    const auto handle = internal::WindowRequestAccess::binding(*secondary).handle;
    check("secondary OS close veto with later false", !internal::allowWindowClose(handle, secondary.get()) && secondary->isOpen());
    closeVeto.disconnect(); closeFalse.disconnect(); processQuit.disconnect(); appQuit.disconnect();
    auto held = source().exit.listen([&] { check("outgoing App held through AfterApp", !outgoing.expired()); }, EventPriority::AfterApp);
    internal::WindowRequestAccess::teardown(*secondary);
    secondary.reset(); // Runtime retention requires an explicit close
    expect("exit", "cdpablz");
    check("outgoing released after complete exit delivery", outgoing.expired());
    secondary = createWindow(); check("exit-clear secondary created", bool(secondary));
    if (secondary) {
        secondary->setApp<ExitOrderApp>(); internal::applyPendingAppForTests(*secondary);
        auto clearer = source().exit.listen([] { source().exit.clear(); }, -100);
        // Keep the owning window available to the clearer during teardown.
        internal::WindowRequestAccess::teardown(*secondary);
        expect("exit", "a");
        secondary.reset();
    }
    auto emptyWindow = createWindow(); check("empty secondary created", bool(emptyWindow));
    if (emptyWindow) {
        int emptyExits = 0; auto subscription = emptyWindow->events().exit.listen([&] { ++emptyExits; });
        internal::WindowRequestAccess::teardown(*emptyWindow);
        internal::WindowRequestAccess::teardown(*emptyWindow);
        emptyWindow.reset(); check("window without App notifies exit once", emptyExits == 1);
    }
}
EventListener after, closeCounter, mandatory;
struct Harness : App {
    int mainFrames = 0;
    void setup() override {
        WindowSettings s; s.width = s.height = 128; s.highDpi = false;
        secondary = createWindow(s); check("real secondary window created", bool(secondary));
        if (!secondary) { exitApp(); return; }
        observerOwner = internal::makeConstructedAppAt<App>(&secondary->context());
        internal::AppOwnerScope observation(internal::appOwner(*observerOwner));
        // Observe the event stream independently of the App bindings under test.
        firstUpdate = source().update.listen([] { secondaryTicked = true; }, -1000);
        firstDraw = source().draw.listen([] { secondaryTicked = true; }, -1000);
        stages(true); secondary->setApp<DeliveryApp>();
        internal::applyPendingAppForTests(*secondary); stages(false);
        after = source().afterFrame.listen([] {
            if (!inputsDone) {
                expect("update", "cdpatblz"); expect("draw", "cdpatblz");
                inputChecks(); inputsDone = true;
            } else if (phase == 0) {
                expect("update", "at"); expect("draw", "at");
                auto app = std::static_pointer_cast<DeliveryApp>(secondary->getApp());
                check("tick hover retains fractional point", secondary->context().hoveredNode.lock() == app->probe);
                if (++framesAfterClear == 2) {
                    app->hoverPhase = false;
                    secondary->setApp<SwapApp>(0); phase = 1;
                }
            } else if (phase == 1 && counts[1].updates) {
                check("A keeps update and draw until boundary", counts[0].updates == 2 && counts[0].draws == 2 && counts[0].exits == 1 && counts[0].cleanups == 1);
                check("setApp direct exit survives exit clear exactly once", exits == 1);
                check("setApp does not notify window exit", closeEvents == 0);
                phase = 2;
            } else if (phase == 2 && counts[2].updates) {
                check("swaps retain exactly one App binding per cleared event", source().keyPressed.listenerCount() == 1 && source().update.listenerCount() == 1 && secondary->context().appEventListeners.size() == 15);
                check("input swaps App exactly once after its full tick", counts[1].updates == 1 && counts[1].draws == 1 && counts[1].exits == 1 && counts[1].cleanups == 1);
                // Cancellation and refusal do not disturb bindings.
                auto current = secondary->getApp();
                auto cancelled = internal::makeConstructedApp<SwapApp>(3);
                internal::retireAppOwner(internal::appOwner(*cancelled));
                internal::WindowRequestAccess::attachCandidate(*secondary, cancelled);
                internal::WindowRequestAccess::attachCandidate(*secondary, current);
                phase = 3;
            } else if (phase == 3) {
                check("cancel/refuse preserves current binding", secondary->getApp() && static_cast<SwapApp*>(secondary->getApp().get())->index == 2 && counts[2].exits == 0 && counts[3].updates == 0);
                secondary->destroyApp(); phase = 4;
            }
        });
        // The exit clearer above removes the ordinary counter on close only.
        closeCounter = source().exit.listen([] { ++closeEvents; }, EventPriority::BeforeApp);
    }
    void update() override {
        if (!secondary) return;
        if (phase == 4 && !secondary->getApp()) {
            check("A to null calls exit and cleanup once without exit event", counts[2].exits == 1 && counts[2].cleanups == 1 && closeEvents == 0);
            check("null attachment retains stable no-op Window slots", source().keyPressed.listenerCount() == 1 && source().update.listenerCount() == 1);
            // The exit clearer would erase the ordinary counter; replace it
            // with protected observers for close and the App's clear in exit.
            source().exit.clear();
            closeCounter = internal::FrameworkEventAccess::listen(source().exit, [] { ++closeEvents; }, EventPriority::BeforeApp);
            mandatory = internal::FrameworkEventAccess::listen(source().exit, [] {
                check("mandatory teardown survives App exit clear", counts[3].exits == 1 && counts[3].cleanups == 0);
                check("outgoing held through AfterApp", !secondary->getApp());
            }, EventPriority::AfterApp);
            secondary->setApp<SwapApp>(3); phase = 5;
        } else if (phase == 5 && counts[3].draws) { secondary->close(); phase = 6; }
        else if (phase == 6 && !secondary->isOpen()) {
            check("close notifies exit exactly once", closeEvents == 1 && counts[3].exits == 1 && counts[3].cleanups == 1);
            internal::WindowRequestAccess::teardown(*secondary);
            check("repeated teardown does not redeliver exit", closeEvents == 1 && counts[3].exits == 1);
            check("closed window has no App bindings", secondary->context().appEventListeners.empty());
            secondary.reset(); after.disconnect(); closeCounter.disconnect(); mandatory.disconnect(); listeners.clear();
            observerOwner.reset();
            closeOrderChecks(); completed = true; exitApp();
        }
        if (++mainFrames == 600) {
            if (!secondaryTicked) {
                std::puts("SKIP: secondaryEventDelivery --display: the secondary window never reached its first tick (occluded or not presented on this host); the delivery checks require it");
                skippedNoTick = true;
            } else check("secondary driver completed within frame guard", false);
            exitApp();
        }
    }
};
}
TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc < 2 || std::string(argv[1]) != "--display") { std::puts("SKIP: secondaryEventDelivery requires --display"); return 0; }
    if (!internal::supportsSecondaryWindows()) { std::puts("SKIP: secondary windows unavailable"); return 0; }
    // Keep part of the main window visible while the secondary ticks.
    WindowSettings s; s.width = s.height = 256; s.highDpi = false;
    return tcCoreTest::runDisplayTest<Harness>(s, failures, [] {
        if (!skippedNoTick) check("all secondary delivery checks completed", completed);
        secondary.reset();
    });
}
#else
TC_CORE_TEST_MAIN(int, char**) { std::puts("SKIP: secondary windows unavailable"); return 0; }
#endif
