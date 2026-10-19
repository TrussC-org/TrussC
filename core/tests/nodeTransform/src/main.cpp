// =============================================================================
// nodeTransform — behavioral regression test for Node global transforms and
// picking (#264)
//
// 1. addChild() / insertChild() / removeChild() / removeAllChildren() /
//    sweepDeadChildren() refresh the moved node's global matrix, so
//    getGlobalPos(), globalToLocal() and the local e.pos of mouse events use
//    the new parent right away.
// 2. Picking uses Mat4::tryInvert(): a node whose local matrix has no inverse
//    (an axis scaled to 0) is not hit, nor is its subtree. Small but valid
//    scales still invert. Headless: drives Node dispatch through a Window.
// =============================================================================

#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include <cmath>
#include <cstdio>

using namespace std;
using namespace tc;

namespace {

static int g_fail = 0;
static void check(const char* name, bool ok) {
    std::printf("%-64s %s\n", name, ok ? "PASS" : "FAIL");
    std::fflush(stdout);
    if (!ok) ++g_fail;
}

static bool near2(const Vec3& v, float x, float y) {
    return std::abs(v.x - x) < 1e-3f && std::abs(v.y - y) < 1e-3f;
}
static bool near2(const Vec2& v, float x, float y) {
    return std::abs(v.x - x) < 1e-3f && std::abs(v.y - y) < 1e-3f;
}

// A RectNode that records presses and consumes on demand.
class Probe : public RectNode {
public:
    explicit Probe(bool consume) : consume_(consume) { enableEvents(); }
    bool onMousePress(const MouseEventArgs& e) override {
        pressCount++; lastLocal = e.pos;
        RectNode::onMousePress(e);
        return consume_;
    }
    int pressCount = 0;
    Vec2 lastLocal {};
    bool consume_;
};

// Headless Window as the doorway into Node's dispatch (same as mouseBubbling).
class Driver {
public:
    Driver() {
        prev_ = tc::internal::currentWindowCtx();
        tc::internal::currentWindowCtx() = &win_.context();
    }
    ~Driver() {
        win_.context().rootNode.reset();
        tc::internal::currentWindowCtx() = prev_;
    }
    void setRoot(const Node::Ptr& root) { win_.context().rootNode = root; }
    void press(float x, float y) {
        MouseEventArgs e;
        e.globalPos = Vec2(x, y); e.pos = e.globalPos; e.button = 0;
        e.syncLegacy();
        win_.dispatchMousePressToTree(e);
        MouseEventArgs r = e;
        win_.dispatchMouseReleaseToTree(r);
    }
    void tick() { win_.tickTree(); }
private:
    Window win_;
    tc::internal::WindowContext* prev_ = nullptr;
};

static void testReparent() {
    Driver drv;
    // root(800x600) > A(0,0), B(300,200); A > p(10,10, 50x50)
    auto root = make_shared<Probe>(false);
    root->setSize(800, 600);
    auto a = make_shared<Node>();
    auto b = make_shared<Node>();
    b->setPos(300, 200);
    root->addChild(a);
    root->addChild(b);
    auto p = make_shared<Probe>(true);
    p->setPos(10, 10); p->setSize(50, 50);
    a->addChild(p);
    auto leaf = make_shared<Node>();
    leaf->setPos(5, 7);
    p->addChild(leaf);
    drv.setRoot(root);

    check("p global pos under A is (10,10)", near2(p->getGlobalPos(), 10, 10));
    check("descendant global pos under A is (15,17)", near2(leaf->getGlobalPos(), 15, 17));

    // addChild moves p to B
    b->addChild(p);
    check("addChild: global pos follows new parent (310,210)",
          near2(p->getGlobalPos(), 310, 210));
    check("addChild: descendant cache follows new parent", near2(leaf->getGlobalPos(), 315, 217));
    check("addChild: globalToLocal uses new parent",
          near2(p->globalToLocal(Vec3(320, 220, 0)), 10, 10));
    drv.press(320, 220);
    check("addChild: press reaches p", p->pressCount == 1);
    check("addChild: e.pos is local to the new parent (10,10)",
          near2(p->lastLocal, 10, 10));

    // removeChild: no parent, global == local
    b->removeChild(p);
    check("removeChild: global pos equals local pos", near2(p->getGlobalPos(), 10, 10));
    check("removeChild: descendant cache follows detached node", near2(leaf->getGlobalPos(), 15, 17));

    // insertChild back under B
    p->getGlobalPos();   // fill the cache
    b->insertChild(0, p);
    check("insertChild: global pos follows new parent (310,210)",
          near2(p->getGlobalPos(), 310, 210));
    check("insertChild: descendant cache follows new parent", near2(leaf->getGlobalPos(), 315, 217));

    // removeAllChildren
    p->getGlobalPos();
    b->removeAllChildren();
    check("removeAllChildren: global pos equals local pos",
          near2(p->getGlobalPos(), 10, 10));
    check("removeAllChildren: descendant cache follows detached node", near2(leaf->getGlobalPos(), 15, 17));

    // sweepDeadChildren
    b->addChild(p);
    check("re-added under B (310,210)", near2(p->getGlobalPos(), 310, 210));
    leaf->getGlobalMatrix();
    p->destroy();
    drv.tick();   // updateTree() sweeps dead children
    check("sweepDeadChildren: global pos equals local pos",
          near2(p->getGlobalPos(), 10, 10));
    check("sweepDeadChildren: descendant cache follows detached node", near2(leaf->getGlobalPos(), 15, 17));

    // keepGlobalPosition where the computed local pos equals the current one:
    // q at local (10,10) under C(50,50) is global (60,60); D sits at (50,50)
    // too, so the move keeps local (10,10) and setPos() sees no change.
    auto c = make_shared<Node>(); c->setPos(50, 50);
    auto d = make_shared<Node>();
    auto e = make_shared<Node>(); e->setPos(50, 50);
    root->addChild(c);
    root->addChild(e);
    e->addChild(d);   // d global (50,50), local (0,0)
    auto q = make_shared<Node>(); q->setPos(10, 10);
    c->addChild(q);
    check("q global (60,60) under C", near2(q->getGlobalPos(), 60, 60));
    d->addChild(q, true);
    check("keepGlobalPosition: local pos unchanged (10,10)", near2(q->getPos(), 10, 10));
    check("keepGlobalPosition: global pos (60,60) via new parent",
          near2(q->getGlobalPos(), 60, 60));
    // Move D; q's global must follow D (proves the matrix tracks the new parent)
    d->setPos(100, 0);
    check("keepGlobalPosition: global follows new parent after it moves",
          near2(q->getGlobalPos(), 160, 60));

    // Equal local positions can still produce different global matrices.
    // Warm both caches before moving to a parent rotated around the same origin.
    c->addChild(q);
    q->setPos(0, 0);
    auto qLeaf = make_shared<Node>(); qLeaf->setPos(10, 0);
    q->addChild(qLeaf);
    q->getGlobalMatrix();
    check("keepGlobalPosition: warm descendant cache before rotation", near2(qLeaf->getGlobalPos(), 60, 50));
    auto rotatedParent = make_shared<Node>();
    rotatedParent->setPos(50, 50); rotatedParent->setRot(QUARTER_TAU);
    root->addChild(rotatedParent);
    rotatedParent->addChild(q, true);
    check("keepGlobalPosition: setPos sees unchanged local origin", near2(q->getPos(), 0, 0));
    check("keepGlobalPosition: global matrix immediately uses rotation",
          near2(q->localToGlobal(Vec3(10, 0, 0)), 50, 60));
    check("keepGlobalPosition: descendant cache immediately uses rotation", near2(qLeaf->getGlobalPos(), 50, 60));
}

static void testScaleZero() {
    Driver drv;
    // root > container(400,300) > btn(100,100, 80x40)
    auto root = make_shared<Probe>(false);
    root->setSize(800, 600);
    auto container = make_shared<Node>();
    container->setPos(400, 300);
    root->addChild(container);
    auto btn = make_shared<Probe>(true);
    btn->setPos(100, 100); btn->setSize(80, 40);
    container->addChild(btn);
    drv.setRoot(root);

    drv.press(520, 410);
    check("scale 1: btn hit at its rect", btn->pressCount == 1);

    btn->setScale(0);
    int rootBefore = root->pressCount;
    drv.press(420, 310);   // (20,10) from container origin
    check("scale 0: btn not hit near container origin", btn->pressCount == 1);
    check("scale 0: press reaches root", root->pressCount == rootBefore + 1);
    drv.press(520, 410);
    check("scale 0: btn not hit at its old rect", btn->pressCount == 1);

    // Valid tiny scale: inverts, so its (tiny) area sits at (500,400) and
    // nearby points miss it.
    btn->setScale(1e-6f, 1e-6f);
    drv.press(420, 310);
    drv.press(510, 405);
    check("scale 1e-6: btn not hit", btn->pressCount == 1);

    auto tiny = make_shared<Probe>(true);
    tiny->setPos(200, 100); tiny->setSize(80, 40); tiny->setScale(1e-6f);
    root->addChild(tiny);
    drv.press(200, 100);
    check("uniform scale 1e-6: tiny area remains pickable", tiny->pressCount == 1);
    drv.press(210, 105);
    check("uniform scale 1e-6: nearby point misses tiny area", tiny->pressCount == 1);

    btn->setScale(1);
    drv.press(510, 405);
    check("scale restored: btn hit near (500,400)", btn->pressCount == 2);

    // Subtree of a scale-0 node is not picked either
    auto child = make_shared<Probe>(true);
    child->setPos(0, 0); child->setSize(80, 40);
    btn->addChild(child);
    btn->setScale(1, 0);
    int childBefore = child->pressCount;
    drv.press(420, 310);
    drv.press(510, 405);
    check("scale (1,0): subtree not hit", child->pressCount == childBefore &&
          btn->pressCount == 2);
    btn->setScale(1);

    // Clipping RectNode at scale 0 hides its children from picking
    auto clip = make_shared<RectNode>();
    clip->setPos(0, 0); clip->setSize(200, 200);
    clip->setClipping(true);
    auto inner = make_shared<Probe>(true);
    inner->setPos(10, 10); inner->setSize(50, 50);
    clip->addChild(inner);
    root->addChild(clip);
    drv.press(20, 20);
    check("clipping RectNode scale 1: inner hit", inner->pressCount == 1);
    clip->setScale(0);
    drv.press(20, 20);
    check("clipping RectNode scale 0: inner not hit", inner->pressCount == 1);
}

static void testTryInvert() {
    Mat4 out = Mat4::translate(1, 2, 3);
    const Mat4 before = out;
    bool ok = Mat4::scale(0, 0, 0).tryInvert(out);
    bool unchanged = true;
    for (int i = 0; i < 16; i++) unchanged = unchanged && out.m[i] == before.m[i];
    check("tryInvert: fails on scale(0,0,0)", !ok);
    check("tryInvert: out unchanged on failure", unchanged);
    check("tryInvert: fails on scale(1,1,0)", !Mat4::scale(1, 1, 0).tryInvert(out));
    check("tryInvert: fails on rotated scale(1,0,1)",
          !(Mat4::rotateZ(0.3f * TAU) * Mat4::scale(1, 0, 1)).tryInvert(out));

    Mat4 m = Mat4::translate(400, 300, 0) * Mat4::rotateZ(0.1f * TAU) * Mat4::scale(2, 3, 1);
    check("tryInvert: succeeds on a regular transform", m.tryInvert(out));
    Mat4 id = m * out;
    bool isId = true;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            isId = isId && std::abs(id.m[r * 4 + c] - (r == c ? 1.0f : 0.0f)) < 1e-4f;
    check("tryInvert: m * inverse == identity", isId);

    // Small but valid scale inverts (relative test, not absolute |det|)
    Mat4 small = Mat4::translate(100, 100, 0) * Mat4::scale(1e-3f, 1e-3f, 1e-3f);
    check("tryInvert: succeeds on uniform scale 1e-3 (det 1e-9)", small.tryInvert(out));
    Vec3 back = out * Vec3(100.0005f, 100.0005f, 0);
    check("tryInvert: small-scale inverse maps back correctly",
          std::abs(back.x - 0.5f) < 0.05f && std::abs(back.y - 0.5f) < 0.05f);

    for (float scale : {1e-6f, 1e-15f, 1e15f, 1e20f}) {
        Mat4 scaled = Mat4::scale(scale, scale, scale);
        bool invertible = scaled.tryInvert(out);
        Mat4 product = scaled * out;
        bool identity = invertible;
        for (int k = 0; k < 16; k++)
            identity = identity && std::abs(product.m[k] - (k % 5 == 0 ? 1.0f : 0.0f)) < 1e-5f;
        char label[100];
        std::snprintf(label, sizeof(label), "tryInvert: uniform scale %g has a valid inverse", scale);
        check(label, identity);
    }

    check("inverted(): keeps absolute cutoff for valid tiny scale",
          [] { Mat4 i = Mat4::scale(1e-6f, 1e-6f, 1e-6f).inverted(); Mat4 e;
               for (int k = 0; k < 16; k++) if (i.m[k] != e.m[k]) return false;
               return true; }());

    check("inverted(): still returns identity on singular",
          [] { Mat4 i = Mat4::scale(0, 0, 0).inverted(); Mat4 e;
               for (int k = 0; k < 16; k++) if (i.m[k] != e.m[k]) return false;
               return true; }());
}

} // namespace

TC_CORE_TEST_MAIN() {
    testReparent();
    testScaleZero();
    testTryInvert();
    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED",
                g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
