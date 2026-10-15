#pragma once
#include <memory>
#include <functional>
#include <utility>
#include "tc/utils/tcAtomicSharedPtr.h"
#include "tc/events/tcEvent.h"

namespace trussc::internal {
// Protect only snapshot acquisition, never a listener or a join. Closing this
// gate prevents a target retained by an in-flight notification using an Event
// whose public handle has already been destroyed.
struct NetworkDelivery {
    std::mutex mutex;
    bool open = true;
    void close() { std::lock_guard<std::mutex> lock(mutex); open = false; }
};

// A run holds only a weak delivery target. The handle drops its targets before
// teardown; Event::notify retains its listener snapshot across self-destruction.
template<class T> struct EventSnapshotDelivery {
    using Target = std::function<void(T&)>;
    std::weak_ptr<Target> target;
    static std::shared_ptr<Target> bind(Event<T>& event, std::shared_ptr<NetworkDelivery> gate) {
        return std::make_shared<Target>([source = &event, gate = std::move(gate)](T& args) {
            typename Event<T>::ConstEntryListPtr snapshot;
            {
                std::lock_guard<std::mutex> lock(gate->mutex);
                if (!gate->open) return;
                snapshot = sharedLoad(source->entries_);
            }
            Event<T>::notifySnapshot(std::move(snapshot), args);
        });
    }
    void notify(T& args) const {
        if (auto delivery = target.lock()) (*delivery)(args);
    }
};

// The same gate and snapshot rule for events without a payload.
template<> struct EventSnapshotDelivery<void> {
    using Target = std::function<void()>;
    std::weak_ptr<Target> target;
    static std::shared_ptr<Target> bind(Event<void>& event, std::shared_ptr<NetworkDelivery> gate) {
        return std::make_shared<Target>([source = &event, gate = std::move(gate)] {
            Event<void>::ConstEntryListPtr snapshot;
            {
                std::lock_guard<std::mutex> lock(gate->mutex);
                if (!gate->open) return;
                snapshot = sharedLoad(source->entries_);
            }
            Event<void>::notifySnapshot(std::move(snapshot));
        });
    }
    void notify() const { if (auto delivery = target.lock()) (*delivery)(); }
};

} // namespace trussc::internal
