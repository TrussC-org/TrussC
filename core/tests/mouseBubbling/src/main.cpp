// =============================================================================
// mouseBubbling — behavioral regression test for input propagation (v0.7)
//
// press/release/move bubble from the front-most hit node up the parent chain
// until a handler consumes (returns true); the press consumer becomes the
// grab target for drag/release. Headless: drives Node::dispatch* directly
// through an App subclass (friend access), no window required.
// =============================================================================

#include <TrussC.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace std;
using namespace tc;

static int g_fail = 0;
static void check(const char* name, bool ok) {
    std::printf("%-64s %s\n", name, ok ? "PASS" : "FAIL");
    std::fflush(stdout);
    if (!ok) ++g_fail;
}

// A RectNode that records calls and consumes on demand.
class Probe : public RectNode {
public:
    explicit Probe(bool consume) : consume_(consume) {
        enableEvents();   // opt in to hit testing (off by default on RectNode)
    }
    bool onMousePress(const MouseEventArgs& e) override {
        pressCount++; lastLocal = e.pos;
        RectNode::onMousePress(e);   // fire events / legacy hooks
        return consume_;
    }
    bool onMouseRelease(const MouseEventArgs& e) override {
        releaseCount++;
        RectNode::onMouseRelease(e);
        return consume_;
    }
    bool onMouseMove(const MouseMoveEventArgs& e) override {
        moveCount++;
        RectNode::onMouseMove(e);
        return consume_;
    }
    bool onMouseDrag(const MouseDragEventArgs& e) override {
        dragCount++;
        RectNode::onMouseDrag(e);
        return consume_;
    }
    int pressCount = 0, releaseCount = 0, moveCount = 0, dragCount = 0;
    Vec2 lastLocal {};
    bool consume_;
};

// Dispatch driver: a headless Window is the friend-access doorway into
// Node's private dispatch functions (its ToTree helpers are public and the
// platform glue uses the same path).
class Driver {
public:
    Driver() {
        prev_ = tc::internal::currentWindowCtx;
        tc::internal::currentWindowCtx = &win_.context();   // grab state lands here
    }
    ~Driver() {
        win_.context().rootNode = nullptr;
        tc::internal::currentWindowCtx = prev_;
    }
    void setRoot(Node* root) { win_.context().rootNode = root; }
    tc::internal::WindowContext& ctx() { return win_.context(); }

    void press(float x, float y, int button = 0) {
        MouseEventArgs e;
        e.globalPos = Vec2(x, y); e.pos = e.globalPos; e.button = button;
        e.syncLegacy();
        win_.dispatchMousePressToTree(e);
    }
    void release(float x, float y, int button = 0) {
        MouseEventArgs e;
        e.globalPos = Vec2(x, y); e.pos = e.globalPos; e.button = button;
        e.syncLegacy();
        win_.dispatchMouseReleaseToTree(e);
    }
    void move(float x, float y) {
        internal::MouseEventRaw e;
        e.globalPos = Vec2(x, y); e.pos = e.globalPos;
        e.globalDelta = Vec2(1, 0); e.delta = e.globalDelta;
        win_.dispatchMouseMoveToTree(e);
    }
private:
    Window win_;
    tc::internal::WindowContext* prev_ = nullptr;
};

int main() {
    Driver drv;

    // Tree: root(0,0,800,600) > panel(100,100,400,300) > label(50,50,100,40)
    // (positions are parent-relative; Probe enables events in its ctor)
    auto root = make_shared<Probe>(false);
    root->setPos(0, 0); root->setSize(800, 600);
    auto panel = make_shared<Probe>(true);    // consumes (a widget)
    panel->setPos(100, 100); panel->setSize(400, 300);
    auto label = make_shared<Probe>(false);   // does NOT consume (decorative)
    label->setPos(50, 50); label->setSize(100, 40);
    root->addChild(panel);
    panel->addChild(label);

    drv.setRoot(root.get());
    auto& ctx = drv.ctx();

    // --- 1. press on the label bubbles to the consuming panel -------------
    // label global rect = (150,150)-(250,190); hit (170, 160)
    drv.press(170, 160);
    check("press on non-consuming label reaches it first", label->pressCount == 1);
    check("press bubbles to the consuming panel", panel->pressCount == 1);
    check("root not reached (panel consumed)", root->pressCount == 0);
    check("panel got PANEL-local coordinates",
          panel->lastLocal.x == 70.0f && panel->lastLocal.y == 60.0f);
    check("grab goes to the consumer (panel)", ctx.grabbedNode == panel.get());

    // --- 2. drag + release follow the grab --------------------------------
    drv.move(180, 165);
    check("drag follows the grabbed panel", panel->dragCount == 1);
    check("label gets no drag", label->dragCount == 0);
    drv.release(180, 165);
    check("release goes to the grabbed panel", panel->releaseCount == 1);
    check("grab cleared after release", ctx.grabbedNode == nullptr);

    // --- 3. nobody consumes: event bubbles through and dies ---------------
    label->consume_ = false; panel->consume_ = false;
    int rootBefore = root->pressCount;
    drv.press(170, 160);
    check("press walks the whole chain (root reached)", root->pressCount == rootBefore + 1);
    check("unconsumed press grabs nothing", ctx.grabbedNode == nullptr);

    // --- 4. front-most consumer stops the walk ----------------------------
    label->consume_ = true;
    int panelBefore = panel->pressCount;
    drv.press(170, 160);
    check("consuming label stops the bubble (grabs the pointer)", ctx.grabbedNode == label.get());
    check("panel not reached when label consumes", panel->pressCount == panelBefore);
    drv.release(170, 160);

    // --- 5. move bubbles like scroll ---------------------------------------
    label->consume_ = false; panel->consume_ = true;
    int panelMoves = panel->moveCount;
    drv.move(170, 160);
    check("move bubbles past the label to the panel", panel->moveCount == panelMoves + 1);

    std::printf("\n%s  (%d failure%s)\n", g_fail ? "FAILED" : "PASSED",
                g_fail, g_fail == 1 ? "" : "s");
    std::fflush(stdout);
    return g_fail ? 1 : 0;
}
