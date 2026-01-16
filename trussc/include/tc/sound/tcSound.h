#pragma once

// =============================================================================
// TrussC Sound
// Sound playback and microphone input based on miniaudio
//
// Design:
// - AudioEngine: Singleton, miniaudio initialization, mixer management
// - SoundBuffer: Decoded sound data (shareable)
// - Sound: User-facing class, playback control
// - MicInput: Microphone input
//
// Usage:
//   tc::Sound sound;
//   sound.load("music.ogg");
//   sound.play();
//   sound.setVolume(0.8f);
//   sound.setPan(-0.5f);   // Left-biased
//   sound.setSpeed(1.5f);  // 1.5x speed
//   sound.setLoop(true);
// =============================================================================

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <mutex>
#include <atomic>
#include <cstring>
#include <cmath>

#ifndef TC_SOUND_IMPL
// stb_vorbis forward declaration
extern "C" {
    typedef struct stb_vorbis stb_vorbis;

    typedef struct {
        unsigned int sample_rate;
        int channels;
        unsigned int setup_memory_required;
        unsigned int setup_temp_memory_required;
        unsigned int temp_memory_required;
        int max_frame_size;
    } stb_vorbis_info;

    stb_vorbis* stb_vorbis_open_filename(const char *filename, int *error, void *alloc_buffer);
    stb_vorbis_info stb_vorbis_get_info(stb_vorbis *f);
    int stb_vorbis_stream_length_in_samples(stb_vorbis *f);
    int stb_vorbis_get_samples_float_interleaved(stb_vorbis *f, int channels, float *buffer, int num_floats);
    void stb_vorbis_close(stb_vorbis *f);
}

// dr_wav forward declaration
extern "C" {
    typedef uint64_t drwav_uint64;
    float* drwav_open_file_and_read_pcm_frames_f32(
        const char* filename, unsigned int* channels, unsigned int* sampleRate,
        drwav_uint64* totalFrameCount, void* pAllocationCallbacks);
    void drwav_free(void* p, void* pAllocationCallbacks);
}

// dr_mp3 forward declaration
extern "C" {
    typedef uint64_t drmp3_uint64;
    typedef uint32_t drmp3_uint32;
    typedef struct {
        drmp3_uint32 channels;
        drmp3_uint32 sampleRate;
    } drmp3_config;
    float* drmp3_open_file_and_read_pcm_frames_f32(
        const char* filePath, drmp3_config* pConfig,
        drmp3_uint64* pTotalFrameCount, void* pAllocationCallbacks);
    float* drmp3_open_memory_and_read_pcm_frames_f32(
        const void* pData, size_t dataSize, drmp3_config* pConfig,
        drmp3_uint64* pTotalFrameCount, void* pAllocationCallbacks);
    void drmp3_free(void* p, void* pAllocationCallbacks);
}
#endif // TC_SOUND_IMPL

namespace trussc {


// ---------------------------------------------------------------------------
// Sound Buffer (decoded data)
// ---------------------------------------------------------------------------
class SoundBuffer {
public:
    std::vector<float> samples;  // Interleaved samples
    int channels = 0;
    int sampleRate = 0;
    size_t numSamples = 0;       // Samples per channel

    bool loadOgg(const std::string& path) {
        int error = 0;
        stb_vorbis* vorbis = stb_vorbis_open_filename(path.c_str(), &error, nullptr);
        if (!vorbis) {
            printf("SoundBuffer: failed to open %s (error=%d)\n", path.c_str(), error);
            return false;
        }

        stb_vorbis_info info = stb_vorbis_get_info(vorbis);
        channels = info.channels;
        sampleRate = info.sample_rate;
        numSamples = stb_vorbis_stream_length_in_samples(vorbis);

        samples.resize(numSamples * channels);

        int decoded = stb_vorbis_get_samples_float_interleaved(
            vorbis, channels, samples.data(), static_cast<int>(samples.size()));

        stb_vorbis_close(vorbis);

        printf("SoundBuffer: loaded %s (%d ch, %d Hz, %zu samples)\n",
               path.c_str(), channels, sampleRate, numSamples);

        return decoded > 0;
    }

    bool loadWav(const std::string& path) {
        unsigned int ch, sr;
        drwav_uint64 frameCount;

        float* data = drwav_open_file_and_read_pcm_frames_f32(
            path.c_str(), &ch, &sr, &frameCount, nullptr);

        if (!data) {
            printf("SoundBuffer: failed to open WAV %s\n", path.c_str());
            return false;
        }

        channels = ch;
        sampleRate = sr;
        numSamples = frameCount;

        samples.resize(numSamples * channels);
        std::memcpy(samples.data(), data, samples.size() * sizeof(float));

        drwav_free(data, nullptr);

        printf("SoundBuffer: loaded WAV %s (%d ch, %d Hz, %zu samples)\n",
               path.c_str(), channels, sampleRate, numSamples);

        return true;
    }

    bool loadMp3(const std::string& path) {
        drmp3_config config = {0, 0};
        drmp3_uint64 frameCount = 0;

        float* data = drmp3_open_file_and_read_pcm_frames_f32(
            path.c_str(), &config, &frameCount, nullptr);

        if (!data) {
            printf("SoundBuffer: failed to open MP3 %s\n", path.c_str());
            return false;
        }

        channels = config.channels;
        sampleRate = config.sampleRate;
        numSamples = frameCount;

        samples.resize(numSamples * channels);
        std::memcpy(samples.data(), data, samples.size() * sizeof(float));

        drmp3_free(data, nullptr);

        printf("SoundBuffer: loaded MP3 %s (%d ch, %d Hz, %zu samples)\n",
               path.c_str(), channels, sampleRate, numSamples);

        return true;
    }

    bool loadMp3FromMemory(const void* data, size_t dataSize) {
        drmp3_config config = {0, 0};
        drmp3_uint64 frameCount = 0;

        float* decoded = drmp3_open_memory_and_read_pcm_frames_f32(
            data, dataSize, &config, &frameCount, nullptr);

        if (!decoded) {
            printf("SoundBuffer: failed to decode MP3 from memory\n");
            return false;
        }

        channels = config.channels;
        sampleRate = config.sampleRate;
        numSamples = frameCount;

        samples.resize(numSamples * channels);
        std::memcpy(samples.data(), decoded, samples.size() * sizeof(float));

        drmp3_free(decoded, nullptr);

        printf("SoundBuffer: decoded MP3 from memory (%d ch, %d Hz, %zu samples)\n",
               channels, sampleRate, numSamples);

        return true;
    }

    // Load AAC/M4A file (platform-specific implementation)
    // Returns false on unsupported platforms
    bool loadAac(const std::string& path);

    // Load AAC data from memory (platform-specific implementation)
    // Returns false on unsupported platforms
    bool loadAacFromMemory(const void* data, size_t dataSize);

#ifdef __EMSCRIPTEN__
    // Web platform: complete deferred AAC loading (blocking)
    // Called from Sound::play() if loading was deferred during setup
    void ensureAacLoaded();

    // Check if this buffer has deferred AAC loading
    bool hasDeferredAac() const { return !deferredAacPath_.empty(); }

    std::string deferredAacPath_;  // Path for deferred AAC loading (Web only)
#endif

    // Load raw PCM data (16-bit signed, little-endian)
    bool loadPcmFromMemory(const void* data, size_t dataSize,
                           int numChannels, int rate, int bitsPerSample = 16,
                           bool bigEndian = false) {
        if (bitsPerSample != 16 && bitsPerSample != 32) {
            printf("SoundBuffer: unsupported bits per sample: %d\n", bitsPerSample);
            return false;
        }

        channels = numChannels;
        sampleRate = rate;

        if (bitsPerSample == 16) {
            // 16-bit signed integer -> float
            size_t sampleCount = dataSize / 2;
            numSamples = sampleCount / channels;
            samples.resize(sampleCount);

            const int16_t* src = static_cast<const int16_t*>(data);
            for (size_t i = 0; i < sampleCount; i++) {
                int16_t s = src[i];
                if (bigEndian) {
                    // Swap bytes for big-endian
                    s = static_cast<int16_t>((s >> 8) | (s << 8));
                }
                samples[i] = s / 32768.0f;
            }
        } else {
            // 32-bit float
            size_t sampleCount = dataSize / 4;
            numSamples = sampleCount / channels;
            samples.resize(sampleCount);

            const float* src = static_cast<const float*>(data);
            std::memcpy(samples.data(), src, dataSize);
        }

        printf("SoundBuffer: loaded PCM from memory (%d ch, %d Hz, %zu samples)\n",
               channels, sampleRate, numSamples);

        return true;
    }

    float getDuration() const {
        if (sampleRate == 0) return 0;
        return (float)numSamples / sampleRate;
    }

    // -------------------------------------------------------------------------
    // Waveform Generation
    // -------------------------------------------------------------------------

    void generateSineWave(float frequency, float duration, float volume = 0.5f, int sr = 44100) {
        sampleRate = sr;
        channels = 1;
        numSamples = (size_t)(duration * sampleRate);
        samples.resize(numSamples);

        constexpr float PI2 = 2.0f * 3.14159265f;
        for (size_t i = 0; i < numSamples; i++) {
            float t = (float)i / sampleRate;
            samples[i] = volume * std::sin(PI2 * frequency * t);
        }
    }

    void generateSquareWave(float frequency, float duration, float volume = 0.5f, int sr = 44100) {
        sampleRate = sr;
        channels = 1;
        numSamples = (size_t)(duration * sampleRate);
        samples.resize(numSamples);

        constexpr float PI2 = 2.0f * 3.14159265f;
        for (size_t i = 0; i < numSamples; i++) {
            float t = (float)i / sampleRate;
            float phase = std::fmod(frequency * t, 1.0f);
            samples[i] = volume * (phase < 0.5f ? 1.0f : -1.0f);
        }
    }

    void generateTriangleWave(float frequency, float duration, float volume = 0.5f, int sr = 44100) {
        sampleRate = sr;
        channels = 1;
        numSamples = (size_t)(duration * sampleRate);
        samples.resize(numSamples);

        for (size_t i = 0; i < numSamples; i++) {
            float t = (float)i / sampleRate;
            float phase = std::fmod(frequency * t, 1.0f);
            // Triangle: 0->1->0->-1->0 over one period
            float value = phase < 0.5f
                ? (4.0f * phase - 1.0f)
                : (3.0f - 4.0f * phase);
            samples[i] = volume * value;
        }
    }

    void generateSawtoothWave(float frequency, float duration, float volume = 0.5f, int sr = 44100) {
        sampleRate = sr;
        channels = 1;
        numSamples = (size_t)(duration * sampleRate);
        samples.resize(numSamples);

        for (size_t i = 0; i < numSamples; i++) {
            float t = (float)i / sampleRate;
            float phase = std::fmod(frequency * t, 1.0f);
            // Sawtooth: -1 to 1 over one period
            samples[i] = volume * (2.0f * phase - 1.0f);
        }
    }

    void generateNoise(float duration, float volume = 0.5f, int sr = 44100) {
        sampleRate = sr;
        channels = 1;
        numSamples = (size_t)(duration * sampleRate);
        samples.resize(numSamples);

        // Simple white noise using linear congruential generator
        uint32_t seed = 12345;
        for (size_t i = 0; i < numSamples; i++) {
            seed = seed * 1103515245 + 12345;
            float noise = ((seed >> 16) & 0x7FFF) / 16383.5f - 1.0f;
            samples[i] = volume * noise;
        }
    }

    void generatePinkNoise(float duration, float volume = 0.5f, int sr = 44100) {
        sampleRate = sr;
        channels = 1;
        numSamples = (size_t)(duration * sampleRate);
        samples.resize(numSamples);

        // Pink noise using Paul Kellet's refined method
        // Uses 7 first-order filters to approximate 1/f spectrum
        float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        uint32_t seed = 12345;

        for (size_t i = 0; i < numSamples; i++) {
            // Generate white noise
            seed = seed * 1103515245 + 12345;
            float white = ((seed >> 16) & 0x7FFF) / 16383.5f - 1.0f;

            // Apply pink noise filter (Paul Kellet's economy method)
            b0 = 0.99886f * b0 + white * 0.0555179f;
            b1 = 0.99332f * b1 + white * 0.0750759f;
            b2 = 0.96900f * b2 + white * 0.1538520f;
            b3 = 0.86650f * b3 + white * 0.3104856f;
            b4 = 0.55000f * b4 + white * 0.5329522f;
            b5 = -0.7616f * b5 - white * 0.0168980f;

            float pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362f;
            b6 = white * 0.115926f;

            // Normalize (pink noise is louder than white)
            samples[i] = volume * pink * 0.11f;
        }
    }

    void generateSilence(float duration, int sr = 44100) {
        sampleRate = sr;
        channels = 1;
        numSamples = (size_t)(duration * sampleRate);
        samples.resize(numSamples, 0.0f);
    }

    // -------------------------------------------------------------------------
    // ADSR Envelope
    // -------------------------------------------------------------------------
    void applyADSR(float attack, float decay, float sustainLevel, float release) {
        if (samples.empty() || sampleRate == 0) return;

        float duration = (float)numSamples / sampleRate;
        float sustainTime = duration - attack - decay - release;
        if (sustainTime < 0) sustainTime = 0;

        for (size_t i = 0; i < numSamples; i++) {
            float t = (float)i / sampleRate;
            float envelope = 0.0f;

            if (t < attack) {
                // Attack: 0 -> 1
                envelope = t / attack;
            } else if (t < attack + decay) {
                // Decay: 1 -> sustainLevel
                float dt = t - attack;
                envelope = 1.0f - (1.0f - sustainLevel) * (dt / decay);
            } else if (t < attack + decay + sustainTime) {
                // Sustain
                envelope = sustainLevel;
            } else {
                // Release: sustainLevel -> 0
                float dt = t - (attack + decay + sustainTime);
                envelope = sustainLevel * (1.0f - dt / release);
                if (envelope < 0) envelope = 0;
            }

            samples[i] *= envelope;
        }
    }

    // -------------------------------------------------------------------------
    // Mixing
    // -------------------------------------------------------------------------
    void mixFrom(const SoundBuffer& other, size_t offsetSamples, float volume = 1.0f) {
        if (other.samples.empty()) return;

        // Ensure we have enough space
        size_t requiredSize = offsetSamples + other.numSamples;
        if (samples.size() < requiredSize) {
            samples.resize(requiredSize, 0.0f);
            numSamples = requiredSize;
        }

        // Mix (add) samples
        for (size_t i = 0; i < other.numSamples && i < other.samples.size(); i++) {
            samples[offsetSamples + i] += other.samples[i] * volume;
        }
    }

    // Clip samples to -1.0 ~ 1.0 range
    void clip() {
        for (auto& s : samples) {
            if (s > 1.0f) s = 1.0f;
            else if (s < -1.0f) s = -1.0f;
        }
    }
};

// ---------------------------------------------------------------------------
// Playing Sound Instance
// ---------------------------------------------------------------------------
struct PlayingSound {
    std::shared_ptr<SoundBuffer> buffer;
    std::atomic<float> volume{1.0f};
    std::atomic<float> pan{0.0f};        // -1.0 (left) ~ 0.0 (center) ~ 1.0 (right)
    std::atomic<float> speed{1.0f};      // 0.5 (half speed) ~ 1.0 (normal) ~ 2.0 (double speed)
    std::atomic<bool> loop{false};
    std::atomic<bool> playing{false};
    std::atomic<bool> paused{false};

    // Playback position (floating-point for speed adjustment)
    double positionF{0.0};
};

// ---------------------------------------------------------------------------
// Audio Engine (singleton, miniaudio-based)
// ---------------------------------------------------------------------------
class AudioEngine {
public:
    static constexpr int MAX_PLAYING_SOUNDS = 32;
    static constexpr int SAMPLE_RATE = 44100;
    static constexpr int NUM_CHANNELS = 2;  // Stereo output
    static constexpr int ANALYSIS_BUFFER_SIZE = 4096;  // FFT analysis buffer size

    static AudioEngine& getInstance() {
        static AudioEngine instance;
        return instance;
    }

    // Initialize and shutdown (implementation in tcAudio_impl.cpp)
    bool init();
    void shutdown();

    // FFT analysis: Get latest audio samples (mono, left+right average)
    // numSamples: Number of samples to get (max ANALYSIS_BUFFER_SIZE)
    // Returns: Number of samples retrieved
    size_t getAnalysisBuffer(float* outBuffer, size_t numSamples) {
        if (!initialized_ || numSamples == 0) return 0;

        numSamples = std::min(numSamples, (size_t)ANALYSIS_BUFFER_SIZE);

        std::lock_guard<std::mutex> lock(analysisMutex_);

        // Copy latest samples from ring buffer
        size_t readPos = (analysisWritePos_ + ANALYSIS_BUFFER_SIZE - numSamples) % ANALYSIS_BUFFER_SIZE;

        for (size_t i = 0; i < numSamples; i++) {
            outBuffer[i] = analysisBuffer_[(readPos + i) % ANALYSIS_BUFFER_SIZE];
        }

        return numSamples;
    }

    // Add new playback instance
    std::shared_ptr<PlayingSound> play(std::shared_ptr<SoundBuffer> buffer) {
        if (!initialized_ || !buffer) return nullptr;

        std::lock_guard<std::mutex> lock(mutex_);

        // Find empty slot
        for (auto& slot : playingSounds_) {
            if (!slot || !slot->playing) {
                slot = std::make_shared<PlayingSound>();
                slot->buffer = buffer;
                slot->positionF = 0.0;
                slot->volume = 1.0f;
                slot->pan = 0.0f;
                slot->speed = 1.0f;
                slot->loop = false;
                slot->playing = true;
                slot->paused = false;
                return slot;
            }
        }

        printf("AudioEngine: max playing sounds reached\n");
        return nullptr;
    }

    // Called from audio callback (internal use)
    void mixAudio(float* buffer, int num_frames, int num_channels);

private:
    AudioEngine() {
        playingSounds_.resize(MAX_PLAYING_SOUNDS);
        analysisBuffer_.resize(ANALYSIS_BUFFER_SIZE, 0.0f);
    }

    ~AudioEngine() {
        shutdown();
    }

    void mixAudioInternal(float* buffer, int num_frames, int num_channels) {
        // Clear buffer
        std::memset(buffer, 0, num_frames * num_channels * sizeof(float));

        std::lock_guard<std::mutex> lock(mutex_);

        for (auto& sound : playingSounds_) {
            if (!sound || !sound->playing || sound->paused) continue;

            auto& src = sound->buffer;
            double posF = sound->positionF;
            float vol = sound->volume;
            float pan = sound->pan;
            float speed = sound->speed;

            // Calculate left/right volume from pan
            // pan = -1.0: left 100%, right 0%
            // pan =  0.0: left 100%, right 100%
            // pan =  1.0: left 0%,   right 100%
            float panL = (pan <= 0.0f) ? 1.0f : (1.0f - pan);
            float panR = (pan >= 0.0f) ? 1.0f : (1.0f + pan);

            for (int frame = 0; frame < num_frames; frame++) {
                size_t pos0 = (size_t)posF;
                size_t pos1 = pos0 + 1;
                float frac = (float)(posF - pos0);

                // Loop handling
                if (pos0 >= src->numSamples) {
                    if (sound->loop) {
                        posF = 0.0;
                        pos0 = 0;
                        pos1 = 1;
                        frac = 0.0f;
                    } else {
                        sound->playing = false;
                        break;
                    }
                }

                // Boundary check (when pos1 is out of range)
                if (pos1 >= src->numSamples) {
                    pos1 = sound->loop ? 0 : pos0;
                }

                // Get samples (linear interpolation)
                float left0, right0, left1, right1;
                if (src->channels == 1) {
                    // Mono
                    left0 = right0 = src->samples[pos0];
                    left1 = right1 = src->samples[pos1];
                } else {
                    // Stereo
                    left0 = src->samples[pos0 * 2];
                    right0 = src->samples[pos0 * 2 + 1];
                    left1 = src->samples[pos1 * 2];
                    right1 = src->samples[pos1 * 2 + 1];
                }

                // Linear interpolation
                float left = left0 + (left1 - left0) * frac;
                float right = right0 + (right1 - right0) * frac;

                // Apply volume and pan
                left *= vol * panL;
                right *= vol * panR;

                // Mix
                buffer[frame * num_channels] += left;
                if (num_channels > 1) {
                    buffer[frame * num_channels + 1] += right;
                }

                posF += speed;
            }

            sound->positionF = posF;
        }

        // Clipping
        for (int i = 0; i < num_frames * num_channels; i++) {
            if (buffer[i] > 1.0f) buffer[i] = 1.0f;
            if (buffer[i] < -1.0f) buffer[i] = -1.0f;
        }

        // Copy to FFT analysis ring buffer (mono: left+right average)
        {
            std::lock_guard<std::mutex> lock(analysisMutex_);
            for (int frame = 0; frame < num_frames; frame++) {
                float mono;
                if (num_channels > 1) {
                    mono = (buffer[frame * num_channels] + buffer[frame * num_channels + 1]) * 0.5f;
                } else {
                    mono = buffer[frame * num_channels];
                }
                analysisBuffer_[analysisWritePos_] = mono;
                analysisWritePos_ = (analysisWritePos_ + 1) % ANALYSIS_BUFFER_SIZE;
            }
        }
    }

    void* device_ = nullptr;  // ma_device*
    bool initialized_ = false;
    std::vector<std::shared_ptr<PlayingSound>> playingSounds_;
    std::mutex mutex_;

    // FFT analysis ring buffer
    std::vector<float> analysisBuffer_;
    size_t analysisWritePos_ = 0;
    std::mutex analysisMutex_;
};

// ---------------------------------------------------------------------------
// Sound Class (user-facing)
// ---------------------------------------------------------------------------
class Sound {
public:
    Sound() = default;
    ~Sound() = default;

    // Copy and move
    Sound(const Sound&) = default;
    Sound& operator=(const Sound&) = default;
    Sound(Sound&&) = default;
    Sound& operator=(Sound&&) = default;

    // -------------------------------------------------------------------------
    // Loading
    // -------------------------------------------------------------------------
    bool load(const std::string& path) {
        // Initialize AudioEngine (only once)
        AudioEngine::getInstance().init();

        buffer_ = std::make_shared<SoundBuffer>();

        // Determine format by extension
        std::string ext = path.substr(path.find_last_of('.') + 1);
        bool success = false;

        if (ext == "ogg" || ext == "OGG") {
            success = buffer_->loadOgg(path);
        } else if (ext == "wav" || ext == "WAV") {
            success = buffer_->loadWav(path);
        } else if (ext == "mp3" || ext == "MP3") {
            success = buffer_->loadMp3(path);
        } else if (ext == "aac" || ext == "AAC" || ext == "m4a" || ext == "M4A") {
            success = buffer_->loadAac(path);
        } else {
            printf("Sound: unsupported format: %s\n", ext.c_str());
        }

        if (!success) {
            buffer_.reset();
            return false;
        }
        return true;
    }

    // For testing: Generate sine wave
    void loadTestTone(float frequency = 440.0f, float duration = 1.0f) {
        AudioEngine::getInstance().init();
        buffer_ = std::make_shared<SoundBuffer>();
        buffer_->generateSineWave(frequency, duration, 0.5f);
    }

    // Load from pre-generated SoundBuffer
    void loadFromBuffer(const SoundBuffer& buf) {
        AudioEngine::getInstance().init();
        buffer_ = std::make_shared<SoundBuffer>(buf);
    }

    void loadFromBuffer(std::shared_ptr<SoundBuffer> buf) {
        AudioEngine::getInstance().init();
        buffer_ = buf;
    }

    bool isLoaded() const { return buffer_ != nullptr; }

    // -------------------------------------------------------------------------
    // Playback Control
    // -------------------------------------------------------------------------
    void play() {
        if (!buffer_) return;

#ifdef __EMSCRIPTEN__
        // Web platform: complete deferred AAC loading if needed
        if (buffer_->hasDeferredAac()) {
            buffer_->ensureAacLoaded();
        }
#endif

        // Stop if already playing
        stop();

        playing_ = AudioEngine::getInstance().play(buffer_);
        if (playing_) {
            playing_->volume = volume_;
            playing_->pan = pan_;
            playing_->speed = speed_;
            playing_->loop = loop_;
        }
    }

    void stop() {
        if (playing_) {
            playing_->playing = false;
            playing_.reset();
        }
    }

    void pause() {
        if (playing_) {
            playing_->paused = true;
        }
    }

    void resume() {
        if (playing_) {
            playing_->paused = false;
        }
    }

    // -------------------------------------------------------------------------
    // Settings
    // -------------------------------------------------------------------------
    void setVolume(float vol) {
        volume_ = vol;
        if (playing_) {
            playing_->volume = vol;
        }
    }

    float getVolume() const { return volume_; }

    void setLoop(bool loop) {
        loop_ = loop;
        if (playing_) {
            playing_->loop = loop;
        }
    }

    bool isLoop() const { return loop_; }

    void setPan(float pan) {
        // -1.0 (left) ~ 0.0 (center) ~ 1.0 (right)
        pan_ = (pan < -1.0f) ? -1.0f : (pan > 1.0f) ? 1.0f : pan;
        if (playing_) {
            playing_->pan = pan_;
        }
    }

    float getPan() const { return pan_; }

    void setSpeed(float speed) {
        // Clamp to 0.1 ~ 4.0 range (prevent extreme values)
        speed_ = (speed < 0.1f) ? 0.1f : (speed > 4.0f) ? 4.0f : speed;
        if (playing_) {
            playing_->speed = speed_;
        }
    }

    float getSpeed() const { return speed_; }

    // -------------------------------------------------------------------------
    // State
    // -------------------------------------------------------------------------
    bool isPlaying() const {
        return playing_ && playing_->playing && !playing_->paused;
    }

    bool isPaused() const {
        return playing_ && playing_->paused;
    }

    float getPosition() const {
        if (!playing_ || !buffer_) return 0;
        return (float)playing_->positionF / buffer_->sampleRate;
    }

    void setPosition(float seconds) {
        if (!playing_ || !buffer_) return;
        double pos = seconds * buffer_->sampleRate;
        // Clamp to valid range
        if (pos < 0) pos = 0;
        if (pos >= buffer_->numSamples) pos = buffer_->numSamples - 1;
        playing_->positionF = pos;
    }

    float getDuration() const {
        return buffer_ ? buffer_->getDuration() : 0;
    }

private:
    std::shared_ptr<SoundBuffer> buffer_;
    std::shared_ptr<PlayingSound> playing_;
    float volume_ = 1.0f;
    float pan_ = 0.0f;
    float speed_ = 1.0f;
    bool loop_ = false;
};

// ---------------------------------------------------------------------------
// Global Functions
// ---------------------------------------------------------------------------

// Initialize audio engine (called automatically in setup())
inline void initAudio() {
    AudioEngine::getInstance().init();
}

// Shutdown audio engine
inline void shutdownAudio() {
    AudioEngine::getInstance().shutdown();
}

// FFT analysis: Get latest audio samples
inline size_t getAudioAnalysisBuffer(float* outBuffer, size_t numSamples) {
    return AudioEngine::getInstance().getAnalysisBuffer(outBuffer, numSamples);
}

// ---------------------------------------------------------------------------
// Microphone Input (miniaudio-based)
// ---------------------------------------------------------------------------

class MicInput {
public:
    static constexpr int BUFFER_SIZE = 4096;  // Ring buffer size
    static constexpr int DEFAULT_SAMPLE_RATE = 44100;

    MicInput() = default;
    ~MicInput();

    // Initialize (open microphone device)
    bool start(int sampleRate = DEFAULT_SAMPLE_RATE);

    // Stop
    void stop();

    // Get latest samples
    // numSamples: Number of samples to get (max BUFFER_SIZE)
    // Returns: Actual number of samples retrieved
    size_t getBuffer(float* outBuffer, size_t numSamples);

    // State
    bool isRunning() const { return running_; }
    int getSampleRate() const { return sampleRate_; }

    // Callback (internal use)
    void onAudioData(const float* input, size_t frameCount);

private:
    void* device_ = nullptr;  // ma_device*
    bool running_ = false;
    int sampleRate_ = DEFAULT_SAMPLE_RATE;

    // Ring buffer
    std::vector<float> buffer_;
    size_t writePos_ = 0;
    std::mutex mutex_;
};

// Global MicInput instance (singleton-like usage)
MicInput& getMicInput();

// Get latest samples from microphone input
inline size_t getMicAnalysisBuffer(float* outBuffer, size_t numSamples) {
    return getMicInput().getBuffer(outBuffer, numSamples);
}

} // namespace trussc

namespace tc = trussc;
