#pragma once

// =============================================================================
// tcEvent - Generic event class
// =============================================================================

#include <functional>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <memory>
#include "tcEventListener.h"

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
enum class EventPriority : int {
    BeforeApp = 0,    // Process before app
    App = 100,        // Normal app processing (default)
    AfterApp = 200    // Process after app
};

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
                                       EventPriority priority = EventPriority::App) {
        EventListener listener;
        listenImpl(listener, std::move(callback), priority);
        return listener;
    }

    // Register listener with member function (returns EventListener) - preferred API
    template<typename Obj>
    [[nodiscard]] EventListener listen(Obj* obj, void (Obj::*method)(T&),
                                       EventPriority priority = EventPriority::App) {
        return listen([obj, method](T& arg) {
            (obj->*method)(arg);
        }, priority);
    }

    // Deprecated: use `listener = event.listen(callback)` instead
    [[deprecated("Use 'listener = event.listen(callback)' instead")]]
    void listen(EventListener& listener, Callback callback,
                EventPriority priority = EventPriority::App) {
        listenImpl(listener, std::move(callback), priority);
    }

    // Deprecated: use `listener = event.listen(obj, &Class::method)` instead
    template<typename Obj>
    [[deprecated("Use 'listener = event.listen(obj, &Class::method)' instead")]]
    void listen(EventListener& listener, Obj* obj, void (Obj::*method)(T&),
                EventPriority priority = EventPriority::App) {
        listenImpl(listener, [obj, method](T& arg) {
            (obj->*method)(arg);
        }, priority);
    }

private:
    void listenImpl(EventListener& listener, Callback callback,
                    EventPriority priority) {
        uint64_t id;
        {
            TC_LOCK_GUARD(mutex_);
            id = nextId_++;
            entries_.push_back({id, static_cast<int>(priority), std::move(callback)});
            sortEntries();
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

    // Fire event
    void notify(T& arg) {
        std::vector<Entry> entriesCopy;
        {
            TC_LOCK_GUARD(mutex_);
            entriesCopy = entries_;
        }
        // Execute outside lock (prevent deadlock)
        for (auto& entry : entriesCopy) {
            if (entry.callback) {
                entry.callback(arg);
            }
        }
    }

    // Get listener count
    size_t listenerCount() const {
        TC_LOCK_GUARD(mutex_);
        return entries_.size();
    }

    // Remove all listeners
    void clear() {
        TC_LOCK_GUARD(mutex_);
        entries_.clear();
    }

private:
    struct Entry {
        uint64_t id;
        int priority;
        Callback callback;
    };

    void removeListener(uint64_t id) {
        TC_LOCK_GUARD(mutex_);
        entries_.erase(
            std::remove_if(entries_.begin(), entries_.end(),
                [id](const Entry& e) { return e.id == id; }),
            entries_.end()
        );
    }

    void sortEntries() {
        std::stable_sort(entries_.begin(), entries_.end(),
            [](const Entry& a, const Entry& b) {
                return a.priority < b.priority;
            });
    }

    std::shared_ptr<bool> alive_;
    mutable TC_MUTEX mutex_;
    std::vector<Entry> entries_;
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
                                       EventPriority priority = EventPriority::App) {
        EventListener listener;
        listenImpl(listener, std::move(callback), priority);
        return listener;
    }

    // Register listener with member function (returns EventListener) - preferred API
    template<typename Obj>
    [[nodiscard]] EventListener listen(Obj* obj, void (Obj::*method)(),
                                       EventPriority priority = EventPriority::App) {
        return listen([obj, method]() {
            (obj->*method)();
        }, priority);
    }

    // Deprecated: use `listener = event.listen(callback)` instead
    [[deprecated("Use 'listener = event.listen(callback)' instead")]]
    void listen(EventListener& listener, Callback callback,
                EventPriority priority = EventPriority::App) {
        listenImpl(listener, std::move(callback), priority);
    }

    // Deprecated: use `listener = event.listen(obj, &Class::method)` instead
    template<typename Obj>
    [[deprecated("Use 'listener = event.listen(obj, &Class::method)' instead")]]
    void listen(EventListener& listener, Obj* obj, void (Obj::*method)(),
                EventPriority priority = EventPriority::App) {
        listenImpl(listener, [obj, method]() {
            (obj->*method)();
        }, priority);
    }

private:
    void listenImpl(EventListener& listener, Callback callback,
                    EventPriority priority) {
        uint64_t id;
        {
            TC_LOCK_GUARD(mutex_);
            id = nextId_++;
            entries_.push_back({id, static_cast<int>(priority), std::move(callback)});
            sortEntries();
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
    // Fire event
    void notify() {
        std::vector<Entry> entriesCopy;
        {
            TC_LOCK_GUARD(mutex_);
            entriesCopy = entries_;
        }
        for (auto& entry : entriesCopy) {
            if (entry.callback) {
                entry.callback();
            }
        }
    }

    size_t listenerCount() const {
        TC_LOCK_GUARD(mutex_);
        return entries_.size();
    }

    void clear() {
        TC_LOCK_GUARD(mutex_);
        entries_.clear();
    }

private:
    struct Entry {
        uint64_t id;
        int priority;
        Callback callback;
    };

    void removeListener(uint64_t id) {
        TC_LOCK_GUARD(mutex_);
        entries_.erase(
            std::remove_if(entries_.begin(), entries_.end(),
                [id](const Entry& e) { return e.id == id; }),
            entries_.end()
        );
    }

    void sortEntries() {
        std::stable_sort(entries_.begin(), entries_.end(),
            [](const Entry& a, const Entry& b) {
                return a.priority < b.priority;
            });
    }

    std::shared_ptr<bool> alive_;
    mutable TC_MUTEX mutex_;
    std::vector<Entry> entries_;
    uint64_t nextId_ = 0;
};

} // namespace trussc
