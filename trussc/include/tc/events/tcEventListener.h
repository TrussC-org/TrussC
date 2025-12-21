#pragma once

// =============================================================================
// tcEventListener - RAII token for event listener
// =============================================================================

#include <functional>
#include <utility>

namespace trussc {

// EventListener - RAII token for listener registration
// Listener is automatically disconnected when out of scope
class EventListener {
public:
    using DisconnectFunc = std::function<void()>;

    // Default constructor (disconnected state)
    EventListener() = default;

    // Move constructor
    EventListener(EventListener&& other) noexcept
        : disconnector_(std::move(other.disconnector_))
        , connected_(other.connected_) {
        other.connected_ = false;
        other.disconnector_ = nullptr;
    }

    // Move assignment operator
    EventListener& operator=(EventListener&& other) noexcept {
        if (this != &other) {
            disconnect();
            disconnector_ = std::move(other.disconnector_);
            connected_ = other.connected_;
            other.connected_ = false;
            other.disconnector_ = nullptr;
        }
        return *this;
    }

    // Copy forbidden (ownership is unique)
    EventListener(const EventListener&) = delete;
    EventListener& operator=(const EventListener&) = delete;

    // Destructor - auto disconnect
    ~EventListener() {
        disconnect();
    }

    // Explicit disconnect
    void disconnect() noexcept {
        if (connected_ && disconnector_) {
            try {
                disconnector_();
            } catch (...) {
                // Event may have been destroyed first due to destruction order at exit
                // Ignore exception in that case (noexcept required since called from destructor)
            }
            connected_ = false;
            disconnector_ = nullptr;
        }
    }

    // Check connection state
    bool isConnected() const {
        return connected_;
    }

    // Explicit bool conversion
    explicit operator bool() const {
        return connected_;
    }

private:
    // Only constructible from Event<T>
    template<typename T> friend class Event;

    // Internal constructor
    explicit EventListener(DisconnectFunc disconnector)
        : disconnector_(std::move(disconnector))
        , connected_(true) {}

    DisconnectFunc disconnector_;
    bool connected_ = false;
};

} // namespace trussc
