#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <climits>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <vector>
#include "tc/utils/tcLog.h"

namespace trussc::internal {

struct VideoDecodeLimits {
    // Bound (security): compressed packets are external input. Overflow rejects the whole packet or extraction and is counted.
    static constexpr size_t inputBytes = 64 * 1024 * 1024;
    // Bound (abi): the ADTS frame-length field is 13 bits including its header. Overflow rejects the whole extraction and is counted.
    static constexpr size_t adtsPayloadBytes = 8191 - 7;

    static bool product(size_t a, size_t b, size_t& result) {
        if (b && a > (std::numeric_limits<size_t>::max)() / b) return false;
        result = a * b;
        return true;
    }
    static bool rgba(int64_t width, int64_t height, size_t& bytes) {
        bytes = 0;
        return width > 0 && height > 0 && width <= INT_MAX / 4 && height <= INT_MAX
            && product(size_t(width), size_t(height), bytes)
            && product(bytes, 4, bytes);
    }
    static bool rows(size_t width, size_t height, size_t stride, size_t available) {
        size_t bytes = 0, span = 0;
        return rgba(int64_t(width), int64_t(height), bytes)
            && stride >= width * 4 && product(stride, height, span)
            && span <= available;
    }
};

// Owned by one player/extractor. Queries never clear the lifetime rejection
// total. Decoder threads may record; the app/extraction thread emits the notice.
class VideoDecodeStatus {
public:
    void reject(uint64_t needed, uint64_t capacity, const char* store = "Video decode storage") {
        rejected_.fetch_add(1);
        bool expected = false;
        if (firstLoss_.compare_exchange_strong(expected, true)) {
            needed_ = needed;
            capacity_ = capacity;
            store_ = store;
            pending_.store(true);
        }
    }
    void warn() {
        if (pending_.exchange(false))
            logWarning("VideoPlayer") << store_ << " rejected a whole unit; capacity "
                << capacity_ << " bytes, needed " << needed_ << " bytes; rejected " << rejected_.load();
    }
    uint64_t rejected() const { return rejected_.load(); }
private:
    std::atomic<uint64_t> rejected_{0};
    std::atomic<bool> firstLoss_{false}, pending_{false};
    uint64_t needed_ = 0, capacity_ = 0;
    const char* store_ = "Video decode storage"; // static string literals only
};

class VideoAudioStorage {
public:
    VideoAudioStorage(std::vector<uint8_t>& output, VideoDecodeStatus& status)
        : output_(output), status_(status) {}
    bool append(const uint8_t* data, size_t bytes) {
        if (!bytes) return true;
        if (!data || bytes > output_.max_size() - output_.size()) return false;
        try {
            output_.insert(output_.end(), data, data + bytes);
            return true;
        } catch (const std::bad_alloc&) {
            status_.reject(bytes, 0, "Video audio allocation");
            return false;
        }
    }
private:
    std::vector<uint8_t>& output_;
    VideoDecodeStatus& status_;
};

} // namespace trussc::internal
