#pragma once
#include "tcSound.h"
#include <condition_variable>

namespace trussc::internal {
struct MicState : std::enable_shared_from_this<MicState> {
    MicState();
    ~MicState();
    std::atomic<bool> cancelled{false};
    std::weak_ptr<std::atomic<bool>> run = appRunToken();
    std::weak_ptr<AppOwner> appOwner = currentAppOwner();
    bool runBound = bool(appRunToken());
    std::weak_ptr<std::function<void(MicState&)>> apply;
    // Device loss: the capture device stopped (micStopped()). lostApply fires
    // onDisconnect on the main thread; stop() and the destructor reset it and
    // cancel the state, so closing the device ourselves reports nothing.
    std::weak_ptr<std::function<void(MicState&)>> lostApply;
    std::atomic<bool> lost{false};
    std::shared_ptr<std::function<void()>> registration;
    std::shared_ptr<MicState> predecessor;
    std::mutex lifecycle;
    std::condition_variable changed;
    bool startupDone = false;
    bool done = false;
    bool lifecycleStarted = false;
    MicStartResult result;
    bool startFailureLogged = false;
    int sampleRate = MicInput::DEFAULT_SAMPLE_RATE;
    int backend = -1;
    std::string name;
    std::optional<AudioDeviceInfo> selectedDevice;
    void* context = nullptr;
    void* device = nullptr;
    unsigned webKey = 0;
    std::mutex ringMutex;
    std::vector<float> ring;
    size_t writePos = 0;
    void audio(const float* input, size_t frames);
#ifndef __EMSCRIPTEN__
    uint64_t framePosition = 0; // capture-thread-owned, resets with each start
    void capture(const float* input, size_t frames);
#endif
};
size_t micStatesAliveForTests();
using MicOpen = std::function<MicStartResult(MicState&)>;
using MicClose = std::function<void(MicState&)>;
void setMicBackendForTests(MicOpen open, MicClose close);
enum class MicAuthorization { Authorized, Denied, Restricted, NotDetermined };
using MicAuthorizationQuery = std::function<MicAuthorization()>;
using MicPermissionRequest = std::function<void(std::function<void(bool)>)>;
// Main-thread seam replaces the platform query/request, including on Linux.
void setMicAuthorizationForTests(MicAuthorizationQuery query, MicPermissionRequest request);
#ifdef __APPLE__
MicAuthorization micAuthorizationPlatform();
void requestMicPermissionPlatform(std::function<void(bool)> completion);
#endif
void launchMic(const std::shared_ptr<MicState>& state, bool asynchronous);
void cancelMic(const std::shared_ptr<MicState>& state);
void completeMic(const std::shared_ptr<MicState>& state);
// The notification thread of the open capture device calls this when the
// device stopped (miniaudio "stopped" notification): state.lost is set
// (isRunning() is false from then on) and onDisconnect is queued for the main
// thread, where it is dropped if the app has stopped or restarted the mic by
// then. Never blocks and never runs app code. The tests call it to stand in
// for the backend.
void micStopped(MicState& state);
}
