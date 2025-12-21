#pragma once

// =============================================================================
// tcEvent - Generic event class
// =============================================================================

#include <functional>
#include <vector>
#include <algorithm>
#include <mutex>
#include <cstdint>
#include "tcEventListener.h"

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

    Event() = default;
    ~Event() = default;

    // Copy/Move forbidden (events have fixed location)
    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;
    Event(Event&&) = delete;
    Event& operator=(Event&&) = delete;

    // Register listener with lambda (EventListener passed by reference)
    void listen(EventListener& listener, Callback callback,
                EventPriority priority = EventPriority::App) {
        uint64_t id;
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            id = nextId_++;
            entries_.push_back({id, static_cast<int>(priority), std::move(callback)});
            sortEntries();
        }
        // Set EventListener outside lock (removeListener() may be called when disconnecting existing)
        listener = EventListener([this, id]() {
            this->removeListener(id);
        });
    }

    // Register listener with member function
    template<typename Obj>
    void listen(EventListener& listener, Obj* obj, void (Obj::*method)(T&),
                EventPriority priority = EventPriority::App) {
        listen(listener, [obj, method](T& arg) {
            (obj->*method)(arg);
        }, priority);
    }

    // Fire event
    void notify(T& arg) {
        std::vector<Entry> entriesCopy;
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
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
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return entries_.size();
    }

    // Remove all listeners
    void clear() {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        entries_.clear();
    }

private:
    struct Entry {
        uint64_t id;
        int priority;
        Callback callback;
    };

    void removeListener(uint64_t id) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
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

    mutable std::recursive_mutex mutex_;
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

    Event() = default;
    ~Event() = default;

    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;
    Event(Event&&) = delete;
    Event& operator=(Event&&) = delete;

    // Register listener with lambda (EventListener passed by reference)
    void listen(EventListener& listener, Callback callback,
                EventPriority priority = EventPriority::App) {
        uint64_t id;
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            id = nextId_++;
            entries_.push_back({id, static_cast<int>(priority), std::move(callback)});
            sortEntries();
        }
        // Set EventListener outside lock (removeListener() may be called when disconnecting existing)
        listener = EventListener([this, id]() {
            this->removeListener(id);
        });
    }

    // Register listener with member function
    template<typename Obj>
    void listen(EventListener& listener, Obj* obj, void (Obj::*method)(),
                EventPriority priority = EventPriority::App) {
        listen(listener, [obj, method]() {
            (obj->*method)();
        }, priority);
    }

    // Fire event
    void notify() {
        std::vector<Entry> entriesCopy;
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            entriesCopy = entries_;
        }
        for (auto& entry : entriesCopy) {
            if (entry.callback) {
                entry.callback();
            }
        }
    }

    size_t listenerCount() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return entries_.size();
    }

    void clear() {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        entries_.clear();
    }

private:
    struct Entry {
        uint64_t id;
        int priority;
        Callback callback;
    };

    void removeListener(uint64_t id) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
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

    mutable std::recursive_mutex mutex_;
    std::vector<Entry> entries_;
    uint64_t nextId_ = 0;
};

} // namespace trussc
