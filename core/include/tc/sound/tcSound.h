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
#include <algorithm>
#include <new>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <atomic>
#include <cstring>
#include <cmath>
#include "../../tcMath.h"
#include "../events/tcEvent.h"
#include "../utils/tcAtomicSharedPtr.h"
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


namespace internal {

// Interleaved sample count of `frames` frames of `channels` channels, for
// sizing SoundBuffer::samples. False when channels < 1 or when the count
// exceeds maxCount (pass samples.max_size()). The product is checked before
// it is formed, so it cannot wrap where size_t is 32-bit (wasm32).
inline bool interleavedSampleCount(uint64_t frames, int channels, size_t maxCount,
                                   size_t& outCount) {
    if (channels < 1) return false;
    if (frames > maxCount / (size_t)channels) return false;
    outCount = (size_t)frames * (size_t)channels;
    return true;
}

// Most decoded samples per byte of encoded input that decodeReserveSamples
// reserves for. The general rate, 16, is 48 kHz stereo down to 48 kbit/s.
// MP3 frames run at 8 kbit/s or more (MPEG-2 / 2.5; MPEG-1 at 32 kbit/s or
// more), so an MP3 decodes to at most 48 samples per byte (24 kHz stereo at
// 8 kbit/s). Vorbis has no such floor; 32 covers its lowest common quality
// settings (about 32 kbit/s for 44.1 kHz stereo). Streams that decode to
// more still load: the buffer grows past the reservation.
constexpr uint64_t kReserveSamplesPerInputByte = 16;
constexpr uint64_t kReserveSamplesPerInputByteMp3 = 48;
constexpr uint64_t kReserveSamplesPerInputByteVorbis = 32;

// Interleaved samples to reserve before decoding a stream whose header states
// `headerFrames` frames of `channels` channels, read from `inputBytes` bytes
// of encoded input (0 when unknown). The stated length is only a hint: the
// reservation is capped by what that much input plausibly decodes to
// (samplesPerInputByte per byte, one of the kReserveSamplesPerInputByte*
// rates) and by maxCount. Decoders append what actually decodes and grow the
// buffer past the reservation when a stream holds more.
inline size_t decodeReserveSamples(uint64_t headerFrames, int channels, uint64_t inputBytes,
                                   uint64_t samplesPerInputByte, size_t maxCount) {
    constexpr uint64_t kMax = ~(uint64_t)0;
    if (channels < 1) return 0;
    const uint64_t ch = (uint64_t)channels;
    const uint64_t fromHeader = headerFrames > kMax / ch ? kMax : headerFrames * ch;
    const uint64_t fromInput =
        samplesPerInputByte != 0 && inputBytes > kMax / samplesPerInputByte
            ? kMax : inputBytes * samplesPerInputByte;
    uint64_t n = fromHeader < fromInput ? fromHeader : fromInput;
    if (n > (uint64_t)maxCount) n = (uint64_t)maxCount;
    return (size_t)n;
}

// Test hook, not a user setting: while nonzero, allocationFits() also
// refuses any single allocation larger than this many bytes, so a headless
// test can run the growth policy below against a memory limit on any
// platform (core/tests/audioDiagnostics). 0, the default, turns it off.
// State lives in tcSound_impl.cpp.
void setAllocationLimitForTests(size_t bytes);
size_t allocationLimitForTests();

// Whether one allocation of `bytes` can be made right now. Web (wasm) builds
// have exception catching off, so there a failed operator new aborts the page
// instead of throwing std::bad_alloc; malloc, which returns null on failure
// under ALLOW_MEMORY_GROWTH, is tried and released first. Elsewhere a failed
// allocation throws and the callers catch it, so this is true unless a test
// set a limit (setAllocationLimitForTests).
inline bool allocationFits(size_t bytes) {
    const size_t limit = allocationLimitForTests();
    if (limit != 0 && bytes > limit) return false;
#ifdef __EMSCRIPTEN__
    // Held in a volatile: the compiler may otherwise drop an unused
    // malloc / free pair and assume the allocation succeeded.
    void* volatile p = std::malloc(bytes);
    if (!p) return false;
    std::free(p);
#endif
    return true;
}

// Grow the capacity of `buf` to hold at least `needed` samples. The new
// capacity is twice the current one (geometric growth), or `preferred` when
// it lies between `needed` and that (a decoder's stated length, so a stream
// whose length is stated correctly ends without spare capacity). Never more
// than twice the current capacity, so the growth follows what was actually
// written.
//
// When that size cannot be allocated, smaller steps are tried, the current
// capacity plus a half, a quarter, an eighth, ... of it, down to the
// smallest one that still holds `needed`, and last `needed` itself; the
// first that can be allocated is taken. On the web a size is checked before
// it is allocated (allocationFits); elsewhere the std::bad_alloc of a failed
// reserve is caught and the next size tried. A smaller step is only taken
// after one at most twice its growth failed, so under a fixed memory limit
// every such step leaves less than half of the room that was left before
// it: near the limit the buffer is reallocated a logarithmic number of
// times, not once per decode step, and a load that cannot finish fails as
// soon as a step no longer fits. (`needed` itself needs no special rule: it
// is only reached when a step at most twice its growth failed, too.)
//
// False, with `buf` unchanged, when even `needed` cannot be allocated.
inline bool growSampleBuffer(std::vector<float>& buf, size_t needed, size_t preferred = 0) {
    const size_t capacity = buf.capacity();
    if (needed <= capacity) return true;
    if (needed > buf.max_size()) return false;
    size_t target = capacity > buf.max_size() / 2 ? buf.max_size() : capacity * 2;
    if (preferred >= needed && preferred < target) target = preferred;
    if (target < needed) target = needed;
    // Reserve `n` samples if that can be allocated. target <= max_size(), and
    // every size tried is at most target, so the byte count cannot wrap. (Web
    // builds do not catch exceptions; there allocationFits decides.)
    auto tryReserve = [&buf](size_t n) {
        if (!allocationFits(n * sizeof(float))) return false;
        try {
            buf.reserve(n);
        } catch (const std::bad_alloc&) {
            return false;
        }
        return true;
    };
    if (tryReserve(target)) return true;
    size_t smallestTried = target;
    for (size_t step = capacity / 2; step > 0 && capacity + step >= needed; step /= 2) {
        // Only sizes below what failed
        if (capacity + step >= target) continue;
        if (tryReserve(capacity + step)) return true;
        smallestTried = capacity + step;
    }
    return smallestTried != needed && tryReserve(needed);
}

} // namespace internal

// ---------------------------------------------------------------------------
// Sound Buffer (decoded data)
// ---------------------------------------------------------------------------
class SoundBuffer : public SoundSource {
public:
    SoundBuffer() : SoundSource(SoundSource::Eager) {}
    std::vector<float> samples;  // Interleaved samples
    // channels and sampleRate are inherited from SoundSource.
    size_t numSamples = 0;       // Samples per channel

    // File the samples were decoded from (set by the path-based loaders;
    // for AAC by load(), which Sound::load() uses). Empty for memory / PCM /
    // generated buffers. Reported per playing sound by
    // AudioEngine::getPlayingSounds() and tc_get_audio_state.
    fs::path getPath() const { return path_; }

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
        // Immutable lookup table (the same in every module's copy)
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

    // Load raw interleaved PCM: 16-bit signed integer or 32-bit float,
    // little-endian unless bigEndian is set. dataSize must be a whole number
    // of frames (bitsPerSample / 8 * numChannels bytes each); anything else
    // fails without touching the buffer.
    LoadResult loadPcmFromMemory(const void* data, size_t dataSize,
                                 int numChannels, int rate, int bitsPerSample = 16,
                                 bool bigEndian = false) {
        if (bitsPerSample != 16 && bitsPerSample != 32) {
            logError("SoundBuffer") << "unsupported bits per sample: " << bitsPerSample;
            return LoadResult::fail(LoadError::UnsupportedFormat,
                                    "unsupported bits per sample: " + std::to_string(bitsPerSample));
        }
        if (numChannels < 1) {
            logError("SoundBuffer") << "invalid PCM channel count: " << numChannels;
            return LoadResult::fail(LoadError::UnsupportedFormat,
                                    "invalid PCM channel count: " + std::to_string(numChannels));
        }
        // Frame size in 64 bits: bytes * channels can exceed a 32-bit size_t.
        const size_t bytesPerSample = (size_t)bitsPerSample / 8;
        const uint64_t frameBytes = (uint64_t)bytesPerSample * (uint64_t)numChannels;
        if ((uint64_t)dataSize % frameBytes != 0) {
            logError("SoundBuffer") << "PCM data size " << dataSize
                                    << " is not a whole number of " << frameBytes << "-byte frames";
            return LoadResult::fail(LoadError::DecodeFailed,
                                    "PCM data size " + std::to_string(dataSize) +
                                    " is not a whole number of " + std::to_string(frameBytes) +
                                    "-byte frames");
        }
        const uint64_t frameCount = (uint64_t)dataSize / frameBytes;
        size_t sampleCount = 0;
        if (!internal::interleavedSampleCount(frameCount, numChannels, samples.max_size(),
                                              sampleCount)) {
            logError("SoundBuffer") << "PCM data too large: " << dataSize << " bytes";
            return LoadResult::fail(LoadError::DecodeFailed,
                                    "PCM data too large: " + std::to_string(dataSize) + " bytes");
        }

        path_.clear();
        channels = numChannels;
        sampleRate = rate;
        numSamples = (size_t)frameCount;

        if (bitsPerSample == 16) {
            // 16-bit signed integer -> float
            samples.resize(sampleCount);

            // Read through memcpy (no alignment assumption on data) and swap
            // on the unsigned value: a shift on a negative int16_t would
            // sign-extend and corrupt the swapped sample.
            const uint8_t* src8 = static_cast<const uint8_t*>(data);
            for (size_t i = 0; i < sampleCount; i++) {
                uint16_t u;
                std::memcpy(&u, src8 + i * 2, sizeof(u));
                if (bigEndian) {
                    u = static_cast<uint16_t>((u >> 8) | (u << 8));
                }
                samples[i] = static_cast<int16_t>(u) / 32768.0f;
            }
        } else {
            // 32-bit float: dataSize == sampleCount * sizeof(float) here
            samples.resize(sampleCount);
            const size_t copyBytes = sampleCount * sizeof(float);
            std::memcpy(samples.data(), data, copyBytes);
            if (bigEndian) {
                // Reverse the bytes of each sample
                for (size_t i = 0; i < sampleCount; i++) {
                    uint32_t u;
                    std::memcpy(&u, &samples[i], sizeof(u));
                    u = (u >> 24) | ((u >> 8) & 0x0000FF00u) |
                        ((u << 8) & 0x00FF0000u) | (u << 24);
                    std::memcpy(&samples[i], &u, sizeof(u));
                }
            }
        }

        logVerbose("SoundBuffer") << "loaded PCM from memory (" << channels << " ch, "
                                  << sampleRate << " Hz, " << numSamples << " samples)";

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
        path_.clear();
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
        path_.clear();
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
        path_.clear();
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
        path_.clear();
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
        path_.clear();
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
        path_.clear();
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
        path_.clear();
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
    // Adds `other` into this buffer, scaled by volume, starting offsetSamples
    // samples per channel in (the unit of numSamples: frames, not interleaved
    // samples). Grows this buffer when `other` runs past its end. Both buffers
    // must have the same channel count; a mismatch, or an end past what a
    // buffer can hold (or what memory allows), is logged and nothing is mixed.
    void mixFrom(const SoundBuffer& other, size_t offsetSamples, float volume = 1.0f) {
        if (other.samples.empty()) return;
        if (channels < 1 || other.channels != channels) {
            logError("SoundBuffer") << "mixFrom: channel counts differ (" << other.channels
                                    << " into " << channels << "), nothing mixed";
            return;
        }
        const size_t ch = (size_t)channels;
        // Whole frames `other` actually holds
        const size_t otherFrames = std::min(other.numSamples, other.samples.size() / ch);
        if (otherFrames == 0) return;

        // End frame and the interleaved size it needs, checked before they are formed
        size_t needed = 0;
        if (offsetSamples > SIZE_MAX - otherFrames ||
            !internal::interleavedSampleCount((uint64_t)(offsetSamples + otherFrames), channels,
                                              samples.max_size(), needed)) {
            logError("SoundBuffer") << "mixFrom: offset " << offsetSamples << " + "
                                    << otherFrames << " frames is past what a buffer holds";
            return;
        }
        const size_t endFrame = offsetSamples + otherFrames;
        if (samples.size() < needed) {
            // growSampleBuffer checks the allocation first on the web, where
            // a failed one aborts instead of throwing
            bool grown = false;
            try {
                grown = internal::growSampleBuffer(samples, needed);
                if (grown) samples.resize(needed, 0.0f);
            } catch (const std::bad_alloc&) {
                grown = false;
            }
            if (!grown) {
                logError("SoundBuffer") << "mixFrom: out of memory growing to " << endFrame
                                        << " frames, nothing mixed";
                return;
            }
        }
        if (numSamples < endFrame) numSamples = endFrame;

        // Mix (add) samples, frame by frame with the channel stride
        float* dst = samples.data() + offsetSamples * ch;
        const float* src = other.samples.data();
        const size_t count = otherFrames * ch;
        for (size_t i = 0; i < count; i++) {
            dst[i] += src[i] * volume;
        }
    }

    // Clip samples to -1.0 ~ 1.0 range
    void clip() {
        for (auto& s : samples) {
            if (s > 1.0f) s = 1.0f;
            else if (s < -1.0f) s = -1.0f;
        }
    }

private:
    fs::path path_;  // see getPath(); cleared by the memory / generator paths
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
//   - setPosition() posts a seek: the StreamWorker seeks the decoder and
//     re-fills the ring buffer, and the audio moves once the mixer
//     reaches the new data (~10 ms blackout, similar tradeoff to other
//     engines; longer on slow storage or for an MP3 several hours long,
//     whose seek table is capped). getPosition() reports the requested
//     target meanwhile. A file whose length is unknown (duration 0, e.g.
//     a FLAC encoded to a pipe) cannot seek, and an engine re-init at
//     another sample rate restarts it from the beginning.
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

// Atomic shared_ptr shim (internal::AtomicSharedPtr / sharedLoad /
// sharedStore): see tc/utils/tcAtomicSharedPtr.h

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
    // std::atomic<std::shared_ptr<T>> and a lock-based fallback based on
    // __cpp_lib_atomic_shared_ptr.
    //
    // null map  → use mixMode rules
    // non-null  → map is the source of truth
    // gains entries beyond .size() default to 1.0
    std::atomic<int> mixMode{(int)MixMode::Auto};
    internal::AtomicSharedPtr<const std::vector<std::vector<int>>> channelMap;
    internal::AtomicSharedPtr<const std::vector<float>>            channelGains;

    // Playback position (floating-point for speed adjustment): source
    // frames for an eager voice, engine-rate frames for a stream. While the
    // device runs, code outside the mixer reads or writes it only under the
    // engine lock (internal::seekVoice() / internal::voicePosition()). A
    // stream voice's seek never writes it directly: the mixer sets it when
    // it reaches the post-seek data.
    double positionF{0.0};

    // Frames per second positionF counts (Sound::getPosition() /
    // setPosition() convert with it). A stream's decoder outputs at the
    // engine rate of the play() that opened it; the re-init migration
    // re-expresses positionF at the new rate and updates this. A voice the
    // migration does not rebuild (it had ended, or its decoder did not
    // reopen) keeps positionF and this at the old rate, so its position in
    // seconds does not change. 0 until play() sets it.
    // internal: set by the engine, do not modify (the trailing underscore
    // keeps it out of the reference and the Lua bindings).
    std::atomic<int> positionRateHz_{0};

    // Buffer-to-engine sample-rate ratio, set when the sound is queued for
    // playback (buffer->sampleRate / AudioEngine::getInstance().getSampleRate()).
    // Each output frame advances positionF by `speed * rateRatio` so a buffer
    // recorded at a different rate than the engine plays at the correct
    // pitch. The user-facing `speed` field stays semantically "1.0 =
    // natural pitch", independent of the engine rate.
    float rateRatio{1.0f};

    // Peak absolute value of this voice's contribution to the mix (after
    // volume / pan / channel gains) over the most recent audio callback.
    // Written by the audio thread, read for diagnostics (tc_get_audio_state).
    std::atomic<float> level{0.0f};
};

// ---------------------------------------------------------------------------
// AudioSettings — configuration for AudioEngine::init().
//
// Pass to AudioEngine::init(settings) before any Sound::load() / play()
// call to override the engine defaults (sample rate, channel count,
// device, polyphony).
//
// Calling init(settings) again on a running engine re-initializes it live:
// the device is reopened with the new settings and playing voices carry on
// from their position (expect a short audible gap while the device is down).
//
// Empty `deviceName` selects the system default playback device.
// Use AudioEngine::listDevices() to enumerate available device names.
// ---------------------------------------------------------------------------
struct AudioSettings {
    int sampleRate   = 0;       // engine output sample rate (Hz);
                                // 0 = AudioEngine::DEFAULT_SAMPLE_RATE (48 kHz)
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
    int         bufferSize = 0;    // period the device runs with, in engine-rate frames (granted by the device)
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
// PlayingSoundInfo — one playing (or paused) sound, as reported by
// AudioEngine::getPlayingSounds(). A snapshot: the values are copied under
// the engine lock, so later changes to the playback are not reflected.
// ---------------------------------------------------------------------------
struct PlayingSoundInfo {
    int         slot = 0;           // playback slot index (0 .. maxPolyphony-1)
    fs::path    path;               // source file as given (same as getPath()); empty for generated / memory buffers
    bool        streaming = false;  // true for SoundStream (loadStream), false for an eager SoundBuffer
    bool        paused = false;
    bool        loop = false;
    float       position = 0.0f;    // playback position in seconds
    float       duration = 0.0f;    // source duration in seconds
    float       volume = 1.0f;
    float       pan = 0.0f;
    float       speed = 1.0f;
    float       level = 0.0f;       // peak of this playback's output in the last callback
                                    // (linear, 1.0 = full scale; 0 while paused)
};

// ---------------------------------------------------------------------------
// AudioStats — engine health counters and meters, as reported by
// AudioEngine::getStats(). Counters are cumulative since the process
// started (they survive re-init); meters describe the recent output.
// ---------------------------------------------------------------------------
struct AudioStats {
    // Plays AudioEngine::play() refused (Sound::play() returned false),
    // in total and by reason.
    uint64_t droppedPlays = 0;
    uint64_t droppedPolyphonyLimit = 0; // every playback slot busy (AudioSettings::maxPolyphony)
    uint64_t droppedStreamLimit = 0;   // the SoundStream's own maxPolyphony reached (copies of one streamed Sound)
    uint64_t droppedDecoderError = 0;  // the stream's file could not be reopened for a new playback
    uint64_t droppedNotRunning = 0;    // no running output device (init failed or engine shut down)

    uint64_t clippedSamples = 0;       // output samples beyond +/-1.0 that were hard-clipped

    // Meters: 0 while no device is running (before init, after shutdown).
    float peak = 0.0f;                 // master output peak over the last ~100 ms (linear, before clipping)
    float rms = 0.0f;                  // master output RMS over the same window
    // Fraction of audio-thread time: mix time / audio time, averaged over
    // ~0.5 s. 1.0 means the callback took as long as the audio it produced.
    float cpuUsage = 0.0f;
    float cpuUsagePeak = 0.0f;         // worst single callback in that window (> 1 = a dropout)
};

// Engine diagnostics state (counters, meters, report timers). Defined in
// tcAudio_impl.cpp so the audio-thread accumulators stay out of this header.
namespace internal {
    struct AudioDiagnostics;
    // Log the dropped plays that were only counted: drops off the main
    // thread, and repeats inside the rate limit. Rate limited. Called once
    // per frame by the app loop on the main thread.
    void pumpAudioDiagnostics();
    // Same, ignoring the rate limit, so no counted drop is left unlogged:
    // the exit paths call it (AudioEngine::shutdown(), runHeadlessApp()).
    // Main thread.
    void flushAudioDiagnostics();

    // Device details for tc_get_audio_state that need miniaudio types.
    struct AudioDeviceReport {
        std::string backend;          // miniaudio backend ("Core Audio", "WASAPI", "PulseAudio", "Null", ...)
        std::string outputDevice;     // name of the open playback device; empty when not running
        bool outputIsDefault = false; // it is the OS default playback device
        int  periodFrames = 0;        // period size the device actually granted
        int  deviceSampleRate = 0;    // device's native format (miniaudio converts when it
        int  deviceChannels = 0;      //   differs from the engine's sampleRate / channels)
        bool enumerated = false;      // the two lists below were filled
        std::vector<AudioDeviceInfo> playbackDevices;
        std::vector<AudioDeviceInfo> captureDevices;
    };
    // `enumerate` also lists the devices, which can take a while on some
    // backends. Main thread; does not initialize the engine.
    AudioDeviceReport audioDeviceReport(bool enumerate);

    // Test hook, not a user setting: AudioEngine, listDevices(),
    // audioDeviceReport() and (native) MicInput open miniaudio's null
    // backend only, a device-less clock that still drives the real mixer
    // callback, so a headless test runs without a sound card. Call it before
    // anything opens an audio context: the engine keeps the context it
    // opened first. State lives in tcAudio_impl.cpp.
    void setNullAudioBackendForTests(bool on);

    // Test hook, not a user setting: AudioRecorder's audio-thread capture
    // calls `hook` with the frame count of every buffer it takes, after
    // copying it into the ring and before handing it to the writer, so a
    // headless test can hold a capture in flight
    // (core/tests/audioListenerTeardown). nullptr, the default, turns it off.
    // State lives in tcAudio_impl.cpp.
    void setAudioRecorderCaptureHookForTests(void (*hook)(int frames));
    void runAudioRecorderCaptureHookForTests(int frames);   // calls the hook, if set

    // Test hook, not a user setting: make the StreamWorker's decoder calls
    // fail, or make the worker skip every stream (Stalls: a worker that falls
    // behind, e.g. on slow storage; seek requests wait too), so a headless
    // test can drive a stream's end-of-stream and seek paths
    // (core/tests/streamSeek). ReadFails reads no frames; ReadFailsWithFrames
    // decodes as usual and then reports an error for every read that
    // returned frames. SeekFails also fails the re-init migration's seek,
    // and ReopenFails fails the migration's decoder open. MixerLags holds
    // the worker back until the mixer, with a seek pending, has read how far
    // the stream's ring is written; the mixer then waits right there (up to
    // 50 ms) until the worker has served the seek and reached the stream's
    // end (an audio thread preempted at that point).
    // Otherwise only the worker's refill is affected, not loadStream() or
    // play(). State lives in tcAudio_impl.cpp.
    enum class StreamFaultForTests { None, ReadFails, ReadFailsWithFrames, SeekFails, Stalls,
                                     ReopenFails, MixerLags };
    void setStreamFaultForTests(StreamFaultForTests fault);

    // Test hook: the number of seek points in the seek table of the stream
    // decoder opened last (by play() or by the re-init migration), 0 when it
    // has none (not an MP3). tcAudio_impl.cpp.
    uint32_t lastStreamSeekPointsForTests();

    // Test hook: the number of passes the StreamWorker has run since the
    // process started (one per wakeup: a notify or the end of its wait), so
    // a headless test can count how often it wakes (core/tests/streamWorkerIdle).
    // tcAudio_impl.cpp.
    uint64_t streamWorkerPassesForTests();

    // Seek a voice (Sound::setPosition()). `frame` counts the voice's
    // positionF units: source frames for an eager voice, engine-rate frames
    // for a stream. An eager voice moves at once (positionF is written
    // under the engine lock). A stream voice only gets a request: the
    // StreamWorker seeks its decoder and refills the ring from the new
    // position, and the mixer, the only writer of the ring's read side and
    // of positionF, moves to it when it reaches that data (~10 ms). Until
    // then the request is pending; a later request replaces it (the last
    // one wins). A stream whose length is unknown ignores it (one warning
    // per voice). Call it from one thread per voice, like the Sound API.
    // tcAudio_impl.cpp.
    void seekVoice(PlayingSound& voice, double frame);

    // The voice's position in positionF units (Sound::getPosition()): the
    // requested target while a stream seek is pending, otherwise positionF,
    // the position the mixer is playing. tcAudio_impl.cpp.
    double voicePosition(const PlayingSound& voice);

    // Stop a voice and release what it holds (Sound::stop(), and the last
    // Sound handle that shares the voice going away): `playing` and `paused`
    // become false, so the engine slot is free for the next play(), and a
    // stream voice gives up its decoder and file (closed on the calling
    // thread, after the engine lock is released). Calling it again is a
    // no-op. tcAudio_impl.cpp.
    void releaseVoice(PlayingSound& voice);

    // The framework's teardown barrier (#256): AudioEngine::waitForAudioCallbacks()
    // without its one-second limit. internal::detachAppAudio() waits here
    // before the framework destroys an App (exit, runHeadlessApp, hot reload,
    // closing a secondary window). A listener that never returns is an app
    // bug; the teardown keeps waiting for it (the app hangs where it can be
    // seen) rather than destroy what the listener may still use. After one
    // second it logs an error, once, and goes on waiting. Returns at once on
    // the audio thread inside a listener. tcAudio_impl.cpp.
    void waitForAudioCallbacksNoTimeout();
}

// ---------------------------------------------------------------------------
// Audio Engine (singleton, miniaudio-based)
// ---------------------------------------------------------------------------
class AudioEngine {
public:
    // FFT analysis buffer is internal-only and unaffected by AudioSettings.
    static constexpr int ANALYSIS_BUFFER_SIZE = 4096;

    // Initial values of the runtime fields: init() with no arguments uses
    // them only until init(settings) is first called (after that it reuses
    // the last settings), and init(settings) picks them for a field given
    // as 0 or less. 48 kHz is the de-facto pro/video/web standard (DAWs,
    // Web Audio, modern OS mixers, game engines all default to 48k), and
    // avoids extra resampling on the way out of the engine. Use
    // init({.sampleRate = 96000}) to opt into a higher rate when needed.
    static constexpr int DEFAULT_SAMPLE_RATE = 48000;
    static constexpr int DEFAULT_CHANNELS = 2;
    static constexpr int DEFAULT_MAX_PLAYING_SOUNDS = 32;
    static constexpr int DEFAULT_BUFFER_SIZE = 0;  // 0 = let miniaudio choose

    // The one engine per process. Defined in tcAudio_impl.cpp, not inline: a
    // hot reload guest on Windows compiles its own copy of every header-inline
    // function, static included, and would run a second engine (#249).
    static AudioEngine& getInstance();

    // Initialize and shutdown (implementation in tcAudio_impl.cpp).
    //
    // init(settings) stores sampleRate, channels, bufferSize and maxPolyphony
    // from `settings` (0 or less picks DEFAULT_*) before it opens the device,
    // so they are kept even when the open fails. init() with no arguments
    // reuses the settings of the last init(settings) call, failed or not
    // (the DEFAULT_* values if there was none), but always opens the system
    // default device: deviceName is not kept. On a running
    // engine it re-initializes live: the device is reopened with the new
    // settings and playing voices move over, keeping their position.
    //
    // With no usable audio backend, miniaudio falls back to its Null
    // backend: init() succeeds on a silent device and logs a warning.
    // Returns false when no output device can be opened (none present, or
    // the requested one refused); the failure is logged
    // through logError("AudioEngine"), naming the requested device, and the
    // engine is left uninitialized. That holds for a re-init too: the
    // running device is closed before the new one is tried, so a failed
    // switch leaves the engine stopped, not on the previous device. init()
    // may be called again later (a device switched on after the app
    // started, or other settings). Each failed try opens the device again
    // and logs again, so retry on a timer (about once a second) or on a
    // user action, not every frame. Sound::load*() also calls init() while
    // the engine is not initialized; play() does not. After a failed
    // init(settings), that implicit init() opens the system default device
    // with those settings, so call init(settings) again before loading
    // sounds if you want the requested device.
    bool init();
    bool init(const AudioSettings& settings);
    void shutdown();

    // Enumerate available playback devices. Names from this list are
    // suitable for AudioSettings::deviceName. Returns an empty vector if
    // device enumeration is unsupported on the current platform.
    static std::vector<AudioDeviceInfo> listDevices();

    // Runtime engine configuration accessors. These reflect the values
    // stored by the last init(AudioSettings) call, whether it succeeded or
    // failed (a zero-arg init() reuses them), or the DEFAULT_* values if
    // init(settings) was never called. They are valid even before init(),
    // so video / audio code that needs the rate up front can rely on the
    // value being sensible.
    int getSampleRate()   const { return sampleRate_; }
    int getChannels()     const { return channels_; }
    int getMaxPolyphony() const { return (int)playingSounds_.size(); }
    int getBufferSize()   const { return bufferSize_; }
    bool isInitialized()  const { return initialized_; }

    // Diagnostics (the tc_get_audio_state MCP tool reports both).
    // getStats() only reads atomics: cheap, callable from any thread.
    // getPlayingSounds() copies the playing sounds under the engine lock:
    // call it from the main thread, never from an audioOut / audioIn listener.
    AudioStats getStats() const;
    std::vector<PlayingSoundInfo> getPlayingSounds() const;

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

    // Teardown barrier for audioOut / audioIn listeners (#256). Returns once
    // every audioOut / audioIn notify that was running when it was called has
    // finished. Event does not wait: when disconnect() returns, the callback
    // may still be running on the audio thread. So an object whose listener
    // touches its members disconnects, then waits here, then lets the members
    // go:
    //
    //   ~Synth() { listener_.disconnect();
    //              AudioEngine::getInstance().waitForAudioCallbacks(); }
    //
    // Do it in the most-derived class (or in cleanup()), not in a base-class
    // destructor, which runs after the derived members are already gone. The
    // App's own audioOut() / audioIn() hooks are handled by the framework:
    // they are detached after cleanup(), and before the App is destroyed
    // (exit, hot reload, closing a secondary window) the framework waits the
    // same way, but without the one-second limit below
    // (internal::waitForAudioCallbacksNoTimeout()).
    //
    //   - Returns at once when no callback is running: the device is stopped
    //     or was never started, or the audio thread is between two buffers.
    //   - Returns at once when called from inside an audioOut / audioIn
    //     listener (the audio thread): waiting there would wait for itself.
    //   - Otherwise waits only for callbacks already running (at most two
    //     back-to-back buffers), not for later ones. Gives up after one
    //     second, logs a warning and returns false: a listener that blocks
    //     that long is stuck (e.g. on a lock the caller holds), and waiting
    //     forever would hang the caller. Returns true otherwise. (The
    //     framework's App teardown does wait forever, see above: there a hang
    //     is better than destroying the App under a running listener.)
    // It waits for every listener running at that moment, not only the
    // caller's: call it without holding a lock that a listener takes.
    bool waitForAudioCallbacks();

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
    // Out of line (tcAudio_impl.cpp): they own the diagnostics state.
    AudioEngine();
    ~AudioEngine();

    // Why AudioEngine::play() refused a play (see AudioStats). The values
    // index the diagnostics arrays in tcAudio_impl.cpp: keep the order.
    enum class DropReason { PolyphonyLimit, StreamLimit, DecoderError, NotRunning };

    // Count a refused play; log it now when on the main thread and not rate
    // limited, otherwise leave it for reportDiagnostics(). `code` is the
    // miniaudio result for DecoderError. tcAudio_impl.cpp.
    void noteDroppedPlay(DropReason reason, const SoundSource* source, int code = 0);

    // Audio thread, once per callback, before the final clamp: master peak /
    // RMS / clipped-sample count. Only atomics and audio-thread-owned
    // accumulators — no locks, no allocation, no logging.
    void meterOutput(const float* buffer, int numFrames, int numChannels);

    // Main thread: log what was only counted (see pumpAudioDiagnostics()).
    // `force` ignores the rate limit (flushAudioDiagnostics()).
    void reportDiagnostics(bool force = false);
    friend void internal::pumpAudioDiagnostics();
    friend void internal::flushAudioDiagnostics();
    friend void internal::waitForAudioCallbacksNoTimeout();
    friend internal::AudioDeviceReport internal::audioDeviceReport(bool);
    friend void internal::seekVoice(PlayingSound&, double);
    friend double internal::voicePosition(const PlayingSound&);
    friend void internal::releaseVoice(PlayingSound&);

    // Zero the output meters, the CPU usage window and every playback's
    // level. Only while no device is running (init(), shutdown()), so the
    // audio thread cannot race it.
    void resetMeters();

    // Mark an audioOut / audioIn notify in flight for waitForAudioCallbacks()
    // (tcAudio_impl.cpp). Audio thread; a thread_local depth and one atomic
    // add each, no lock. beginCallback() returns the slot to pass to
    // endCallback(). audioIn has no engine-side source yet: whatever fires it
    // from the engine must enclose that notify the same way.
    int  beginCallback();
    void endCallback(int slot);

    // Both barriers (tcAudio_impl.cpp): waitForAudioCallbacks() gives up after
    // one second (giveUp), internal::waitForAudioCallbacksNoTimeout() does not.
    bool waitForCallbacks(bool giveUp);

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
        // A buffer with no frames, or with fewer samples than numSamples *
        // channels, has nothing to play: the voice stops, looping or not.
        size_t srcCount = 0;
        if (src.numSamples == 0 ||
            !internal::interleavedSampleCount(src.numSamples, src.channels, src.samples.size(),
                                              srcCount)) {
            sound.playing = false;
            sound.level.store(0.0f, std::memory_order_relaxed);
            return;
        }

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
        float level = 0.0f;  // peak of this voice's contribution (diagnostics)

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

                float out = sample * gain * panMul * vol;
                buffer[frame * num_channels + c] += out;
                float mag = std::fabs(out);
                if (mag > level) level = mag;
            }

            posF += posStep;
        }

        sound.positionF = posF;
        sound.level.store(level, std::memory_order_relaxed);
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
            // In flight for waitForAudioCallbacks(). Must enclose the notify:
            // it is what loads the listener snapshot.
            const int slot = beginCallback();
            audioOut.notify(ob);
            endCallback(slot);
        }
        framePosition_ += (uint64_t)num_frames;

        // Meter the final mix (voices + audioOut listeners) before the clamp,
        // so peak / clipped-sample counts see what the clamp throws away.
        meterOutput(buffer, num_frames, num_channels);

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
    mutable std::mutex mutex_;  // mutable: getPlayingSounds() const locks it

    // Runtime engine configuration. Initialized to defaults; overwritten by
    // every init(AudioSettings) call, success or failure, and reused by a
    // zero-arg init(). Reading these before init() returns the
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

    // Drop counters, output meters, audio-thread CPU usage and the log rate
    // limiter (see getStats(), pumpAudioDiagnostics()).
    std::unique_ptr<internal::AudioDiagnostics> diag_;

    // Callbacks in flight (beginCallback / endCallback), counted in one of two
    // slots picked by the epoch's low bit. waitForAudioCallbacks() advances the
    // epoch so new callbacks count in the other slot, then waits for the old
    // slot to drain, twice (once per slot): it waits only for callbacks that
    // were already running, and a callback that read the epoch just before an
    // advance is still caught. The mutex serializes barriers (the epoch
    // advances of two barriers must not interleave); the audio thread never
    // takes it. Timed, so waitForAudioCallbacks() keeps its one-second limit
    // while a framework teardown holds it waiting for a stuck listener.
    std::atomic<uint32_t> callbackEpoch_{0};
    std::atomic<int>      callbacksInFlight_[2]{};
    std::timed_mutex      callbackBarrierMutex_;
};

namespace internal {
    // The owner token of a voice started by Sound::play(). Sound copies share
    // it (Sound::playing_ aliases it), so the voice is released when the last
    // Sound handle that shares it is destroyed or overwritten.
    struct VoiceOwner {
        std::shared_ptr<PlayingSound> voice;
        explicit VoiceOwner(std::shared_ptr<PlayingSound> v) : voice(std::move(v)) {}
        ~VoiceOwner() { if (voice) releaseVoice(*voice); }
        VoiceOwner(const VoiceOwner&) = delete;
        VoiceOwner& operator=(const VoiceOwner&) = delete;
    };
}

// ---------------------------------------------------------------------------
// Sound Class (user-facing)
// ---------------------------------------------------------------------------
//
// Lifetime: a Sound plays only while it, or a copy of it, is alive. Copies
// share the voice that play() started; when the last Sound handle that
// shares it is destroyed or overwritten (copy or move assignment), the voice
// stops, looping or not, and its slot is free again. A temporary copy going
// away does not stop the original. To play overlapping one-shots, keep the
// Sound objects alive (for example as members):
//
//   Sound hits_[4];   // members, each loaded once
//   int next_ = 0;
//   hits_[next_].play(); next_ = (next_ + 1) % 4;   // up to 4 overlap
//
// `{ Sound s = hit; s.play(); }` stops at the closing brace.
//
// Stopped means released: stop(), and the last handle going away, also
// close a streamed voice's decoder and file.
class Sound {
public:
    Sound() = default;
    ~Sound() = default;   // releases the voice if this is its last handle

    // Copy and move. Copies share the voice; assignment releases the old
    // voice when this was its last handle.
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
        if (!AudioEngine::getInstance().isInitialized()) AudioEngine::getInstance().init();

        // Decode into a SoundBuffer, then store as the polymorphic source.
        // SoundBuffer::load() picks the decoder from the extension, ignoring
        // its case (the path itself is used as given), records the file for
        // getPath() and logs a failure with the file name. Relative paths
        // resolve via getDataPath, like Image::load.
        auto buf = std::make_shared<SoundBuffer>();
        LoadResult result = buf->load(getDataPath(path));

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
    //   - setPosition() incurs a seek + ring-buffer refill (usually
    //     ~10 ms); getPosition() reports the requested position meanwhile.
    //     A file whose length is unknown (getDuration() is 0) cannot seek,
    //     and an engine re-init at another sample rate restarts it from the
    //     beginning.
    //
    // Web (Emscripten): streaming relies on std::thread + on-disk file I/O,
    // neither of which is available in the default browser build. To keep
    // user apps portable we fall back to eager load() and log a warning so
    // the developer can branch explicitly with isStreaming() / #ifdef
    // __EMSCRIPTEN__ if they need to know.
    TC_PLATFORMS("macos,windows,linux,android,ios") LoadResult loadStream(const fs::path& path, int maxPolyphony = 1) {
        if (!AudioEngine::getInstance().isInitialized()) AudioEngine::getInstance().init();
#ifdef __EMSCRIPTEN__
        (void)maxPolyphony;
        logWarning("Sound") << "loadStream() is not supported on Web — "
                               "falling back to eager load() for '" << path << "'. "
                               "Branch on isStreaming() or __EMSCRIPTEN__ if you need "
                               "to handle this explicitly.";
        return load(path);
#else
        auto stream = std::make_shared<SoundStream>();
        // Relative paths resolve via getDataPath, like load().
        LoadResult r = stream->loadStream(getDataPath(path), maxPolyphony);
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
        if (!AudioEngine::getInstance().isInitialized()) AudioEngine::getInstance().init();
        auto buf = std::make_shared<SoundBuffer>();
        buf->generateSineWave(frequency, duration, 0.5f);
        buffer_ = std::move(buf);
    }

    // Load from pre-generated SoundBuffer
    void loadFromBuffer(const SoundBuffer& buf) {
        if (!AudioEngine::getInstance().isInitialized()) AudioEngine::getInstance().init();
        buffer_ = std::make_shared<SoundBuffer>(buf);
    }

    void loadFromBuffer(std::shared_ptr<SoundBuffer> buf) {
        if (!AudioEngine::getInstance().isInitialized()) AudioEngine::getInstance().init();
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

    // Start playing from the beginning (this Sound's previous voice is
    // stopped first). Returns false when nothing will play: not loaded, or
    // the engine dropped the play — every playback slot busy
    // (AudioSettings::maxPolyphony), the stream's own maxPolyphony reached
    // (copies of a streamed Sound share it), the stream file could not be
    // reopened, or no output device is running. Drops are logged as
    // warnings and counted in AudioEngine::getStats().
    bool play() {
        if (!buffer_) return false;

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

        if (auto voice = AudioEngine::getInstance().play(buffer_)) {
            // playing_ points at the voice and shares ownership of its
            // VoiceOwner, which every copy of this Sound then shares too.
            auto owner = std::make_shared<internal::VoiceOwner>(voice);
            playing_ = std::shared_ptr<PlayingSound>(owner, voice.get());
        }
        if (playing_) {
            playing_->volume = volume_;
            playing_->pan = pan_;
            playing_->speed = speed_;
            playing_->loop = loop_;
            playing_->mixMode.store((int)mixMode_, std::memory_order_release);
            internal::sharedStore(playing_->channelMap,   channelMap_);
            internal::sharedStore(playing_->channelGains, channelGains_);
        }
        return playing_ != nullptr;
    }

    // Stop and release the voice (a stream's decoder and file too). Copies
    // that share the voice see it stopped.
    void stop() {
        if (playing_) {
            internal::releaseVoice(*playing_);
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

    // Playback position in seconds. On a stream, after setPosition() and
    // until the audio has moved there (usually ~10 ms), this is the requested
    // position; otherwise it is the position being played.
    float getPosition() const {
        if (!playing_ || !buffer_) return 0;
        const int rate = positionRate();
        return rate > 0 ? (float)(internal::voicePosition(*playing_) / (double)rate) : 0.0f;
    }

    // Seek to `seconds`. Eager sounds move at once. A stream moves after
    // its decoder has seeked and the ring has refilled (~10 ms of silence;
    // longer on slow storage or for an MP3 several hours long);
    // getPosition() reports the new position right away, and if
    // setPosition() is called again before that, the last call wins. A
    // paused stream moves when it resumes. A stream whose length is
    // unknown (getDuration() is 0) cannot seek: the call is ignored with a
    // warning.
    void setPosition(float seconds) {
        if (!playing_ || !buffer_) return;
        const int rate = positionRate();
        double pos = seconds * (double)rate;
        if (pos < 0) pos = 0;
        // For eager: clamp to numSamples. For streams: clamp to duration.
        if (buffer_->kind() == SoundSource::Eager) {
            auto* eager = static_cast<const SoundBuffer*>(buffer_.get());
            // An empty buffer clamps to 0.
            if (pos >= (double)eager->numSamples) {
                pos = eager->numSamples > 0 ? (double)eager->numSamples - 1 : 0.0;
            }
        } else {
            // The float duration can be a few frames past the last frame
            // on a long file; the StreamWorker clamps to the decoder's
            // length. An unknown length (0) is refused by seekVoice().
            double maxPos = (double)buffer_->getDuration() * (double)rate;
            if (pos >= maxPos) pos = maxPos - 1;
        }
        internal::seekVoice(*playing_, pos);
    }

    float getDuration() const {
        return buffer_ ? buffer_->getDuration() : 0;
    }

private:
    // Frames per second of the voice's positionF: the source rate for eager
    // sources; for streams the rate the voice's positionF counts
    // (PlayingSound::positionRateHz_), the engine rate its decoder outputs
    // at. Not the engine's current rate (a voice the re-init migration did
    // not rebuild keeps the old one), nor the stream's sampleRate (the
    // engine rate at loadStream(), stale after a re-init).
    int positionRate() const {
        if (buffer_->kind() != SoundSource::Stream) return buffer_->sampleRate;
        return playing_->positionRateHz_.load(std::memory_order_relaxed);
    }

    std::shared_ptr<SoundSource> buffer_;
    // Points at the voice; owns (and shares with copies) its VoiceOwner.
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
    if (!AudioEngine::getInstance().isInitialized()) AudioEngine::getInstance().init();
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
    static constexpr int DEFAULT_SAMPLE_RATE = 48000;  // match AudioEngine::DEFAULT_SAMPLE_RATE

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

    // Name of the capture device start() opened; empty while stopped (and
    // on Web, where the browser does not expose it).
    std::string getDeviceName() const { return deviceName_; }

    // Callback (internal use)
    void onAudioData(const float* input, size_t frameCount);

private:
    void* device_ = nullptr;  // ma_device*
    bool running_ = false;
    int sampleRate_ = DEFAULT_SAMPLE_RATE;
    std::string deviceName_;

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
