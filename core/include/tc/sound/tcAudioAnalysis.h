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
          storageCapacity_(capacity_ + (size_t(sampleRate) + 1) / 2),
          samples_(new std::atomic<uint32_t>[storageCapacity_ * size_t(channels)]{}) {}

    // One writer, the audio callback. No locks, allocations or reader waits.
    void write(const float* samples, int frames, int channels) {
        if (channels != channels_ || frames <= 0) return;
        const uint64_t start = framesWritten_.load(std::memory_order_relaxed);
        // Announce the entire range before touching any slot. The release
        // fence (a release store alone is insufficient) pairs with the reader's
        // acquire fence if it observes any of this write's relaxed payloads.
        writeEnd_.store(start + frames, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        for (int ch = 0; ch < channels_; ++ch) {
            for (int f = std::max(0, frames - int(storageCapacity_)); f < frames; ++f) {
                uint32_t bits;
                std::memcpy(&bits, samples + size_t(f) * channels_ + ch, sizeof(bits));
                samples_[size_t(ch) * storageCapacity_ + (start + f) % storageCapacity_].store(bits, std::memory_order_relaxed);
            }
        }
        framesWritten_.store(start + frames, std::memory_order_release);
    }

    AudioOutputSnapshot snapshot(size_t frames) const {
        AudioOutputSnapshot result;
        result.sampleRate = sampleRate_;
        result.channels = channels_;
        result.capacity = capacity_;
        frames = std::min(frames, capacity_);
        result.samples.resize(frames * size_t(channels_));
        // Atomic payloads make even an overlapping read data-race-free.
        // Only reject writes that can overwrite the copied range. Half a second
        // of private slack lets even a full two-second read overlap callbacks.
        // Allocation stays outside the bounded retries; the producer never waits.
        for (int attempt = 0; attempt < 3; ++attempt) {
            result.framesWritten = framesWritten_.load(std::memory_order_acquire);
            const size_t count = size_t(std::min<uint64_t>(frames, result.framesWritten));
            const uint64_t start = result.framesWritten - count;
            for (int ch = 0; ch < channels_; ++ch) {
                for (size_t f = 0; f < count; ++f) {
                    const uint32_t bits = samples_[size_t(ch) * storageCapacity_ + (start + f) % storageCapacity_].load(std::memory_order_relaxed);
                    std::memcpy(&result.samples[f * channels_ + ch], &bits, sizeof(bits));
                }
            }
            std::atomic_thread_fence(std::memory_order_acquire);
            const uint64_t end = writeEnd_.load(std::memory_order_relaxed);
            if (end - start <= storageCapacity_) {
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
    size_t capacity_, storageCapacity_;
    std::unique_ptr<std::atomic<uint32_t>[]> samples_;
    std::atomic<uint64_t> writeEnd_{0}, framesWritten_{0};
};

struct AudioAnalysisAccess {
    static AudioOutputSnapshot snapshot(AudioEngine& engine, size_t frames);
};

} // namespace internal
} // namespace trussc
