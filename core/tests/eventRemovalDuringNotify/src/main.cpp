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
#include "../../common/tcCoreTest.h"

#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include <thread>

using namespace std;
using namespace tc;

namespace {

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

// Borrowed payload delivery reconstructs before every listener, cancels an
// active snapshot, and never resumes through an Event deleted by a listener.
static void guardedBorrowedDelivery() {
    auto source = make_unique<Event<int>>();
    vector<int> seen;
    bool open = true;
    int value = 0;
    auto after = source->listen([&](int& v) { seen.push_back(v); v = 99; }, EventPriority::AfterApp);
    EventListener removed = source->listen([&](int&) { seen.push_back(500); });
    auto first = source->listen([&](int& v) {
        seen.push_back(v); v = 77; removed.disconnect();
    }, EventPriority::BeforeApp);
    internal::GuardedEventDelivery<int>::notify(*source, value, [&] { value = 3; return open; });
    check("guarded delivery: priority, disconnect and payload reconstruction", seen == vector<int>({3, 3}) && value == 3);
    seen.clear();
    auto cancel = source->listen([&](int&) { open = false; value = 0; }, EventPriority::BeforeApp - 1);
    internal::GuardedEventDelivery<int>::notify(*source, value, [&] { if (!open) value = 0; return open; });
    check("guarded delivery: cancellation stops every later listener", seen.empty() && value == 0);
    cancel.disconnect(); first.disconnect();
    open = true;
    auto destroy = source->listen([&](int&) { source.reset(); open = false; }, EventPriority::BeforeApp);
    internal::GuardedEventDelivery<int>::notify(*source, value, [&] { return open; });
    check("guarded delivery: source deletion unwinds with owned snapshot", !source && seen.empty());
}

template<class T> void frameworkClear() {
    // clear before, inside, and after the framework callback during a pass.
    for (int position : {-1, 0, 1}) {
        Event<T> event;
        int frameworkCalls = 0, ordinaryCalls = 0, addedCalls = 0;
        EventListener added;
        auto clearAndAdd = [&] {
            event.clear(); event.clear();
            check("clear keeps one framework entry", event.listenerCount() == 1);
            added = event.listen([&](auto&...) { ++addedCalls; });
        };
        auto framework = internal::FrameworkEventAccess::listen(event,
            [&](auto&...) { ++frameworkCalls; if (position == 0) clearAndAdd(); }, 100);
        auto clearer = event.listen([&](auto&...) { clearAndAdd(); }, position < 0 ? 0 : 200);
        auto ordinary = event.listen([&](auto&...) { ++ordinaryCalls; }, 300);
        // For position==0 the framework clears the later clearer as well.
        auto notify = [&] {
            if constexpr (std::is_void_v<T>) event.notify();
            else { T payload{}; event.notify(payload); }
        };
        notify();
        check("clear during notify preserves framework live token", frameworkCalls == 1 && ordinaryCalls == 0 && addedCalls == 0);
        check("added during clear starts next pass", event.listenerCount() == 2);
        event.clear(); event.clear();
        check("repeated clear outside notify preserves framework", event.listenerCount() == 1);
        // Stop the framework callback from clearing on the second pass.
        position = 1;
        notify();
        check("preserved framework runs on next pass", frameworkCalls == 2 && addedCalls == 0);
        framework.disconnect();
        notify();
        check("framework disconnect after clear removes entry", event.listenerCount() == 0 && frameworkCalls == 2);
    }
    Event<T> event;
    int calls = 0;
    EventListener framework;
    auto killer = event.listen([&](auto&...) { event.clear(); framework.disconnect(); }, 0);
    framework = internal::FrameworkEventAccess::listen(event, [&](auto&...) { ++calls; }, 100);
    if constexpr (std::is_void_v<T>) event.notify();
    else { T payload{}; event.notify(payload); }
    check("disconnect during pass invalidates preserved framework", calls == 0 && event.listenerCount() == 0);

    framework = internal::FrameworkEventAccess::listen(event, [&](auto&...) { ++calls; event.clear(); framework.disconnect(); }, 100);
    if constexpr (std::is_void_v<T>) event.notify();
    else { T payload{}; event.notify(payload); }
    check("framework can disconnect itself after clear", calls == 1 && event.listenerCount() == 0);

    // A newly published framework entry still obeys the snapshot boundary.
    auto adder = event.listen([&](auto&...) {
        framework = internal::FrameworkEventAccess::listen(event, [&](auto&...) { ++calls; }, 100);
        event.clear();
    }, 0);
    auto notify = [&] {
        if constexpr (std::is_void_v<T>) event.notify();
        else { T payload{}; event.notify(payload); }
    };
    notify();
    check("framework added during pass survives clear but waits", calls == 1 && event.listenerCount() == 1);
    notify();
    check("framework added during pass runs next pass", calls == 2);
    framework.disconnect();

}

// A real borrowed pointer is rebuilt before each listener. clear() must retain
// the framework entry's original liveness even in the active guarded snapshot.
static void guardedFrameworkClear() {
    struct Borrowed { int* value; };
    Event<Borrowed> event;
    auto owner = std::make_unique<int>(42);
    Borrowed args{owner.get()};
    int seen = 0, ordinaryCalls = 0;
    auto clearer = event.listen([&](Borrowed&) { event.clear(); owner = std::make_unique<int>(73); }, 0);
    auto framework = internal::FrameworkEventAccess::listen(event, [&](Borrowed& payload) { seen = *payload.value; }, 100);
    auto ordinary = event.listen([&](Borrowed&) { ++ordinaryCalls; }, 200);
    internal::GuardedEventDelivery<Borrowed>::notify(event, args, [&] { args.value = owner.get(); return true; });
    check("guarded borrowed payload: clear preserves framework and rebuilds pointer", seen == 73 && ordinaryCalls == 0 && event.listenerCount() == 1);
    framework.disconnect();
    check("guarded framework handle disconnects after clear", event.listenerCount() == 0);
}

#if !defined(__EMSCRIPTEN__)
template<class T> void queuedFrameworkClear() {
    Event<T> event;
    int frameworkCalls = 0, ordinaryCalls = 0;
    auto framework = internal::FrameworkEventAccess::listen(event, [&](auto&...) { ++frameworkCalls; }, 100, Deliver::Main);
    auto ordinary = event.listen([&](auto&...) { ++ordinaryCalls; }, Deliver::Main);
    auto enqueue = [&] {
        std::thread worker([&] {
            if constexpr (std::is_void_v<T>) event.notify();
            else { T payload{}; event.notify(payload); }
        });
        worker.join();
    };
    enqueue(); event.clear(); event.clear();
    internal::drainMainThreadQueue();
    check("queued Main: clear preserves framework token only", frameworkCalls == 1 && ordinaryCalls == 0);
    enqueue(); event.clear(); framework.disconnect();
    internal::drainMainThreadQueue();
    check("queued Main: framework disconnect cancels retained call", frameworkCalls == 1 && event.listenerCount() == 0);
}
#endif

} // namespace

TC_CORE_TEST_MAIN() {
    getMainThreadId();
    frameworkClear<int>(); frameworkClear<void>();
    guardedFrameworkClear();
#if !defined(__EMSCRIPTEN__)
    queuedFrameworkClear<int>(); queuedFrameworkClear<void>();
#endif
    removedByEarlierListener();
    clearDuringNotify();
    selfRemoval();
    addedDuringNotify();
    tweenShape();
    guardedBorrowedDelivery();

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED",
           g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
