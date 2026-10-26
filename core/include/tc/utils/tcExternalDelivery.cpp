// Internal admission and safe-thread reporting for external Main deliveries.
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
    store->pending -= count;
    store->retainedBytes -= bytes;
}

std::shared_ptr<ExternalDeliveryStore::Lease> ExternalDeliveryStore::reserve(size_t count, size_t bytesPerCopy) {
    bool warn = false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (count <= deliveryLimit - pending && bytesPerCopy <= byteLimit &&
            (!count || bytesPerCopy <= (byteLimit - retainedBytes) / count)) {
            auto lease = std::make_shared<Lease>();
            lease->store = shared_from_this();
            lease->count = count;
            lease->bytes = count * bytesPerCopy;
            pending += count;
            retainedBytes += lease->bytes;
            return lease;
        }
        if (rejectedCopies != (std::numeric_limits<size_t>::max)()) ++rejectedCopies;
        if (!warningScheduled) { warningScheduled = true; warn = true; }
    }
    if (warn) {
        // One job per store, never one per loss; app/teardown jobs stay growable.
        runOnMainThread([store = shared_from_this(), count, bytesPerCopy] {
            size_t losses;
            {
                std::lock_guard<std::mutex> lock(store->mutex);
                losses = store->rejectedCopies;
            }
            logWarning("ExternalDelivery") << "Main delivery store rejected " << losses << " whole external Main copy/copies: "
                << "capacity " << deliveryLimit << " deliveries / " << byteLimit
                << " payload bytes; needed " << count << " deliveries of " << bytesPerCopy
                << " bytes each on first loss";
        });
    }
    return {};
}

} // namespace trussc::internal
