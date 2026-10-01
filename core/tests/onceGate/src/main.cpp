// =============================================================================
// core/tests/onceGate — behavioral regression test for OnceGate (#308).
//
// Headless, console, exit code = pass/fail (build_all.py runs it in CI).
//
// Guards the invariants:
//   - isFirstTime() is true the first time and false afterwards; each gate
//     object is its own key (a static per call site, a member per object).
//   - With an interval, it is true again once the interval has passed since
//     the last true, and not before; an interval of 0 or below means once.
//   - Thread-safe: many threads calling one gate get exactly one true between
//     them (also for an interval gate).
//   - A static gate works from a static destructor (it is constexpr-
//     constructible and trivially destructible), checked at process exit.
// =============================================================================

#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <thread>
#include <type_traits>
#include <vector>

using namespace std;
using namespace tc;

namespace {

// Compile-time guarantees from the Decision.
static_assert(is_trivially_destructible_v<OnceGate>, "OnceGate: trivially destructible");
static_assert(!is_copy_constructible_v<OnceGate> && !is_copy_assignable_v<OnceGate>,
              "OnceGate: not copyable");
static_assert(!is_move_constructible_v<OnceGate> && !is_move_assignable_v<OnceGate>,
              "OnceGate: not movable");
constinit OnceGate g_constantInitialized;          // constexpr-constructible
constinit OnceGate g_constantInitializedInterval{1.0};

static int g_fail = 0;
static void check(const char* name, bool ok) {
    printf("%-66s %s\n", name, ok ? "PASS" : "FAIL");
    fflush(stdout);
    if (!ok) ++g_fail;
}

static void sleepSeconds(double s) {
    this_thread::sleep_for(chrono::duration<double>(s));
}

// A call site with its own static gate, as core headers use it.
static bool warnSite() {
    static OnceGate warned;
    return warned.isFirstTime();
}

struct PerObject {
    OnceGate warned;
};

// --- used from a static destructor ------------------------------------------
// The LateUser at the top of main() is constructed there and destroyed after
// main() returns, during static destruction. Its destructor uses a static
// gate that main() already opened (it must stay closed) and one it touches
// for the first time there (true once). Both are
// constant-initialized and trivially destructible, so no destruction order
// can leave them unusable.
static OnceGate& exitGate() {
    static OnceGate gate;
    return gate;
}

struct LateUser {
    ~LateUser() {
        // exitGate() was opened once in main(), so it must stay closed here.
        bool reopened = exitGate().isFirstTime();
        static OnceGate firstUsedAtExit;
        bool first = firstUsedAtExit.isFirstTime();
        bool second = firstUsedAtExit.isFirstTime();
        bool ok = !reopened && first && !second;
        printf("%-66s %s\n", "static destructor: gates work during static destruction",
               ok ? "PASS" : "FAIL");
        printf("%s\n", ok && failsBeforeExit == 0 ? "onceGate: all passed" : "onceGate: FAILED");
        fflush(stdout);
        // Report the result through the exit status.
        if (!ok || failsBeforeExit) _Exit(1);
    }
    static inline int failsBeforeExit = 0;
};

} // namespace

TC_CORE_TEST_MAIN() {
    // A function-local static, so it exists only in this test's process (in
    // allCoreTests a namespace-scope one would check every other test at exit).
    static LateUser late;

    // --- once ---------------------------------------------------------------
    {
        OnceGate g;
        bool first = g.isFirstTime();
        bool second = g.isFirstTime();
        bool third = g.isFirstTime();
        check("once: first call is true", first);
        check("once: later calls are false", !second && !third);
        check("once: a constinit global gate works",
              g_constantInitialized.isFirstTime() && !g_constantInitialized.isFirstTime());
    }

    // --- each gate is its own key --------------------------------------------
    {
        bool a = warnSite();
        bool a2 = warnSite();
        check("static gate: one call site is true once", a && !a2);
        PerObject p, q;
        bool p1 = p.warned.isFirstTime();
        bool q1 = q.warned.isFirstTime();
        bool p2 = p.warned.isFirstTime();
        check("member gate: each object is true once", p1 && q1 && !p2);
        check("member gate: another call site does not reopen", !warnSite());
    }

    // --- interval -----------------------------------------------------------
    {
        const double iv = 0.3;
        OnceGate g{iv};
        bool first = g.isFirstTime();
        bool soon = g.isFirstTime();
        check("interval: first call is true", first);
        check("interval: false within the interval", !soon);
        sleepSeconds(iv + 0.1);
        // The gate's last true lies between these two clock reads, so a check
        // can tell for sure whether a whole interval has passed since it: a
        // slow machine that oversleeps skips the "not yet" check instead of
        // failing it.
        const auto beforeTrue = std::chrono::steady_clock::now();
        bool after = g.isFirstTime();
        auto afterTrue = std::chrono::steady_clock::now();
        bool afterAgain = g.isFirstTime();
        check("interval: true again after the interval", after);
        check("interval: then false again (interval restarts)", !afterAgain);
        auto secondsSince = [](std::chrono::steady_clock::time_point t) {
            return std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count();
        };
        sleepSeconds(iv / 2);
        const bool halfTrue = g.isFirstTime();
        if (secondsSince(beforeTrue) < iv) {
            check("interval: false half an interval after the last true", !halfTrue);
        } else {
            std::printf("interval: half-interval check skipped (overslept to %.3f s)\n",
                        secondsSince(beforeTrue));
            if (halfTrue) afterTrue = std::chrono::steady_clock::now();
        }
        while (secondsSince(afterTrue) < iv + 0.05) sleepSeconds(0.02);
        check("interval: true again after another interval", g.isFirstTime());

        // 0, negative and NaN intervals mean once.
        OnceGate zero{0.0}, neg{-1.0}, nan{numeric_limits<double>::quiet_NaN()};
        check("interval 0 / -1 / NaN: first true",
              zero.isFirstTime() && neg.isFirstTime() && nan.isFirstTime());
        sleepSeconds(0.05);
        check("interval 0 / -1 / NaN: never true again",
              !zero.isFirstTime() && !neg.isFirstTime() && !nan.isFirstTime());

        // A huge interval is not an overflow: true once, then not again.
        OnceGate huge{1e300};
        bool h1 = huge.isFirstTime();
        check("interval 1e300: true once, then false", h1 && !huge.isFirstTime());
    }

    // --- thread safety ------------------------------------------------------
    auto hammer = [](OnceGate& gate, int threads, int calls) {
        atomic<int> trues{0};
        atomic<bool> go{false};
        vector<thread> ts;
        for (int t = 0; t < threads; ++t) {
            ts.emplace_back([&] {
                while (!go.load()) this_thread::yield();
                for (int i = 0; i < calls; ++i) {
                    if (gate.isFirstTime()) trues.fetch_add(1);
                }
            });
        }
        go = true;
        for (auto& th : ts) th.join();
        return trues.load();
    };
    {
        bool allOne = true;
        for (int round = 0; round < 200; ++round) {
            OnceGate g;
            if (hammer(g, 8, 200) != 1) allOne = false;
        }
        check("threads: 8 threads, one gate -> exactly one true (200 rounds)", allOne);
    }
    {
        // An interval longer than the run: still exactly one true.
        bool allOne = true;
        for (int round = 0; round < 200; ++round) {
            OnceGate g{60.0};
            if (hammer(g, 8, 200) != 1) allOne = false;
        }
        check("threads: interval gate -> exactly one true per interval", allOne);
    }

    // --- open the gate the static destructor checks ---------------------------
    check("exit gate: first use in main() is true", exitGate().isFirstTime());

    LateUser::failsBeforeExit = g_fail;
    // The final verdict is printed by late's destructor during static
    // destruction (exit status 1 on failure).
    return g_fail ? 1 : 0;
}
