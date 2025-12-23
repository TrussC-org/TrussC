// =============================================================================
// tcAudio implementation
// Sound playback and microphone input using miniaudio
// =============================================================================

#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "tc/sound/tcSound.h"

namespace trussc {

// ---------------------------------------------------------------------------
// AudioEngine miniaudio callback
// ---------------------------------------------------------------------------
static void playbackDataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    (void)pInput;  // Playback only, input not used

    AudioEngine* engine = static_cast<AudioEngine*>(pDevice->pUserData);
    if (engine && pOutput) {
        engine->mixAudio(static_cast<float*>(pOutput), frameCount, pDevice->playback.channels);
    }
}

// ---------------------------------------------------------------------------
// AudioEngine implementation
// ---------------------------------------------------------------------------

bool AudioEngine::init() {
    if (initialized_) return true;

    ma_device* device = new ma_device();

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = NUM_CHANNELS;
    config.sampleRate = SAMPLE_RATE;
    config.dataCallback = playbackDataCallback;
    config.pUserData = this;

    ma_result result = ma_device_init(nullptr, &config, device);
    if (result != MA_SUCCESS) {
        printf("AudioEngine: failed to initialize device (error=%d)\n", result);
        delete device;
        return false;
    }

    result = ma_device_start(device);
    if (result != MA_SUCCESS) {
        printf("AudioEngine: failed to start device (error=%d)\n", result);
        ma_device_uninit(device);
        delete device;
        return false;
    }

    device_ = device;
    initialized_ = true;

    printf("AudioEngine: initialized (%d Hz, %d ch) [miniaudio]\n", SAMPLE_RATE, NUM_CHANNELS);
    return true;
}

void AudioEngine::shutdown() {
    if (!initialized_) return;

    if (device_) {
        ma_device* device = static_cast<ma_device*>(device_);
        ma_device_stop(device);
        ma_device_uninit(device);
        delete device;
        device_ = nullptr;
    }

    initialized_ = false;
    printf("AudioEngine: shutdown\n");
}

void AudioEngine::mixAudio(float* buffer, int num_frames, int num_channels) {
    mixAudioInternal(buffer, num_frames, num_channels);
}

// ---------------------------------------------------------------------------
// MicInput implementation (Native only - Web version in platform/web/tcMicInput_web.cpp)
// ---------------------------------------------------------------------------
#ifndef __EMSCRIPTEN__

// MicInput miniaudio callback
static void micDataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    (void)pOutput;  // Capture only, output not used

    MicInput* mic = static_cast<MicInput*>(pDevice->pUserData);
    if (mic && pInput) {
        mic->onAudioData(static_cast<const float*>(pInput), frameCount);
    }
}

MicInput::~MicInput() {
    stop();
}

bool MicInput::start(int sampleRate) {
    if (running_) {
        stop();
    }

    sampleRate_ = sampleRate;
    buffer_.resize(BUFFER_SIZE, 0.0f);
    writePos_ = 0;

    // Create ma_device
    ma_device* device = new ma_device();

    ma_device_config config = ma_device_config_init(ma_device_type_capture);
    config.capture.format = ma_format_f32;
    config.capture.channels = 1;  // Mono
    config.sampleRate = sampleRate;
    config.dataCallback = micDataCallback;
    config.pUserData = this;

    ma_result result = ma_device_init(nullptr, &config, device);
    if (result != MA_SUCCESS) {
        printf("MicInput: failed to initialize device (error=%d)\n", result);
        delete device;
        return false;
    }

    result = ma_device_start(device);
    if (result != MA_SUCCESS) {
        printf("MicInput: failed to start device (error=%d)\n", result);
        ma_device_uninit(device);
        delete device;
        return false;
    }

    device_ = device;
    running_ = true;

    printf("MicInput: started (%d Hz, mono)\n", sampleRate);
    return true;
}

void MicInput::stop() {
    if (!running_) return;

    if (device_) {
        ma_device* device = static_cast<ma_device*>(device_);
        ma_device_stop(device);
        ma_device_uninit(device);
        delete device;
        device_ = nullptr;
    }

    running_ = false;
    printf("MicInput: stopped\n");
}

size_t MicInput::getBuffer(float* outBuffer, size_t numSamples) {
    if (!running_ || numSamples == 0) return 0;

    numSamples = std::min(numSamples, (size_t)BUFFER_SIZE);

    std::lock_guard<std::mutex> lock(mutex_);

    // Copy latest samples from ring buffer
    size_t readPos = (writePos_ + BUFFER_SIZE - numSamples) % BUFFER_SIZE;

    for (size_t i = 0; i < numSamples; i++) {
        outBuffer[i] = buffer_[(readPos + i) % BUFFER_SIZE];
    }

    return numSamples;
}

void MicInput::onAudioData(const float* input, size_t frameCount) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (size_t i = 0; i < frameCount; i++) {
        buffer_[writePos_] = input[i];
        writePos_ = (writePos_ + 1) % BUFFER_SIZE;
    }
}

// ---------------------------------------------------------------------------
// Global instance
// ---------------------------------------------------------------------------
MicInput& getMicInput() {
    static MicInput instance;
    return instance;
}

#endif // !__EMSCRIPTEN__

} // namespace trussc
