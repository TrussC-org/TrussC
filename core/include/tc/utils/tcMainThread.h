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
//   tcp.onReceive.listen([&](Msg& m){
//       Msg copy = m;
//       runOnMainThread([this, copy]{ scene->addChild(make_shared<Enemy>(copy.pos)); });
//   });
//
// Event<T> builds a typed convenience on top of this — see Deliver::Main in
// tcEvent.h, which captures the payload and marshals the listener for you.
// ---------------------------------------------------------------------------

#include "tcThread.h"        // isMainThread()
#include <functional>

#if !defined(__EMSCRIPTEN__)
#include "tcThreadChannel.h" // ThreadChannel<T>
#endif

namespace trussc {

#if defined(__EMSCRIPTEN__)

// Web is single-threaded — everything already runs on the main thread, so
// there is nothing to marshal and no queue to drain.
inline void runOnMainThread(const std::function<void()>& fn) { if (fn) fn(); }
inline void drainMainThreadQueue() {}

#else

namespace internal {
    // FIFO of pending main-thread work. A function-local static so the queue is
    // constructed on first use and shared process-wide; ThreadChannel is itself
    // thread-safe (mutex + condition_variable).
    inline ThreadChannel<std::function<void()>>& mainThreadQueue() {
        static ThreadChannel<std::function<void()>> q;
        return q;
    }
}

// Run `fn` on the main thread. Immediately if already on it; otherwise queued
// to run at the start of the next frame. Safe to call from any thread.
inline void runOnMainThread(std::function<void()> fn) {
    if (!fn) return;
    if (isMainThread()) { fn(); return; }
    internal::mainThreadQueue().send(std::move(fn));
}

// Drain all pending main-thread work. Called by the framework once per frame
// (in _frame_cb, before update/draw). Headless loops can call it manually.
inline void drainMainThreadQueue() {
    std::function<void()> fn;
    while (internal::mainThreadQueue().tryReceive(fn)) {
        if (fn) fn();
    }
}

#endif

} // namespace trussc
