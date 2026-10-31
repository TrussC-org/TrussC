#pragma once
#include "../app/tcExit.h"

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
// at the next safe outer host turn, including turns with no window draw,
// when no traversal is in flight. If you are already on the main thread it runs
// immediately.
//
// Each turn runs what was queued when its drain started, in order; work queued
// while the drain runs waits for the next turn, so the host always makes progress.
// App-owned calls check owner liveness when delivered. Unowned calls without
// a token are never dropped. Queued captures live until queue storage releases
// them, with guest code pinned. There is no queue limit: a closure may edit the tree or free something. The count taken at drain start
// is reported by the tc_get_health MCP tool (mainQueuePending). Code that may queue faster than the
// app runs it, and can drop values, keeps its own bounded or latest-value buffer.
//
//   // In App::setup(), with receiveListener an EventListener App member:
//   receiveListener = tcp.onReceive.listen([this](Msg& copy) {
//       scene->addChild(make_shared<Enemy>(copy.pos));
//   }, Deliver::Main);
//
// This owned subscription checks the App at delivery. A raw user-created
// worker does not inherit App ownership; use an explicit liveness token for
// runOnMainThread work that captures an object from such a worker.
//
// Event<T> builds a typed convenience on top of this — see Deliver::Main in
// tcEvent.h, which captures the payload and marshals the listener for you.
// ---------------------------------------------------------------------------

#include "../app/internal/tcAppConstruction.h"
#include "tcThread.h"        // isMainThread()
#include <atomic>
#include <memory>
#include <cstddef>
#include <functional>
#include <utility>

#if !defined(__EMSCRIPTEN__)
#include "tcThreadChannel.h" // ThreadChannel<T>
#include <vector>
#endif

namespace trussc {

namespace internal {
    // Runtime-owned liveness for native async dialog completions. One copy
    // in the host Runtime, shared with hot reload guests; accessed on main
    // thread. During construction it selects the innermost ticket's token.
    std::shared_ptr<std::atomic<bool>>& appRunToken();
    std::function<void()> attributeMainThreadWork(std::function<void()>);
    // During a complete derived constructor this selects its ticket's token.
    // This accessor and appRunToken use the same host Runtime across DLLs.
    std::shared_ptr<std::atomic<bool>> constructorRunToken();
    inline bool appRunIsAlive(const std::weak_ptr<std::atomic<bool>>& token) {
        auto alive = token.lock();
        return alive && alive->load();
    }

    // Preallocated main-thread delivery for worker code that must not
    // allocate or throw (runOnMainThread allocates a closure and a queue
    // entry). The owner embeds the slot in state held by a shared_ptr and
    // posts it; the main-thread drain (drainMainThreadQueue) calls run once.
    // A slot is queued at most once at a time: posting an already queued
    // slot does nothing, so a flood cannot queue more than one delivery.
    // While queued, the list holds keepAlive, which must own the slot's
    // storage: the owner may be moved, closed or destroyed before the drain.
    // A post always waits for the next drain, also from the main thread.
    // The list head is constant-initialized and has no destructor, so a
    // worker may still post during static destruction; nothing drains then
    // and the posted state stays referenced by the list until process exit.
    class MainThreadSlot {
    public:
        using Run = void (*)(MainThreadSlot&);
        explicit MainThreadSlot(Run run, Run discard = nullptr) noexcept : run_(run), discard_(discard) {}
        MainThreadSlot(const MainThreadSlot&) = delete;
        MainThreadSlot& operator=(const MainThreadSlot&) = delete;

        // Any thread. Never allocates or throws. Returns whether it queued.
        bool post(std::shared_ptr<void> keepAlive) noexcept;
        // Host-owned mailboxes can use the same allocation-free slots while
        // taking their snapshot independently of the ordinary main queue.
        bool post(std::shared_ptr<void> keepAlive, std::atomic<MainThreadSlot*>& head) noexcept;
        bool isQueued() const noexcept { return queued_.load(std::memory_order_acquire); }

    private:
        friend void drainMainThreadSlots();
        friend size_t countQueuedMainThreadSlots() noexcept;
        friend class MainThreadSlotBatch;
        Run run_;
        Run discard_;
        MainThreadSlot* next_ = nullptr;     // list link while queued
        std::shared_ptr<void> keepAlive_;    // set by post, taken by the drain
        std::atomic<bool> queued_{false};
    };

    // Intrusive LIFO of posted slots, one per process. Defined in tcGlobal.cpp
    // (shared with hot reload guests, see mainThreadQueue below).
    std::atomic<MainThreadSlot*>& mainThreadSlots() noexcept;

    inline bool MainThreadSlot::post(std::shared_ptr<void> keepAlive) noexcept {
        return post(std::move(keepAlive), mainThreadSlots());
    }
    inline bool MainThreadSlot::post(std::shared_ptr<void> keepAlive,
                                     std::atomic<MainThreadSlot*>& head) noexcept {
        if (!keepAlive || queued_.exchange(true, std::memory_order_acq_rel)) return false;
        // Only the winner of `queued_` writes these until the drain takes them.
        keepAlive_ = std::move(keepAlive);
        MainThreadSlot* top = head.load(std::memory_order_relaxed);
        do {
            next_ = top;
        } while (!head.compare_exchange_weak(top, this, std::memory_order_release,
                                             std::memory_order_relaxed));
        return true;
    }

    class MainThreadSlotBatch {
    public:
        MainThreadSlotBatch() = default;
        explicit MainThreadSlotBatch(std::atomic<MainThreadSlot*>& head) noexcept {
            auto* posted = head.exchange(nullptr, std::memory_order_acquire);
            while (posted) {
                auto* next = posted->next_;
                posted->next_ = first_;
                first_ = posted;
                posted = next;
            }
        }
        MainThreadSlotBatch(MainThreadSlotBatch&& other) noexcept
            : first_(std::exchange(other.first_, nullptr)) {}
        MainThreadSlotBatch& operator=(MainThreadSlotBatch&& other) noexcept {
            consume(false);
            first_ = std::exchange(other.first_, nullptr);
            return *this;
        }
        ~MainThreadSlotBatch() { consume(false); }
        bool empty() const noexcept { return first_ == nullptr; }
        void deliver() { consume(true); }
    private:
        MainThreadSlot* first_ = nullptr;
        void consume(bool deliver) {
            while (first_) {
                auto* slot = first_;
                first_ = slot->next_;
                auto keepAlive = std::move(slot->keepAlive_);
                slot->queued_.store(false, std::memory_order_release);
                if (deliver) slot->run_(*slot);
                else if (slot->discard_) slot->discard_(*slot);
            }
        }
    };

    // Main thread: run the slots posted so far, oldest first. Each slot is
    // unqueued before its run, so a post during the run queues it again for
    // the next drain. keepAlive is released only after run returns, so run
    // may destroy the owner (for example from a Logger listener).
    inline void drainMainThreadSlots() {
        MainThreadSlotBatch(mainThreadSlots()).deliver();
    }

    // Main thread only (the drain runs there): slots currently queued.
    inline size_t countQueuedMainThreadSlots() noexcept {
        size_t count = 0;
        for (MainThreadSlot* slot = mainThreadSlots().load(std::memory_order_acquire);
             slot; slot = slot->next_) {
            ++count;
        }
        return count;
    }
}

#if defined(__EMSCRIPTEN__)

// Web is single-threaded — everything already runs on the main thread, so
// there is nothing to marshal and no queue to drain.
inline void runOnMainThread(const std::function<void()>& fn) {
    if (!fn) return;
    if (auto owner = internal::currentAppOwner()) {
        if (owner->ended || owner->retiring || !owner->token->load(std::memory_order_acquire)) return;
        internal::AppCallbackScope scope(owner);
        fn();
    } else fn();
}
namespace internal {
    inline void drainMainThreadQueue() { drainMainThreadSlots(); }
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
// to run at the next safe outer host turn. Safe to call from any thread.
inline void runOnMainThread(std::function<void()> fn) {
    if (!fn) return;
    if (auto owner = internal::currentAppOwner()) {
        if (owner->ended || owner->retiring || !owner->token->load(std::memory_order_acquire)) return;
        if (isMainThread()) {
            internal::AppCallbackScope scope(owner);
            fn();
            return;
        }
    } else if (isMainThread()) { fn(); return; }
    fn = internal::attributeMainThreadWork(std::move(fn));
    if (fn) internal::mainThreadQueue().send(std::move(fn));
}

// Run the main-thread work queued so far. Called by the framework once per
// outer host turn, before window traversal. Headless loops call it via the
// framework's run loop; exposed under internal:: for those paths.
// Takes everything queued at its start under one lock and runs it in order.
// A runOnMainThread() called from a closure inside the drain is on the main
// thread, so it runs right there (inline), not queued. Only work sent from
// other threads while the drain runs is deferred to the next call.
// Posted MainThreadSlots run after the closures.
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
    drainMainThreadSlots();
}
}

#endif

// Same dispatch as above, with the Deliver::Main liveness token. Check at
// execution time: an expired or false token drops the closure. Workers only
// enqueue and return; they never wait for the main thread. The token is not a
// barrier for a callback already running: its owner must serialize teardown
// with delivery (normally on the main thread). The wrapper costs one extra
// std::function per call; fine for per-event delivery, but a hot path would
// want the queue to hold {fn, token} directly.
inline void runOnMainThread(std::function<void()> fn,
                            std::weak_ptr<std::atomic<bool>> token) {
    if (!fn) return;
    auto owner = internal::currentAppOwner();
    if (owner && (owner->retiring || owner->ended || !owner->token->load(std::memory_order_acquire))) return;
    auto delivery = [owner = std::weak_ptr(owner),
                     owned = bool(owner), fn = std::move(fn), token = std::move(token)]() {
        auto alive = token.lock();
        if (!alive || !alive->load(std::memory_order_acquire)) return;
        auto app = owner.lock();
        if (owned && (!app || app->retiring || app->ended || !app->token->load(std::memory_order_acquire))) return;
        internal::AppCallbackScope scope(app);
        fn();
    };
    internal::AppOwnerScope process({});
    runOnMainThread(std::move(delivery));
}

} // namespace trussc
