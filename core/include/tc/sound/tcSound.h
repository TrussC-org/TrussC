#pragma once
#include "tc/utils/tcAnnotations.h"
#include "tc/utils/tcFileIO.h"   // fs alias + path boundary helpers
#include "tc/utils/tcLoadResult.h"

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
#include "../../tcMath.h"
#include "../events/tcEvent.h"
#include "../utils/tcLog.h"

namespace trussc {


// ---------------------------------------------------------------------------
// SoundSource — abstract base for anything Sound::play() can consume.
//
// Two concrete subclasses:
//   - SoundBuffer (eager): full decoded PCM in memory.
//   - SoundStream (streaming): file kept open, decoded on demand by a
//     worker thread into a per-instance ring buffer.
//
// The `kind_` enum lets the audio mixer dispatch on type without a
// virtual call per frame. Per-block work (channels / sampleRate /
// getDuration) is fine with virtuals.
// ---------------------------------------------------------------------------
class SoundSource {
public:
    enum Kind { Eager, Stream };

    int channels = 0;
    int sampleRate = 0;

    SoundSource() = default;
    explicit SoundSource(Kind k) : kind_(k) {}
    virtual ~SoundSource() = default;

    Kind kind() const { return kind_; }

    // Duration in seconds. For streams this is the decoded file's
    // duration (queried from ma_decoder at loadStream time); for buffers
    // it's `numSamples / sampleRate`.
    virtual float getDuration() const = 0;

protected:
    Kind kind_ = Eager;
};


// ---------------------------------------------------------------------------
// Sound Buffer (decoded data)
// ---------------------------------------------------------------------------
class SoundBuffer : public SoundSource {
public:
    SoundBuffer() : SoundSource(SoundSource::Eager) {}
    std::vector<float> samples;  // Interleaved samples
    // channels and sampleRate are inherited from SoundSource.
    size_t numSamples = 0;       // Samples per channel

    // File-based decoders (implemented in tcSound_impl.cpp).
    // WAV / MP3 / FLAC go through ma_decoder (miniaudio); OGG goes through
    // stb_vorbis directly because miniaudio does not bundle a Vorbis decoder.
    LoadResult loadOgg(const fs::path& path);
    LoadResult loadWav(const fs::path& path);
    LoadResult loadMp3(const fs::path& path);
    LoadResult loadFlac(const fs::path& path);

    // Auto-detect entry point. Dispatches to the format-specific loader
    // based on the file's extension (.wav / .mp3 / .ogg / .flac / .aac /
    // .m4a — case-insensitive).
    LoadResult load(const fs::path& path);

    // Memory-based decoders. Format must be known (no extension to sniff).
    LoadResult loadWavFromMemory(const void* data, size_t dataSize);
    LoadResult loadMp3FromMemory(const void* data, size_t dataSize);
    LoadResult loadFlacFromMemory(const void* data, size_t dataSize);
    LoadResult loadOggFromMemory(const void* data, size_t dataSize);

    // Load AAC/M4A file (platform-specific implementation)
    // Fails on unsupported platforms
    TC_PLATFORMS("macos,windows,linux,ios,web") LoadResult loadAac(const fs::path& path);

    // Load AAC data from memory (platform-specific implementation)
    // Fails on unsupported platforms
    TC_PLATFORMS("macos,windows,linux,ios,web") LoadResult loadAacFromMemory(const void* data, size_t dataSize);

    // -------------------------------------------------------------------------
    // ADTS header utilities (for raw AAC from MOV containers)
    // -------------------------------------------------------------------------
    // Get ADTS sample rate index
    static int getAdtsSampleRateIndex(int sampleRate) {
        static const int rates[] = {96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350};
        for (int i = 0; i < 13; i++) {
            if (rates[i] == sampleRate) return i;
        }
        return 4; // Default to 44100
    }

    // Create 7-byte ADTS header for one AAC frame
    // frameLength: size of raw AAC frame data (without header)
    // profile: AAC profile (2 = AAC-LC, which is most common)
    static void createAdtsHeader(uint8_t* header, int frameLength, int sampleRate, int channels, int profile = 2) {
        int sampleRateIndex = getAdtsSampleRateIndex(sampleRate);
        int channelConfig = channels;
        int fullLength = frameLength + 7; // ADTS header is 7 bytes
        int adtsProfile = (profile > 0) ? (profile - 1) : 1; // ADTS uses profile-1

        header[0] = 0xFF; // Syncword high
        header[1] = 0xF1; // Syncword low (4) + ID (0) + Layer (00) + Protection absent (1)
        header[2] = ((adtsProfile & 0x03) << 6) | ((sampleRateIndex & 0x0F) << 2) | ((channelConfig >> 2) & 0x01);
        header[3] = ((channelConfig & 0x03) << 6) | ((fullLength >> 11) & 0x03);
        header[4] = (fullLength >> 3) & 0xFF;
        header[5] = ((fullLength & 0x07) << 5) | 0x1F; // Buffer fullness high (0x7FF)
        header[6] = 0xFC; // Buffer fullness low + frames - 1
    }

#ifdef __EMSCRIPTEN__
    // Web platform: complete deferred AAC loading (blocking)
    // Called from Sound::play() if loading was deferred during setup
    void ensureAacLoaded();

    // Check if this buffer has deferred AAC loading
    bool hasDeferredAac() const { return !deferredAacPath_.empty(); }

    std::string deferredAacPath_;  // Path for deferred AAC loading (Web only)
#endif

    // Load raw PCM data (16-bit signed, little-endian)
    LoadResult loadPcmFromMemory(const void* data, size_t dataSize,
                                 int numChannels, int rate, int bitsPerSample = 16,
                                 bool bigEndian = false) {
        if (bitsPerSample != 16 && bitsPerSample != 32) {
            printf("SoundBuffer: unsupported bits per sample: %d\n", bitsPerSample);
            return LoadResult::fail(LoadError::UnsupportedFormat,
                                    "unsupported bits per sample: " + std::to_string(bitsPerSample));
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

        return LoadResult::success();
    }

    float getDuration() const override {
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

        for (size_t i = 0; i < numSamples; i++) {
            float t = (float)i / sampleRate;
            samples[i] = volume * std::sin(TAU * frequency * t);
        }
    }

    void generateSquareWave(float frequency, float duration, float volume = 0.5f, int sr = 44100) {
        sampleRate = sr;
        channels = 1;
        numSamples = (size_t)(duration * sampleRate);
        samples.resize(numSamples);

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
// SoundStream — streaming source. File stays open; samples are decoded on
// demand by the engine's StreamWorker thread into a per-PlayingSound ring
// buffer. Use when the file is too large to decode into RAM up front
// (multi-minute BGM, podcasts) — a few-hundred-KB working set per voice
// instead of full PCM.
//
// One SoundStream describes the source; per-voice decoder + ring buffer
// state lives in StreamInstance (declared in tcAudio_impl.cpp because it
// includes miniaudio types). maxPolyphony controls how many concurrent
// `play()` instances are allowed before the engine recycles the slot.
//
// Constraints (vs eager SoundBuffer):
//   - setSpeed() is treated as 1.0 (no resampling on the fly — decoder
//     outputs engine-rate frames).
//   - setPosition() seeks the decoder and re-fills the ring buffer
//     (~10 ms blackout, similar tradeoff to other engines).
//   - Each polyphony slot costs one open file handle + one decoder +
//     one ring buffer (default ~16 KB).
// ---------------------------------------------------------------------------
// Per-voice decoder + ring-buffer state. Full definition lives in
// tcAudio_impl.cpp (where miniaudio's headers are visible).
namespace internal { struct StreamInstance; }

class SoundStream : public SoundSource {
public:
    SoundStream() : SoundSource(SoundSource::Stream) {}
    ~SoundStream() override = default;

    // Open the file, validate format, populate channels / sampleRate /
    // duration. Decoders for individual voices are opened later by the
    // engine when play() is called. Fails if the file can't be
    // opened or the format is unsupported. Format is detected from
    // extension (.wav .mp3 .flac .ogg — same as SoundBuffer::load).
    LoadResult loadStream(const fs::path& path, int maxPolyphony = 1);

    float getDuration() const override { return duration_; }

    fs::path getPath() const { return path_; }
    int getMaxPolyphony() const { return maxPolyphony_; }

private:
    fs::path path_;
    int maxPolyphony_ = 1;
    int encodingFormatHint_ = 0;  // ma_encoding_format value, stored as int
                                  // to avoid pulling miniaudio.h into the header.
    float duration_ = 0.0f;

    friend struct internal::StreamInstance;
    friend class AudioEngine;
};

// ---------------------------------------------------------------------------
// Per-PlayingSound stream state. Owns a miniaudio decoder and a ring
// buffer fed by StreamWorker. Mixer reads from `ring`. Declared as a
// forward declaration here; full definition is in tcAudio_impl.cpp
// where miniaudio's headers are visible (see internal::StreamInstance
// forward-declared above).
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// MixMode — high-level routing preset for how a Sound's source channels
// fan out to the device's output channels. Set per-Sound via
// Sound::setMixMode(). Default = Auto.
//
//   Auto         — mono source broadcasts to all device channels;
//                  multi-channel source routes 1:1 to matching device
//                  channels with truncation (extra device channels stay
//                  silent, extra source channels are dropped).
//   DownmixMono  — sum all source channels / N → broadcast to every
//                  device channel. Useful for previewing a multichannel
//                  file on a stereo speaker, or for "play this through
//                  every speaker" notification sounds.
//
// Sound::setChannelMap() overrides MixMode if the map is non-empty —
// the map is the source of truth for routing. setChannelGains() is
// orthogonal (per-output-channel multiplier).
// ---------------------------------------------------------------------------
enum class MixMode {
    Auto = 0,
    DownmixMono = 1,
};

// ---------------------------------------------------------------------------
// Atomic shared_ptr shim
//
// PlayingSound stores routing snapshots (channelMap / channelGains) as
// shared_ptr that the UI thread updates and the audio thread reads. We'd
// like to use the C++20 std::atomic<std::shared_ptr<T>> specialization,
// but Apple libc++ doesn't ship it yet (verified 2026-05). We fall back
// to the (C++20-deprecated) std::atomic_load / std::atomic_store free
// functions, suppressing the deprecation warning locally — when the
// specialization lands the storage type and accessors auto-switch.
// ---------------------------------------------------------------------------
namespace internal {

#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
    // Native C++20 specialization path — lock-free where supported,
    // no deprecation warnings.
    template<class T>
    using AtomicSharedPtr = std::atomic<std::shared_ptr<T>>;

    template<class T>
    inline std::shared_ptr<T> sharedLoad(const AtomicSharedPtr<T>& p) {
        return p.load(std::memory_order_acquire);
    }
    template<class T>
    inline void sharedStore(AtomicSharedPtr<T>& p, std::shared_ptr<T> v) {
        p.store(std::move(v), std::memory_order_release);
    }
#else
    // Fallback: a plain shared_ptr accessed via the deprecated free-
    // function atomic API. Still lock-free for shared_ptr on common
    // platforms; the deprecation is for ergonomics only.
    template<class T>
    using AtomicSharedPtr = std::shared_ptr<T>;

    // Use the *_explicit forms with matching acquire/release ordering so
    // this path is symmetric with the C++20 specialization branch above
    // — without the explicit, the free functions default to seq_cst and
    // we'd silently take a stronger fence on Apple while GCC / MSVC ran
    // with the weaker order. Identical observable behavior for our 1
    // producer (UI) / 1 consumer (audio) usage, but keeps the two
    // branches honest.
    template<class T>
    inline std::shared_ptr<T> sharedLoad(const std::shared_ptr<T>& p) {
#       if defined(__clang__) || defined(__GNUC__)
#           pragma GCC diagnostic push
#           pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#       elif defined(_MSC_VER)
#           pragma warning(push)
#           pragma warning(disable: 4996)
#       endif
        return std::atomic_load_explicit(&p, std::memory_order_acquire);
#       if defined(__clang__) || defined(__GNUC__)
#           pragma GCC diagnostic pop
#       elif defined(_MSC_VER)
#           pragma warning(pop)
#       endif
    }
    template<class T>
    inline void sharedStore(std::shared_ptr<T>& p, std::shared_ptr<T> v) {
#       if defined(__clang__) || defined(__GNUC__)
#           pragma GCC diagnostic push
#           pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#       elif defined(_MSC_VER)
#           pragma warning(push)
#           pragma warning(disable: 4996)
#       endif
        std::atomic_store_explicit(&p, std::move(v), std::memory_order_release);
#       if defined(__clang__) || defined(__GNUC__)
#           pragma GCC diagnostic pop
#       elif defined(_MSC_VER)
#           pragma warning(pop)
#       endif
    }
#endif

} // namespace internal

// ---------------------------------------------------------------------------
// Playing Sound Instance
// ---------------------------------------------------------------------------
struct PlayingSound {
    // Polymorphic — either SoundBuffer (eager) or SoundStream (streaming).
    // Dispatch in the mixer is by kind() to avoid a vtable lookup per
    // frame. Field name kept as `buffer` for backward compatibility with
    // tcxLua bindings; the type is now the wider SoundSource.
    std::shared_ptr<SoundSource> buffer;

    // Per-instance streaming state. Null for eager voices; allocated by
    // AudioEngine::play() when buffer->kind() == Stream.
    std::shared_ptr<internal::StreamInstance> stream;

    std::atomic<float> volume{1.0f};
    std::atomic<float> pan{0.0f};        // -1.0 (left) ~ 0.0 (center) ~ 1.0 (right)
    std::atomic<float> speed{1.0f};      // 0.5 (half speed) ~ 1.0 (normal) ~ 2.0 (double speed)
    std::atomic<bool> loop{false};
    std::atomic<bool> playing{false};
    std::atomic<bool> paused{false};

    // Routing. mixMode is a plain atomic int (enum value). channelMap /
    // channelGains are atomically-updatable shared_ptr — the UI thread
    // installs new versions via internal::sharedStore and the
    // audio thread reads via internal::sharedLoad each callback.
    // The shim type auto-switches between C++20
    // std::atomic<std::shared_ptr<T>> and the deprecated free-function
    // atomic API based on __cpp_lib_atomic_shared_ptr.
    //
    // null map  → use mixMode rules
    // non-null  → map is the source of truth
    // gains entries beyond .size() default to 1.0
    std::atomic<int> mixMode{(int)MixMode::Auto};
    internal::AtomicSharedPtr<const std::vector<std::vector<int>>> channelMap;
    internal::AtomicSharedPtr<const std::vector<float>>            channelGains;

    // Playback position (floating-point for speed adjustment)
    double positionF{0.0};

    // Buffer-to-engine sample-rate ratio, set when the sound is queued for
    // playback (buffer->sampleRate / AudioEngine::getInstance().getSampleRate()).
    // Each output frame advances positionF by `speed * rateRatio` so a buffer
    // recorded at a different rate than the engine plays at the correct
    // pitch. The user-facing `speed` field stays semantically "1.0 =
    // natural pitch", independent of the engine rate.
    float rateRatio{1.0f};
};

// ---------------------------------------------------------------------------
// AudioSettings — configuration for AudioEngine::init().
//
// Pass to AudioEngine::init(settings) before any Sound::load() / play()
// call to override the engine defaults (sample rate, channel count,
// device, polyphony).
//
// Once init() succeeds, the engine is locked in — calling init() again
// with different settings is currently a no-op + warning (avoiding the
// disruption of tearing down a running device while sounds are playing).
//
// Empty `deviceName` selects the system default playback device.
// Use AudioEngine::listDevices() to enumerate available device names.
// ---------------------------------------------------------------------------
struct AudioSettings {
    int sampleRate   = 96000;   // engine output sample rate (Hz)
    int channels     = 2;       // engine output channel count (1 = mono, 2 = stereo)
    int bufferSize   = 0;       // requested device buffer size in frames; 0 = let miniaudio choose
    int maxPolyphony = 32;      // max simultaneously-playing Sound voices
    std::string deviceName;     // playback device name; empty = system default
};

// ---------------------------------------------------------------------------
// AudioDeviceInfo — entry in the list returned by AudioEngine::listDevices().
// ---------------------------------------------------------------------------
struct AudioDeviceInfo {
    std::string name;           // device name (pass to AudioSettings::deviceName)
    bool isDefault = false;     // true if this is the system default playback device
};

// ---------------------------------------------------------------------------
// AudioDeviceChangedArgs — fired via AudioEngine::audioDeviceChanged at the
// end of every successful init() call (initial init AND re-init).
//
// `deviceName` is the actual device that was opened. If the caller passed
// an empty AudioSettings::deviceName to request the system default, this
// field still reports the resolved device's real name — listeners never
// see an empty string here.
//
// `isDefaultDevice` is true when the opened device is the OS's current
// default playback device (regardless of whether the caller asked for it
// by name or implicitly by passing empty deviceName).
// ---------------------------------------------------------------------------
struct AudioDeviceChangedArgs {
    std::string deviceName;     // actual device name now active
    bool        isDefaultDevice = false;
    int         sampleRate = 0;
    int         channels = 0;
    int         bufferSize = 0;
    int         maxPolyphony = 0;
};

// ---------------------------------------------------------------------------
// AudioOutBuffer / AudioInBuffer — argument types for the audioOut /
// audioIn event listeners. Buffer is interleaved float, frameCount frames
// of `channels` floats each. Listeners on `AudioEngine::audioOut` should
// ADD their contribution to `data` (mixer already wrote pre-existing
// Sound voices into it); zeroing or overwriting silences other voices.
//
// framePosition is a monotonic count of output frames emitted by the
// engine since init(). Useful as a phase / time reference inside the
// callback (sample-accurate, independent of wall clock).
//
// IMPORTANT: these structs are passed by reference to a callback that
// runs on the audio thread. Do NOT call AudioEngine::play() / load() /
// other engine APIs from inside the listener — they may deadlock or
// be RT-unsafe. Just fill / process the buffer and return quickly.
// ---------------------------------------------------------------------------
struct AudioOutBuffer {
    float*   data;            // interleaved mutable, frameCount * channels samples
    int      frameCount;
    int      channels;
    int      sampleRate;      // engine output sample rate
    uint64_t framePosition;   // monotonic frame counter since engine init
};

struct AudioInBuffer {
    const float* data;        // interleaved read-only mic input
    int          frameCount;
    int          channels;
    int          sampleRate;
    uint64_t     framePosition;
};

// ---------------------------------------------------------------------------
// Audio Engine (singleton, miniaudio-based)
// ---------------------------------------------------------------------------
class AudioEngine {
public:
    // FFT analysis buffer is internal-only and unaffected by AudioSettings.
    static constexpr int ANALYSIS_BUFFER_SIZE = 4096;

    // Default values used when init() is called without an explicit
    // AudioSettings, and as initial values for the runtime fields. 48 kHz
    // is the de-facto pro/video/web standard (DAWs, Web Audio, modern OS
    // mixers, game engines all default to 48k), and avoids extra resampling
    // on the way out of the engine. Use init({.sampleRate = 96000}) to opt
    // into a higher rate when needed.
    static constexpr int DEFAULT_SAMPLE_RATE = 48000;
    static constexpr int DEFAULT_CHANNELS = 2;
    static constexpr int DEFAULT_MAX_PLAYING_SOUNDS = 32;
    static constexpr int DEFAULT_BUFFER_SIZE = 0;  // 0 = let miniaudio choose

    static AudioEngine& getInstance() {
        static AudioEngine instance;
        return instance;
    }

    // Initialize and shutdown (implementation in tcAudio_impl.cpp).
    //
    // init() with no arguments uses the defaults (DEFAULT_SAMPLE_RATE etc.).
    // init(settings) writes the runtime config from `settings`. If the
    // engine is already running, init returns true immediately with a
    // warning (silent re-init would tear down playing voices).
    bool init();
    bool init(const AudioSettings& settings);
    void shutdown();

    // Enumerate available playback devices. Names from this list are
    // suitable for AudioSettings::deviceName. Returns an empty vector if
    // device enumeration is unsupported on the current platform.
    static std::vector<AudioDeviceInfo> listDevices();

    // Runtime engine configuration accessors. These reflect the values
    // passed to init(AudioSettings) — or the defaults if init() was called
    // without an argument. They return the default even before init() is
    // called, so video / audio code that needs the rate up front can rely
    // on the value being sensible.
    int getSampleRate()   const { return sampleRate_; }
    int getChannels()     const { return channels_; }
    int getMaxPolyphony() const { return (int)playingSounds_.size(); }
    int getBufferSize()   const { return bufferSize_; }
    bool isInitialized()  const { return initialized_; }

    // Real-time audio listeners. audioOut fires once per audio device
    // callback AFTER all Sound voices have been mixed into the output
    // buffer; listeners should ADD their contribution. audioIn fires
    // when microphone input is available (only when a capture device is
    // actually running — currently provided by tc::MicInput, but the
    // event channel is here so both paths can use the same wiring).
    //
    // Listeners run on the audio thread with NO engine lock held. Do
    // not call AudioEngine::play() / Sound::load() / etc. from inside
    // them — these are RT-unsafe and can deadlock.
    //
    // Typical use (synthesis):
    //   AudioEngine::getInstance().audioOut.listen([](AudioOutBuffer& b){
    //       for (int i = 0; i < b.frameCount; i++) ...
    //   });
    Event<AudioOutBuffer> audioOut;
    Event<AudioInBuffer>  audioIn;

    // Fired on every successful init() — both the initial startup and any
    // subsequent live re-init. The args carry the new device's real name
    // (never empty), whether it's the system default, and the current
    // sampleRate / channels / bufferSize / maxPolyphony. Listeners run on
    // the thread that called init() (typically main), not the audio
    // thread, so it's safe to call Sound::setChannelMap / setVolume / etc.
    // from inside.
    Event<AudioDeviceChangedArgs> audioDeviceChanged;

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

    // Add new playback instance. Accepts any SoundSource — eager
    // SoundBuffer or streaming SoundStream. For streams, also allocates a
    // StreamInstance (decoder + ring buffer) and registers it with the
    // StreamWorker. Implementation lives in tcAudio_impl.cpp so the
    // streaming branch can see miniaudio types.
    std::shared_ptr<PlayingSound> play(std::shared_ptr<SoundSource> source);

    // Backward-compat overload — most callers pass shared_ptr<SoundBuffer>
    // directly, and we don't want to force them through an explicit
    // upcast.
    std::shared_ptr<PlayingSound> play(std::shared_ptr<SoundBuffer> buffer) {
        return play(std::static_pointer_cast<SoundSource>(buffer));
    }

    // Called from audio callback (internal use)
    void mixAudio(float* buffer, int num_frames, int num_channels);

private:
    AudioEngine() {
        playingSounds_.resize(DEFAULT_MAX_PLAYING_SOUNDS);
        analysisBuffer_.resize(ANALYSIS_BUFFER_SIZE, 0.0f);
    }

    ~AudioEngine() {
        shutdown();
    }

    // Eager mix path: linear interpolation over a fully-decoded SoundBuffer.
    //
    // Supports the full setSpeed range (currently [-10, 10]):
    //   - speed > 0: forward playback (1.0 = natural pitch at engine rate).
    //   - speed = 0: posF stays put, same sample emitted = freeze.
    //   - speed < 0: reverse playback, posF decreases toward 0 / wraps.
    //
    // Channel routing:
    //   - sound.channelMap   non-null/non-empty: out[c] = sum of src[s]
    //                        for s in channelMap[c]. c >= map.size() = silent.
    //   - sound.channelMap   null/empty + mixMode == DownmixMono: average
    //                        all src ch → broadcast to all output ch.
    //   - sound.channelMap   null/empty + mixMode == Auto: mono src
    //                        broadcasts; multi-ch src routes 1:1 to
    //                        matching output ch with truncation.
    //   - sound.channelGains entries scale per-output-ch (default 1.0).
    //   - pan multiplier applies to ch0/ch1 only (legacy stereo balance).
    //
    // Bounds are resolved at the top of each iteration so posF never reaches
    // the indexing step with a negative or out-of-range value (size_t cast
    // of a negative double is UB).
    static void mixEagerVoice(PlayingSound& sound, const SoundBuffer& src,
                              float* buffer, int num_frames, int num_channels) {
        double posF = sound.positionF;
        float vol = sound.volume;
        float pan = sound.pan;
        float speed = sound.speed;
        double posStep = (double)speed * (double)sound.rateRatio;
        double srcLen = (double)src.numSamples;

        // pan = -1: full left, 0: center, +1: full right
        float panL = (pan <= 0.0f) ? 1.0f : (1.0f - pan);
        float panR = (pan >= 0.0f) ? 1.0f : (1.0f + pan);

        // Snapshot routing state once at the top of the callback. Audio
        // thread reads atomically so the UI thread's setChannelMap store
        // sees a happens-before edge.
        auto map = internal::sharedLoad(sound.channelMap);
        auto gains = internal::sharedLoad(sound.channelGains);
        int mm = sound.mixMode.load(std::memory_order_acquire);
        const int srcCh = src.channels;
        const int mapSize = map ? (int)map->size() : 0;
        const int gainsSize = gains ? (int)gains->size() : 0;

        for (int frame = 0; frame < num_frames; frame++) {
            // Resolve bounds for both directions BEFORE indexing.
            if (posF < 0.0) {
                if (sound.loop) {
                    posF += srcLen;
                    // Defensive: very large negative step could still be < 0.
                    if (posF < 0.0) posF = std::fmod(posF, srcLen) + srcLen;
                } else {
                    sound.playing = false;
                    break;
                }
            }
            if (posF >= srcLen) {
                if (sound.loop) {
                    posF -= srcLen;
                    if (posF >= srcLen) posF = std::fmod(posF, srcLen);
                } else {
                    sound.playing = false;
                    break;
                }
            }

            size_t pos0 = (size_t)posF;
            size_t pos1 = pos0 + 1;
            float frac = (float)(posF - (double)pos0);

            if (pos1 >= src.numSamples) {
                pos1 = sound.loop ? 0 : pos0;
            }

            // Pull an interpolated sample for source channel s. Handles
            // mono / multi-ch indexing uniformly via stride = srcCh.
            auto srcAt = [&](int s) -> float {
                if (s < 0 || s >= srcCh) return 0.0f;
                float a = src.samples[pos0 * (size_t)srcCh + (size_t)s];
                float b = src.samples[pos1 * (size_t)srcCh + (size_t)s];
                return a + (b - a) * frac;
            };

            // Per-output-channel routing.
            for (int c = 0; c < num_channels; c++) {
                float sample = 0.0f;

                if (mapSize > 0) {
                    // Explicit map wins. c beyond map.size() = silent.
                    if (c < mapSize) {
                        for (int s : (*map)[c]) sample += srcAt(s);
                    }
                } else if (mm == (int)MixMode::DownmixMono) {
                    // Sum all source channels, then average. Broadcasts
                    // identical mono content to every output channel.
                    for (int s = 0; s < srcCh; s++) sample += srcAt(s);
                    if (srcCh > 0) sample /= (float)srcCh;
                } else {
                    // Auto: mono broadcasts, multi-ch 1:1 with truncation.
                    if (srcCh == 1) {
                        sample = srcAt(0);
                    } else {
                        sample = (c < srcCh) ? srcAt(c) : 0.0f;
                    }
                }

                float gain = (c < gainsSize) ? (*gains)[c] : 1.0f;
                float panMul = (c == 0) ? panL : ((c == 1) ? panR : 1.0f);

                buffer[frame * num_channels + c] += sample * gain * panMul * vol;
            }

            posF += posStep;
        }

        sound.positionF = posF;
    }

    // Streaming mix path: full implementation in tcAudio_impl.cpp where
    // StreamInstance / ma_decoder types are visible. Declared here, body
    // is out-of-line.
    static void mixStreamVoice(PlayingSound& sound, SoundStream& src,
                               float* buffer, int num_frames, int num_channels);

    // Re-init helper: rate-adjust active voices so they keep playing from
    // the same point in time after the engine restarts at a new sample
    // rate. Eager voices just recompute rateRatio. Streaming voices need
    // their per-voice decoder rebuilt at the new rate + seeked to the
    // current playback position; the ring is cleared so the worker
    // refills it with samples at the new rate. Implementation lives in
    // tcAudio_impl.cpp (miniaudio types). Called with mutex_ NOT held —
    // the device is already stopped, so no audio callback can race.
    void migrateVoicesToNewRate(int oldRate, int newRate);

    void mixAudioInternal(float* buffer, int num_frames, int num_channels) {
        // Clear buffer
        std::memset(buffer, 0, num_frames * num_channels * sizeof(float));

        // Mix all live Sound voices under the engine lock. The lock guards
        // the playingSounds_ slot list against concurrent play() — without
        // it a play() that swaps a slot mid-iteration would crash.
        {
            std::lock_guard<std::mutex> lock(mutex_);

            for (auto& sound : playingSounds_) {
                if (!sound || !sound->playing || sound->paused) continue;
                if (!sound->buffer) continue;

                // Dispatch on source kind. Eager path is the common case
                // and stays inline-friendly; streaming path lives in the
                // impl TU.
                if (sound->buffer->kind() == SoundSource::Eager) {
                    mixEagerVoice(*sound,
                                  *static_cast<SoundBuffer*>(sound->buffer.get()),
                                  buffer, num_frames, num_channels);
                } else {
                    mixStreamVoice(*sound,
                                   *static_cast<SoundStream*>(sound->buffer.get()),
                                   buffer, num_frames, num_channels);
                }
            }
        }

        // audioOut listeners run AFTER Sound voices and OUTSIDE the lock.
        // The lock is released first so a listener that calls
        // engine.play() doesn't deadlock — that's a discouraged but
        // possible foot-gun. Listeners read/write only the buffer and
        // their own captured state, so they don't need the engine lock.
        if (audioOut.listenerCount() > 0) {
            AudioOutBuffer ob;
            ob.data          = buffer;
            ob.frameCount    = num_frames;
            ob.channels      = num_channels;
            ob.sampleRate    = sampleRate_;
            ob.framePosition = framePosition_;
            audioOut.notify(ob);
        }
        framePosition_ += (uint64_t)num_frames;

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

    void* device_ = nullptr;   // ma_device*
    void* context_ = nullptr;  // ma_context* — persistent across re-inits so
                               // CoreAudio internal state stays consistent
                               // when devices are torn down + recreated.
    bool initialized_ = false;
    std::vector<std::shared_ptr<PlayingSound>> playingSounds_;
    std::mutex mutex_;

    // Runtime engine configuration. Initialized to defaults; replaced when
    // init(AudioSettings) succeeds. Reading these before init() returns the
    // defaults (intentional — code that needs the rate up front, e.g. video
    // resampler setup in tcVideoPlayer_*, can pull the value without first
    // forcing engine startup).
    int sampleRate_ = DEFAULT_SAMPLE_RATE;
    int channels_   = DEFAULT_CHANNELS;
    int bufferSize_ = DEFAULT_BUFFER_SIZE;

    // Monotonic count of output frames emitted since the engine started.
    // Exposed to audioOut listeners via AudioOutBuffer::framePosition so
    // synthesis code has a sample-accurate phase / time reference. Only
    // touched on the audio thread (mixAudioInternal), no atomicity needed.
    uint64_t framePosition_ = 0;

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
    //
    // load(path)              — eager: decode the full file into RAM.
    //                            Best for short SFX and cases that need
    //                            zero-latency play / seek / multi-instance.
    //
    // loadStream(path, n=1)   — streaming: keep the file open and decode
    //                            on demand into a small ring buffer.
    //                            Best for long files (BGM, podcasts) where
    //                            full PCM in RAM is wasteful (multi-MB+).
    //                            `n` (maxPolyphony) reserves N decoder
    //                            slots so up to N concurrent play() calls
    //                            can overlap. Default 1 = single-instance
    //                            (typical BGM); raise for cross-fade or
    //                            layered ambient tracks.
    LoadResult load(const fs::path& path) {
        // Initialize AudioEngine (only once)
        AudioEngine::getInstance().init();

        // Decode into a SoundBuffer, then store as the polymorphic source.
        auto buf = std::make_shared<SoundBuffer>();

        // Determine format by extension
        std::string ext = path.extension().string();
        if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);
        LoadResult result = LoadResult::fail(LoadError::UnsupportedFormat,
                                             "unsupported extension '." + ext + "'");

        if (ext == "ogg" || ext == "OGG") {
            result = buf->loadOgg(path);
        } else if (ext == "wav" || ext == "WAV") {
            result = buf->loadWav(path);
        } else if (ext == "mp3" || ext == "MP3") {
            result = buf->loadMp3(path);
        } else if (ext == "flac" || ext == "FLAC") {
            result = buf->loadFlac(path);
        } else if (ext == "aac" || ext == "AAC" || ext == "m4a" || ext == "M4A") {
            result = buf->loadAac(path);
        } else {
            printf("Sound: unsupported format: %s\n", ext.c_str());
        }

        if (!result) {
            buffer_.reset();
            return result;
        }
        buffer_ = std::move(buf);
        return LoadResult::success();
    }

    // Stream the file from disk instead of loading it all into RAM.
    // maxPolyphony >= 1 reserves that many concurrent voices. See class
    // doc comment above for when to prefer this over load().
    //
    // Limitations vs eager load():
    //   - setSpeed() is ignored (decoder outputs engine-rate frames).
    //   - setPosition() incurs a seek + ring-buffer refill (~10 ms).
    //
    // Web (Emscripten): streaming relies on std::thread + on-disk file I/O,
    // neither of which is available in the default browser build. To keep
    // user apps portable we fall back to eager load() and log a warning so
    // the developer can branch explicitly with isStreaming() / #ifdef
    // __EMSCRIPTEN__ if they need to know.
    TC_PLATFORMS("macos,windows,linux,android,ios") LoadResult loadStream(const fs::path& path, int maxPolyphony = 1) {
        AudioEngine::getInstance().init();
#ifdef __EMSCRIPTEN__
        (void)maxPolyphony;
        logWarning("Sound") << "loadStream() is not supported on Web — "
                               "falling back to eager load() for '" << path << "'. "
                               "Branch on isStreaming() or __EMSCRIPTEN__ if you need "
                               "to handle this explicitly.";
        return load(path);
#else
        auto stream = std::make_shared<SoundStream>();
        LoadResult r = stream->loadStream(path, maxPolyphony);
        if (!r) {
            buffer_.reset();
            return r;
        }
        buffer_ = std::move(stream);
        return LoadResult::success();
#endif
    }

    // For testing: Generate sine wave
    void loadTestTone(float frequency = 440.0f, float duration = 1.0f) {
        AudioEngine::getInstance().init();
        auto buf = std::make_shared<SoundBuffer>();
        buf->generateSineWave(frequency, duration, 0.5f);
        buffer_ = std::move(buf);
    }

    // Load from pre-generated SoundBuffer
    void loadFromBuffer(const SoundBuffer& buf) {
        AudioEngine::getInstance().init();
        buffer_ = std::make_shared<SoundBuffer>(buf);
    }

    void loadFromBuffer(std::shared_ptr<SoundBuffer> buf) {
        AudioEngine::getInstance().init();
        buffer_ = buf;  // upcast SoundBuffer -> SoundSource via shared_ptr conversion
    }

    bool isLoaded() const { return buffer_ != nullptr; }

    // True for streams loaded via loadStream(); false for eager loads.
    bool isStreaming() const {
        return buffer_ && buffer_->kind() == SoundSource::Stream;
    }

    // -------------------------------------------------------------------------
    // Playback Control
    // -------------------------------------------------------------------------
    void play() {
        if (!buffer_) return;

#ifdef __EMSCRIPTEN__
        // Web platform: complete deferred AAC loading if needed.
        // Only eager SoundBuffer can have a deferred AAC payload —
        // SoundStream is decoded incrementally at play time and doesn't
        // use the deferred-load mechanism.
        if (buffer_->kind() == SoundSource::Eager) {
            auto* eager = static_cast<SoundBuffer*>(buffer_.get());
            if (eager->hasDeferredAac()) {
                eager->ensureAacLoaded();
            }
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
            playing_->mixMode.store((int)mixMode_, std::memory_order_release);
            internal::sharedStore(playing_->channelMap,   channelMap_);
            internal::sharedStore(playing_->channelGains, channelGains_);
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

    // Set playback speed.
    //
    // Range:
    //   - Eager voices: [-10, 10]. Negative = reverse playback. Zero = freeze
    //     (same sample emitted, no advance). Tiny positive values down to 0.0
    //     are accepted with no floor — caller is responsible for "0 means
    //     frozen but still alive".
    //   - Streaming voices: [0, 10]. Negative is currently clamped to 0
    //     because reverse streaming would need direction-aware ring fill +
    //     per-chunk reverse, not yet implemented. Zero = freeze.
    //
    // Values outside [-10, 10] are clamped.
    void setSpeed(float speed) {
        if (speed < -10.0f) speed = -10.0f;
        if (speed >  10.0f) speed =  10.0f;
        // Streams don't support reverse yet — clamp away the negative half.
        if (buffer_ && buffer_->kind() == SoundSource::Stream && speed < 0.0f) {
            speed = 0.0f;
        }
        speed_ = speed;
        if (playing_) {
            playing_->speed = speed_;
        }
    }

    float getSpeed() const { return speed_; }

    // -------------------------------------------------------------------------
    // Channel routing
    // -------------------------------------------------------------------------
    //
    // Three orthogonal knobs control how this Sound's source channels
    // (N) end up on the audio device's output channels (M):
    //
    //   setMixMode(MixMode)              — Auto / DownmixMono presets
    //   setChannelMap(vector<int>)       — 1:1 routing per output ch
    //   setChannelMap(vector<vector<int>>) — multi-source-per-output (sum)
    //   setChannelGains(vector<float>)   — per-output-channel multiplier
    //
    // Composition at mix time:
    //   sample[c] = (sum of mapped src channels) * channelGains[c]
    //             * pan_multiplier[c] * volume
    //
    //   - channelMap.empty() / null → mixMode rules apply
    //     * Auto: mono src broadcasts to all output ch;
    //             multi-ch src → out[c] = src[c] (c<N), else 0
    //     * DownmixMono: sum all src ch / N → broadcast to all out ch
    //   - channelMap non-empty → that wins
    //     * out[c] = sum of src[s] for s in channelMap[c]
    //     * c >= channelMap.size() → silent
    //   - channelGains[c] defaults to 1.0 for entries beyond .size()
    //   - pan is the existing setPan(float) for stereo balance (ch0+ch1
    //     only); ignored on ch >= 2.
    //
    // All setters are safe to call while the sound is playing.

    void setMixMode(MixMode m) {
        mixMode_ = m;
        if (playing_) {
            playing_->mixMode.store((int)m, std::memory_order_release);
        }
    }

    MixMode getMixMode() const { return mixMode_; }

    // 1:1 channel map. Each entry is a source channel index (-1 = silent).
    // map.size() determines how many output channels we write; outputs
    // beyond that stay silent. Empty map = fall back to mixMode rules.
    void setChannelMap(const std::vector<int>& map) {
        std::vector<std::vector<int>> m2d;
        m2d.reserve(map.size());
        for (int s : map) {
            if (s >= 0) m2d.push_back({s});
            else        m2d.emplace_back();  // empty = silent on this output
        }
        setChannelMap(std::move(m2d));
    }

    // Multi-source-per-output channel map. Each entry lists source channel
    // indices that sum into that output channel. Empty inner vector =
    // silent on that output. Empty outer vector clears any previous map.
    void setChannelMap(std::vector<std::vector<int>> map) {
        auto sp = map.empty()
                ? std::shared_ptr<const std::vector<std::vector<int>>>{}
                : std::make_shared<const std::vector<std::vector<int>>>(std::move(map));
        channelMap_ = sp;
        if (playing_) {
            internal::sharedStore(playing_->channelMap, sp);
        }
    }

    // Get the current channel map. Returns the underlying shared_ptr;
    // empty/null means "no explicit map, mixMode rules apply".
    std::shared_ptr<const std::vector<std::vector<int>>> getChannelMap() const {
        return channelMap_;
    }

    // Per-output-channel gain. Entries beyond .size() default to 1.0.
    // Empty vector clears any previous gains (back to uniform 1.0).
    void setChannelGains(const std::vector<float>& gains) {
        auto sp = gains.empty()
                ? std::shared_ptr<const std::vector<float>>{}
                : std::make_shared<const std::vector<float>>(gains);
        channelGains_ = sp;
        if (playing_) {
            internal::sharedStore(playing_->channelGains, sp);
        }
    }

    std::shared_ptr<const std::vector<float>> getChannelGains() const {
        return channelGains_;
    }

    void clearChannelMap()   { setChannelMap(std::vector<std::vector<int>>{}); }
    void clearChannelGains() { setChannelGains(std::vector<float>{}); }

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
        // For both eager and stream sources positionF is in source-rate
        // frames so the division yields seconds either way.
        return (float)playing_->positionF / buffer_->sampleRate;
    }

    void setPosition(float seconds) {
        if (!playing_ || !buffer_) return;
        double pos = seconds * buffer_->sampleRate;
        if (pos < 0) pos = 0;
        // For eager: clamp to numSamples. For streams: clamp to duration
        // (decoder seek happens lazily in the stream mixer).
        if (buffer_->kind() == SoundSource::Eager) {
            auto* eager = static_cast<const SoundBuffer*>(buffer_.get());
            if (pos >= (double)eager->numSamples) pos = (double)eager->numSamples - 1;
        } else {
            double maxPos = (double)buffer_->getDuration() * buffer_->sampleRate;
            if (pos >= maxPos) pos = maxPos - 1;
        }
        playing_->positionF = pos;
    }

    float getDuration() const {
        return buffer_ ? buffer_->getDuration() : 0;
    }

private:
    std::shared_ptr<SoundSource> buffer_;
    std::shared_ptr<PlayingSound> playing_;
    float   volume_  = 1.0f;
    float   pan_     = 0.0f;
    float   speed_   = 1.0f;
    bool    loop_    = false;
    MixMode mixMode_ = MixMode::Auto;
    std::shared_ptr<const std::vector<std::vector<int>>> channelMap_;
    std::shared_ptr<const std::vector<float>>            channelGains_;
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
