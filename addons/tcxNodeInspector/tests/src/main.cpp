// =============================================================================
// tcxNodeInspector tests - headless behavioral test (no window, no GPU).
//
// Built and run by CI on every push/PR via examples/build_all.py
// --addon-tests-only (exit 0 = pass, non-zero = fail). Console only: hand
// edits are recorded through tcx::nodeinspector::internal::
// recordTouchedForTests() (what the Inspector panel calls), and the tree is
// updated through a headless Window's tickTree(), which runs updateTree() and
// so the sweep of destroyed children.
//
// Touched record (#326):
//   - a node destroyed while the test still holds it reports destroyed: true
//     with the value as of the last edit, right after destroy() and again
//     after the sweep; a child of a destroyed node does too after the sweep;
//     a live node reports its current value
//   - a removed mod reports modRemoved: true with the value as of the last edit
//   - a mod of the same type added again right away continues the same entry:
//     one entry, the new mod's value, no modRemoved, and a later edit updates
//     that entry
//   - a mod of another type added in place of a removed one leaves the entry
//     at modRemoved, under the old type, whatever address the new mod gets
// =============================================================================

#include <TrussC.h>
#include <tcxNodeInspector.h>

#include <cstdio>

using namespace std;
using namespace tc;
using tcx::nodeinspector::NodeInspector;

static int g_pass = 0, g_fail = 0;
static void check(const char* name, bool ok) {
    std::printf("%-64s %s\n", name, ok ? "PASS" : "FAIL");
    std::fflush(stdout);
    ok ? ++g_pass : ++g_fail;
}

struct Ball : public Node {
    float radius = 10.0f;
    TC_REFLECT(Ball, Node) {
        TC_VALUE(radius)
    }
};

struct OutlineMod : public Mod {
    float gap = 6.0f;
    TC_REFLECT(OutlineMod, Mod) {
        TC_VALUE(gap)
    }
};

// Same layout and member name as OutlineMod, another type.
struct GlowMod : public Mod {
    float gap = 100.0f;
    TC_REFLECT(GlowMod, Mod) {
        TC_VALUE(gap)
    }
};

// A headless Window drives updateTree() (and so the sweep) on a root.
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
    void tick() { win_.tickTree(); }
private:
    Window win_;
    tc::internal::WindowContext* prev_ = nullptr;
};

// The entries for (node, mod type, member); modType "" = the node's own member.
static vector<Json> entries(uint64_t nodeId, const string& modType, const string& member) {
    vector<Json> out;
    for (auto& e : NodeInspector::instance().getTouched()) {
        if (e.value("nodeId", uint64_t(0)) != nodeId) continue;
        if (e.value("mod", string()) != modType) continue;
        if (e.value("member", string()) != member) continue;
        out.push_back(e);
    }
    return out;
}

static bool flag(const Json& e, const char* key) {
    return e.contains(key) && e[key] == true;
}

static bool valueIs(const Json& e, float v) {
    return e.contains("value") && e["value"].is_number() && e["value"].get<float>() == v;
}

// ---------------------------------------------------------------------------
// Destroyed nodes the app still holds
// ---------------------------------------------------------------------------
static void testDestroyed() {
    auto& insp = NodeInspector::instance();
    insp.resetTouched();

    Driver drv;
    auto root = make_shared<Node>();
    auto ball = make_shared<Ball>();      // held here, like an app's Ptr member
    auto other = make_shared<Ball>();     // stays alive
    auto parent = make_shared<Node>();
    auto child = make_shared<Ball>();     // child of a node that gets destroyed
    root->addChild(ball);
    root->addChild(other);
    root->addChild(parent);
    parent->addChild(child);
    drv.setRoot(root);
    drv.tick();

    ball->radius = 20.0f;
    tcx::nodeinspector::internal::recordTouchedForTests(insp, ball.get(), nullptr, "radius");
    other->radius = 30.0f;
    tcx::nodeinspector::internal::recordTouchedForTests(insp, other.get(), nullptr, "radius");
    child->radius = 40.0f;
    tcx::nodeinspector::internal::recordTouchedForTests(insp, child.get(), nullptr, "radius");

    auto e = entries(ball->getInstanceId(), "", "radius");
    check("live node: one entry, current value, no destroyed",
          e.size() == 1 && valueIs(e[0], 20.0f) && !flag(e[0], "destroyed"));

    ball->destroy();
    ball->radius = 99.0f;                 // a change after destroy() is not reported
    e = entries(ball->getInstanceId(), "", "radius");
    check("destroy(), still held: destroyed true", e.size() == 1 && flag(e[0], "destroyed"));
    check("destroy(), still held: value as of the last edit", e.size() == 1 && valueIs(e[0], 20.0f));

    parent->destroy();
    drv.tick();                           // sweep: removed from the tree, still held
    check("sweep removed the destroyed nodes from the tree",
          ball->getParent() == nullptr && parent->getParent() == nullptr);
    e = entries(ball->getInstanceId(), "", "radius");
    check("after the sweep, still held: destroyed true, last value",
          e.size() == 1 && flag(e[0], "destroyed") && valueIs(e[0], 20.0f));

    e = entries(child->getInstanceId(), "", "radius");
    check("child of a destroyed node after the sweep: destroyed true",
          e.size() == 1 && flag(e[0], "destroyed") && valueIs(e[0], 40.0f));

    other->radius = 31.0f;                // a change from code: reported as current value
    e = entries(other->getInstanceId(), "", "radius");
    check("live sibling: current value, no destroyed",
          e.size() == 1 && valueIs(e[0], 31.0f) && !flag(e[0], "destroyed"));

    const uint64_t ballId = ball->getInstanceId();
    ball.reset();                         // freed
    e = entries(ballId, "", "radius");
    check("freed node: destroyed true, last value",
          e.size() == 1 && flag(e[0], "destroyed") && valueIs(e[0], 20.0f));
}

// ---------------------------------------------------------------------------
// Mods: removed, re-added (same type), replaced (other type)
// ---------------------------------------------------------------------------
static void testMods() {
    auto& insp = NodeInspector::instance();

    // (b) removed, not re-added
    {
        insp.resetTouched();
        auto node = make_shared<Ball>();
        auto* m = node->addMod<OutlineMod>();
        m->gap = 12.0f;
        tcx::nodeinspector::internal::recordTouchedForTests(insp, node.get(), m, "gap");
        auto e = entries(node->getInstanceId(), "OutlineMod", "gap");
        check("mod: entry under its type, current value, no modRemoved",
              e.size() == 1 && valueIs(e[0], 12.0f) && !flag(e[0], "modRemoved"));

        node->removeMod<OutlineMod>();
        e = entries(node->getInstanceId(), "OutlineMod", "gap");
        check("removed mod: modRemoved true, last value",
              e.size() == 1 && flag(e[0], "modRemoved") && valueIs(e[0], 12.0f));
    }

    // (c) removed, a mod of the same type added right away
    {
        insp.resetTouched();
        auto node = make_shared<Ball>();
        auto* m = node->addMod<OutlineMod>();
        m->gap = 12.0f;
        tcx::nodeinspector::internal::recordTouchedForTests(insp, node.get(), m, "gap");
        node->removeMod<OutlineMod>();
        auto* m2 = node->addMod<OutlineMod>();   // gap = 6 (default)
        std::printf("  (re-added mod %s the freed address)\n", m2 == m ? "reuses" : "does not reuse");

        auto e = entries(node->getInstanceId(), "OutlineMod", "gap");
        check("re-added same type: one entry, new mod's value, no modRemoved",
              e.size() == 1 && valueIs(e[0], 6.0f) && !flag(e[0], "modRemoved"));

        m2->gap = 8.0f;
        tcx::nodeinspector::internal::recordTouchedForTests(insp, node.get(), m2, "gap");
        e = entries(node->getInstanceId(), "OutlineMod", "gap");
        check("edit of the re-added mod updates the same entry",
              e.size() == 1 && valueIs(e[0], 8.0f) && !flag(e[0], "modRemoved"));
        check("one entry in the whole record", insp.getTouched().size() == 1);
    }

    // a mod of another type in place of the removed one
    {
        insp.resetTouched();
        auto node = make_shared<Ball>();
        auto* m = node->addMod<OutlineMod>();
        m->gap = 12.0f;
        tcx::nodeinspector::internal::recordTouchedForTests(insp, node.get(), m, "gap");
        node->removeMod<OutlineMod>();
        auto* g = node->addMod<GlowMod>();       // same size: may land at m's address
        std::printf("  (other-type mod %s the freed address)\n",
                    static_cast<void*>(g) == static_cast<void*>(m) ? "reuses" : "does not reuse");

        auto e = entries(node->getInstanceId(), "OutlineMod", "gap");
        check("other type in its place: old entry modRemoved, last value",
              e.size() == 1 && flag(e[0], "modRemoved") && valueIs(e[0], 12.0f));
        check("other type in its place: no entry under the new type",
              entries(node->getInstanceId(), "GlowMod", "gap").empty());
    }

    // a mod of a destroyed node: destroyed wins
    {
        insp.resetTouched();
        auto node = make_shared<Ball>();
        auto* m = node->addMod<OutlineMod>();
        m->gap = 12.0f;
        tcx::nodeinspector::internal::recordTouchedForTests(insp, node.get(), m, "gap");
        node->destroy();
        auto e = entries(node->getInstanceId(), "OutlineMod", "gap");
        check("mod of a destroyed node: destroyed true, last value",
              e.size() == 1 && flag(e[0], "destroyed") && valueIs(e[0], 12.0f));
    }
    insp.resetTouched();
}

int main() {
    std::printf("=== tcxNodeInspector tests ===\n");
    testDestroyed();
    testMods();
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
