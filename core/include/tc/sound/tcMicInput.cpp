#include "tcMicInputInternal.h"

namespace trussc {
namespace internal {
namespace {
std::atomic<size_t> liveMicStates{0};
}
MicState::MicState() { ++liveMicStates; }
MicState::~MicState() { --liveMicStates; }
size_t micStatesAliveForTests() { return liveMicStates.load(); }
void MicState::audio(const float* input, size_t frames) {
    std::lock_guard<std::mutex> lock(ringMutex);
    if (ring.empty()) return;
    for (size_t i = 0; i < frames; ++i) {
        ring[writePos] = input[i];
        writePos = (writePos + 1) % ring.size();
    }
}
void completeMic(const std::shared_ptr<MicState>& state) {
    enqueueResourceLoad([state] {
        if (state->cancelled || !appRunIsAlive(state->run)) return;
        AppOwnerScope appScope(state->appOwner.lock());
        if (auto target = state->apply.lock()) (*target)(*state);
    });
}
}
MicInput::~MicInput() {
    // Global user instances may outlive the logger. Cancellation never logs.
    gate_->close();
    apply_.reset();
    if (state_) internal::cancelMic(state_);
}
bool MicInput::isStarting() const {
    return starting_ && state_ && !state_->cancelled && (!state_->runBound || internal::appRunIsAlive(state_->run));
}
bool MicInput::isRunning() const {
    return running_ && state_ && !state_->cancelled && (!state_->runBound || internal::appRunIsAlive(state_->run));
}
void MicInput::stop() {
    const bool wasRunning = isRunning();
    apply_.reset();
    if (state_) {
        internal::cancelMic(state_);
        predecessor_ = std::move(state_);
        // Permission failures/cancellations have no worker to consume their
        // chain. Hand only unfinished cleanup forward, releasing completed
        // states (and their stop registrations) without waiting on a worker.
        while (predecessor_) {
            auto previous = predecessor_;
            std::lock_guard<std::mutex> lock(previous->lifecycle);
            if (!previous->done) break;
            predecessor_ = std::move(previous->predecessor);
        }
    }
    starting_ = false; running_ = false; deviceName_.clear();
    if (wasRunning) logNotice("MicInput") << "stopped";
}
bool MicInput::beginStart(int sampleRate, bool asynchronous) {
    stop();
    auto state = std::make_shared<internal::MicState>();
    state->sampleRate = sampleRate;
    state->backend = selectedBackend(); // engine is observed only on main
    state->predecessor = std::move(predecessor_);
    state_ = state;
    starting_ = true;
    sampleRate_ = sampleRate;
    auto delivery = internal::EventSnapshotDelivery<MicStartResult>::bind(onStart, gate_);
    apply_ = std::make_shared<std::function<void(internal::MicState&)>>(
        [this, delivery, asynchronous](internal::MicState& ready) {
            if (state_.get() != &ready) return;
            starting_ = false;
            running_ = ready.result.success;
            sampleRate_ = ready.sampleRate;
            deviceName_ = running_ ? ready.name : std::string{};
            apply_.reset();
            if (asynchronous) {
                if (running_) logNotice("MicInput") << "started (" << ready.sampleRate << " Hz, mono, " << ready.name << ")";
                (*delivery)(ready.result);
            }
            // A listener can delete or restart this object.
        });
    state->apply = apply_;
    auto cancel = [weak = std::weak_ptr(state)] {
        if (auto live = weak.lock()) internal::cancelMic(live);
    };
    state->registration = internal::registerResourceStop(cancel);
    internal::ownAppCancellation(cancel);
    try {
        internal::launchMic(state, asynchronous);
    } catch (const std::exception& e) {
        state->result = {false, e.what()};
        state->startupDone = state->done = true;
        if (asynchronous) internal::completeMic(state);
    }
#ifdef __EMSCRIPTEN__
    // Legacy Web start() acknowledges a submitted request, not readiness.
    return true;
#else
    if (asynchronous) return true;
    {
        std::unique_lock<std::mutex> lock(state->lifecycle);
        state->changed.wait(lock, [&] { return state->startupDone; });
    }
    if (auto target = state->apply.lock()) (*target)(*state);
    const bool success = state->result.success;
    if (success) logNotice("MicInput") << "started (" << state->sampleRate << " Hz, mono, " << state->name << ")";
    else logError("MicInput") << state->result.message;
    return success;
#endif
}
bool MicInput::start(int sampleRate) { return beginStart(sampleRate, false); }
void MicInput::startAsync(int sampleRate) { beginStart(sampleRate, true); }
size_t MicInput::getBuffer(float* outBuffer, size_t numSamples) {
    if (!isRunning() || !outBuffer || !numSamples) return 0;
    numSamples = std::min(numSamples, (size_t)BUFFER_SIZE);
    auto state = state_;
    std::lock_guard<std::mutex> lock(state->ringMutex);
    size_t readPos = (state->writePos + BUFFER_SIZE - numSamples) % BUFFER_SIZE;
    for (size_t i = 0; i < numSamples; ++i) outBuffer[i] = state->ring[(readPos + i) % BUFFER_SIZE];
    return numSamples;
}
void MicInput::onAudioData(const float* input, size_t frameCount) {
    if (state_ && input) state_->audio(input, frameCount);
}
MicInput& getMicInput() { static auto* instance = new MicInput; return *instance; }
}
