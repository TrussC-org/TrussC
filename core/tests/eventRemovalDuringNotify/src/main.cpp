// =============================================================================
// core/tests/eventRemovalDuringNotify — behavioral regression test for #256
// (item 1): what a notify() pass does when its own listener list changes.
//
// Headless, plain main(), exit code = pass/fail (build_all.py runs it in CI).
//
// Guards the invariants, for Event<T> and Event<void>:
//   - A listener that an earlier listener disconnects (or destroys) during a
//     pass is not called in that pass, nor later. Before #256 the pass still
//     called it from its snapshot of the list, so a [this] capture reached an
//     object that was already gone.
//   - clear() during a pass stops the listeners after the one that called it.
//   - A listener that disconnects itself does not stop the ones after it.
//   - A listener added during a pass starts from the next pass (#107).
//   - The Tween shape: objects kept in a vector listen with [this]; their move
//     constructor disconnects the old listener and listens again. A listener
//     that grows the vector during the pass moves them all: no call reaches a
//     moved-from (freed) object, and the new listeners start next pass.
// =============================================================================

#include <TrussC.h>

#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace std;
using namespace tc;

static int g_fail = 0;
static void check(const string& name, bool ok, const string& detail = "") {
    printf("%-72s %s%s\n", name.c_str(), ok ? "PASS" : "FAIL",
           ok || detail.empty() ? "" : ("  -- " + detail).c_str());
    fflush(stdout);   // flush per line so CI logs survive a later crash
    if (!ok) ++g_fail;
}

// --- Removal by an earlier listener -------------------------------------------

static void removedByEarlierListener() {
    {   // The issue's repro: same priority, so registration order.
        Event<int> ev;
        int bCalls = 0;
        EventListener la, lb;
        la = ev.listen([&](int&) { lb.disconnect(); });
        lb = ev.listen([&](int&) { ++bCalls; });
        int x = 0;
        ev.notify(x);
        check("Event<T>: disconnected by an earlier listener, not called in the pass",
              bCalls == 0, "calls " + to_string(bCalls));
        ev.notify(x);
        check("Event<T>: ... nor in the next pass", bCalls == 0);
    }
    {
        Event<void> ev;
        int bCalls = 0;
        EventListener la, lb;
        la = ev.listen([&]() { lb.disconnect(); });
        lb = ev.listen([&]() { ++bCalls; });
        ev.notify();
        check("Event<void>: disconnected by an earlier listener, not called in the pass",
              bCalls == 0, "calls " + to_string(bCalls));
        ev.notify();
        check("Event<void>: ... nor in the next pass", bCalls == 0);
    }
    {   // Across priorities: BeforeApp runs first even though it listens last.
        Event<int> ev;
        int bCalls = 0;
        EventListener la, lb;
        lb = ev.listen([&](int&) { ++bCalls; }, EventPriority::AfterApp);
        la = ev.listen([&](int&) { lb.disconnect(); }, EventPriority::BeforeApp);
        int x = 0;
        ev.notify(x);
        check("Event<T>: removal by a higher-priority listener holds too", bCalls == 0);
    }
    {   // The RAII path: the EventListener itself is destroyed mid-pass.
        Event<int> ev;
        int bCalls = 0;
        auto lb = make_unique<EventListener>();
        EventListener la = ev.listen([&](int&) { lb.reset(); });
        *lb = ev.listen([&](int&) { ++bCalls; });
        int x = 0;
        ev.notify(x);
        check("Event<T>: EventListener destroyed by an earlier listener, not called",
              bCalls == 0);
    }
    {
        Event<void> ev;
        int bCalls = 0;
        auto lb = make_unique<EventListener>();
        EventListener la = ev.listen([&]() { lb.reset(); });
        *lb = ev.listen([&]() { ++bCalls; });
        ev.notify();
        check("Event<void>: EventListener destroyed by an earlier listener, not called",
              bCalls == 0);
    }
}

// --- clear() during a pass ------------------------------------------------------

static void clearDuringNotify() {
    {
        Event<int> ev;
        int before = 0, after = 0;
        EventListener l1 = ev.listen([&](int&) { ++before; });
        EventListener l2 = ev.listen([&](int&) { ev.clear(); });
        EventListener l3 = ev.listen([&](int&) { ++after; });
        int x = 0;
        ev.notify(x);
        check("Event<T>: clear() during a pass stops the later listeners",
              before == 1 && after == 0, "before " + to_string(before) + ", after " + to_string(after));
        check("Event<T>: ... and empties the list", ev.listenerCount() == 0);
    }
    {
        Event<void> ev;
        int before = 0, after = 0;
        EventListener l1 = ev.listen([&]() { ++before; });
        EventListener l2 = ev.listen([&]() { ev.clear(); });
        EventListener l3 = ev.listen([&]() { ++after; });
        ev.notify();
        check("Event<void>: clear() during a pass stops the later listeners",
              before == 1 && after == 0, "before " + to_string(before) + ", after " + to_string(after));
    }
}

// --- Self-removal ---------------------------------------------------------------

static void selfRemoval() {
    {
        Event<int> ev;
        int selfCalls = 0, laterCalls = 0;
        EventListener self, later;
        self = ev.listen([&](int&) { ++selfCalls; self.disconnect(); });
        later = ev.listen([&](int&) { ++laterCalls; });
        int x = 0;
        ev.notify(x);
        ev.notify(x);
        check("Event<T>: a listener that removes itself runs once",
              selfCalls == 1, "calls " + to_string(selfCalls));
        check("Event<T>: ... and does not stop the ones after it",
              laterCalls == 2, "calls " + to_string(laterCalls));
    }
    {
        Event<void> ev;
        int selfCalls = 0, laterCalls = 0;
        EventListener self, later;
        self = ev.listen([&]() { ++selfCalls; self.disconnect(); });
        later = ev.listen([&]() { ++laterCalls; });
        ev.notify();
        ev.notify();
        check("Event<void>: a listener that removes itself runs once, the next ones still run",
              selfCalls == 1 && laterCalls == 2,
              to_string(selfCalls) + " / " + to_string(laterCalls));
    }
}

// --- Added during a pass (#107: starts from the next notify) --------------------

static void addedDuringNotify() {
    {
        Event<int> ev;
        int newCalls = 0;
        bool added = false;
        EventListener adder, fresh;
        adder = ev.listen([&](int&) {
            if (added) return;
            added = true;
            // AfterApp: it would come after the adder if the pass saw it.
            fresh = ev.listen([&](int&) { ++newCalls; }, EventPriority::AfterApp);
        });
        int x = 0;
        ev.notify(x);
        check("Event<T>: a listener added during a pass is not called in it",
              newCalls == 0, "calls " + to_string(newCalls));
        ev.notify(x);
        check("Event<T>: ... and is called from the next pass", newCalls == 1,
              "calls " + to_string(newCalls));
    }
    {
        Event<void> ev;
        int newCalls = 0;
        bool added = false;
        EventListener adder, fresh;
        adder = ev.listen([&]() {
            if (added) return;
            added = true;
            fresh = ev.listen([&]() { ++newCalls; }, EventPriority::AfterApp);
        });
        ev.notify();
        const int firstPass = newCalls;
        ev.notify();
        check("Event<void>: added during a pass: not called in it, called from the next",
              firstPass == 0 && newCalls == 1,
              to_string(firstPass) + " / " + to_string(newCalls));
    }
}

// --- The Tween shape ------------------------------------------------------------
// Like Tween: listens with [this], and its move constructor disconnects the
// moved-from object's listener and listens again for the new address. Every
// constructed, not yet destroyed Mover is in g_live; a call whose `this` is
// not there went to a moved-from object whose storage the vector has freed
// (the check only compares the pointer, it never dereferences it).

static set<const void*> g_live;
static int g_deadCalls = 0;

struct Mover {
    Event<void>* ev;
    int steps = 0;
    EventListener listener;

    explicit Mover(Event<void>& e) : ev(&e) { g_live.insert(this); listen(); }
    Mover(Mover&& o) noexcept : ev(o.ev), steps(o.steps) {
        g_live.insert(this);
        o.listener.disconnect();   // it captures the old address
        listen();
    }
    Mover& operator=(Mover&&) = delete;
    ~Mover() { g_live.erase(this); }

    void listen() {
        listener = ev->listen([this]() {
            if (!g_live.count(this)) { ++g_deadCalls; return; }
            ++steps;
        });
    }
};

static void tweenShape() {
    g_live.clear();
    g_deadCalls = 0;
    Event<void> tick;
    vector<Mover> movers;
    movers.reserve(3);
    bool grown = false;
    // Runs first in the pass and reallocates the vector once, moving every
    // Mover (the issue's `tweens.reserve(64)` inside an update listener).
    EventListener grower = tick.listen([&]() {
        if (grown) return;
        grown = true;
        movers.reserve(64);
    }, EventPriority::BeforeApp);
    for (int i = 0; i < 3; ++i) movers.emplace_back(tick);

    tick.notify();
    check("Tween shape: no call reaches a moved-from object in the moving pass",
          g_deadCalls == 0, to_string(g_deadCalls) + " calls to freed objects");
    bool none = true;
    for (auto& m : movers) none = none && m.steps == 0;
    check("Tween shape: the re-registered listeners start from the next pass", none);

    tick.notify();
    bool once = true;
    for (auto& m : movers) once = once && m.steps == 1;
    check("Tween shape: next pass, every moved object steps exactly once",
          once && g_deadCalls == 0);
}

int main() {
    removedByEarlierListener();
    clearDuringNotify();
    selfRemoval();
    addedDuringNotify();
    tweenShape();

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED",
           g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
