// Optional count limit for external Main deliveries.
#include "tc/events/tcExternalDelivery.h"
#include <limits>

namespace trussc::internal {
namespace {
thread_local const ExternalEventScope* currentExternalEvent = nullptr;
}
ExternalEventScope::ExternalEventScope(const void* payload, bool external)
    : payload_(payload), external_(external), previous_(currentExternalEvent) {
    currentExternalEvent = this;
}
ExternalEventScope::~ExternalEventScope() { currentExternalEvent = previous_; }
bool ExternalEventScope::isExternal(const void* payload) {
    return currentExternalEvent && currentExternalEvent->payload_ == payload && currentExternalEvent->external_;
}

ExternalDeliveryStore::Lease::~Lease() {
    std::lock_guard<std::mutex> lock(store->mutex);
    --store->pending;
}

std::shared_ptr<ExternalDeliveryStore::Lease> ExternalDeliveryStore::reserve() {
    std::lock_guard<std::mutex> lock(mutex);
    if (deliveryLimit && pending >= deliveryLimit) return {};
    auto lease = std::make_shared<Lease>();
    lease->store = shared_from_this();
    ++pending;
    return lease;
}

void ExternalDeliveryStore::reject() {
    std::lock_guard<std::mutex> lock(mutex);
    if (rejectedCopies != (std::numeric_limits<size_t>::max)()) ++rejectedCopies;
}

} // namespace trussc::internal
