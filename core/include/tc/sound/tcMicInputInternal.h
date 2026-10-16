#pragma once
#include "tcSound.h"
#include <condition_variable>

namespace trussc::internal {
struct MicState {
    std::atomic<bool> cancelled{false};
    std::weak_ptr<std::atomic<bool>> run = appRunToken();
    bool runBound = bool(appRunToken());
    std::weak_ptr<std::function<void(MicState&)>> apply;
    std::shared_ptr<std::function<void()>> registration;
    std::shared_ptr<MicState> predecessor;
    std::mutex lifecycle;
    std::condition_variable changed;
    bool startupDone = false;
    bool done = false;
    MicStartResult result;
    int sampleRate = MicInput::DEFAULT_SAMPLE_RATE;
    int backend = -1;
    std::string name;
    void* device = nullptr;
    unsigned webKey = 0;
    std::mutex ringMutex;
    std::vector<float> ring = std::vector<float>(MicInput::BUFFER_SIZE, 0.0f);
    size_t writePos = 0;
    void audio(const float* input, size_t frames);
};
using MicOpen = std::function<MicStartResult(MicState&)>;
using MicClose = std::function<void(MicState&)>;
void setMicBackendForTests(MicOpen open, MicClose close);
void launchMic(const std::shared_ptr<MicState>& state, bool asynchronous);
void cancelMic(const std::shared_ptr<MicState>& state);
void completeMic(const std::shared_ptr<MicState>& state);
}
