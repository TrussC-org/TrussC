// =============================================================================
// core/tests/logOnce — behavioral regression test for logOnce() (#308).
//
// Headless, console, exit code = pass/fail (build_all.py runs it in CI).
//
// Guards the invariants:
//   - logOnce(key) is true the first time for a key and false afterwards.
//   - Keys are independent: a new key is true once, whatever other keys did.
//   - With an interval, it is true again once the interval has passed since
//     the last true, and not before; an interval of 0 or below means once.
//   - Thread-safe: many threads calling it with the same key get exactly one
//     true between them; with one key per call across threads, each key is
//     true exactly once.
// =============================================================================

#include <TrussC.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using namespace std;
using namespace tc;

static int g_fail = 0;
static void check(const char* name, bool ok) {
    printf("%-66s %s\n", name, ok ? "PASS" : "FAIL");
    fflush(stdout);
    if (!ok) ++g_fail;
}

static void sleepSeconds(double s) {
    this_thread::sleep_for(chrono::duration<double>(s));
}

int main() {
    // --- once per key -------------------------------------------------------
    {
        bool first = logOnce("test.once");
        bool second = logOnce("test.once");
        bool third = logOnce("test.once");
        check("once: first call is true", first);
        check("once: later calls are false", !second && !third);
    }

    // --- keys are independent ----------------------------------------------
    {
        bool a = logOnce("test.keyA");
        bool b = logOnce("test.keyB");
        bool a2 = logOnce("test.keyA");
        bool b2 = logOnce("test.keyB");
        check("keys: each new key is true once", a && b);
        check("keys: each key is false after its first", !a2 && !b2);
        check("keys: an unrelated key does not reset another", !logOnce("test.once"));
    }

    // --- interval -----------------------------------------------------------
    {
        const double iv = 0.3;
        bool first = logOnce("test.interval", iv);
        bool soon = logOnce("test.interval", iv);
        check("interval: first call is true", first);
        check("interval: false within the interval", !soon);
        sleepSeconds(iv + 0.1);
        bool after = logOnce("test.interval", iv);
        bool afterAgain = logOnce("test.interval", iv);
        check("interval: true again after the interval", after);
        check("interval: then false again (interval restarts)", !afterAgain);
        sleepSeconds(iv + 0.1);
        check("interval: true again after another interval", logOnce("test.interval", iv));

        // 0 and negative intervals mean once.
        check("interval 0: first true", logOnce("test.zero", 0));
        check("interval -1: first true", logOnce("test.neg", -1.0));
        sleepSeconds(0.05);
        check("interval 0 / -1: never true again", !logOnce("test.zero", 0) && !logOnce("test.neg", -1.0));
    }

    // --- thread safety: one key, many threads -------------------------------
    {
        const int kThreads = 8;
        const int kCalls = 2000;
        atomic<int> trues{0};
        atomic<bool> go{false};
        vector<thread> ts;
        for (int t = 0; t < kThreads; ++t) {
            ts.emplace_back([&] {
                while (!go.load()) this_thread::yield();
                for (int i = 0; i < kCalls; ++i) {
                    if (logOnce("test.shared")) trues.fetch_add(1);
                }
            });
        }
        go = true;
        for (auto& th : ts) th.join();
        check("threads: one shared key is true exactly once", trues.load() == 1);
    }

    // --- thread safety: many keys, many threads -----------------------------
    {
        const int kThreads = 8;
        const int kKeys = 500;
        vector<atomic<int>> perKey(kKeys);
        for (auto& c : perKey) c = 0;
        atomic<bool> go{false};
        vector<thread> ts;
        for (int t = 0; t < kThreads; ++t) {
            ts.emplace_back([&] {
                while (!go.load()) this_thread::yield();
                for (int k = 0; k < kKeys; ++k) {
                    if (logOnce("test.many." + to_string(k))) perKey[k].fetch_add(1);
                }
            });
        }
        go = true;
        for (auto& th : ts) th.join();
        int wrong = 0;
        for (auto& c : perKey) if (c.load() != 1) ++wrong;
        check("threads: each of 500 keys is true exactly once", wrong == 0);
    }

    printf("%s\n", g_fail ? "logOnce: FAILED" : "logOnce: all passed");
    return g_fail ? 1 : 0;
}
