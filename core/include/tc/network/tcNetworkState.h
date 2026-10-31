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
// Each external emission opts in; app results and teardown stay uncapped.
template<class T> struct EventSnapshotDelivery {
    using RunToken = std::shared_ptr<std::atomic<bool>>;
    struct Target {
        std::function<bool(T&, RunToken, bool)> dispatch;
        template<class Fn> explicit Target(Fn fn) : dispatch(std::move(fn)) {}
        bool operator()(T& args, RunToken run = {}) const {
            return dispatch(args, std::move(run), false);
        }
    };
    std::weak_ptr<Target> target;
    static std::shared_ptr<Target> bind(Event<T>& event, std::shared_ptr<NetworkDelivery> gate) {
        return std::make_shared<Target>([source = &event, gate = std::move(gate)](T& args, RunToken run, bool external) {
            typename Event<T>::ConstEntryListPtr snapshot;
            std::shared_ptr<ExternalDeliveryStore> store;
            std::weak_ptr<std::atomic<bool>> sourceAlive;
            bool mainStoreFailed = false;
            {
                std::lock_guard<std::mutex> lock(gate->mutex);
                if (!gate->open) return true;
                snapshot = sharedLoad(source->entries_);
                sourceAlive = source->alive_;
                if (external) {
                    store = ExternalEventDelivery<T>::store(*source, snapshot, mainStoreFailed);
                }
            }
            ExternalEventScope origin(&args, external);
            return Event<T>::notifySnapshot(std::move(snapshot), args, std::move(store), std::move(sourceAlive), std::move(run), mainStoreFailed);
        });
    }
    bool notify(T& args, RunToken run = {}) const {
        if (auto delivery = target.lock()) return (*delivery)(args, std::move(run));
        return true;
    }
    bool notifyExternal(T& args, RunToken run = {}) const {
        if (auto delivery = target.lock()) return delivery->dispatch(args, std::move(run), true);
        return true;
    }
};

// The same gate and snapshot rule for events without a payload.
template<> struct EventSnapshotDelivery<void> {
    struct Target {
        std::function<void(bool)> dispatch;
        template<class Fn> explicit Target(Fn fn) : dispatch(std::move(fn)) {}
        void operator()() const { dispatch(false); }
    };
    std::weak_ptr<Target> target;
    static std::shared_ptr<Target> bind(Event<void>& event, std::shared_ptr<NetworkDelivery> gate) {
        return std::make_shared<Target>([source = &event, gate = std::move(gate)](bool external) {
            Event<void>::ConstEntryListPtr snapshot;
            std::shared_ptr<ExternalDeliveryStore> store;
            std::weak_ptr<std::atomic<bool>> sourceAlive;
            bool mainStoreFailed = false;
            {
                std::lock_guard<std::mutex> lock(gate->mutex);
                if (!gate->open) return;
                snapshot = sharedLoad(source->entries_);
                sourceAlive = source->alive_;
                if (external) {
                    store = ExternalEventDelivery<void>::store(*source, snapshot, mainStoreFailed);
                }
            }
            Event<void>::notifySnapshot(std::move(snapshot), std::move(store), std::move(sourceAlive), mainStoreFailed);
        });
    }
    void notify() const { if (auto delivery = target.lock()) (*delivery)(); }
    void notifyExternal() const { if (auto delivery = target.lock()) delivery->dispatch(true); }
};

} // namespace trussc::internal
