#pragma once

// Internal output history shared by MCP and the legacy mono analysis API.
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace trussc {
class AudioEngine;
namespace internal {

struct AudioOutputSnapshot {
    int sampleRate = 0;
    int channels = 0;
    size_t capacity = 0;
    uint64_t framesWritten = 0;
    std::vector<float> samples; // interleaved, oldest first; no startup padding
};

class AudioOutputRing {
public:
    AudioOutputRing(int sampleRate, int channels)
        : sampleRate_(sampleRate), channels_(channels), capacity_(size_t(sampleRate) * 2),
          samples_(new std::atomic<uint32_t>[capacity_ * size_t(channels)]{}) {}

    // One writer, the audio callback. No locks, allocations or reader waits.
    void write(const float* samples, int frames, int channels) {
        if (channels != channels_) return;
        sequence_.fetch_add(1); // odd while any slot may be changing
        const uint64_t start = framesWritten_.load();
        for (int ch = 0; ch < channels_; ++ch) {
            for (int f = std::max(0, frames - int(capacity_)); f < frames; ++f) {
                uint32_t bits;
                std::memcpy(&bits, samples + size_t(f) * channels_ + ch, sizeof(bits));
                samples_[size_t(ch) * capacity_ + (start + f) % capacity_].store(bits);
            }
        }
        framesWritten_.store(start + frames);
        sequence_.fetch_add(1);
    }

    AudioOutputSnapshot snapshot(size_t frames) const {
        AudioOutputSnapshot result;
        result.sampleRate = sampleRate_;
        result.channels = channels_;
        result.capacity = capacity_;
        frames = std::min(frames, capacity_);
        result.samples.resize(frames * size_t(channels_));
        // Atomic payloads make even an overlapping read data-race-free.
        // Sequential consistency orders the payload with the sequence checks;
        // retrying never holds up the producer. Allocation stays outside retries.
        // A request must not spin indefinitely if the writer is preempted or
        // callbacks overlap every copy on a heavily loaded device. Three
        // immediate attempts bound reader work; an empty result is retryable.
        for (int attempt = 0; attempt < 3; ++attempt) {
            const auto before = sequence_.load();
            if (before & 1) continue;
            result.framesWritten = framesWritten_.load();
            const size_t count = size_t(std::min<uint64_t>(frames, result.framesWritten));
            const uint64_t start = result.framesWritten - count;
            for (int ch = 0; ch < channels_; ++ch) {
                for (size_t f = 0; f < count; ++f) {
                    const uint32_t bits = samples_[size_t(ch) * capacity_ + (start + f) % capacity_].load();
                    std::memcpy(&result.samples[f * channels_ + ch], &bits, sizeof(bits));
                }
            }
            if (sequence_.load() == before) {
                result.samples.resize(count * size_t(channels_));
                return result;
            }
        }
        return {};
    }

private:
    static_assert(std::atomic<uint32_t>::is_always_lock_free);
    static_assert(std::atomic<uint64_t>::is_always_lock_free);
    int sampleRate_, channels_;
    size_t capacity_;
    std::unique_ptr<std::atomic<uint32_t>[]> samples_;
    std::atomic<uint64_t> sequence_{0}, framesWritten_{0};
};

struct AudioAnalysisAccess {
    static AudioOutputSnapshot snapshot(AudioEngine& engine, size_t frames);
};

} // namespace internal
} // namespace trussc
