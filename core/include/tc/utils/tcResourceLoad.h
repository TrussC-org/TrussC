#pragma once

// Resource-specific preparation uses this host-owned completion mailbox and
// retirement owner. Neither owns an application handle. All handle operations
// and delivery are serialized on main; only cancellation is read by workers.
#include "tcMainThread.h"
#include "tcLoadResult.h"
#include "tc/network/tcNetworkState.h"
#include <filesystem>
#include <vector>
#include <type_traits>

namespace trussc::internal {

struct ResourceLoadBatch : std::vector<std::function<void()>> {
    MainThreadSlotBatch results;
    bool empty() const { return std::vector<std::function<void()>>::empty() && results.empty(); }
};
void enqueueResourceLoad(std::function<void()> completion);
ResourceLoadBatch takeResourceLoadBatch();
void deliverResourceLoadBatch(ResourceLoadBatch batch);
void startResourceLoad(std::function<void()> preparation);
void finishResourceLoadsAtExit();
// A backend retains this registration until its callbacks have quiesced.
std::shared_ptr<std::function<void()>> registerResourceStop(std::function<void()> stop);
void cancelResourceBackends();
void waitForResourceLoadsAtAppExit();

// Deterministic test seam. Installed/removed only with workers quiescent.
using ResourceLoadProbe = std::function<void(const char*, const std::filesystem::path&)>;
void setResourceLoadProbeForTests(ResourceLoadProbe probe);
void probeResourceLoad(const char* stage, const std::filesystem::path& path);
void waitForResourceLoadsForTests();
size_t resourceLoadWorkersForTests();

struct ResourceLoadState : MainThreadSlot, std::enable_shared_from_this<ResourceLoadState> {
    ResourceLoadState();
    virtual ~ResourceLoadState();
    using Target = std::function<void(ResourceLoadState&)>;
    std::atomic<bool> cancelled{false};
    std::weak_ptr<std::atomic<bool>> run;
    std::weak_ptr<Target> apply;
    EventSnapshotDelivery<LoadResult> event;
    LoadResult result;
    // Completion/cancel fields are guarded by the host mutex, never held
    // over decoding, application code, event delivery or a worker join.
    bool completed = false;
    bool delivering = false;
    size_t credits = 0;
    bool requestCredit = false;
    bool backendPending = false; // Web callbacks retain their active slot
    bool externalActive = false; // Web active slot, not a queued request
    bool preparing = false;
    std::shared_ptr<ResourceLoadState> nextPending;
    std::function<void()> preparation;
    std::function<void()> cancelBackend; // main-thread Web cancellation only
    std::shared_ptr<std::function<void()>> stop;
    virtual size_t retainedBytes() const noexcept { return 0; }
    virtual void discardPayload() noexcept {}
    void cancel();
};

// External loads alone use bounded preparation and retained-result admission.
// startResourceLoad / enqueueResourceLoad remain growable for app-authored jobs.
// On Web, recovery from allocation failure is guaranteed only for TrussC's
// out-of-line host code. Inline helpers from headers may be compiled into the
// application without exception support; allocation failure inside them is a
// known limitation.
void startExternalResourceLoad(std::shared_ptr<ResourceLoadState> state, std::function<void()> preparation);
// The guard is compiled in the host, including exception-enabled Web code.
bool prepareResourceLoad(ResourceLoadState& state, const std::function<void()>& preparation);
void completeResourceLoad(const std::shared_ptr<ResourceLoadState>& state);
template<class State, class Prepare> requires std::is_empty_v<Prepare>
void startExternalResourceLoad(const std::shared_ptr<State>& state, Prepare) {
    try {
        // The host job owns State; the thunk only borrows it. Never create an
        // owning temporary in an application TU with disabled Web unwinding.
        // A trivial pointer-sized thunk fits the supported
        // standard libraries' inline function storage, also on Web.
        std::function<void()> work = [raw = state.get()] {
            Prepare{}(raw);
        };
        startExternalResourceLoad(std::shared_ptr<ResourceLoadState>(state), std::move(work));
    } catch (...) {
        state->result.error = LoadError::Unknown;
        state->result.message.clear();
        completeResourceLoad(state);
    }
}
struct ResourceLoadUsage { size_t active, pending, retainedBytes; uint64_t rejected; size_t outstanding; };
ResourceLoadUsage resourceLoadUsageForTests();
void setResourceLoadLimitsForTests(size_t active, size_t pending, size_t bytes);
void failResourceLoadLaunchForTests(bool fail);

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

    bool loading() const { return state_ && !state_->cancelled.load() && appRunIsAlive(state_->run); }
    void cancel() {
        if (state_) state_->cancel();
        state_.reset();
        apply_.reset();
        event_.reset();
    }
    template<class Apply> std::shared_ptr<State> begin(Event<LoadResult>& event, Apply apply) {
        cancel();
        auto state = std::make_shared<State>();
        state->run = appRunToken();
        state->stop = registerResourceStop([weak = std::weak_ptr<State>(state)] {
            if (auto live = weak.lock()) live->cancel();
        });
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
                auto application = [&] { apply(ready); };
                if (!prepareResourceLoad(ready, std::ref(application)))
                    (void)prepareResourceLoad(ready, std::ref(application)); // whole-failure cleanup
                ready.cancel(); // application consumed the external payload
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
