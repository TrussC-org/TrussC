#pragma once
#include "tcSound.h"
#include <condition_variable>

namespace trussc::internal {
struct MicState {
    MicState();
    ~MicState();
    std::atomic<bool> cancelled{false};
    std::weak_ptr<std::atomic<bool>> run = appRunToken();
    std::weak_ptr<AppOwner> appOwner = currentAppOwner();
    bool runBound = bool(appRunToken());
    std::weak_ptr<std::function<void(MicState&)>> apply;
    std::shared_ptr<std::function<void()>> registration;
    std::shared_ptr<MicState> predecessor;
    std::mutex lifecycle;
    std::condition_variable changed;
    bool startupDone = false;
    bool done = false;
    bool lifecycleStarted = false;
    MicStartResult result;
    int sampleRate = MicInput::DEFAULT_SAMPLE_RATE;
    int backend = -1;
    std::string name;
    void* device = nullptr;
    unsigned webKey = 0;
    std::mutex ringMutex;
    std::vector<float> ring;
    size_t writePos = 0;
    void audio(const float* input, size_t frames);
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
}
