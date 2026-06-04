#pragma once

// =============================================================================
// tcAsyncScheduler.h - precise off-thread timers
// =============================================================================
// A single background thread that fires scheduled callbacks at precise times,
// independent of the render frame rate. Backs Node::callAfterAsync /
// callEveryAsync (the frame-driven callAfter / callEvery are quantized to the
// update loop, ~16 ms, and drift; these are not).
//
// IMPORTANT: callbacks run ON THE SCHEDULER THREAD, not the update/draw thread.
//   - Guard any state shared with update()/draw() behind a mutex.
//   - Never draw or touch GPU resources from a callback.
//   - Sound (AudioEngine::play) is safe; serialize MIDI/other output so the
//     main thread isn't sending at the same time.
//
// Repeating timers reschedule at absolute time (period never drifts); if a tick
// falls behind by more than one interval it resyncs to "now" instead of
// bursting. Each owner (a Node) can cancel all of its timers at once.
// =============================================================================

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace trussc {

class AsyncScheduler {
public:
    using Clock    = std::chrono::steady_clock;
    using Callback = std::function<void()>;

    static AsyncScheduler& get() {
        static AsyncScheduler instance;
        return instance;
    }

    // A unique, non-zero owner token so a Node can group + cancel its timers.
    static uint64_t newOwner() {
        static std::atomic<uint64_t> next{1};
        return next.fetch_add(1, std::memory_order_relaxed);
    }

    // Fire `cb` once after `seconds`. Returns a task id (for cancel()).
    uint64_t after(uint64_t owner, double seconds, Callback cb) {
        return schedule(owner, seconds, /*interval=*/0.0, std::move(cb));
    }

    // Fire `cb` every `seconds`, starting one interval from now.
    uint64_t every(uint64_t owner, double seconds, Callback cb) {
        if (seconds < 0.0) seconds = 0.0;
        return schedule(owner, seconds, /*interval=*/seconds, std::move(cb));
    }

    // Cancel one task. Blocks until its callback finishes if it is executing
    // right now (unless called from inside the callback itself).
    void cancel(uint64_t id) {
        std::unique_lock<std::mutex> lk(mtx_);
        eraseIf([id](const Task& t) { return t.id == id; });
        waitInFlight(lk, [&] { return executingId_ == id; });
    }

    // Cancel every task belonging to `owner` (node destroy / mode change).
    void cancelOwner(uint64_t owner) {
        if (owner == 0) return;
        std::unique_lock<std::mutex> lk(mtx_);
        eraseIf([owner](const Task& t) { return t.owner == owner; });
        waitInFlight(lk, [&] { return executingOwner_ == owner; });
    }

private:
    struct Task {
        uint64_t          id;
        uint64_t          owner;
        Clock::time_point when;
        double            interval;   // 0 = one-shot
        Callback          cb;
    };

    AsyncScheduler() : worker_([this] { run(); }) {}
    ~AsyncScheduler() {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            stop_ = true;
        }
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
    }
    AsyncScheduler(const AsyncScheduler&)            = delete;
    AsyncScheduler& operator=(const AsyncScheduler&) = delete;

    static Clock::duration toDuration(double seconds) {
        return std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(seconds));
    }

    uint64_t schedule(uint64_t owner, double delay, double interval, Callback cb) {
        if (delay < 0.0) delay = 0.0;
        uint64_t id   = nextId_.fetch_add(1, std::memory_order_relaxed);
        auto     when = Clock::now() + toDuration(delay);
        {
            std::lock_guard<std::mutex> lk(mtx_);
            tasks_.push_back({id, owner, when, interval, std::move(cb)});
        }
        cv_.notify_all();   // wake the worker to recompute its sleep target
        return id;
    }

    template <typename Pred>
    void eraseIf(Pred pred) {   // caller holds mtx_
        tasks_.erase(std::remove_if(tasks_.begin(), tasks_.end(), pred), tasks_.end());
    }

    // Wait until no in-flight callback matches. No-op on the worker thread (a
    // callback cancelling its own timer) so it can't deadlock on itself.
    template <typename Match>
    void waitInFlight(std::unique_lock<std::mutex>& lk, Match isExecuting) {
        if (std::this_thread::get_id() == workerId_) return;
        doneCv_.wait(lk, [&] { return !isExecuting(); });
    }

    void run() {
        workerId_ = std::this_thread::get_id();
        std::unique_lock<std::mutex> lk(mtx_);
        while (!stop_) {
            if (tasks_.empty()) {
                cv_.wait(lk, [&] { return stop_ || !tasks_.empty(); });
                continue;
            }

            auto next = std::min_element(tasks_.begin(), tasks_.end(),
                [](const Task& a, const Task& b) { return a.when < b.when; });
            auto when = next->when;
            if (Clock::now() < when) {
                cv_.wait_until(lk, when);   // wakes early if a task is added/removed
                continue;                  // re-evaluate the earliest task
            }

            uint64_t id    = next->id;
            uint64_t owner = next->owner;
            Callback cb    = next->cb;      // copy so reschedule/erase can't dangle it

            if (next->interval > 0.0) {
                // Absolute reschedule -> no drift; resync to now if we fell behind.
                auto due = when + toDuration(next->interval);
                auto now = Clock::now();
                next->when = (due < now) ? now : due;
            } else {
                eraseIf([id](const Task& t) { return t.id == id; });
            }

            executingId_    = id;
            executingOwner_ = owner;
            lk.unlock();
            cb();                           // run the callback WITHOUT the lock
            lk.lock();
            executingId_    = 0;
            executingOwner_ = 0;
            doneCv_.notify_all();
        }
    }

    std::mutex              mtx_;
    std::condition_variable cv_;       // worker sleep / wake
    std::condition_variable doneCv_;   // callback-finished signal for cancel()
    std::vector<Task>       tasks_;
    std::atomic<uint64_t>   nextId_{1};
    uint64_t                executingId_    = 0;
    uint64_t                executingOwner_ = 0;
    std::thread::id         workerId_;
    bool                    stop_ = false;
    std::thread             worker_;    // declared last: starts after the rest
};

} // namespace trussc
