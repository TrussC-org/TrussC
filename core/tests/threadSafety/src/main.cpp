// =============================================================================
// core/tests/threadSafety — behavioral regression test for the main-thread
// affinity work (runOnMainThread / Event Deliver::Main / atomic destroy()).
//
// Headless, console, exit code = pass/fail (build_all.py runs it in CI).
//
// Guards the invariant: worker threads can hand tree edits to the main thread
// (and they actually run THERE), and destroy() is safe from any thread. A future
// refactor of the Event hot path, the frame drain, or Node::dead_ that silently
// broke this would compile fine but fail here.
//
// Run under ThreadSanitizer for the stronger race-free guarantee:
//   c++ -fsanitize=thread ... (see the thread-safety branch notes)
// =============================================================================

#include <TrussC.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

using namespace std;
using namespace tc;

static int g_fail = 0;
static void check(const char* name, bool ok) {
    std::printf("%-56s %s\n", name, ok ? "PASS" : "FAIL");
    std::fflush(stdout);   // flush per line so CI logs survive a later crash
    if (!ok) ++g_fail;
}

static void sleepUs(int us) { this_thread::sleep_for(chrono::microseconds(us)); }

// --- File-scope state for the headless stress app (outlives the App instance) ---
static atomic<bool>          g_stop{false};
static atomic<long>          g_edits{0};
static shared_ptr<Node>      g_root;

// A real headless app: a worker hammers the tree via runOnMainThread while the
// app's update() reads it. Exercises the actual headless runtime (setup/update +
// the per-frame drain). If marshalling were broken, addChild would assert (debug)
// or the unsynchronised children_ access would crash.
struct StressApp : App {
    thread worker;
    int frames = 0;

    void setup() override {
        g_root = make_shared<Node>();
        for (int i = 0; i < 16; ++i) g_root->addChild(make_shared<Node>());
        worker = thread([] {
            while (!g_stop.load(memory_order_relaxed)) {
                runOnMainThread([] {
                    g_root->addChild(make_shared<Node>());
                    auto kids = g_root->getChildren();
                    if (kids.size() > 16) g_root->removeChild(kids.front());
                    g_edits.fetch_add(1, memory_order_relaxed);
                });
                sleepUs(50);
            }
        });
    }

    void update() override {
        auto snap = g_root->getChildren();    // drain already ran this frame
        volatile uint64_t acc = 0;
        for (auto& c : snap) acc += c->getInstanceId();
        (void)acc;
        if (++frames >= 150) requestExit();
    }

    void exit() override {
        g_stop.store(true, memory_order_relaxed);
        if (worker.joinable()) worker.join();
    }
};

int main() {
    getMainThreadId();   // record the main thread id (headless test has no _setup_cb)

    // --- 1. runOnMainThread defers off-thread work and runs it ON the main thread ---
    {
        atomic<bool> ran{false};
        atomic<bool> onMain{false};
        thread t([&] {
            runOnMainThread([&] { onMain.store(isMainThread()); ran.store(true); });
        });
        t.join();   // worker has submitted; closure must NOT have run yet
        check("runOnMainThread: deferred (not run before drain)", !ran.load());
        internal::drainMainThreadQueue();
        check("runOnMainThread: runs after drain", ran.load());
        check("runOnMainThread: ran on the main thread", onMain.load());
    }

    // --- 2. destroy() is safe from a worker thread (atomic dead_) ---
    {
        auto n = make_shared<Node>();
        thread t([&] { for (int i = 0; i < 200000; ++i) n->destroy(); });
        bool seen = false;
        for (int i = 0; i < 200000; ++i) seen = n->isDead();   // concurrent reads
        t.join();
        (void)seen;
        check("destroy() from worker + concurrent isDead() reads", n->isDead());
    }

    // --- 3. Event<T> Deliver::Main: notify() on a worker delivers on main ---
    {
        Event<int> ev;
        atomic<long> delivered{0};
        atomic<bool> allOnMain{true};
        auto L = ev.listen([&](int&) {
            if (!isMainThread()) allOnMain.store(false);
            delivered.fetch_add(1, memory_order_relaxed);
        }, Deliver::Main);

        const int N = 500;
        thread t([&] { for (int i = 0; i < N; ++i) { int v = i; ev.notify(v); } });
        t.join();
        internal::drainMainThreadQueue();   // run the marshalled listeners on main
        check("Event Deliver::Main: delivered every notify", delivered.load() == N);
        check("Event Deliver::Main: every listener ran on main", allOnMain.load());
    }

    // --- 3b. Deliver::Main honors listener lifetime across the queue ---
    // A marshalled call sits in the main-thread queue for up to a frame. If the
    // EventListener dies in that window, the queued call must be dropped —
    // otherwise it runs a callback whose captures (typically `this`) dangle.
    {
        // Listener destroyed between notify() and drain
        {
            Event<int> ev;
            atomic<int> ran{0};
            {
                auto L = ev.listen([&](int&) { ran.fetch_add(1); }, Deliver::Main);
                thread t([&] { int v = 1; ev.notify(v); });   // queues the call
                t.join();
            }   // L destroyed here, queued call still pending
            internal::drainMainThreadQueue();
            check("Deliver::Main: queued call dropped after listener death", ran.load() == 0);
        }

        // clear() between notify() and drain
        {
            Event<int> ev;
            atomic<int> ran{0};
            auto L = ev.listen([&](int&) { ran.fetch_add(1); }, Deliver::Main);
            thread t([&] { int v = 1; ev.notify(v); });
            t.join();
            ev.clear();
            internal::drainMainThreadQueue();
            check("Deliver::Main: queued call dropped after clear()", ran.load() == 0);
        }

        // Event itself destroyed between notify() and drain
        {
            atomic<int> ran{0};
            EventListener L;
            {
                Event<int> ev;
                L = ev.listen([&](int&) { ran.fetch_add(1); }, Deliver::Main);
                thread t([&] { int v = 1; ev.notify(v); });
                t.join();
            }   // ev destroyed, queued call still pending
            internal::drainMainThreadQueue();
            check("Deliver::Main: queued call dropped after Event death", ran.load() == 0);
        }

        // Event<void>: listener destroyed between notify() and drain
        {
            Event<void> ev;
            atomic<int> ran{0};
            {
                auto L = ev.listen([&] { ran.fetch_add(1); }, Deliver::Main);
                thread t([&] { ev.notify(); });
                t.join();
            }
            internal::drainMainThreadQueue();
            check("Deliver::Main (void): queued call dropped after listener death", ran.load() == 0);
        }

        // Positive control: listener still alive at drain → call runs
        {
            Event<int> ev;
            atomic<int> ran{0};
            auto L = ev.listen([&](int&) { ran.fetch_add(1); }, Deliver::Main);
            thread t([&] { int v = 1; ev.notify(v); });
            t.join();
            internal::drainMainThreadQueue();
            check("Deliver::Main: queued call runs while listener alive", ran.load() == 1);
        }
    }

    // --- 4. Headless runtime stress: worker marshals tree edits, no crash ---
    {
        g_stop.store(false);
        g_edits.store(0);
        HeadlessSettings hs;
        hs.setFps(500.0f);
        runHeadlessApp<StressApp>(hs);   // returns when update() requests exit
        check("headless stress: survived (no crash)", true);
        check("headless stress: worker edits applied on main", g_edits.load() > 0);
        check("headless stress: tree stayed bounded/consistent",
              g_root && g_root->getChildren().size() >= 16 &&
              g_root->getChildren().size() <= 17);
    }

    std::printf("\n%s  (%d failure%s)\n", g_fail ? "FAILED" : "PASSED",
                g_fail, g_fail == 1 ? "" : "s");
    std::fflush(stdout);
    return g_fail ? 1 : 0;
}
