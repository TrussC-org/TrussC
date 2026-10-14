#pragma once
#include <memory>
#include <functional>
#include <utility>
#include <type_traits>
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
template<class T> struct NetworkEvent {
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

// Protected compatibility access for TcpClient subclasses. These aliases always
// select the handle's current run, while core workers use their captured State.
template<class T> decltype(auto) networkFieldValue(T& value) {
    if constexpr (requires { value.load(); }) return value.load();
    else if constexpr (std::is_arithmetic_v<T>) return T(value);
    else return (value);
}
template<class S, class T> class NetworkField {
    AtomicSharedPtr<S>* state_;
    T S::* field_;
    T& get() const { return (*sharedLoad(*state_)).*field_; }
public:
    NetworkField(AtomicSharedPtr<S>& state, T S::* field) : state_(&state), field_(field) {}
    operator decltype(networkFieldValue(std::declval<T&>()))() const {
        auto state = sharedLoad(*state_);
        return networkFieldValue((*state).*field_);
    }
    template<class V> NetworkField& operator=(V&& value) { get() = std::forward<V>(value); return *this; }
    T* operator&() const { return &get(); }
    auto load() const { auto state = sharedLoad(*state_); return ((*state).*field_).load(); }
    template<class V> auto exchange(V value) { return get().exchange(value); }
    void lock() { get().lock(); }
    bool try_lock() { return get().try_lock(); }
    void unlock() { get().unlock(); }
};
} // namespace trussc::internal
