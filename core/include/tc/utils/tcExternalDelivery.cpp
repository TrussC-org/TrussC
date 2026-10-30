// Optional count limit for external Main deliveries.
#include "tc/events/tcExternalDelivery.h"
#include "tc/utils/tcMainThread.h"
#include "tc/utils/tcLog.h"

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
    if (!store->deliveryLimit || store->pending < store->deliveryLimit) store->overflowing = false;
}

std::shared_ptr<ExternalDeliveryStore::Lease> ExternalDeliveryStore::reserve() {
    size_t warnLimit = 0;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!deliveryLimit || pending < deliveryLimit) {
            auto lease = std::make_shared<Lease>();
            lease->store = shared_from_this();
            ++pending;
            return lease;
        }
        if (!overflowing) {
            overflowing = true;
            warnLimit = deliveryLimit;
        }
    }
    // Outside the store mutex: on the main thread runOnMainThread runs inline.
    if (warnLimit) {
        runOnMainThread([warnLimit] {
            logWarning("Event") << "Main delivery limit (" << warnLimit
                << ") reached; new external copies are being dropped";
        });
    }
    return {};
}

} // namespace trussc::internal
