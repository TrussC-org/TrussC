// =============================================================================
// core/tests/threadLifecycle — behavioral regression test for tc::Thread
// destruction (#257).
//
// Headless, console, exit code = pass/fail (build_all.py runs it in CI).
//
// Guards the invariants:
//   - Destroying a Thread whose worker has entered threadedFunction() never
//     calls std::terminate: not when its worker has already returned, not when
//     only stopThread() was called, not after a restart, not when a subclass
//     that does not wait is destroyed from its own threadedFunction().
//     ~Thread() used to join only while isThreadRunning() was true, so those
//     cases left a joinable std::thread behind (SIGABRT), and the last one
//     joined itself (resource_deadlock_would_occur, then std::terminate).
//   - After that self-destruction, the worker writes nothing to the freed
//     object.
//   - A subclass that waits in its own destructor (the documented contract)
//     never has threadedFunction() running after its members are destroyed.
//   - The base destructor logs one warning when it finds threadedFunction()
//     still running (the subclass did not wait), also after only stopThread(),
//     and none otherwise.
//
// Not covered, because both still terminate (see "Destruction" in
// tcThread.h): a subclass that does not wait and is destroyed before its
// worker entered threadedFunction() (e.g. right after startThread()), and a
// subclass that waits and is destroyed from its own threadedFunction().
//
// The pre-fix build aborts on the first case, so each case prints its own line
// as soon as it is done. A watchdog turns a hang (a join that never returns)
// into a FAIL instead of a stuck CI job.
// =============================================================================

#include <TrussC.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

using namespace std;
using namespace tc;

static int g_fail = 0;
static void check(const char* name, bool ok) {
    printf("%-60s %s\n", name, ok ? "PASS" : "FAIL");
    fflush(stdout);   // flush per line so CI logs survive a later abort
    if (!ok) ++g_fail;
}

// Poll a flag set by a worker, with a deadline so a broken build fails
// instead of hanging.
template <typename Pred>
static bool waitUntil(Pred pred, int ms = 5000) {
    const auto deadline = chrono::steady_clock::now() + chrono::milliseconds(ms);
    while (!pred()) {
        if (chrono::steady_clock::now() >= deadline) return false;
        this_thread::sleep_for(chrono::milliseconds(1));
    }
    return true;
}

// Warnings the Thread destructor logged ("[Thread] ..." at Warning level).
static atomic<int> g_threadWarnings{0};

// Globals, not members: the cases below inspect them after the Thread object
// (and its members) are gone.
static atomic<int>  g_oneShotRuns{0};
static atomic<bool> g_loopEntered{false};
static atomic<bool> g_loopExited{false};
static atomic<bool> g_waiterEntered{false};
static atomic<bool> g_waiterMembersGone{false};
static atomic<int>  g_ranAfterMembersGone{0};
static atomic<bool> g_lingerEntered{false};
static atomic<bool> g_lingerExited{false};
static atomic<bool> g_selfDeleted{false};

// Returns without looping: the worker finishes on its own.
struct OneShot : Thread {
protected:
    void threadedFunction() override { g_oneShotRuns.fetch_add(1); }
};

// Loops until stopped. Its destructor does NOT wait (the ofThread habit).
struct Loop : Thread {
protected:
    void threadedFunction() override {
        g_loopEntered.store(true);
        while (isThreadRunning()) sleep(1);
        g_loopExited.store(true);
    }
};

// Marks when the subclass members are destroyed.
struct MemberProbe {
    ~MemberProbe() { g_waiterMembersGone.store(true); }
};

// Follows the contract: waits in its own destructor, before its members go.
struct Waiter : Thread {
    MemberProbe probe;
    ~Waiter() override { waitForThread(); }
protected:
    void threadedFunction() override {
        g_waiterEntered.store(true);
        while (isThreadRunning()) {
            if (g_waiterMembersGone.load()) g_ranAfterMembersGone.fetch_add(1);
            sleep(1);
        }
        if (g_waiterMembersGone.load()) g_ranAfterMembersGone.fetch_add(1);
    }
};

// Leaves its loop when stopped, but stays inside threadedFunction() until the
// destructor's warning arrives (2 s at most), so the destructor always finds
// it still running after stopThread().
struct Lingering : Thread {
protected:
    void threadedFunction() override {
        g_lingerEntered.store(true);
        while (isThreadRunning()) sleep(1);
        waitUntil([] { return g_threadWarnings.load() > 0; }, 2000);
        g_lingerExited.store(true);
    }
};

// Deletes itself from its own worker, so ~Thread runs on that worker. It lives
// in a static buffer that operator delete poisons instead of freeing, so any
// write the worker makes after the delete shows up.
struct SelfDeleting : Thread {
    static void* operator new(size_t size);
    static void operator delete(void* p) noexcept;
protected:
    void threadedFunction() override {
        delete this;
        g_selfDeleted.store(true);   // globals only from here: the object is gone
    }
};

constexpr unsigned char kSelfPoison = 0xAB;
alignas(SelfDeleting) static unsigned char g_selfStorage[sizeof(SelfDeleting)];

void* SelfDeleting::operator new(size_t) { return g_selfStorage; }   // one object only
void SelfDeleting::operator delete(void* p) noexcept {
    memset(p, kSelfPoison, sizeof(g_selfStorage));
}

static bool selfStorageUntouched() {
    for (unsigned char b : g_selfStorage) {
        if (b != kSelfPoison) return false;
    }
    return true;
}

int main() {
    // A join that never returns would hang CI; fail loudly instead.
    thread([] {
        this_thread::sleep_for(chrono::seconds(60));
        printf("FAIL: watchdog timeout (a Thread destructor did not return)\n");
        fflush(stdout);
        _Exit(3);
    }).detach();

    EventListener warnListener = getLogger().onLog.listen([](LogEventArgs& e) {
        if (e.level == LogLevel::Warning && e.message.rfind("[Thread]", 0) == 0) {
            g_threadWarnings.fetch_add(1);
        }
    });

    // --- 1. Destroyed after the worker returned on its own ---
    {
        g_threadWarnings.store(0);
        {
            OneShot t;
            t.startThread();
            check("one-shot: worker finished before destruction",
                  waitUntil([&] { return !t.isThreadRunning(); }));
        }   // joinable std::thread here: used to be std::terminate
        check("one-shot: destroyed without terminate", true);
        check("one-shot: ran once", g_oneShotRuns.load() == 1);
        check("one-shot: no warning (the worker had returned)", g_threadWarnings.load() == 0);
    }

    // --- 2. Destroyed after stopThread() only (the ofThread exit() habit) ---
    {
        g_loopEntered.store(false);
        g_loopExited.store(false);
        {
            Loop t;
            t.startThread();
            // Only once the worker is inside threadedFunction(): before that, a
            // non-waiting subclass can still reach the pure virtual (see
            // "Destruction" in tcThread.h).
            check("stopThread only: worker entered the loop",
                  waitUntil([] { return g_loopEntered.load(); }));
            t.stopThread();
        }
        check("stopThread only: destroyed without terminate", true);
        check("stopThread only: destructor joined the worker", g_loopExited.load());
    }

    // --- 3. Restarted after the first run finished, then destroyed ---
    {
        g_threadWarnings.store(0);
        g_oneShotRuns.store(0);
        {
            OneShot t;
            t.startThread();
            bool first = waitUntil([&] { return !t.isThreadRunning(); });
            t.startThread();
            bool second = waitUntil([&] { return !t.isThreadRunning(); });
            check("restart: both runs finished", first && second);
        }
        check("restart: destroyed without terminate", true);
        check("restart: ran twice", g_oneShotRuns.load() == 2);
        check("restart: no warning", g_threadWarnings.load() == 0);
    }

    // --- 4. The contract: the subclass waits in its own destructor ---
    {
        g_threadWarnings.store(0);
        g_waiterEntered.store(false);
        g_waiterMembersGone.store(false);
        g_ranAfterMembersGone.store(0);
        {
            Waiter t;
            t.startThread();
            check("waiting subclass: worker entered the loop",
                  waitUntil([] { return g_waiterEntered.load(); }));
        }
        check("waiting subclass: members were destroyed", g_waiterMembersGone.load());
        check("waiting subclass: threadedFunction never ran after its members",
              g_ranAfterMembersGone.load() == 0);
        check("waiting subclass: no warning", g_threadWarnings.load() == 0);
    }

    // --- 5. The subclass did not wait: one warning, then the base joins ---
    {
        g_threadWarnings.store(0);
        g_loopEntered.store(false);
        g_loopExited.store(false);
        {
            Loop t;
            t.startThread();
            check("not waited: worker entered the loop",
                  waitUntil([] { return g_loopEntered.load(); }));
        }   // never stopped: the base destructor finds the loop still running
        check("not waited: destructor joined the worker", g_loopExited.load());
        check("not waited: exactly one warning", g_threadWarnings.load() == 1);
    }

    // --- 6. stopThread() only, while threadedFunction() is still running ---
    {
        g_threadWarnings.store(0);
        g_lingerEntered.store(false);
        g_lingerExited.store(false);
        {
            Lingering t;
            t.startThread();
            check("stopped, still running: worker entered the loop",
                  waitUntil([] { return g_lingerEntered.load(); }));
            t.stopThread();
        }   // isThreadRunning() is false, but threadedFunction() has not returned
        check("stopped, still running: destructor joined the worker", g_lingerExited.load());
        check("stopped, still running: exactly one warning", g_threadWarnings.load() == 1);
    }

    // --- 7. Never started ---
    {
        g_threadWarnings.store(0);
        { Loop t; }
        check("never started: no warning", g_threadWarnings.load() == 0);
    }

    // --- 8. Destroyed from its own threadedFunction(): detach, not self-join ---
    {
        g_threadWarnings.store(0);
        g_selfDeleted.store(false);
        auto* t = new SelfDeleting();
        t->startThread();   // the worker deletes the object right away
        check("self-destroy: destructor returned on its own worker",
              waitUntil([] { return g_selfDeleted.load(); }));
        check("self-destroy: no warning", g_threadWarnings.load() == 0);
        // Let the detached worker leave its lambda, then check that neither it
        // nor startThread() wrote to the object after the delete.
        this_thread::sleep_for(chrono::milliseconds(50));
        check("self-destroy: nothing written to the object after delete",
              selfStorageUntouched());
    }

    printf("\n%s  (%d failure%s)\n", g_fail ? "FAILED" : "PASSED",
           g_fail, g_fail == 1 ? "" : "s");
    fflush(stdout);
    return g_fail ? 1 : 0;
}
