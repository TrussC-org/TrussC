#pragma once

#include "tc/sound/tcSound.h"
#include <cstdio>
#include <iomanip>

namespace trussc::internal {
constexpr size_t audioLargeLoadNoticeBytes = 256u * 1024 * 1024;

uint64_t audioDecodeRejections(bool streaming = false);
void reportAudioDecodeRejection(bool streaming, const char* store, uint64_t needed, uint64_t limit);
void setAudioNoticeThresholdForTests(size_t bytes);
void noticeAudioDecodedBytes(size_t bytes);

// Per-operation error reporting only; decoder allocations use their defaults.
struct AudioDecodeStatus {
    bool rejected = false, streaming = false, allocationFailed = false;
    explicit AudioDecodeStatus(bool stream = false) : streaming(stream) {}
    bool sampleRateFits(double rate, const std::string& input) {
        // Bound (representation): SoundBuffer stores the sample rate as int; rates above INT_MAX cannot be represented; the load fails with this Error.
        if (rate <= INT_MAX) return true;
        if (!rejected) {
            rejected = true;
            logError("SoundBuffer") << input << ": sample rate " << std::fixed << std::setprecision(0)
                << rate << " Hz does not fit SoundBuffer's int sample rate";
        }
        return false;
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
};

template<class T>
bool growAudioVector(std::vector<T>& out, size_t needed, AudioDecodeStatus& status, size_t preferred = 0) {
    if (needed > out.max_size()) return status.failAllocation();
    if (needed <= out.capacity()) return true;
    size_t target = out.capacity() > out.max_size() / 2 ? out.max_size() : out.capacity() * 2;
    if (preferred >= needed && preferred < target) target = preferred;
    target = std::max(target, needed);
    try { out.reserve(target); }
    catch (const std::bad_alloc&) { return status.failAllocation(); }
    catch (const std::length_error&) { return status.failAllocation(); }
    return true;
}
inline bool appendAudio(std::vector<float>& out, const float* data, size_t count,
                        AudioDecodeStatus& status, size_t preferred = 0) {
    if (count > out.max_size() - out.size()) return status.failAllocation();
    if (!growAudioVector(out, out.size() + count, status, preferred)) return false;
    out.insert(out.end(), data, data + count);
    if (!status.streaming) noticeAudioDecodedBytes(out.size() * sizeof(float));
    return true;
}
// Read until EOF, including growth after stat. A supplied file transfers ownership.
inline bool readAudioFile(const fs::path& path, std::vector<unsigned char>& bytes, AudioDecodeStatus& status, FILE* opened = nullptr) {
    FILE* f = opened;
    if (!f) {
#ifdef _WIN32
        f = _wfopen(path.c_str(), L"rb");
#else
        f = std::fopen(path.c_str(), "rb");
#endif
    }
    if (!f) return false;
    unsigned char chunk[16384];
    bool ok = true;
    for (;;) {
        const size_t n = std::fread(chunk, 1, sizeof(chunk), f);
        if (!n) { ok = !std::ferror(f); break; }
        if (n > bytes.max_size() - bytes.size() ||
            !growAudioVector(bytes, bytes.size() + n, status)) { ok = status.failAllocation(); break; }
        bytes.insert(bytes.end(), chunk, chunk + n);
    }
    std::fclose(f);
    return ok;
}
} // namespace trussc::internal
