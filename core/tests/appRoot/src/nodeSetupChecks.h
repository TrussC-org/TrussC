#pragma once
// =============================================================================
// Node / Mod setup timing (#949): a node's or mod's setup() runs once, when it
// joins the App's tree. Checked through a headless Window whose root is a
// plain Node, driven the way the platform glue drives it (tickTree()).
//   - The root's first tick sets up the tree built before it, parent-first in
//     pre-order, each node's mods right after the node.
//   - addChild / insertChild under a set-up node in a live tree, and addMod on
//     one, run setup() before they return; a detached subtree waits and is set
//     up as a whole when it is attached.
//   - A mod added inside its owner's setup() is set up at once.
//   - Nothing is set up twice: removing and re-adding a node does not repeat
//     it, but children added while it was detached are set up on re-add.
//   - setup() runs with the tree's window context current.
//   - An update traversal skips a child removed after its parent's snapshot.
// =============================================================================

#include <TrussC.h>
#include <string>
#include <vector>

namespace nodesetup {

using namespace std;
using namespace tc;

inline vector<string> g_log;
inline string joined() {
    string s;
    for (auto& e : g_log) s += (s.empty() ? "" : " ") + e;
    return s;
}

class LogMod : public Mod {
public:
    explicit LogMod(string name) : name_(std::move(name)) {}
    int setups = 0;
protected:
    void setup() override { ++setups; g_log.push_back(name_); }
private:
    string name_;
};
class OtherMod : public LogMod { using LogMod::LogMod; };

class Rec : public Node {
public:
    explicit Rec(string name) : name_(std::move(name)) {}
    int setups = 0, updates = 0;
    string configured;   // set by callers before addChild
    string seenConfig;   // what setup() saw
    const internal::WindowContext* setupContext = nullptr;
    function<void(Rec&)> onSetup, onUpdate;
    void setup() override {
        ++setups;
        seenConfig = configured;
        setupContext = &internal::currentWindowContext();
        g_log.push_back(name_);
        if (onSetup) onSetup(*this);
    }
    void update() override {
        ++updates;
        if (onUpdate) onUpdate(*this);
    }
private:
    string name_;
};

inline shared_ptr<Rec> rec(const string& name) { return make_shared<Rec>(name); }

// A headless Window as the doorway into the tree (as in nodeTransform).
class Driver {
public:
    Driver() {
        internal::WindowRequestAccess::bindForTests(win_, 101);
        prev_ = internal::currentWindowCtx();
        internal::currentWindowCtx() = &context();
    }
    ~Driver() {
        context().rootNode.reset();
        internal::currentWindowCtx() = prev_;
    }
    internal::WindowContext& context() { return internal::WindowRequestAccess::context(win_); }
    void setRoot(const Node::Ptr& root) { context().rootNode = root; }
    void tick() { internal::WindowRequestAccess::tickTree(win_); }
private:
    Window win_;
    internal::WindowContext* prev_ = nullptr;
};

template<class Check>
inline void run(Check check) {
    Driver drv;

    // --- The root's first tick sets up the tree built before it ---
    g_log.clear();
    auto root = rec("root");
    auto a = rec("a"), a1 = rec("a1"), b = rec("b");
    root->addChild(a);
    a->addChild(a1);
    root->addChild(b);
    a->addMod<LogMod>("a.mod");
    check("detached tree: nothing set up before the root starts", g_log.empty());
    drv.setRoot(root);
    drv.tick();
    check("root start: pre-order, mods right after their node",
          joined() == "root a a.mod a1 b");
    drv.tick();
    check("root start: later ticks set nothing up again",
          root->setups == 1 && a->setups == 1 && a1->setups == 1 && b->setups == 1 &&
          a->getMod<LogMod>()->setups == 1);

    // --- addChild under a live tree sets up before it returns ---
    g_log.clear();
    auto c = rec("c");
    c->configured = "before";
    root->addChild(c);
    check("live addChild: setup() ran inside addChild()", c->setups == 1 && joined() == "c");
    check("live addChild: configuration made before addChild is seen", c->seenConfig == "before");
    g_log.clear();
    auto ins = rec("ins");
    root->insertChild(0, ins);
    check("live insertChild: setup() ran inside insertChild()", ins->setups == 1);

    // --- A detached subtree is set up as a whole when attached ---
    g_log.clear();
    auto p = rec("p"), q = rec("q"), q1 = rec("q1"), r = rec("r");
    p->addChild(q);
    q->addChild(q1);
    p->addChild(r);
    p->addMod<LogMod>("p.mod");
    q1->addMod<LogMod>("q1.mod");
    check("detached addChild / addMod: nothing set up", g_log.empty());
    b->addChild(p);
    check("attach: subtree parent-first in pre-order, mods after their node",
          joined() == "p p.mod q q1 q1.mod r");

    // --- Mods ---
    g_log.clear();
    auto* liveMod = b->addMod<LogMod>("b.mod");
    check("addMod on a live, set-up node: setup() ran inside addMod()",
          liveMod->setups == 1 && joined() == "b.mod");
    g_log.clear();
    auto owner = rec("owner");
    owner->onSetup = [](Rec& self) {
        self.addMod<OtherMod>("inner.mod");
        g_log.push_back("owner.after");
    };
    owner->addMod<LogMod>("outer.mod");
    root->addChild(owner);
    {
        // The mod added inside setup() runs first; outer.mod after the owner.
        check("mod added in its owner's setup(): set up at once, others after the owner",
              joined() == "owner inner.mod owner.after outer.mod");
        check("mods: each set up once",
              owner->getMod<OtherMod>()->setups == 1 && owner->getMod<LogMod>()->setups == 1);
    }

    // --- A child added inside a parent's setup() is set up at once ---
    g_log.clear();
    auto builder = rec("builder");
    auto built = rec("built");
    builder->onSetup = [built](Rec& self) {
        self.addChild(built);
        g_log.push_back("builder.after");
    };
    root->addChild(builder);
    check("addChild in a parent's setup(): set up at once, then not again",
          joined() == "builder built builder.after" && built->setups == 1);

    // --- Removing and re-adding never sets up twice ---
    g_log.clear();
    root->removeChild(c);
    auto late = rec("late");
    c->addChild(late);
    check("detached (set-up) node: a new child waits", late->setups == 0);
    root->addChild(c);
    check("re-add: the node is not set up again, its new child is",
          c->setups == 1 && late->setups == 1 && joined() == "late");
    a->addChild(c);   // reparent within the live tree
    check("reparent in a live tree: no second setup()", c->setups == 1 && late->setups == 1);

    // --- Inactive nodes are set up when they join ---
    auto off = rec("off");
    off->setActive(false);
    root->addChild(off);
    check("inactive node: set up when it joins", off->setups == 1);

    // --- Every node that gets update() has been set up ---
    auto spawned = rec("spawned");
    bool setUpBeforeNextLine = false;
    // ins is a child of root, so root's child list was copied before this.
    ins->onUpdate = [&](Rec&) {
        if (spawned->getParent()) return;
        root->addChild(spawned);
        setUpBeforeNextLine = spawned->setups == 1;
    };
    drv.tick();
    check("addChild inside update(): set up before addChild() returns", setUpBeforeNextLine);
    check("... and not updated in the same pass (the parent's copy)", spawned->updates == 0);
    drv.tick();
    check("... updated on the next pass", spawned->updates == 1 && spawned->setups == 1);

    // --- The traversal skips a child removed after its parent's snapshot ---
    auto first = rec("first"), second = rec("second");
    auto holder = rec("holder");
    holder->addChild(first);
    holder->addChild(second);
    root->addChild(holder);
    first->onUpdate = [&](Rec&) { holder->removeChild(second); };
    const int secondUpdates = second->updates;
    drv.tick();
    check("snapshot: a sibling removed during update() is not updated",
          second->updates == secondUpdates);
    first->onUpdate = nullptr;

    // --- setup() runs with the tree's window context current ---
    {
        Driver other;   // another window's context is current now
        auto inOther = rec("inOther");
        root->addChild(inOther);
        check("window context: setup() sees the tree's window, not the caller's",
              inOther->setupContext == &drv.context());
        check("window context: the caller's context is restored",
              &internal::currentWindowContext() == &other.context());
    }

    // --- A tree that is not live (no started root) never sets up ---
    g_log.clear();
    auto loose = rec("loose"), looseChild = rec("looseChild");
    loose->addChild(looseChild);
    looseChild->addMod<LogMod>("loose.mod");
    check("no live root: nothing set up", g_log.empty());
}

} // namespace nodesetup
