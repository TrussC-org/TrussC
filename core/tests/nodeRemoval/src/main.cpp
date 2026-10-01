// =============================================================================
// nodeRemoval — regression test for node lifetime in mouse dispatch (#255)
//
// The window context holds the hovered / grabbed / selected node weakly, and
// mouse dispatch keeps a strong reference to every node whose handlers it
// runs. So:
//   - a node removed with removeChild() / removeAllChildren() while the tree
//     was its last owner is never touched again: not by the next hover
//     update, drag or release, and getSelectedNode() returns null;
//   - a handler or an Event listener that removes its own node or an ancestor
//     (the usual close button) runs to the end on a live node, the node's
//     mods still get the event (checked for the grab release), and the node
//     is freed when dispatch returns.
//     Covered for every dispatch path: the grab's release and drag, the
//     press / release / move / scroll bubbling loops, and the hover update's
//     mouseEnter / mouseLeave;
//   - isMouseOver() and getSelectedNode() never match a new node that took a
//     freed node's address, and setSelectedNode() with a node no shared_ptr
//     owns clears the selection;
//   - working code behaves as before: a removed node the app still holds gets
//     its Leave, reparenting fires no extra Enter / Leave, and destroy()
//     drops hover, grab and selection at once.
// Every case runs twice: in a secondary window's context and in the main
// window's.
//
// Use-after-free is made observable instead of being left to the heap. The
// nodes under test live in fixed slots, and when one is freed its deleter
// constructs a sentinel Probe in the same memory. A call that still reaches
// that address through a stale pointer lands on the sentinel, which counts
// it (g_staleCalls), instead of on freed memory that may or may not crash.
// Removing a node inside its own handler is checked from the handler itself:
// the node must still be alive right after the removal.
//
// Headless: dispatch is driven through a Window object's tree helpers (the
// path the platform glue uses) and through App's event handlers (the main
// window's path), no native window. Only public API and that glue are used,
// so the file also builds against the code before #255, where it fails.
// =============================================================================

#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>
#include <memory>
#include <new>
#include <string>

using namespace std;
using namespace tc;

namespace {

static int g_fail = 0;
static string g_scope;   // "[window] " / "[main] ", prefixed to each check
static void check(const string& name, bool ok) {
    std::printf("%-86s %s\n", (g_scope + name).c_str(), ok ? "PASS" : "FAIL");
    std::fflush(stdout);
    if (!ok) ++g_fail;
}

// Calls that reached a freed node's address (counted by the sentinels), and
// exceptions out of a dispatch (e.g. bad_weak_ptr from shared_from_this() on
// a node no shared_ptr owns any more).
static int g_staleCalls = 0;
static int g_dispatchErrors = 0;

// ---------------------------------------------------------------------------
// Probe: a RectNode that counts its mouse events, returns `consume` from the
// press / release / move / drag / scroll handlers and runs an optional hook in
// each. A sentinel Probe is what a freed Probe's memory holds afterwards: it
// is in no tree and owned by nothing, so every call on it came through a
// stale pointer.
// ---------------------------------------------------------------------------
class Probe : public RectNode {
public:
    struct SentinelTag {};
    Probe() { enableEvents(); }
    explicit Probe(SentinelTag) : sentinel_(true) {}

    bool consume = true;
    function<void(Probe&)> onPress, onRelease, onMove, onDrag, onScroll, onEnter, onLeave;
    int pressCount = 0, releaseCount = 0, moveCount = 0, dragCount = 0;
    int scrollCount = 0, enterCount = 0, leaveCount = 0;

    bool onMousePress(const MouseEventArgs& e) override {
        if (stale()) return false;
        ++pressCount;
        RectNode::onMousePress(e);   // fires mousePressed + the legacy hook
        if (onPress) onPress(*this);
        return consume;
    }
    bool onMouseRelease(const MouseEventArgs& e) override {
        if (stale()) return false;
        ++releaseCount;
        RectNode::onMouseRelease(e);
        if (onRelease) onRelease(*this);
        return consume;
    }
    bool onMouseMove(const MouseMoveEventArgs& e) override {
        if (stale()) return false;
        ++moveCount;
        RectNode::onMouseMove(e);
        if (onMove) onMove(*this);
        return consume;
    }
    bool onMouseDrag(const MouseDragEventArgs& e) override {
        if (stale()) return false;
        ++dragCount;
        RectNode::onMouseDrag(e);
        if (onDrag) onDrag(*this);
        return consume;
    }
    bool onMouseScroll(const ScrollEventArgs& e) override {
        if (stale()) return false;
        ++scrollCount;
        RectNode::onMouseScroll(e);
        if (onScroll) onScroll(*this);
        return consume;
    }
    void onMouseEnter() override {
        if (stale()) return;
        ++enterCount;
        if (onEnter) onEnter(*this);
    }
    void onMouseLeave() override {
        if (stale()) return;
        ++leaveCount;
        if (onLeave) onLeave(*this);
    }

    // Legacy hooks RectNode's rich handlers call on `this` after notifying
    // their Event (mousePressed / mouseReleased / ...).
    bool onMousePress(Vec2, int) override { stale(); return false; }
    bool onMouseRelease(Vec2, int) override { stale(); return false; }
    bool onMouseDrag(Vec2, int) override { stale(); return false; }
    bool onMouseScroll(Vec2, Vec2) override { stale(); return false; }

private:
    bool sentinel_ = false;
    bool stale() {
        if (!sentinel_) return false;
        ++g_staleCalls;
        return true;
    }
};

// ---------------------------------------------------------------------------
// Probe slots
// ---------------------------------------------------------------------------
constexpr int kSlots = 256;
alignas(Probe) static unsigned char g_slots[kSlots][sizeof(Probe)];
enum class Slot { Free, Live, Sentinel };
static Slot g_slotState[kSlots];

static int slotOf(const void* p) {
    for (int i = 0; i < kSlots; ++i) {
        if (p == static_cast<const void*>(g_slots[i])) return i;
    }
    return -1;
}

// Destroys the probe, then puts a sentinel where it was.
struct SentinelDeleter {
    void operator()(Probe* p) const {
        p->~Probe();
        ::new (static_cast<void*>(p)) Probe(Probe::SentinelTag{});
        g_slotState[slotOf(p)] = Slot::Sentinel;
    }
};

// A new probe owned by a shared_ptr, in a free slot, or in `reuse` (a freed
// probe's slot) to put a new node at a freed node's address.
static shared_ptr<Probe> makeProbe(int reuse = -1) {
    int i = reuse;
    if (i < 0) {
        i = 0;
        while (i < kSlots && g_slotState[i] != Slot::Free) ++i;
    } else if (g_slotState[i] == Slot::Sentinel) {
        std::launder(reinterpret_cast<Probe*>(g_slots[i]))->~Probe();
    } else {
        i = kSlots;   // not a freed probe's slot: a bug in this test
    }
    if (i >= kSlots) {
        std::printf("nodeRemoval: no probe slot available\n");
        std::exit(2);
    }
    g_slotState[i] = Slot::Live;
    Probe* p = ::new (static_cast<void*>(g_slots[i])) Probe();
    return shared_ptr<Probe>(p, SentinelDeleter{});
}

static shared_ptr<Probe> makeProbeAt(Vec2 pos, Vec2 size) {
    auto p = makeProbe();
    p->setPos(pos.x, pos.y);
    p->setSize(size.x, size.y);
    return p;
}

// ---------------------------------------------------------------------------
// Drivers: one per window context
// ---------------------------------------------------------------------------
class Driver {
public:
    virtual ~Driver() = default;
    App& root() { return *app_; }

    void press(Vec2 p) { guarded("press", [&] { doPress(mouseArgs(p)); }); }
    void release(Vec2 p) { guarded("release", [&] { doRelease(mouseArgs(p)); }); }
    void move(Vec2 p) {
        internal::MouseEventRaw e;
        e.globalPos = p; e.pos = p;
        e.globalDelta = Vec2(1, 0); e.delta = e.globalDelta;
        guarded("move", [&] { doMove(e); });
    }
    void scroll(Vec2 p) {
        ScrollEventArgs e;
        e.globalPos = p; e.pos = p; e.scroll = Vec2(0, 1);
        e.syncLegacy();
        guarded("scroll", [&] { doScroll(e); });
    }
    // One frame: tree update + hover update with the cursor at p
    void tick(Vec2 p) { guarded("tick", [&] { doTick(p); }); }

    // Back to an empty tree, no grab, no hover, no selection
    void clear() {
        release(Vec2(-100, -100));
        root().removeAllChildren();
        tick(Vec2(-100, -100));
        setSelectedNode(nullptr);
    }

protected:
    shared_ptr<App> app_;
    virtual void doPress(const MouseEventArgs& e) = 0;
    virtual void doRelease(const MouseEventArgs& e) = 0;
    virtual void doMove(const internal::MouseEventRaw& e) = 0;
    virtual void doScroll(const ScrollEventArgs& e) = 0;
    virtual void doTick(Vec2 p) = 0;

private:
    static MouseEventArgs mouseArgs(Vec2 p) {
        MouseEventArgs e;
        e.globalPos = p; e.pos = p; e.button = 0;
        e.syncLegacy();
        return e;
    }
    template<typename F>
    static void guarded(const char* what, F&& f) {
        try {
            f();
        } catch (const std::exception& ex) {
            ++g_dispatchErrors;
            std::printf("  (%s threw: %s)\n", what, ex.what());
        }
    }
};

// A secondary window's context: its Window drives the tree like the
// platform glue does. A Window made in a headless test has no native side,
// and setApp() only takes an open window (#256): native_ points at a
// stand-in that nothing here dereferences, cleared before ~Window() would
// close() it.
class WindowDriver : public Driver {
public:
    WindowDriver() {
        prev_ = internal::currentWindowCtx();
        internal::currentWindowCtx() = &win_.context();
        win_.native_ = &nativeStandIn_;
        app_ = make_shared<App>();
        win_.setApp(app_);
        win_.applyPendingApp();   // the frame boundary
    }
    ~WindowDriver() override {
        win_.setApp(nullptr);
        win_.applyPendingApp();   // the frame boundary
        win_.native_ = nullptr;
        app_.reset();
        internal::currentWindowCtx() = prev_;
    }

protected:
    void doPress(const MouseEventArgs& e) override { win_.dispatchMousePressToTree(e); }
    void doRelease(const MouseEventArgs& e) override { win_.dispatchMouseReleaseToTree(e); }
    void doMove(const internal::MouseEventRaw& e) override { win_.dispatchMouseMoveToTree(e); }
    void doScroll(const ScrollEventArgs& e) override { win_.dispatchMouseScrollToTree(e); }
    void doTick(Vec2 p) override {
        win_.context().mouseX = p.x;
        win_.context().mouseY = p.y;
        win_.tickTree();
    }

private:
    int nativeStandIn_ = 0;
    Window win_;
    internal::WindowContext* prev_ = nullptr;
};

// The main window's context: the App's event handlers, as the main loop
// calls them.
class MainDriver : public Driver {
public:
    MainDriver() {
        prev_ = internal::currentWindowCtx();
        internal::currentWindowCtx() = nullptr;   // the main window's context
        app_ = make_shared<App>();
    }
    ~MainDriver() override {
        app_.reset();
        internal::currentWindowCtx() = prev_;
    }

protected:
    void doPress(const MouseEventArgs& e) override { app_->handleMousePressed(e); }
    void doRelease(const MouseEventArgs& e) override { app_->handleMouseReleased(e); }
    void doMove(const internal::MouseEventRaw& e) override { app_->handleMouseMoved(e); }
    void doScroll(const ScrollEventArgs& e) override { app_->handleMouseScrolled(e); }
    void doTick(Vec2 p) override {
        internal::mainWindowContext().mouseX = p.x;
        internal::mainWindowContext().mouseY = p.y;
        app_->handleUpdate((int)p.x, (int)p.y);
    }

private:
    internal::WindowContext* prev_ = nullptr;
};

// The cursor positions: over the probes placed at (100,100)-(200,200), and
// over nothing.
static const Vec2 kIn(150, 150);
static const Vec2 kIn2(160, 150);
static const Vec2 kOut(700, 500);

// Takes a snapshot of the failure counters; unchanged() is true when no
// stale call and no dispatch error happened since.
struct Untouched {
    int stale = g_staleCalls, errors = g_dispatchErrors;
    bool unchanged() const { return g_staleCalls == stale && g_dispatchErrors == errors; }
};

// Results a handler writes while it removes its own node. Globals, not lambda
// captures: before #255 the node (and the std::function holding the hook)
// is gone while the hook still runs.
static struct {
    weak_ptr<Node> self;
    bool ran = false;
    bool aliveAfterRemoval = false;
} g_hook;

// The hook: removes the node it runs on from its parent, then checks the
// node is still alive.
static void removeSelf(Probe& p) {
    g_hook.ran = true;
    g_hook.self = p.weak_from_this();
    if (auto parent = p.getParent()) parent->removeChild(p.shared_from_this());
    g_hook.aliveAfterRemoval = !g_hook.self.expired();
}

// A mod that records whether it got the release, and whether its node was
// alive then.
static struct {
    weak_ptr<Node> owner;
    bool ran = false;
    bool ownerAlive = false;
} g_modWatch;

class ReleaseWatchMod : public Mod {
protected:
    bool onMouseRelease(const MouseEventArgs& e) override {
        (void)e;
        g_modWatch.ran = true;
        g_modWatch.ownerAlive = !g_modWatch.owner.expired();
        return false;
    }
};

// ---------------------------------------------------------------------------
// 1. Removed while the tree was the last owner: never touched again
// ---------------------------------------------------------------------------

static void hoveredNodeRemoved(Driver& d) {
    auto item = makeProbeAt(Vec2(100, 100), Vec2(100, 100));
    d.root().addChild(item);
    weak_ptr<Probe> w = item;
    item.reset();   // the tree is the only owner

    d.tick(kIn);
    {
        auto p = w.lock();
        check("hover: the item is hovered", p && p->isMouseOver() && p->enterCount == 1);
    }
    Untouched u;
    d.root().removeChild(w.lock());
    check("hover: removeChild() of the hovered item freed it", w.expired());
    d.tick(kIn);
    d.tick(kOut);
    d.move(kIn);
    check("hover: the next hover updates and moves never touch the freed item", u.unchanged());
    d.clear();
}

static void hoveredDescendantRemoved(Driver& d) {
    auto group = make_shared<Node>();
    auto item = makeProbeAt(Vec2(100, 100), Vec2(100, 100));
    group->addChild(item);
    d.root().addChild(group);
    weak_ptr<Probe> w = item;
    group.reset();
    item.reset();

    d.tick(kIn);
    check("hover: a nested item is hovered", w.lock() && w.lock()->isMouseOver());
    Untouched u;
    d.root().removeAllChildren();
    check("hover: removeAllChildren() of its ancestor freed it", w.expired());
    d.tick(kIn);
    d.tick(kOut);
    check("hover: the next hover updates never touch the freed nested item", u.unchanged());
    d.clear();
}

static void grabbedNodeRemoved(Driver& d) {
    auto item = makeProbeAt(Vec2(100, 100), Vec2(100, 100));
    d.root().addChild(item);
    weak_ptr<Probe> w = item;
    item.reset();

    d.press(kIn);
    check("grab: the item got the press (and holds the grab)", w.lock() && w.lock()->pressCount == 1);
    Untouched u;
    d.root().removeAllChildren();
    check("grab: removeAllChildren() freed the grabbed item", w.expired());
    d.move(kIn2);
    d.release(kIn2);
    check("grab: the drag and the release never touch the freed item", u.unchanged());

    // The dead grab does not linger: the next press / drag / release pair
    // goes to the node under the cursor as usual.
    auto next = makeProbeAt(Vec2(100, 100), Vec2(100, 100));
    d.root().addChild(next);
    d.press(kIn);
    d.move(kIn2);
    d.release(kIn2);
    check("grab: a new node gets press, drag and release as usual",
          next->pressCount == 1 && next->dragCount == 1 && next->releaseCount == 1);
    d.clear();
}

static void selectedNodeRemoved(Driver& d) {
    auto item = makeProbeAt(Vec2(100, 100), Vec2(100, 100));
    d.root().addChild(item);
    weak_ptr<Probe> w = item;
    void* address = item.get();
    item.reset();

    d.press(kIn);
    d.release(kIn);
    check("selection: the clicked item is selected", w.lock() && getSelectedNode() == w.lock().get());
    d.root().removeChild(w.lock());
    check("selection: removeChild() freed the selected item", w.expired());
    check("selection: getSelectedNode() is null once the item is freed", getSelectedNode() == nullptr);

    auto newcomer = makeProbe(slotOf(address));
    check("selection: (a new node now has the freed item's address)", newcomer.get() == address);
    check("selection: the new node at that address is not selected", getSelectedNode() == nullptr);
    d.clear();
}

static void newNodeAtHoveredAddress(Driver& d) {
    auto item = makeProbeAt(Vec2(100, 100), Vec2(100, 100));
    d.root().addChild(item);
    weak_ptr<Probe> w = item;
    void* address = item.get();
    item.reset();

    d.tick(kIn);
    d.root().removeChild(w.lock());
    auto newcomer = makeProbe(slotOf(address));
    newcomer->setPos(400, 400);
    newcomer->setSize(50, 50);
    d.root().addChild(newcomer);
    check("isMouseOver: (a new node now has the freed hovered item's address)", newcomer.get() == address);
    check("isMouseOver: false for the new node at the freed hovered item's address", !newcomer->isMouseOver());
    d.tick(kIn);   // where the freed item was; nothing is there now
    check("isMouseOver: the new node gets no Enter / Leave meant for the freed one",
          newcomer->enterCount == 0 && newcomer->leaveCount == 0 && !newcomer->isMouseOver());
    d.clear();
}

// ---------------------------------------------------------------------------
// 2. A handler removes its own node or an ancestor
// ---------------------------------------------------------------------------

static void releaseHandlerRemovesItself(Driver& d) {
    auto item = makeProbeAt(Vec2(100, 100), Vec2(100, 100));
    item->onRelease = removeSelf;
    item->addMod<ReleaseWatchMod>();
    d.root().addChild(item);
    weak_ptr<Probe> w = item;
    g_modWatch = {};
    g_modWatch.owner = item;
    g_hook = {};
    item.reset();

    Untouched u;
    d.press(kIn);
    d.release(kIn);
    check("self-removal (grab release): the node is alive right after removing itself",
          g_hook.ran && g_hook.aliveAfterRemoval);
    check("self-removal (grab release): its mod gets the release, on the live node",
          g_modWatch.ran && g_modWatch.ownerAlive);
    check("self-removal (grab release): the node is freed once dispatch returns", w.expired());
    d.move(kIn2);
    d.press(kIn);
    d.release(kIn);
    check("self-removal (grab release): nothing touches it afterwards", u.unchanged());
    d.clear();
}

static void pressHandlerRemovesItself(Driver& d) {
    auto item = makeProbeAt(Vec2(100, 100), Vec2(100, 100));
    item->onPress = removeSelf;   // and consumes: it becomes the grab
    d.root().addChild(item);
    weak_ptr<Probe> w = item;
    g_hook = {};
    item.reset();

    Untouched u;
    d.press(kIn);
    check("self-removal (press): the node is alive right after removing itself",
          g_hook.ran && g_hook.aliveAfterRemoval);
    check("self-removal (press): the node is freed once dispatch returns", w.expired());
    d.move(kIn2);
    d.release(kIn2);
    d.tick(kIn);
    check("self-removal (press): its grab never reaches it afterwards", u.unchanged());
    d.clear();
}

static void dragHandlerRemovesItself(Driver& d) {
    auto item = makeProbeAt(Vec2(100, 100), Vec2(100, 100));
    item->onDrag = removeSelf;
    d.root().addChild(item);
    weak_ptr<Probe> w = item;
    g_hook = {};
    item.reset();

    Untouched u;
    d.press(kIn);
    d.move(kIn2);
    check("self-removal (drag): the node is alive right after removing itself",
          g_hook.ran && g_hook.aliveAfterRemoval);
    check("self-removal (drag): the node is freed once dispatch returns", w.expired());
    d.move(kIn);
    d.release(kIn);
    check("self-removal (drag): nothing touches it afterwards", u.unchanged());
    d.clear();
}

static void hoverHandlerRemovesItself(Driver& d) {
    auto item = makeProbeAt(Vec2(100, 100), Vec2(100, 100));
    item->onLeave = removeSelf;
    d.root().addChild(item);
    weak_ptr<Probe> w = item;
    g_hook = {};
    item.reset();

    d.tick(kIn);
    Untouched u;
    d.tick(kOut);   // Leave
    check("self-removal (mouseLeave): the node is alive right after removing itself",
          g_hook.ran && g_hook.aliveAfterRemoval);
    check("self-removal (mouseLeave): the node is freed once the hover update returns", w.expired());
    d.tick(kIn);
    d.tick(kOut);
    check("self-removal (mouseLeave): nothing touches it afterwards", u.unchanged());
    d.clear();

    item = makeProbeAt(Vec2(100, 100), Vec2(100, 100));
    item->onEnter = removeSelf;
    d.root().addChild(item);
    w = item;
    g_hook = {};
    item.reset();

    d.tick(kIn);    // Enter
    check("self-removal (mouseEnter): the node is alive right after removing itself",
          g_hook.ran && g_hook.aliveAfterRemoval);
    check("self-removal (mouseEnter): the node is freed once the hover update returns", w.expired());
    d.tick(kIn);
    d.tick(kOut);
    check("self-removal (mouseEnter): nothing touches it afterwards", u.unchanged());
    d.clear();
}

// The event bubbles from a button that does not consume to the dialog around
// it, whose handler removes the dialog (and so the button) from the tree.
enum class Bubble { Press, Release, Move, Scroll };

static void ancestorRemovesItselfWhileBubbling(Driver& d, Bubble kind, const char* label) {
    auto dialog = makeProbeAt(Vec2(100, 100), Vec2(300, 200));
    auto button = makeProbeAt(Vec2(10, 10), Vec2(50, 50));   // global (110,110)-(160,160)
    dialog->consume = false;
    button->consume = false;
    switch (kind) {
        case Bubble::Press:   dialog->onPress = removeSelf; break;
        case Bubble::Release: dialog->onRelease = removeSelf; break;
        case Bubble::Move:    dialog->onMove = removeSelf; break;
        case Bubble::Scroll:  dialog->onScroll = removeSelf; break;
    }
    dialog->addChild(button);
    d.root().addChild(dialog);
    weak_ptr<Probe> wDialog = dialog, wButton = button;
    g_hook = {};
    dialog.reset();
    button.reset();

    const Vec2 at(120, 120);
    Untouched u;
    switch (kind) {
        case Bubble::Press:   d.press(at); break;
        case Bubble::Release: d.release(at); break;   // no grab: the bubbling path
        case Bubble::Move:    d.move(at); break;
        case Bubble::Scroll:  d.scroll(at); break;
    }
    string name = string("ancestor removal (") + label + " bubbling): ";
    check(name + "the dialog is alive right after removing itself", g_hook.ran && g_hook.aliveAfterRemoval);
    check(name + "dialog and button are freed once dispatch returns", wDialog.expired() && wButton.expired());
    d.release(at);
    d.tick(at);
    check(name + "nothing touches them afterwards", u.unchanged());
    d.clear();
}

// The close button from #255: a mouseReleased listener on the button removes
// the dialog that owns it.
static struct {
    weak_ptr<Node> dialog, button;
    bool ran = false, buttonAlive = false, dialogFreed = false;
} g_close;

static void listenerRemovesAncestor(Driver& d) {
    auto dialog = makeProbeAt(Vec2(100, 100), Vec2(300, 200));
    dialog->disableEvents();   // only the button is hit
    auto button = makeProbeAt(Vec2(10, 10), Vec2(40, 40));
    dialog->addChild(button);
    d.root().addChild(dialog);
    g_close = {};
    g_close.dialog = dialog;
    g_close.button = button;
    EventListener closeListener = button->mouseReleased.listen([](MouseEventArgs&) {
        g_close.ran = true;
        if (auto dlg = g_close.dialog.lock()) {
            if (auto parent = dlg->getParent()) parent->removeChild(dlg);
        }
        g_close.buttonAlive = !g_close.button.expired();
        g_close.dialogFreed = g_close.dialog.expired();
    });
    dialog.reset();
    button.reset();

    Untouched u;
    d.press(Vec2(115, 115));
    d.release(Vec2(115, 115));
    check("close button: the listener ran and freed the dialog", g_close.ran && g_close.dialogFreed);
    check("close button: the button is alive through the rest of its release",
          g_close.buttonAlive && u.unchanged());
    check("close button: the button is freed once dispatch returns", g_close.button.expired());
    d.move(Vec2(120, 120));
    d.tick(Vec2(120, 120));
    check("close button: nothing touches them afterwards", u.unchanged());
    d.clear();
}

// ---------------------------------------------------------------------------
// 3. Working code behaves as before
// ---------------------------------------------------------------------------

static void removedButHeld(Driver& d) {
    auto item = makeProbeAt(Vec2(100, 100), Vec2(100, 100));
    d.root().addChild(item);
    d.tick(kIn);
    d.root().removeChild(item);   // the app still holds it
    d.tick(kIn);
    check("still held: a removed node the app holds gets its Leave, as before",
          item->enterCount == 1 && item->leaveCount == 1 && !item->isMouseOver());
    d.clear();
}

static void reparentKeepsHover(Driver& d) {
    auto from = make_shared<Node>();
    auto to = make_shared<Node>();
    d.root().addChild(from);
    d.root().addChild(to);
    auto item = makeProbeAt(Vec2(100, 100), Vec2(100, 100));
    from->addChild(item);
    d.tick(kIn);
    to->addChild(item, true);   // removeChild() from `from`, same global place
    d.tick(kIn);
    check("reparent: no extra Enter / Leave, still hovered",
          item->enterCount == 1 && item->leaveCount == 0 && item->isMouseOver());
    d.clear();
}

static void destroyDropsHoverGrabSelection(Driver& d) {
    auto item = makeProbeAt(Vec2(100, 100), Vec2(100, 100));
    d.root().addChild(item);
    d.tick(kIn);
    d.press(kIn);   // grabbed and selected
    item->destroy();
    d.tick(kIn);    // swept here
    check("destroy(): hover dropped without a Leave, as before",
          item->leaveCount == 0 && !item->isMouseOver());
    check("destroy(): selection dropped", getSelectedNode() == nullptr);
    d.move(kIn2);
    d.release(kIn2);
    check("destroy(): grab dropped (no drag, no release)", item->dragCount == 0 && item->releaseCount == 0);
    d.clear();
}

static void selectNodeNobodyOwns(Driver& d) {
    auto item = makeProbeAt(Vec2(100, 100), Vec2(100, 100));
    d.root().addChild(item);
    setSelectedNode(item.get());
    check("setSelectedNode(): selects a shared-owned node", getSelectedNode() == item.get());
    RectNode local;   // not owned by a shared_ptr
    setSelectedNode(&local);
    check("setSelectedNode(): a node no shared_ptr owns clears the selection", getSelectedNode() == nullptr);
    setSelectedNode(item.get());
    setSelectedNode(nullptr);
    check("setSelectedNode(nullptr) clears the selection", getSelectedNode() == nullptr);
    d.clear();
}

static void runAll(Driver& d) {
    hoveredNodeRemoved(d);
    hoveredDescendantRemoved(d);
    grabbedNodeRemoved(d);
    selectedNodeRemoved(d);
    newNodeAtHoveredAddress(d);
    releaseHandlerRemovesItself(d);
    pressHandlerRemovesItself(d);
    dragHandlerRemovesItself(d);
    hoverHandlerRemovesItself(d);
    ancestorRemovesItselfWhileBubbling(d, Bubble::Press, "press");
    ancestorRemovesItselfWhileBubbling(d, Bubble::Release, "release");
    ancestorRemovesItselfWhileBubbling(d, Bubble::Move, "move");
    ancestorRemovesItselfWhileBubbling(d, Bubble::Scroll, "scroll");
    listenerRemovesAncestor(d);
    removedButHeld(d);
    reparentKeepsHover(d);
    destroyDropsHoverGrabSelection(d);
    selectNodeNobodyOwns(d);
}

} // namespace

TC_CORE_TEST_MAIN() {
    {
        g_scope = "[window] ";
        WindowDriver d;
        runAll(d);
    }
    {
        g_scope = "[main] ";
        MainDriver d;
        runAll(d);
    }
    g_scope.clear();
    check("no call reached a freed node's address", g_staleCalls == 0);
    check("no dispatch threw", g_dispatchErrors == 0);

    std::printf("\n%s  (%d failure%s)\n", g_fail ? "FAILED" : "PASSED",
                g_fail, g_fail == 1 ? "" : "s");
    std::fflush(stdout);
    return g_fail ? 1 : 0;
}
