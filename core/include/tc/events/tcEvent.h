#pragma once

// =============================================================================
// tcEvent - Generic event class
// =============================================================================

#include <functional>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <type_traits>
#include "tcEventListener.h"
#include "../utils/tcMainThread.h"  // runOnMainThread (for Deliver::Main)

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
//                      behaviour. Hot path, no allocation.
//   Main             : run on the main thread. If notify() is already on the
//                      main thread it runs immediately; otherwise the payload
//                      is copied and the call is marshalled via runOnMainThread
//                      (runs at the start of the next frame).
//
// Notes for Deliver::Main:
//   - The payload T is COPIED (the original `arg` reference is gone by next
//     frame), so the listener cannot write back to `arg` and does not
//     participate in `consumed` propagation. Fine for reactive events
//     (onReceive); input events that use `consumed` already fire on the main
//     thread, where Main is a no-op anyway.
//   - Requires T to be copy-constructible (enforced by static_assert).
//
// Scoped enum on purpose: it does NOT convert to int, so listen(fn, Deliver)
// and the existing listen(fn, int priority) overloads never collide.
enum class Deliver { Inline, Main };

// ---------------------------------------------------------------------------
// Event<T> - Event with arguments
// ---------------------------------------------------------------------------
template<typename T>
class Event {
public:
    // Callback type (argument passed by reference - can be modified)
    using Callback = std::function<void(T&)>;

    Event() : alive_(std::make_shared<bool>(true)) {}
    ~Event() { *alive_ = false; }

    // Copy/Move forbidden (events have fixed location)
    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;
    Event(Event&&) = delete;
    Event& operator=(Event&&) = delete;

    // Register listener with lambda (returns EventListener) - preferred API
    [[nodiscard]] EventListener listen(Callback callback,
                                       int priority = EventPriority::App) {
        EventListener listener;
        listenImpl(listener, std::move(callback), Deliver::Inline, priority);
        return listener;
    }

    // Same, but declare the delivery thread. listen(fn, Deliver::Main) runs the
    // listener on the main thread regardless of which thread fires notify().
    [[nodiscard]] EventListener listen(Callback callback, Deliver deliver,
                                       int priority = EventPriority::App) {
        EventListener listener;
        listenImpl(listener, std::move(callback), deliver, priority);
        return listener;
    }

    // Register listener with member function (returns EventListener) - preferred API
    template<typename Obj>
    [[nodiscard]] EventListener listen(Obj* obj, void (Obj::*method)(T&),
                                       int priority = EventPriority::App) {
        return listen([obj, method](T& arg) {
            (obj->*method)(arg);
        }, priority);
    }

    // Same, with explicit delivery thread.
    template<typename Obj>
    [[nodiscard]] EventListener listen(Obj* obj, void (Obj::*method)(T&),
                                       Deliver deliver,
                                       int priority = EventPriority::App) {
        return listen([obj, method](T& arg) {
            (obj->*method)(arg);
        }, deliver, priority);
    }

    // Deprecated: use `listener = event.listen(callback)` instead
    [[deprecated("Use 'listener = event.listen(callback)' instead")]]
    void listen(EventListener& listener, Callback callback,
                int priority = EventPriority::App) {
        listenImpl(listener, std::move(callback), Deliver::Inline, priority);
    }

    // Deprecated: use `listener = event.listen(obj, &Class::method)` instead
    template<typename Obj>
    [[deprecated("Use 'listener = event.listen(obj, &Class::method)' instead")]]
    void listen(EventListener& listener, Obj* obj, void (Obj::*method)(T&),
                int priority = EventPriority::App) {
        listenImpl(listener, [obj, method](T& arg) {
            (obj->*method)(arg);
        }, Deliver::Inline, priority);
    }

private:
    struct Entry {
        uint64_t id;
        int priority;
        Deliver deliver;
        Callback callback;
    };

    using EntryList = std::vector<Entry>;
    using ConstEntryListPtr = std::shared_ptr<const EntryList>;

    void listenImpl(EventListener& listener, Callback callback,
                    Deliver deliver, int priority) {
        uint64_t id;
        {
            TC_LOCK_GUARD(mutex_);
            id = nextId_++;
            ConstEntryListPtr cur = std::atomic_load(&entries_);
            auto next = cur ? std::make_shared<EntryList>(*cur)
                            : std::make_shared<EntryList>();
            next->push_back({id, priority, deliver, std::move(callback)});
            std::stable_sort(next->begin(), next->end(),
                [](const Entry& a, const Entry& b) {
                    return a.priority < b.priority;
                });
            std::atomic_store(&entries_, ConstEntryListPtr(next));
        }
        // Set EventListener outside lock (removeListener() may be called when disconnecting existing)
        // Capture weak_ptr to check if Event is still alive before removing
        std::weak_ptr<bool> weak = alive_;
        listener = EventListener([this, id, weak]() {
            if (auto alive = weak.lock()) {
                if (*alive) {
                    this->removeListener(id);
                }
            }
        });
    }

public:

    // Fire event. Hot path: a single atomic_load (no allocation, no mutex)
    // — safe to call from the audio thread.
    void notify(T& arg) {
        ConstEntryListPtr snapshot = std::atomic_load(&entries_);
        if (!snapshot) return;
        for (const auto& entry : *snapshot) {
            if (!entry.callback) continue;

            // Deliver::Main from a worker thread: copy the payload and run the
            // listener on the main thread next frame. Already-main (or Inline)
            // takes the hot path below unchanged.
            if (entry.deliver == Deliver::Main && !isMainThread()) {
                if constexpr (std::is_copy_constructible_v<T>) {
                    T copy = arg;
                    runOnMainThread([cb = entry.callback, copy]() mutable {
                        cb(copy);
                    });
                    continue;
                }
                // Non-copyable payload can't be marshalled — fall through and
                // run inline (documented limitation of Deliver::Main).
            }

            entry.callback(arg);
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
    }

    // Get listener count
    size_t listenerCount() const {
        ConstEntryListPtr snapshot = std::atomic_load(&entries_);
        return snapshot ? snapshot->size() : 0;
    }

    // Remove all listeners
    void clear() {
        TC_LOCK_GUARD(mutex_);
        std::atomic_store(&entries_, ConstEntryListPtr{});
    }

private:
    void removeListener(uint64_t id) {
        TC_LOCK_GUARD(mutex_);
        ConstEntryListPtr cur = std::atomic_load(&entries_);
        if (!cur) return;
        auto next = std::make_shared<EntryList>(*cur);
        next->erase(
            std::remove_if(next->begin(), next->end(),
                [id](const Entry& e) { return e.id == id; }),
            next->end()
        );
        std::atomic_store(&entries_, ConstEntryListPtr(next));
    }

    std::shared_ptr<bool> alive_;
    mutable TC_MUTEX mutex_;            // serializes listen / remove / clear
    ConstEntryListPtr entries_;          // RCU snapshot, atomic_load/store
    uint64_t nextId_ = 0;
};

// ---------------------------------------------------------------------------
// Event<void> - Specialization for events without arguments
// ---------------------------------------------------------------------------
template<>
class Event<void> {
public:
    // Callback type (no arguments)
    using Callback = std::function<void()>;

    Event() : alive_(std::make_shared<bool>(true)) {}
    ~Event() { *alive_ = false; }

    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;
    Event(Event&&) = delete;
    Event& operator=(Event&&) = delete;

    // Register listener with lambda (returns EventListener) - preferred API
    [[nodiscard]] EventListener listen(Callback callback,
                                       int priority = EventPriority::App) {
        EventListener listener;
        listenImpl(listener, std::move(callback), Deliver::Inline, priority);
        return listener;
    }

    // Same, but declare the delivery thread (Deliver::Main runs on the main thread).
    [[nodiscard]] EventListener listen(Callback callback, Deliver deliver,
                                       int priority = EventPriority::App) {
        EventListener listener;
        listenImpl(listener, std::move(callback), deliver, priority);
        return listener;
    }

    // Register listener with member function (returns EventListener) - preferred API
    template<typename Obj>
    [[nodiscard]] EventListener listen(Obj* obj, void (Obj::*method)(),
                                       int priority = EventPriority::App) {
        return listen([obj, method]() {
            (obj->*method)();
        }, priority);
    }

    // Same, with explicit delivery thread.
    template<typename Obj>
    [[nodiscard]] EventListener listen(Obj* obj, void (Obj::*method)(),
                                       Deliver deliver,
                                       int priority = EventPriority::App) {
        return listen([obj, method]() {
            (obj->*method)();
        }, deliver, priority);
    }

    // Deprecated: use `listener = event.listen(callback)` instead
    [[deprecated("Use 'listener = event.listen(callback)' instead")]]
    void listen(EventListener& listener, Callback callback,
                int priority = EventPriority::App) {
        listenImpl(listener, std::move(callback), Deliver::Inline, priority);
    }

    // Deprecated: use `listener = event.listen(obj, &Class::method)` instead
    template<typename Obj>
    [[deprecated("Use 'listener = event.listen(obj, &Class::method)' instead")]]
    void listen(EventListener& listener, Obj* obj, void (Obj::*method)(),
                int priority = EventPriority::App) {
        listenImpl(listener, [obj, method]() {
            (obj->*method)();
        }, Deliver::Inline, priority);
    }

private:
    struct Entry {
        uint64_t id;
        int priority;
        Deliver deliver;
        Callback callback;
    };

    using EntryList = std::vector<Entry>;
    using ConstEntryListPtr = std::shared_ptr<const EntryList>;

    void listenImpl(EventListener& listener, Callback callback,
                    Deliver deliver, int priority) {
        uint64_t id;
        {
            TC_LOCK_GUARD(mutex_);
            id = nextId_++;
            ConstEntryListPtr cur = std::atomic_load(&entries_);
            auto next = cur ? std::make_shared<EntryList>(*cur)
                            : std::make_shared<EntryList>();
            next->push_back({id, priority, deliver, std::move(callback)});
            std::stable_sort(next->begin(), next->end(),
                [](const Entry& a, const Entry& b) {
                    return a.priority < b.priority;
                });
            std::atomic_store(&entries_, ConstEntryListPtr(next));
        }
        // Set EventListener outside lock (removeListener() may be called when disconnecting existing)
        // Capture weak_ptr to check if Event is still alive before removing
        std::weak_ptr<bool> weak = alive_;
        listener = EventListener([this, id, weak]() {
            if (auto alive = weak.lock()) {
                if (*alive) {
                    this->removeListener(id);
                }
            }
        });
    }

public:
    // Fire event. Hot path: a single atomic_load, no allocation, no mutex.
    void notify() {
        ConstEntryListPtr snapshot = std::atomic_load(&entries_);
        if (!snapshot) return;
        for (const auto& entry : *snapshot) {
            if (!entry.callback) continue;
            // Deliver::Main from a worker thread: run on the main thread next
            // frame (no payload to copy for Event<void>).
            if (entry.deliver == Deliver::Main && !isMainThread()) {
                runOnMainThread(entry.callback);
                continue;
            }
            {
                entry.callback();
            }
        }
    }

    size_t listenerCount() const {
        ConstEntryListPtr snapshot = std::atomic_load(&entries_);
        return snapshot ? snapshot->size() : 0;
    }

    void clear() {
        TC_LOCK_GUARD(mutex_);
        std::atomic_store(&entries_, ConstEntryListPtr{});
    }

private:
    void removeListener(uint64_t id) {
        TC_LOCK_GUARD(mutex_);
        ConstEntryListPtr cur = std::atomic_load(&entries_);
        if (!cur) return;
        auto next = std::make_shared<EntryList>(*cur);
        next->erase(
            std::remove_if(next->begin(), next->end(),
                [id](const Entry& e) { return e.id == id; }),
            next->end()
        );
        std::atomic_store(&entries_, ConstEntryListPtr(next));
    }

    std::shared_ptr<bool> alive_;
    mutable TC_MUTEX mutex_;            // serializes listen / remove / clear
    ConstEntryListPtr entries_;          // RCU snapshot, atomic_load/store
    uint64_t nextId_ = 0;
};

} // namespace trussc
