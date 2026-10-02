#pragma once

// ---------------------------------------------------------------------------
// runOnMainThread - marshal work onto the main (scene) thread
// ---------------------------------------------------------------------------
//
// The Node tree and all GPU/draw state are owned by the main thread. Worker
// threads (network onReceive, async timer callbacks, audio callbacks, user
// tc::Thread subclasses) must NOT touch the tree directly — doing so is a data
// race on Node::children_ and friends (it crashes, see SECURITY/threading docs).
//
// runOnMainThread() is the safe path: it queues `fn` and the framework runs it
// at the start of the next frame (drained in _frame_cb, before update/draw),
// when no traversal is in flight. If you are already on the main thread it runs
// immediately.
//
// Each frame runs what was queued when its drain started, in order; work queued
// while the drain runs waits for the next frame, so the frame always starts.
// Nothing is dropped and there is no limit: a queued closure may edit the tree
// or free something. The count taken at drain start is reported by the
// tc_get_health MCP tool (mainQueuePending). Code that may queue faster than the
// app runs it, and can drop values, keeps its own bounded or latest-value buffer.
//
//   tcp.onReceive.listen([&](Msg& m){
//       Msg copy = m;
//       runOnMainThread([this, copy]{ scene->addChild(make_shared<Enemy>(copy.pos)); });
//   });
//
// Event<T> builds a typed convenience on top of this — see Deliver::Main in
// tcEvent.h, which captures the payload and marshals the listener for you.
// ---------------------------------------------------------------------------

#include "tcThread.h"        // isMainThread()
#include <cstddef>
#include <functional>

#if !defined(__EMSCRIPTEN__)
#include "tcThreadChannel.h" // ThreadChannel<T>
#include <atomic>
#include <vector>
#endif

namespace trussc {

#if defined(__EMSCRIPTEN__)

// Web is single-threaded — everything already runs on the main thread, so
// there is nothing to marshal and no queue to drain.
inline void runOnMainThread(const std::function<void()>& fn) { if (fn) fn(); }
namespace internal {
    inline void drainMainThreadQueue() {}
    inline size_t getMainThreadQueuePending() { return 0; }
}

#else

namespace internal {
    // FIFO of pending main-thread work, one per process; ThreadChannel is
    // itself thread-safe (mutex + condition_variable). Defined in tcGlobal.cpp:
    // header-inline, a hot reload guest on Windows queued into its own copy,
    // which the host's frame loop never drained (#249).
    ThreadChannel<std::function<void()>>& mainThreadQueue();

    // Number of closures the latest drain started with. Written by the drain,
    // read by tc_get_health. Defined in tcGlobal.cpp (one per process).
    std::atomic<size_t>& mainThreadQueuePendingCount();
    inline size_t getMainThreadQueuePending() {
        return mainThreadQueuePendingCount().load(std::memory_order_relaxed);
    }
}

// Run `fn` on the main thread. Immediately if already on it; otherwise queued
// to run at the start of the next frame. Safe to call from any thread.
inline void runOnMainThread(std::function<void()> fn) {
    if (!fn) return;
    if (isMainThread()) { fn(); return; }
    internal::mainThreadQueue().send(std::move(fn));
}

// Run the main-thread work queued so far. Called by the framework once per
// frame (in _frame_cb, before update/draw). Headless loops call it via the
// framework's run loop; exposed under internal:: for those paths.
// Takes everything queued at its start under one lock and runs it in order.
// A runOnMainThread() called from a closure inside the drain is on the main
// thread, so it runs right there (inline), not queued. Only work sent from
// other threads while the drain runs is deferred to the next call.
namespace internal {
inline void drainMainThreadQueue() {
    std::vector<std::function<void()>> batch = mainThreadQueue().receiveAll();
    mainThreadQueuePendingCount().store(batch.size(), std::memory_order_relaxed);
    for (auto& slot : batch) {
        // Move out so each closure (and what it captured) is released right
        // after it runs, not at the end of the drain.
        std::function<void()> fn = std::move(slot);
        if (fn) fn();
    }
}
}

#endif

} // namespace trussc
