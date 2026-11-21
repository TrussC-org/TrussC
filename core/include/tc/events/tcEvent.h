#pragma once

// =============================================================================
// tcEvent - Generic event class
// =============================================================================

#include <functional>
#include <vector>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <limits>
#include <utility>
#include <type_traits>
#include <optional>
#include "tcEventTree.h"
#include "tcEventDispatch.h"
#include "tcEventListener.h"
#include "tcExternalDelivery.h"
#include "../utils/tcMainThread.h"  // runOnMainThread (for Deliver::Main)
#include "../utils/tcAtomicSharedPtr.h"  // internal::AtomicSharedPtr (listener snapshot)
#include "../utils/tcTeardown.h"

// ---------------------------------------------------------------------------
// Mutex configuration for thread safety
// ---------------------------------------------------------------------------
// Emscripten in single-threaded mode (default) does not support pthreads.
// Including <mutex> or using std::recursive_mutex causes WASM instantiation
// to fail with "Import #0 env: module is not an object or function".
//
// Solution: Use a no-op mutex for single-threaded Emscripten builds.
// If you need threading in Emscripten, compile with -pthread flag, which
// defines __EMSCRIPTEN_PTHREADS__ and enables Web Workers-based threading.
// ---------------------------------------------------------------------------
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
namespace trussc {
    struct NullMutex {
        void lock() {}
        bool try_lock() { return true; }
        void unlock() {}
    };
}
#define TC_MUTEX trussc::NullMutex
#define TC_LOCK_GUARD(m) (void)0
#else
#include <mutex>
#define TC_MUTEX std::recursive_mutex
#define TC_LOCK_GUARD(m) std::lock_guard<std::recursive_mutex> lock(m)
#endif

namespace trussc {
namespace internal {
    template<class> struct GuardedEventDelivery;
    struct FrameworkEventAccess;
}

// ---------------------------------------------------------------------------
// Event priority
// ---------------------------------------------------------------------------
namespace EventPriority {
    constexpr int BeforeApp = 0;
    constexpr int App = 100;
    constexpr int AfterApp = 200;
}

// Audio listener priorities for AudioEngine::audioOut.
// Use these so that synth / effector / monitor addons (tcxSynth-*, tcxAudioEffector,
// tcxScope, ...) compose deterministically even when the user instantiates them
// in arbitrary order. The numeric gap leaves room for sub-categories (early effect
// vs late effect, etc.) without renumbering the well-known ones.
//
// Recommended use:
//   - Generator (oscillators / synths)  → produces audio into the buffer
//   - Effect    (reverb / filter / EQ)  → reads + writes the buffer
//   - Monitor   (scope / FFT / record)  → reads only, ideally last
//
// Listeners with no priority specified fall in `App = 100`, which equals
// `Generator`. That matches the most common "user wrote a synth callback in
// audioOut" case — explicit values are only needed for effect / monitor.
namespace audio {
namespace priority {
    constexpr int Generator = 100;   // == EventPriority::App; the default
    constexpr int Effect    = 500;
    constexpr int Monitor   = 900;
}
}

// ---------------------------------------------------------------------------
// Delivery thread for a listener
// ---------------------------------------------------------------------------
// Events may be fired (notify) from any thread — network onReceive runs on a
// worker thread, audioOut on the audio thread, etc. A listener that touches the
// Node tree / GPU must run on the main thread. Deliver controls this per
// listener:
//
//   Inline (default) : run on whatever thread called notify() — the existing
//                      behavior. Hot path, no allocation.
//   Main             : run on the main thread. If notify() is already on the
//                      main thread it runs immediately; otherwise the payload
//                      is copied and the call is marshalled via runOnMainThread
//                      (runs at the start of the next frame).
//
// Notes for Deliver::Main:
//   - App retirement cancels delivery. An outstanding synchronous snapshot or
//     queued Main call retains callback captures until that snapshot or queue
//     item is released. Release App resources explicitly in cleanup(), rather
//     than relying on a callback capture's destructor at retirement:
//       void cleanup() override {
//           texture.clear();
//       }
//   - The payload T is COPIED (the original `arg` reference is gone by next
//     frame), so the listener cannot write back to `arg` and does not
//     participate in `consumed` propagation. Fine for reactive events
//     (onReceive); input events that use `consumed` already fire on the main
//     thread, where Main is a no-op anyway.
//   - External network/device Main copies are unlimited by default.
//     setMainThreadDeliveryLimit(count) optionally limits queued and executing
//     copies across this event's listeners (0 restores unlimited delivery).
//     Overflow drops each whole copy and logs one Warning per episode; TCP stays connected.
//     App-authored notify() and runOnMainThread() remain growable.
//   - Requires T to be copy-constructible (otherwise delivered inline).
//   - Listener lifetime is honored across the marshal boundary: a call queued
//     for next frame re-checks the listener's liveness token when it runs, so
//     it is dropped if the EventListener was destroyed (or removeListener /
//     clear() ran) while it sat in the queue. The RAII guarantee — "after the
//     listener dies, the callback never runs" — holds for Deliver::Main.
//     (Raw runOnMainThread(fn) has no such token; there the captured object's
//     lifetime is the caller's responsibility.)
//
// Scoped enum on purpose: it does NOT convert to int, so listen(fn, Deliver)
// and the existing listen(fn, int priority) overloads never collide.
enum class Deliver { Inline, Main };

// Borrowed device buffers cannot be queued. Keep a separate diagnostic for
// each event and reject runtime delivery choices at the call site.
struct AudioOutBuffer;
struct AudioInBuffer;
namespace internal {
// A runtime Deliver value fails as "not a constant expression" and points here:
// audioOut runs on the audio thread; read AudioEngine::getAnalysisBuffer() in update().
struct AudioOutDelivery {
    consteval AudioOutDelivery(Deliver delivery) {
        if (delivery != Deliver::Inline)
            throw "audioOut runs on the audio thread; read AudioEngine::getAnalysisBuffer() in update() to visualize";
    }
};
// audioIn runs on the capture thread; read MicInput::getBuffer() in update().
struct AudioInDelivery {
    consteval AudioInDelivery(Deliver delivery) {
        if (delivery != Deliver::Inline)
            throw "audioIn runs on the capture thread; read MicInput::getBuffer() in update()";
    }
};
// onFrame runs on the capture thread; use getQueuedFrames() or isFrameNew()/getPixels() in update().
struct VideoFrameDelivery {
    consteval VideoFrameDelivery(Deliver delivery) {
        if (delivery != Deliver::Inline)
            throw "onFrame runs on the capture thread; use getQueuedFrames() or isFrameNew()/getPixels() in update()";
    }
};
template<class T>
inline constexpr bool isAudioEvent = std::is_same_v<T, AudioOutBuffer> || std::is_same_v<T, AudioInBuffer>;
template<class T>
using AudioEventDelivery = std::conditional_t<std::is_same_v<T, AudioOutBuffer>, AudioOutDelivery, AudioInDelivery>;
}

// ---------------------------------------------------------------------------
// Event<T> - Event with arguments
// ---------------------------------------------------------------------------
namespace internal { template<class T> struct EventSnapshotDelivery; }

template<typename T>
class Event {
public:
    // Callback type (argument passed by reference - can be modified)
    using Callback = std::function<void(T&)>;

    Event() : Event(EventPriority::App) {}
    explicit Event(int defaultPriority)
        : alive_(std::make_shared<std::atomic<bool>>(true)), defaultPriority_(defaultPriority) {}
    ~Event() { alive_->store(false, std::memory_order_relaxed); }

    // Optional count limit for external Main copies across this event's listeners.
    // Default/0: unlimited. Existing copies survive a lower limit. Overflow
    // drops new copies and logs one Warning per episode; allocation failure drops silently.
    void setMainThreadDeliveryLimit(size_t count) {
        TC_LOCK_GUARD(mutex_);
        if (!externalStore_) {
            externalStore_ = std::make_shared<internal::ExternalDeliveryStore>();
        }
        std::lock_guard<std::mutex> storeLock(externalStore_->mutex);
        externalStore_->deliveryLimit = count;
        if (!count || externalStore_->pending < count) externalStore_->overflowing = false;
    }

    // Copy/Move forbidden (events have fixed location)
    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;
    Event(Event&&) = delete;
    Event& operator=(Event&&) = delete;

    // Omitted priority uses this event instance's default; every integer is explicit.
    [[nodiscard]] EventListener listen(Callback callback) {
        return listen(std::move(callback), defaultPriority_);
    }
    [[nodiscard]] EventListener listen(Callback callback, Deliver deliver) requires (!internal::isAudioEvent<T>) {
        return listen(std::move(callback), deliver, defaultPriority_);
    }
    template<typename Obj>
    [[nodiscard]] EventListener listen(Obj* obj, void (Obj::*method)(T&)) {
        return listen(obj, method, defaultPriority_);
    }
    template<typename Obj>
    [[nodiscard]] EventListener listen(Obj* obj, void (Obj::*method)(T&), Deliver deliver) requires (!internal::isAudioEvent<T>) {
        return listen(obj, method, deliver, defaultPriority_);
    }
    [[deprecated("Use 'listener = event.listen(callback)' instead")]]
    void listen(EventListener& listener, Callback callback) {
        listenImpl(listener, std::move(callback), Deliver::Inline, defaultPriority_);
    }
    template<typename Obj>
    [[deprecated("Use 'listener = event.listen(obj, &Class::method)' instead")]]
    void listen(EventListener& listener, Obj* obj, void (Obj::*method)(T&)) {
        listenImpl(listener, [obj, method](T& arg) { (obj->*method)(arg); },
                   Deliver::Inline, defaultPriority_);
    }

    // Register listener with lambda (returns EventListener) - preferred API
    [[nodiscard]] EventListener listen(Callback callback,
                                       int priority) {
        EventListener listener;
        listenImpl(listener, std::move(callback), Deliver::Inline, priority);
        return listener;
    }

    // Same, but declare the delivery thread. listen(fn, Deliver::Main) runs the
    // listener on the main thread regardless of which thread fires notify().
    [[nodiscard]] EventListener listen(Callback callback, Deliver deliver,
                                       int priority) requires (!internal::isAudioEvent<T>) {
        EventListener listener;
        listenImpl(listener, std::move(callback), deliver, priority);
        return listener;
    }

    // Register listener with member function (returns EventListener) - preferred API
    template<typename Obj>
    [[nodiscard]] EventListener listen(Obj* obj, void (Obj::*method)(T&),
                                       int priority) {
        return listen([obj, method](T& arg) {
            (obj->*method)(arg);
        }, priority);
    }

    // Same, with explicit delivery thread.
    template<typename Obj>
    [[nodiscard]] EventListener listen(Obj* obj, void (Obj::*method)(T&),
                                       Deliver deliver,
                                       int priority) requires (!internal::isAudioEvent<T>) {
        return listen([obj, method](T& arg) {
            (obj->*method)(arg);
        }, deliver, priority);
    }

    // Audio events accept only a compile-time Inline choice. Omission and
    // explicit integer priorities continue to use the existing inline path.
    [[nodiscard]] EventListener listen(Callback callback, internal::AudioEventDelivery<T>)
        requires internal::isAudioEvent<T> {
        return listen(std::move(callback), defaultPriority_);
    }
    [[nodiscard]] EventListener listen(Callback callback, internal::AudioEventDelivery<T>, int priority)
        requires internal::isAudioEvent<T> {
        return listen(std::move(callback), priority);
    }
    template<typename Obj>
    [[nodiscard]] EventListener listen(Obj* obj, void (Obj::*method)(T&), internal::AudioEventDelivery<T>)
        requires internal::isAudioEvent<T> {
        return listen(obj, method, defaultPriority_);
    }
    template<typename Obj>
    [[nodiscard]] EventListener listen(Obj* obj, void (Obj::*method)(T&), internal::AudioEventDelivery<T>, int priority)
        requires internal::isAudioEvent<T> {
        return listen(obj, method, priority);
    }

    // Deprecated: use `listener = event.listen(callback)` instead
    [[deprecated("Use 'listener = event.listen(callback)' instead")]]
    void listen(EventListener& listener, Callback callback,
                int priority) {
        listenImpl(listener, std::move(callback), Deliver::Inline, priority);
    }

    // Deprecated: use `listener = event.listen(obj, &Class::method)` instead
    template<typename Obj>
    [[deprecated("Use 'listener = event.listen(obj, &Class::method)' instead")]]
    void listen(EventListener& listener, Obj* obj, void (Obj::*method)(T&),
                int priority) {
        listenImpl(listener, [obj, method](T& arg) {
            (obj->*method)(arg);
        }, Deliver::Inline, priority);
    }

private:
    struct Entry : std::enable_shared_from_this<Entry> {
        uint64_t id = 0;
        int priority;
        Deliver deliver;
        bool frameworkOwned;
        std::optional<Callback> callback;
        std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);
        std::shared_ptr<internal::AppOwner> owner;
        std::shared_ptr<internal::AppListenerCancellation> cancellation;
        Entry(int priority, Deliver deliver, bool frameworkOwned, Callback callback,
              std::shared_ptr<internal::AppOwner> owner)
            : priority(priority), deliver(deliver), frameworkOwned(frameworkOwned),
              callback(callback ? std::optional<Callback>(std::move(callback)) : std::nullopt), owner(std::move(owner)) {}
        ~Entry() { internal::unregisterAppListener(cancellation); }
    };
    using EntryList = internal::EventTree<Entry>;
    using ConstEntryListPtr = typename EntryList::Tree;

    void listenImpl(EventListener& listener, Callback callback,
                    Deliver deliver, int priority, bool frameworkOwned = false) {
        if (!alive_->load(std::memory_order_acquire)) return;
        auto owner = frameworkOwned ? std::shared_ptr<internal::AppOwner>{} : internal::currentAppOwner();
        if (owner && (owner->retiring || owner->ended || owner->callbacksClosing || !owner->token->load())) return;
        auto entry = std::make_shared<Entry>(priority, deliver, frameworkOwned, std::move(callback), owner);
        // Reserve, register with the App, publish only if still alive. App and
        // Event locks are never nested, even if closing races registration.
        {
            TC_LOCK_GUARD(mutex_);
            if (nextId_ == (std::numeric_limits<uint64_t>::max)()) throw std::overflow_error("Event listener identities exhausted");
            entry->id = nextId_++;
        }
        std::weak_ptr<std::atomic<bool>> weak = alive_;
        EventListener::DisconnectFunc disconnect = [this, weak, slot = std::weak_ptr(entry)] {
            auto source = weak.lock();
            if (auto live = slot.lock()) {
                live->alive->store(false, std::memory_order_release);
                if (source && source->load(std::memory_order_acquire)) removeListener(*live);
            }
        };
        if (owner) entry->cancellation = internal::registerAppListener(owner, entry->alive,
            internal::isAppExitEvent(this), disconnect);
        ConstEntryListPtr cur, previousFramework;
        {
            TC_LOCK_GUARD(mutex_);
            if (!entry->alive->load(std::memory_order_acquire) || !alive_->load(std::memory_order_acquire)) return;
            cur = internal::sharedLoad(entries_);
            previousFramework = frameworkEntries_;
            auto next = EntryList::insert(needsSweep_ ? EntryList::sweep(cur) : cur, entry);
            auto framework = needsSweep_ ? EntryList::sweep(frameworkEntries_) : frameworkEntries_;
            if (frameworkOwned) framework = EntryList::insert(framework, entry);
            frameworkEntries_ = std::move(framework);
            needsSweep_ = false;
            internal::sharedStore(entries_, std::move(next));
        }
        listener = EventListener(std::move(disconnect));
    }

public:

    // Fire event. Hot path: a single atomic load (no allocation, no mutex)
    // — safe to call from the audio thread.
    //
    // Changes to the listener list during a notify() pass (#107, #256):
    //   - A listener added during the pass starts from the next notify().
    //   - A listener removed during the pass on the notifying thread (an
    //     earlier listener disconnects it, or clear() runs) is not called
    //     again in that pass: its liveness flag is checked before each call.
    //   - Removal from ANOTHER thread does not wait. A callback already
    //     running keeps running after disconnect() returns, and one whose
    //     flag was read just before the removal can still start. The owner
    //     of the notifying thread provides the barrier for that (e.g.
    //     AudioEngine::waitForAudioCallbacks()), or the listener uses
    //     Deliver::Main.
    void notify(T& arg) {
        notifySnapshot(internal::sharedLoad(entries_), arg, {}, alive_);
    }

private:
    friend struct internal::FrameworkEventAccess;
    template<class> friend struct internal::EventSnapshotDelivery;
    template<class> friend struct internal::ExternalEventDelivery;
    template<class> friend struct internal::GuardedEventDelivery;
    // Internal delivery can notify from a snapshot after the Event's owner is gone.
    // Dispatch accesses only the owned entries and payload, never the Event.
    static bool notifySnapshot(ConstEntryListPtr snapshot, T& arg,
                               std::shared_ptr<internal::ExternalDeliveryStore> store = {},
                               std::weak_ptr<std::atomic<bool>> sourceAlive = {},
                               std::shared_ptr<std::atomic<bool>> run = {},
                               bool mainStoreFailed = false) {
        if (!snapshot) return true;
        struct OwnedCopy {
            std::shared_ptr<internal::ExternalDeliveryStore::Lease> lease;
            T payload;
            std::shared_ptr<const Entry> entry;
            // Process/framework callbacks historically get independent queue
            // copies. App-owned callbacks share their snapshot-owned target.
            std::optional<Callback> callback;
        };
        bool accepted = true;
        internal::EventDispatch dispatch;
        for (const auto& entry : *snapshot) {
            if (!entry.callback) continue;
            // Removed earlier in this pass (the snapshot still holds it).
            // One acquire load per entry, no lock.
            if (entry.alive && !entry.alive->load(std::memory_order_acquire)) continue;

            // Deliver::Main from a worker thread: copy the payload and run the
            // listener on the main thread next frame. Already-main (or Inline)
            // takes the hot path below unchanged.
            if (entry.deliver == Deliver::Main && !dispatch.mainThread()) {
                if constexpr (std::is_copy_constructible_v<T>) {
                    if (mainStoreFailed) { accepted = false; continue; }
                    try {
                        std::shared_ptr<internal::ExternalDeliveryStore::Lease> lease;
                        if (store) {
                            // Reserve before either copy, using the value after
                            // earlier Inline listeners have finished editing it.
                            lease = store->reserve();
                            if (!lease) { accepted = false; continue; }
                        }
                        internal::AppOwnerScope workerOwner(entry.owner);
                        auto copy = std::make_shared<OwnedCopy>(std::move(lease), arg, entry.shared_from_this(),
                            entry.owner ? std::nullopt : std::optional<Callback>(*entry.callback));
                        internal::AppOwnerScope queueOwner({});
                        runOnMainThread([copy, run, sourceAlive]() mutable {
                            if (internal::eventSourceIsAlive(sourceAlive) && copy->entry->alive->load(std::memory_order_acquire) &&
                                (!run || run->load(std::memory_order_acquire))) {
                                internal::EventDispatch queued;
                                queued.capture();
                                queued.invokeQueued(*copy->entry, [&] {
                                    auto& callback = copy->callback ? *copy->callback : *copy->entry->callback;
                                    callback(copy->payload);
                                });
                            }
                        }, std::weak_ptr<std::atomic<bool>>(entry.alive));
                    } catch (const std::bad_alloc&) {
                        if (!store) throw;
                        accepted = false;
                    }
                    continue;
                }
                // Non-copyable payload can't be marshalled — fall through and
                // run inline (documented limitation of Deliver::Main).
            }

            dispatch.invoke(entry, [&] { (*entry.callback)(arg); });
            // Stop propagation once a listener marks the event consumed.
            // Only arg types that carry a `consumed` flag (input events)
            // participate; all others ignore this branch at compile time.
            // (Marshalled Main listeners run on a copy and never reach here,
            // so they don't take part in consume — input events fire on the
            // main thread anyway, where Main is inline.)
            if constexpr (requires { arg.consumed; }) {
                if (arg.consumed) break;
            }
        }
        return accepted;
    }

public:
    // Get listener count
    size_t listenerCount() const {
        ConstEntryListPtr snapshot = internal::sharedLoad(entries_);
        return snapshot ? std::count_if(snapshot->begin(), snapshot->end(), [](const Entry& e) {
            return !e.alive || e.alive->load(std::memory_order_acquire);
        }) : 0;
    }

    // Remove user listeners, preserving framework-owned listeners and their live tokens.
    void clear() {
        // User captures in the old snapshot may reenter a network owner.
        // Release them (`cur`) only after releasing the listener-list mutex.
        ConstEntryListPtr cur;
        {
            TC_LOCK_GUARD(mutex_);
            cur = internal::sharedLoad(entries_);
            if (!cur) return;
            // Cancel current/retained snapshots and queued Main deliveries.
            // The protected snapshot was prepared during listen/remove; copying
            // its shared_ptr cannot allocate. Release discarded captures outside
            // the mutex, just like an ordinary clear with no survivors.
            for (const auto& e : *cur) {
                if (!e.frameworkOwned && e.alive) e.alive->store(false, std::memory_order_release);
            }
            internal::sharedStore(entries_, frameworkEntries_);
        }
    }

private:
    void removeListener(const Entry& entry) {
        ConstEntryListPtr cur, previousFramework;
        {
            TC_LOCK_GUARD(mutex_);
            cur = internal::sharedLoad(entries_);
            previousFramework = frameworkEntries_;
            entry.alive->store(false, std::memory_order_release);
            try {
                auto next = needsSweep_ ? EntryList::sweep(cur) : EntryList::erase(cur, entry);
                auto framework = needsSweep_ ? EntryList::sweep(frameworkEntries_)
                    : (entry.frameworkOwned ? EntryList::erase(frameworkEntries_, entry) : frameworkEntries_);
                frameworkEntries_ = std::move(framework);
                needsSweep_ = false;
                internal::sharedStore(entries_, std::move(next));
            } catch (...) {
                // The flag has canceled old snapshots and Main work already.
                // A later successful mutation sweeps this tombstone.
                needsSweep_ = true;
                throw;
            }
        }
    }

    // Liveness only, not a teardown barrier or publication of other state.
    std::shared_ptr<std::atomic<bool>> alive_;
    const int defaultPriority_;
    mutable TC_MUTEX mutex_;            // serializes listen / remove / clear
    internal::AtomicSharedPtr<const EntryList> entries_;  // persistent snapshot: sharedLoad (acquire) / sharedStore (release)
    ConstEntryListPtr frameworkEntries_; // guarded by mutex_; prepared clear() survivors
    uint64_t nextId_ = 0;
    bool needsSweep_ = false;
    // Created only when an external source first marshals to Main.
    std::shared_ptr<internal::ExternalDeliveryStore> externalStore_;
};

// ---------------------------------------------------------------------------
// Event<void> - Specialization for events without arguments
// ---------------------------------------------------------------------------
template<>
class Event<void> {
public:
    // Callback type (no arguments)
    using Callback = std::function<void()>;

    Event() : Event(EventPriority::App) {}
    explicit Event(int defaultPriority)
        : alive_(std::make_shared<std::atomic<bool>>(true)), defaultPriority_(defaultPriority) {}
    ~Event() { alive_->store(false, std::memory_order_relaxed); }

    // Optional count limit for external Main copies across this event's listeners.
    // Default/0: unlimited. Existing copies survive a lower limit. Overflow
    // drops new copies and logs one Warning per episode; allocation failure drops silently.
    void setMainThreadDeliveryLimit(size_t count) {
        TC_LOCK_GUARD(mutex_);
        if (!externalStore_) {
            externalStore_ = std::make_shared<internal::ExternalDeliveryStore>();
        }
        std::lock_guard<std::mutex> storeLock(externalStore_->mutex);
        externalStore_->deliveryLimit = count;
        if (!count || externalStore_->pending < count) externalStore_->overflowing = false;
    }

    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;
    Event(Event&&) = delete;
    Event& operator=(Event&&) = delete;

    // Omitted priority uses this event instance's default; every integer is explicit.
    [[nodiscard]] EventListener listen(Callback callback) {
        return listen(std::move(callback), defaultPriority_);
    }
    [[nodiscard]] EventListener listen(Callback callback, Deliver deliver) {
        return listen(std::move(callback), deliver, defaultPriority_);
    }
    template<typename Obj>
    [[nodiscard]] EventListener listen(Obj* obj, void (Obj::*method)()) {
        return listen(obj, method, defaultPriority_);
    }
    template<typename Obj>
    [[nodiscard]] EventListener listen(Obj* obj, void (Obj::*method)(), Deliver deliver) {
        return listen(obj, method, deliver, defaultPriority_);
    }
    [[deprecated("Use 'listener = event.listen(callback)' instead")]]
    void listen(EventListener& listener, Callback callback) {
        listenImpl(listener, std::move(callback), Deliver::Inline, defaultPriority_);
    }
    template<typename Obj>
    [[deprecated("Use 'listener = event.listen(obj, &Class::method)' instead")]]
    void listen(EventListener& listener, Obj* obj, void (Obj::*method)()) {
        listenImpl(listener, [obj, method]() { (obj->*method)(); },
                   Deliver::Inline, defaultPriority_);
    }

    // Register listener with lambda (returns EventListener) - preferred API
    [[nodiscard]] EventListener listen(Callback callback,
                                       int priority) {
        EventListener listener;
        listenImpl(listener, std::move(callback), Deliver::Inline, priority);
        return listener;
    }

    // Same, but declare the delivery thread (Deliver::Main runs on the main thread).
    [[nodiscard]] EventListener listen(Callback callback, Deliver deliver,
                                       int priority) {
        EventListener listener;
        listenImpl(listener, std::move(callback), deliver, priority);
        return listener;
    }

    // Register listener with member function (returns EventListener) - preferred API
    template<typename Obj>
    [[nodiscard]] EventListener listen(Obj* obj, void (Obj::*method)(),
                                       int priority) {
        return listen([obj, method]() {
            (obj->*method)();
        }, priority);
    }

    // Same, with explicit delivery thread.
    template<typename Obj>
    [[nodiscard]] EventListener listen(Obj* obj, void (Obj::*method)(),
                                       Deliver deliver,
                                       int priority) {
        return listen([obj, method]() {
            (obj->*method)();
        }, deliver, priority);
    }

    // Deprecated: use `listener = event.listen(callback)` instead
    [[deprecated("Use 'listener = event.listen(callback)' instead")]]
    void listen(EventListener& listener, Callback callback,
                int priority) {
        listenImpl(listener, std::move(callback), Deliver::Inline, priority);
    }

    // Deprecated: use `listener = event.listen(obj, &Class::method)` instead
    template<typename Obj>
    [[deprecated("Use 'listener = event.listen(obj, &Class::method)' instead")]]
    void listen(EventListener& listener, Obj* obj, void (Obj::*method)(),
                int priority) {
        listenImpl(listener, [obj, method]() {
            (obj->*method)();
        }, Deliver::Inline, priority);
    }

private:
    struct Entry : std::enable_shared_from_this<Entry> {
        uint64_t id = 0;
        int priority;
        Deliver deliver;
        bool frameworkOwned;
        std::optional<Callback> callback;
        std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);
        std::shared_ptr<internal::AppOwner> owner;
        std::shared_ptr<internal::AppListenerCancellation> cancellation;
        Entry(int priority, Deliver deliver, bool frameworkOwned, Callback callback,
              std::shared_ptr<internal::AppOwner> owner)
            : priority(priority), deliver(deliver), frameworkOwned(frameworkOwned),
              callback(callback ? std::optional<Callback>(std::move(callback)) : std::nullopt), owner(std::move(owner)) {}
        ~Entry() { internal::unregisterAppListener(cancellation); }
    };
    using EntryList = internal::EventTree<Entry>;
    using ConstEntryListPtr = typename EntryList::Tree;

    void listenImpl(EventListener& listener, Callback callback,
                    Deliver deliver, int priority, bool frameworkOwned = false) {
        if (!alive_->load(std::memory_order_acquire)) return;
        auto owner = frameworkOwned ? std::shared_ptr<internal::AppOwner>{} : internal::currentAppOwner();
        if (owner && (owner->retiring || owner->ended || owner->callbacksClosing || !owner->token->load())) return;
        auto entry = std::make_shared<Entry>(priority, deliver, frameworkOwned, std::move(callback), owner);
        // Reserve, register with the App, publish only if still alive. App and
        // Event locks are never nested, even if closing races registration.
        {
            TC_LOCK_GUARD(mutex_);
            if (nextId_ == (std::numeric_limits<uint64_t>::max)()) throw std::overflow_error("Event listener identities exhausted");
            entry->id = nextId_++;
        }
        std::weak_ptr<std::atomic<bool>> weak = alive_;
        EventListener::DisconnectFunc disconnect = [this, weak, slot = std::weak_ptr(entry)] {
            auto source = weak.lock();
            if (auto live = slot.lock()) {
                live->alive->store(false, std::memory_order_release);
                if (source && source->load(std::memory_order_acquire)) removeListener(*live);
            }
        };
        if (owner) entry->cancellation = internal::registerAppListener(owner, entry->alive,
            internal::isAppExitEvent(this), disconnect);
        ConstEntryListPtr cur, previousFramework;
        {
            TC_LOCK_GUARD(mutex_);
            if (!entry->alive->load(std::memory_order_acquire) || !alive_->load(std::memory_order_acquire)) return;
            cur = internal::sharedLoad(entries_);
            previousFramework = frameworkEntries_;
            auto next = EntryList::insert(needsSweep_ ? EntryList::sweep(cur) : cur, entry);
            auto framework = needsSweep_ ? EntryList::sweep(frameworkEntries_) : frameworkEntries_;
            if (frameworkOwned) framework = EntryList::insert(framework, entry);
            frameworkEntries_ = std::move(framework);
            needsSweep_ = false;
            internal::sharedStore(entries_, std::move(next));
        }
        listener = EventListener(std::move(disconnect));
    }

public:
    // Fire event. Hot path: a single atomic load, no allocation, no mutex.
    // Changes to the listener list during a pass: see Event<T>::notify().
    void notify() {
        notifySnapshot(internal::sharedLoad(entries_), {}, alive_);
    }

private:
    friend struct internal::FrameworkEventAccess;
    template<class> friend struct internal::EventSnapshotDelivery;
    template<class> friend struct internal::ExternalEventDelivery;
    template<class> friend struct internal::GuardedEventDelivery;
    static bool notifySnapshot(ConstEntryListPtr snapshot,
                               std::shared_ptr<internal::ExternalDeliveryStore> store = {},
                               std::weak_ptr<std::atomic<bool>> sourceAlive = {},
                               bool mainStoreFailed = false) {
        if (!snapshot) return true;
        struct OwnedCall {
            std::shared_ptr<internal::ExternalDeliveryStore::Lease> lease;
            std::shared_ptr<const Entry> entry; // Release the callable before the pending slot.
            std::optional<Callback> callback;
        };
        bool accepted = true;
        internal::EventDispatch dispatch;
        for (const auto& entry : *snapshot) {
            if (!entry.callback) continue;
            // Removed earlier in this pass (the snapshot still holds it).
            if (entry.alive && !entry.alive->load(std::memory_order_acquire)) continue;
            // Deliver::Main from a worker thread: run on the main thread next
            // frame (no payload to copy for Event<void>).
            if (entry.deliver == Deliver::Main && !dispatch.mainThread()) {
                if (mainStoreFailed) { accepted = false; continue; }
                try {
                    std::shared_ptr<internal::ExternalDeliveryStore::Lease> lease;
                    if (store) {
                        lease = store->reserve();
                        if (!lease) { accepted = false; continue; }
                    }
                    internal::AppOwnerScope workerOwner(entry.owner);
                    auto call = std::make_shared<OwnedCall>(std::move(lease), entry.shared_from_this(),
                        entry.owner ? std::nullopt : std::optional<Callback>(*entry.callback));
                    internal::AppOwnerScope queueOwner({});
                    runOnMainThread([call, sourceAlive] {
                        if (internal::eventSourceIsAlive(sourceAlive) && call->entry->alive->load(std::memory_order_acquire)) {
                            internal::EventDispatch queued;
                            queued.capture();
                            queued.invokeQueued(*call->entry, [&] {
                                auto& callback = call->callback ? *call->callback : *call->entry->callback;
                                callback();
                            });
                        }
                    }, std::weak_ptr<std::atomic<bool>>(entry.alive));
                } catch (const std::bad_alloc&) {
                    if (!store) throw;
                    accepted = false;
                }
                continue;
            }
            {
                dispatch.invoke(entry, [&] { (*entry.callback)(); });
            }
        }
        return accepted;
    }

public:
    size_t listenerCount() const {
        ConstEntryListPtr snapshot = internal::sharedLoad(entries_);
        return snapshot ? std::count_if(snapshot->begin(), snapshot->end(), [](const Entry& e) {
            return !e.alive || e.alive->load(std::memory_order_acquire);
        }) : 0;
    }

    void clear() {
        // User captures in the old snapshot may reenter a network owner.
        // Release them (`cur`) only after releasing the listener-list mutex.
        ConstEntryListPtr cur;
        {
            TC_LOCK_GUARD(mutex_);
            cur = internal::sharedLoad(entries_);
            if (!cur) return;
            // Cancel current/retained snapshots and queued Main deliveries.
            // The protected snapshot was prepared during listen/remove; copying
            // its shared_ptr cannot allocate. Release discarded captures outside
            // the mutex, just like an ordinary clear with no survivors.
            for (const auto& e : *cur) {
                if (!e.frameworkOwned && e.alive) e.alive->store(false, std::memory_order_release);
            }
            internal::sharedStore(entries_, frameworkEntries_);
        }
    }

private:
    void removeListener(const Entry& entry) {
        ConstEntryListPtr cur, previousFramework;
        {
            TC_LOCK_GUARD(mutex_);
            cur = internal::sharedLoad(entries_);
            previousFramework = frameworkEntries_;
            entry.alive->store(false, std::memory_order_release);
            try {
                auto next = needsSweep_ ? EntryList::sweep(cur) : EntryList::erase(cur, entry);
                auto framework = needsSweep_ ? EntryList::sweep(frameworkEntries_)
                    : (entry.frameworkOwned ? EntryList::erase(frameworkEntries_, entry) : frameworkEntries_);
                frameworkEntries_ = std::move(framework);
                needsSweep_ = false;
                internal::sharedStore(entries_, std::move(next));
            } catch (...) {
                // The flag has canceled old snapshots and Main work already.
                // A later successful mutation sweeps this tombstone.
                needsSweep_ = true;
                throw;
            }
        }
    }

    // Liveness only, not a teardown barrier or publication of other state.
    std::shared_ptr<std::atomic<bool>> alive_;
    const int defaultPriority_;
    mutable TC_MUTEX mutex_;            // serializes listen / remove / clear
    internal::AtomicSharedPtr<const EntryList> entries_;  // persistent snapshot: sharedLoad (acquire) / sharedStore (release)
    ConstEntryListPtr frameworkEntries_; // guarded by mutex_; prepared clear() survivors
    uint64_t nextId_ = 0;
    bool needsSweep_ = false;
    // Created only when an external source first marshals to Main.
    std::shared_ptr<internal::ExternalDeliveryStore> externalStore_;
};

} // namespace trussc

// Internal registration only: the returned handle is the framework's teardown path.
namespace trussc::internal {
struct FrameworkEventAccess {
    template<class T> static auto snapshot(Event<T>& event) {
        return sharedLoad(event.entries_);
    }
    // Framework-only synchronous delivery. Keep generic mutable notify intact.
    // The caller owns exception containment and the per-listener payload policy.
    template<class Snapshot, class Invoke>
    static void forEachOwned(const Snapshot& snapshot, Invoke invoke) {
        if (!snapshot) return;
        EventDispatch dispatch;
        for (const auto& entry : *snapshot) {
            if (!entry.callback || (entry.alive && !entry.alive->load(std::memory_order_acquire))) continue;
            auto enter = [&](auto&& work) { dispatch.invoke(entry, std::forward<decltype(work)>(work)); };
            invoke(*entry.callback, enter);
        }
    }
    template<class T> static void disableSubscriptions(Event<T>& event) noexcept {
        event.alive_->store(false, std::memory_order_release);
    }
    // Terminal main-thread delivery. A failed listener must not skip App exit
    // or a later protected AfterApp hook. Keep the original snapshot and live
    // tokens, so clear/removal during shutdown retains its usual semantics.
    static void notifyShutdown(Event<void>& event, TeardownSteps& steps) noexcept {
        auto snapshot = sharedLoad(event.entries_);
        if (!snapshot) return;
        EventDispatch dispatch;
        for (const auto& entry : *snapshot) {
            if (!entry.callback || (entry.alive && !entry.alive->load(std::memory_order_acquire))) continue;
            steps.run("exit listener", [&] { dispatch.invoke(entry, [&] { (*entry.callback)(); }); });
        }
    }
    template<class T>
    static EventListener listen(Event<T>& event, typename Event<T>::Callback callback,
                                int priority, Deliver deliver = Deliver::Inline) {
        EventListener listener;
        event.listenImpl(listener, std::move(callback), deliver, priority, true);
        return listener;
    }
};
} // namespace trussc::internal

// Synchronous internal delivery for borrowed payloads. Own the snapshot before
// entering user code; prepare reconstructs/cancels the payload before every
// listener and after each return. Never queue a borrowed payload to Main.
namespace trussc::internal {
template<class T> struct GuardedEventDelivery {
    template<class Prepare>
    static void notify(Event<T>& source, T& args, Prepare prepare) {
        auto snapshot = sharedLoad(source.entries_);
        if (!snapshot) return;
        EventDispatch dispatch;
        for (const auto& entry : *snapshot) {
            if (!entry.callback || (entry.alive && !entry.alive->load(std::memory_order_acquire))) continue;
            if (!prepare()) break;
            dispatch.invoke(entry, [&] { (*entry.callback)(args); });
            if (!prepare()) break;
            if constexpr (requires { args.consumed; }) {
                if (args.consumed) break;
            }
        }
    }
};
} // namespace trussc::internal

// Internal opt-in: application-authored Event::notify remains growable. A
// snapshot plus its store is acquired before entering user code; after that
// dispatch never accesses the Event, even if a listener destroys its owner.
namespace trussc::internal {
template<class T> struct ExternalEventDelivery {
    static std::shared_ptr<ExternalDeliveryStore> diagnostics(Event<T>& source) {
        TC_LOCK_GUARD(source.mutex_);
        return source.externalStore_;
    }
    static std::shared_ptr<ExternalDeliveryStore> store(Event<T>& source,
                                                       const typename Event<T>::ConstEntryListPtr& snapshot,
                                                       bool& failed) {
        if (isMainThread() || !snapshot) return {};
        bool needed = false;
        for (const auto& entry : *snapshot)
            if (entry.deliver == Deliver::Main && entry.callback && entry.alive->load()) { needed = true; break; }
        if (!needed) return {};
        TC_LOCK_GUARD(source.mutex_);
        if (!source.externalStore_) {
            try {
                source.externalStore_ = std::make_shared<ExternalDeliveryStore>();
            } catch (const std::bad_alloc&) {
                failed = true;
                return {};
            }
        }
        return source.externalStore_;
    }
    template<class... Args>
    static bool notify(Event<T>& source, Args&... args) {
        auto snapshot = sharedLoad(source.entries_);
        bool failed = false;
        auto ownedStore = store(source, snapshot, failed);
        if constexpr (std::is_void_v<T>)
            return Event<T>::notifySnapshot(std::move(snapshot), std::move(ownedStore), source.alive_, failed);
        else
            return Event<T>::notifySnapshot(std::move(snapshot), args..., std::move(ownedStore), source.alive_, {}, failed);
    }
};
} // namespace trussc::internal
