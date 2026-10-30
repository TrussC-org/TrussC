#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>

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

// Count queued and executing Main copies across all listeners of one event.
// Zero means unlimited; lowering the limit keeps already accepted copies.
struct ExternalDeliveryStore : std::enable_shared_from_this<ExternalDeliveryStore> {
    struct Lease {
        std::shared_ptr<ExternalDeliveryStore> store;
        ~Lease();
    };
    std::shared_ptr<Lease> reserve();
    mutable std::mutex mutex;
    // Bound (network input): opt-in backlog of queued/executing Main copies; 0 = unlimited, over the limit a whole new copy is rejected with one Warning per overflow episode.
    size_t deliveryLimit = 0;
    size_t pending = 0;
    bool overflowing = false;
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
