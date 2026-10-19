// Async scheduler wait / wake regression (#292). No window or update loop.
// Precision is reported against the nominal 10 ms grid, with the Decision's
// loose 5 ms p95 reference. It is diagnostic, not a wall-time assertion:
// shared runners can stall the scheduler and trigger its intentional resync.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <vector>

using namespace std;
using namespace tc;

namespace {

using Clock = internal::AsyncScheduler::Clock;

bool await(condition_variable& cv, unique_lock<mutex>& lock, const function<bool()>& ready) {
    // A deadlock guard only; success depends on the callback's condition.
    return cv.wait_for(lock, chrono::seconds(30), ready);
}

bool testWakeAndCancel() {
    Node node;
    mutex m;
    condition_variable cv;
    int calls = 0;
    int cancelledCalls = 0;
    auto signal = [&] {
        lock_guard<mutex> lock(m);
        ++calls;
        cv.notify_all();
    };
    auto cancelled = [&] {
        lock_guard<mutex> lock(m);
        ++cancelledCalls;
    };

    // Exercise both idle wake-up and a newly added task ahead of a far-off
    // deadline. A missing task-change signal would leave the worker asleep.
    node.callAfterAsync(0.0, signal);
    {
        unique_lock<mutex> lock(m);
        if (!await(cv, lock, [&] { return calls == 1; })) {
            lock.unlock();
            node.cancelAllAsyncTimers();
            return false;
        }
    }
    const auto later = node.callAfterAsync(3600.0, cancelled);
    node.callAfterAsync(0.0, signal);
    {
        unique_lock<mutex> lock(m);
        if (!await(cv, lock, [&] { return calls == 2; })) {
            lock.unlock();
            node.cancelAllAsyncTimers();
            return false;
        }
    }
    node.cancelAsyncTimer(later);

    // Hold the worker in another owner's callback while cancelling tasks
    // already due. The barrier then proves neither cancelled task ran.
    Node gate;
    bool entered = false;
    bool released = false;
    gate.callAfterAsync(0.0, [&] {
        unique_lock<mutex> lock(m);
        entered = true;
        cv.notify_all();
        await(cv, lock, [&] { return released; });
    });
    {
        unique_lock<mutex> lock(m);
        if (!await(cv, lock, [&] { return entered; })) {
            released = true;
            cv.notify_all();
            lock.unlock();
            gate.cancelAllAsyncTimers();
            return false;
        }
    }
    const auto due = node.callAfterAsync(0.0, cancelled);
    node.cancelAsyncTimer(due);
    node.callAfterAsync(0.0, cancelled);
    node.cancelAllAsyncTimers();
    node.callAfterAsync(0.0, signal);
    unique_lock<mutex> lock(m);
    released = true;
    cv.notify_all();
    const bool complete = await(cv, lock, [&] { return calls == 3; });
    lock.unlock();
    gate.cancelAllAsyncTimers();
    node.cancelAllAsyncTimers();
    return complete && calls == 3 && cancelledCalls == 0;
}

bool samplePrecision() {
    constexpr int samples = 200;
    constexpr double interval = 0.01;
    Node node;
    mutex m;
    condition_variable cv;
    vector<double> lateness;
    lateness.reserve(samples);
    uint64_t id = 0;
    unique_lock<mutex> lock(m);
    const auto start = Clock::now();
    id = node.callEveryAsync(interval, [&] {
        const auto fired = Clock::now();
        lock_guard<mutex> callbackLock(m);
        const auto expected = start + chrono::duration_cast<Clock::duration>(
            chrono::duration<double>(interval * (lateness.size() + 1)));
        lateness.push_back(chrono::duration<double, milli>(fired - expected).count());
        if (lateness.size() == samples) {
            node.cancelAsyncTimer(id); // self-cancel must not wait for itself
            cv.notify_all();
        }
    });
    const bool complete = await(cv, lock, [&] { return lateness.size() == samples; });
    lock.unlock();
    node.cancelAllAsyncTimers(); // waits for any in-flight callback before reading
    if (!complete || lateness.size() != samples) return false;

    sort(lateness.begin(), lateness.end());
    const double p95 = lateness[(samples * 95 + 99) / 100 - 1];
    printf("200 ticks at 10 ms: nominal-grid p95 lateness %.3f ms, max %.3f ms\n",
           p95, lateness.back());
    printf("Loose CI reference p95 < 5 ms: %s (diagnostic only)\n",
           p95 < 5.0 ? "met" : "exceeded");
    return true;
}

} // namespace

TC_CORE_TEST_MAIN() {
    const bool wakes = testWakeAndCancel();
    printf("idle / earlier-deadline wake and cancellation: %s\n", wakes ? "PASS" : "FAIL");
    const bool samples = samplePrecision();
    printf("repeating callbacks and self-cancellation: %s\n", samples ? "PASS" : "FAIL");
    // Leave a far-off task for singleton shutdown: its destructor must wake
    // the native wait before joining, rather than wait for this deadline.
    internal::AsyncScheduler::get().after(internal::AsyncScheduler::newOwner(), 3600.0, [] {});
    return wakes && samples ? 0 : 1;
}
