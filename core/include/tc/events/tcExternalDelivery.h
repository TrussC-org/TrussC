#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <limits>
#include <string>
#include <vector>

namespace trussc::internal {

// Synchronous forwarding bridges query the origin of this exact payload.
// Nested snapshot emissions restore the enclosing scope on return. Runtime
// ownership keeps the thread-local context shared across hot-reload images.
class ExternalEventScope {
public:
    ExternalEventScope(const void* payload, bool external);
    ~ExternalEventScope();
    ExternalEventScope(const ExternalEventScope&) = delete;
    ExternalEventScope& operator=(const ExternalEventScope&) = delete;
    static bool isExternal(const void* payload);
private:
    const void* payload_;
    bool external_;
    const ExternalEventScope* previous_;
};

// Credits cover queued AND executing copies. A lease outlives every payload
// it admits, even when a listener, source, or app run disappears before drain.
struct ExternalDeliveryStore : std::enable_shared_from_this<ExternalDeliveryStore> {
    // Bound (security): external producers can outpace the main thread. Overflow rejects whole Main copies before copying payloads or callables and is counted; TCP receive callers disconnect and invalidate their run.
    static constexpr size_t deliveryLimit = 1024;
    // Bound (security): listener fanout retains independent external payload copies. Overflow rejects whole Main copies before copying and is counted.
    static constexpr size_t byteLimit = 64 * 1024 * 1024;
    struct Lease {
        std::shared_ptr<ExternalDeliveryStore> store;
        size_t count, bytes;
        ~Lease();
    };
    std::shared_ptr<Lease> reserve(size_t count, size_t bytesPerCopy);
    mutable std::mutex mutex;
    size_t pending = 0, retainedBytes = 0;
    size_t rejectedCopies = 0;
    bool warningScheduled = false;
};

inline size_t externalSizeAdd(size_t a, size_t b) {
    constexpr size_t limit = ExternalDeliveryStore::byteLimit;
    return a > limit || b > limit - a ? limit + 1 : a + b;
}
inline size_t externalSizeMultiply(size_t a, size_t b) {
    constexpr size_t limit = ExternalDeliveryStore::byteLimit;
    return b && a > limit / b ? limit + 1 : a * b;
}

// Measure retained storage without making a payload copy. Charge source
// capacities conservatively (including string terminators/SSO). Addon trees
// specialize this trait to account for every nested allocation.
template<class T> struct ExternalPayload {
    static size_t bytes(const T& arg) {
        size_t result = sizeof(T);
        if constexpr (requires { arg.data.capacity(); })
            result = externalSizeAdd(result, externalSizeMultiply(arg.data.capacity(), sizeof(typename decltype(arg.data)::value_type)));
        if constexpr (requires { arg.bytes.capacity(); })
            result = externalSizeAdd(result, arg.bytes.capacity());
        if constexpr (requires { arg.message.capacity(); }) result = externalSizeAdd(result, externalSizeAdd(arg.message.capacity(), 1));
        if constexpr (requires { arg.reason.capacity(); }) result = externalSizeAdd(result, externalSizeAdd(arg.reason.capacity(), 1));
        if constexpr (requires { arg.host.capacity(); }) result = externalSizeAdd(result, externalSizeAdd(arg.host.capacity(), 1));
        if constexpr (requires { arg.remoteHost.capacity(); }) result = externalSizeAdd(result, externalSizeAdd(arg.remoteHost.capacity(), 1));
        if constexpr (requires { arg.portName.capacity(); }) result = externalSizeAdd(result, externalSizeAdd(arg.portName.capacity(), 1));
        if constexpr (requires { arg.port.capacity(); }) result = externalSizeAdd(result, externalSizeAdd(arg.port.capacity(), 1));
        return result;
    }
};
template<> struct ExternalPayload<std::string> {
    static size_t bytes(const std::string& arg) {
        return externalSizeAdd(sizeof(arg), externalSizeAdd(arg.capacity(), 1));
    }
};

// The source token is separate from listener tokens: an in-flight snapshot
// can retain listeners after source destruction. Only queued calls check it;
// synchronous snapshot delivery can finish after its emitter is destroyed.
inline bool eventSourceIsAlive(const std::weak_ptr<std::atomic<bool>>& token) {
    auto alive = token.lock();
    return alive && alive->load(std::memory_order_acquire);
}

template<class> struct ExternalEventDelivery;
} // namespace trussc::internal
