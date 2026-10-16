#pragma once

// Resource-specific preparation uses this host-owned completion mailbox and
// retirement owner. Neither owns an application handle. All handle operations
// and delivery are serialized on main; only cancellation is read by workers.
#include "tcMainThread.h"
#include "tcLoadResult.h"
#include "tc/network/tcNetworkState.h"
#include <filesystem>
#include <vector>

namespace trussc::internal {

using ResourceLoadBatch = std::vector<std::function<void()>>;
void enqueueResourceLoad(std::function<void()> completion);
ResourceLoadBatch takeResourceLoadBatch();
void deliverResourceLoadBatch(ResourceLoadBatch batch);
void startResourceLoad(std::function<void()> preparation);
void finishResourceLoadsAtExit();
void waitForResourceLoadsAtAppExit();

// Deterministic test seam. Installed/removed only with workers quiescent.
using ResourceLoadProbe = std::function<void(const char*, const std::filesystem::path&)>;
void setResourceLoadProbeForTests(ResourceLoadProbe probe);
void probeResourceLoad(const char* stage, const std::filesystem::path& path);
void waitForResourceLoadsForTests();
size_t resourceLoadWorkersForTests();

struct ResourceLoadState {
    using Target = std::function<void(ResourceLoadState&)>;
    std::atomic<bool> cancelled{false};
    std::weak_ptr<std::atomic<bool>> run;
    std::weak_ptr<Target> apply;
    EventSnapshotDelivery<LoadResult> event;
    LoadResult result;
};

template<class State> void completeResourceLoad(const std::shared_ptr<State>& state) {
    enqueueResourceLoad([state] {
        if (state->cancelled.load() || !appRunIsAlive(state->run)) return;
        if (auto target = state->apply.lock()) (*target)(*state);
    });
}

template<class State> class ResourceLoadSlot {
public:
    ResourceLoadSlot() = default;
    ResourceLoadSlot(const ResourceLoadSlot&) {}
    ResourceLoadSlot(ResourceLoadSlot&& other) noexcept { other.cancel(); }
    ResourceLoadSlot& operator=(const ResourceLoadSlot&) { cancel(); return *this; }
    ResourceLoadSlot& operator=(ResourceLoadSlot&& other) noexcept {
        cancel(); other.cancel(); return *this;
    }
    ~ResourceLoadSlot() { cancel(); gate_->close(); }

    bool loading() const { return state_ && appRunIsAlive(state_->run); }
    void cancel() {
        if (state_) state_->cancelled.store(true);
        state_.reset();
        apply_.reset();
        event_.reset();
    }
    template<class Apply> std::shared_ptr<State> begin(Event<LoadResult>& event, Apply apply) {
        cancel();
        auto state = std::make_shared<State>();
        state->run = appRunToken();
        event_ = EventSnapshotDelivery<LoadResult>::bind(event, gate_);
        state->event.target = event_;
        apply_ = std::make_shared<ResourceLoadState::Target>(
            [this, apply = std::move(apply)](ResourceLoadState& base) {
                auto& ready = static_cast<State&>(base);
                if (state_.get() != &ready) return;
                // Retain the snapshot target across completion and a listener
                // deleting/moving/restarting its emitter. No handle access
                // after notify (including access to this slot).
                auto delivery = event_;
                apply(ready);
                state_.reset();
                apply_.reset();
                event_.reset();
                if (delivery) (*delivery)(ready.result);
            });
        state->apply = apply_;
        state_ = state;
        return state;
    }
private:
    std::shared_ptr<State> state_;
    std::shared_ptr<ResourceLoadState::Target> apply_;
    std::shared_ptr<typename EventSnapshotDelivery<LoadResult>::Target> event_;
    std::shared_ptr<NetworkDelivery> gate_ = std::make_shared<NetworkDelivery>();
};

} // namespace trussc::internal
