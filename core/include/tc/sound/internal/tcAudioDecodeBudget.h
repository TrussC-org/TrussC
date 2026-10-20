#pragma once

#include "tc/sound/tcSound.h"
#include <cstddef>
#include <cstdio>
#include <limits>

namespace trussc::internal {
// Bound (security): external full-track input must not dictate unlimited storage. Overflow rejects the whole sound with a first Warning and is counted.
constexpr size_t audioEncodedLimit = 64u * 1024 * 1024;
// Bound (security): streaming decoder allocations and overlapping copies share a working budget independent of duration. Overflow rejects the whole stream with a first Warning and is counted.
constexpr size_t audioWorkingLimit = 256u * 1024 * 1024;
constexpr size_t audioLargeLoadNoticeBytes = 256u * 1024 * 1024;
// Bound (security): external audio formats must not dictate excessive channel/rate driven work. Overflow rejects the whole sound or stream with a first Warning and is counted.
constexpr unsigned audioChannelLimit = 32, audioRateLimit = 384000;

// Process-lifetime diagnostics, deliberately internal. Queries never reset them.
uint64_t audioDecodeRejections(bool streaming = false);
void reportAudioDecodeRejection(bool streaming, const char* store, uint64_t needed, uint64_t limit);
size_t audioWorkingLimitForTests();
void setAudioWorkingLimitForTests(size_t bytes);
void setAudioNoticeThresholdForTests(size_t bytes);
void noticeAudioDecodedBytes(size_t bytes);

struct AudioDecodeBudget {
    size_t live = 0, retained = 0, peak = 0;
    bool rejected = false, streaming = false, allocationFailed = false;
    explicit AudioDecodeBudget(size_t held = 0, bool stream = false) : retained(held), streaming(stream) {}
    size_t limit() const {
        if (!streaming) return SIZE_MAX; // Full loads grow until allocation fails.
        const size_t test = audioWorkingLimitForTests();
        return test ? std::min(test, audioWorkingLimit) : audioWorkingLimit;
    }
    bool failAllocation() {
        if (!allocationFailed) {
            allocationFailed = true;
            logError(streaming ? "SoundStream" : "SoundBuffer") << "audio allocation failed; previous PCM kept";
        }
        return false;
    }
    bool reject(const char* store, uint64_t needed, uint64_t cap) {
        if (!rejected) { rejected = true; reportAudioDecodeRejection(streaming, store, needed, cap); }
        return false;
    }
    bool fits(size_t bytes, size_t extra = 0) const {
        const size_t cap = limit();
        return retained <= cap && live <= cap - retained && extra <= cap - retained - live
            && bytes <= cap - retained - live - extra;
    }
    bool admit(size_t bytes, size_t extra = 0) {
        if (!fits(bytes, extra)) {
            uint64_t total = bytes;
            for (size_t value : {retained, live, extra})
                total = value > UINT64_MAX - total ? UINT64_MAX : total + value;
            return reject("audio working bytes", total, limit());
        }
        peak = std::max(peak, retained + live + extra + bytes);
        return true;
    }
    bool format(uint64_t channels, double rate) {
        if (channels < 1 || channels > audioChannelLimit)
            return reject("audio channels", channels, audioChannelLimit);
        if (!std::isfinite(rate) || rate < 1 || rate > audioRateLimit)
            return reject("audio sample rate", rate > 0 && std::isfinite(rate) && rate < double(UINT64_MAX) ? uint64_t(rate) : UINT64_MAX, audioRateLimit);
        return true;
    }
    bool encoded(uint64_t bytes) {
        return bytes <= audioEncodedLimit || reject("encoded audio bytes", bytes, audioEncodedLimit);
    }
    bool pcm(uint64_t frames, unsigned channels, size_t extra = 0) {
        if (!channels || frames > limit() / sizeof(float) / channels)
            return reject("decoded audio frames", frames, channels ? limit() / sizeof(float) / channels : 0);
        return admit(size_t(frames) * channels * sizeof(float), extra);
    }
    struct alignas(std::max_align_t) Allocation { size_t bytes; AudioDecodeBudget* owner; };
    static void* allocate(size_t bytes, void* user) {
        auto* b = static_cast<AudioDecodeBudget*>(user);
        if (bytes > SIZE_MAX - sizeof(Allocation)) {
            if (b) b->reject("decoder allocation bytes", bytes, b->limit());
            return nullptr;
        }
        if (b && !b->admit(bytes + sizeof(Allocation))) return nullptr;
        auto* p = allocationFits(bytes + sizeof(Allocation)) ? static_cast<Allocation*>(std::malloc(bytes + sizeof(Allocation))) : nullptr;
        if (!p) { if (b) b->failAllocation(); return nullptr; }
        p->bytes = bytes + sizeof(Allocation); p->owner = b;
        if (b) b->live += p->bytes;
        return p + 1;
    }
    static void release(void* ptr, void*) {
        if (!ptr) return;
        auto* p = static_cast<Allocation*>(ptr) - 1;
        if (p->owner) p->owner->live -= p->bytes;
        std::free(p);
    }
    static void* reallocate(void* ptr, size_t bytes, void* user) {
        if (!ptr) return allocate(bytes, user);
        if (!bytes) { release(ptr, user); return nullptr; }
        void* next = allocate(bytes, user); // old + replacement are live together
        if (!next) return nullptr;
        std::memcpy(next, ptr, std::min(bytes, (static_cast<Allocation*>(ptr) - 1)->bytes - sizeof(Allocation)));
        release(ptr, user);
        return next;
    }
};

// A reserve accounts the old capacity until the replacement has been allocated.
// Initial storage is a chunk; duration hints never allocate unused whole tracks.
template<class T>
bool growAudioVector(std::vector<T>& out, size_t needed, AudioDecodeBudget& b, size_t scratch = 0, size_t preferred = 0) {
    if (needed > out.max_size() || needed > b.limit() / sizeof(T))
        return b.reject("audio vector elements", needed, b.limit() / sizeof(T));
    const size_t oldBytes = out.capacity() * sizeof(T);
    if (scratch > SIZE_MAX - oldBytes)
        return b.reject("audio scratch bytes", scratch, SIZE_MAX - oldBytes);
    if (needed <= out.capacity()) return b.admit(oldBytes, scratch);
    size_t target = std::max(needed, std::min(out.max_size() / 2, out.capacity()) * 2);
    // Bound (security): a header hint cannot enlarge growth beyond actual decoded demand. Overflow ignores the hint and decoding continues without loss.
    if (preferred >= needed && preferred < target) target = preferred;
    if (target > b.limit() / sizeof(T) || !b.fits(target * sizeof(T), oldBytes + scratch)) target = needed;
    if (!b.admit(needed * sizeof(T), oldBytes + scratch)) return false;
    auto reserve = [&](size_t count) {
        if (!b.fits(count * sizeof(T), oldBytes + scratch) || !allocationFits(count * sizeof(T))) return false;
        b.admit(count * sizeof(T), oldBytes + scratch);
        try { out.reserve(count); } catch (const std::exception&) { return false; }
        return true;
    };
    if (reserve(target)) return true;
    for (size_t step = out.capacity() / 2; step && out.capacity() + step >= needed; step /= 2)
        if (out.capacity() + step < target && reserve(out.capacity() + step)) return true;
    if (target != needed && reserve(needed)) return true;
    return b.failAllocation();
}
inline bool appendAudio(std::vector<float>& out, const float* data, size_t count,
                        AudioDecodeBudget& b, size_t scratch = 0, size_t preferred = 0) {
    if (count > out.max_size() - out.size()) return b.reject("audio samples", count, out.max_size());
    if (!growAudioVector(out, out.size() + count, b, scratch, preferred)) return false;
    out.insert(out.end(), data, data + count);
    if (!b.streaming) noticeAudioDecodedBytes(out.size() * sizeof(float));
    return true;
}
inline void trimAudioVector(std::vector<float>& pcm, AudioDecodeBudget& budget, size_t scratch = 0) {
    if (scratch > SIZE_MAX - pcm.capacity() * sizeof(float) || pcm.capacity() - pcm.size() <= pcm.size() / 8 ||
        !budget.fits(pcm.size() * sizeof(float), pcm.capacity() * sizeof(float) + scratch) ||
        !allocationFits(pcm.size() * sizeof(float))) return;
    budget.admit(pcm.size() * sizeof(float), pcm.capacity() * sizeof(float) + scratch);
    try { pcm.shrink_to_fit(); } catch (const std::exception&) {}
}
inline bool admitAudioFile(const fs::path& path, AudioDecodeBudget& b) {
    std::error_code ec;
    const auto bytes = fs::file_size(path, ec);
    return !ec && b.encoded(bytes);
}
// Read until EOF, including growth after stat, without allocating the full cap.
// A supplied file transfers ownership, including on admission failure.
inline bool readAudioFile(const fs::path& path, std::vector<unsigned char>& bytes, AudioDecodeBudget& b, FILE* opened = nullptr) {
    if (!admitAudioFile(path, b)) {
        if (opened) std::fclose(opened);
        return false;
    }
    FILE* f = opened;
    if (!f) {
#ifdef _WIN32
        f = _wfopen(path.c_str(), L"rb");
#else
        f = std::fopen(path.c_str(), "rb");
#endif
    }
    if (!f) return false;
    // Bound (security): file scratch holds one 16 KiB read. Overflow continues in later reads; encoded-input rejection rejects the whole sound and is counted.
    unsigned char chunk[16384];
    bool ok = true;
    for (;;) {
        const size_t n = std::fread(chunk, 1, sizeof(chunk), f);
        if (!n) { ok = !std::ferror(f); break; }
        if (!b.encoded(uint64_t(bytes.size()) + n) ||
            !growAudioVector(bytes, bytes.size() + n, b, sizeof(chunk))) { ok = false; break; }
        bytes.insert(bytes.end(), chunk, chunk + n);
    }
    std::fclose(f);
    return ok;
}
} // namespace trussc::internal
