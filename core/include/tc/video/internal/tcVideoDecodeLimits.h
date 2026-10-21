#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <vector>
#include "tc/utils/tcLog.h"

namespace trussc::internal {

struct VideoDecodeLimits {
    // Bound (security): file dimensions must not drive unlimited frame storage. Overflow rejects the whole frame or load and is counted.
    static constexpr size_t axis = 8192;
    static constexpr size_t frameBytes = 128 * 1024 * 1024;
    // Bound (security): decoder frames, output, queues and temporary copies coexist. Overflow rejects the whole frame or load and is counted.
    static constexpr size_t workingBytes = 512 * 1024 * 1024;
    // Bound (security): compressed packets and extracted tracks are external input. Overflow rejects the whole packet or extraction and is counted.
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
        return width > 0 && height > 0 && width <= int64_t(axis) && height <= int64_t(axis)
            && product(size_t(width), size_t(height), bytes)
            && product(bytes, 4, bytes) && bytes <= frameBytes;
    }
    static bool rows(size_t width, size_t height, size_t stride, size_t available) {
        size_t bytes = 0, span = 0;
        return width <= axis && height <= axis && rgba(int64_t(width), int64_t(height), bytes)
            && stride >= width * 4 && product(stride, height, span)
            && span <= frameBytes && span <= available;
    }
};

// Owned by one player/extractor. Queries never clear the lifetime rejection
// total. Decoder threads may record; the app/extraction thread emits the notice.
class VideoDecodeBudget {
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
    bool acquire(size_t bytes) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (bytes > VideoDecodeLimits::workingBytes - used_) return false;
        used_ += bytes;
        if (used_ > peak_) peak_ = used_;
        return true;
    }
    void release(size_t bytes) {
        std::lock_guard<std::mutex> lock(mutex_);
        used_ -= bytes;
    }
    size_t used() const { std::lock_guard<std::mutex> lock(mutex_); return used_; }
    size_t peak() const { std::lock_guard<std::mutex> lock(mutex_); return peak_; }
private:
    mutable std::mutex mutex_;
    size_t used_ = 0, peak_ = 0;
    std::atomic<uint64_t> rejected_{0};
    std::atomic<bool> firstLoss_{false}, pending_{false};
    uint64_t needed_ = 0, capacity_ = 0;
    const char* store_ = "Video decode storage"; // static string literals only
};

class VideoDecodeLease {
public:
    VideoDecodeLease(std::shared_ptr<VideoDecodeBudget> budget, size_t bytes)
        : budget_(std::move(budget)) {
        if (budget_ && budget_->acquire(bytes)) bytes_ = bytes;
        else if (budget_) budget_->reject(bytes, VideoDecodeLimits::workingBytes);
    }
    ~VideoDecodeLease() { if (budget_) budget_->release(bytes_); }
    bool acquired() const { return bytes_ != 0; }
    void shrink(size_t bytes) {
        if (bytes <= bytes_) { budget_->release(bytes_ - bytes); bytes_ = bytes; }
    }
    VideoDecodeLease(const VideoDecodeLease&) = delete;
    VideoDecodeLease& operator=(const VideoDecodeLease&) = delete;
private:
    std::shared_ptr<VideoDecodeBudget> budget_;
    size_t bytes_ = 0;
};

// A track's vector capacity and a replacement coexist during growth. Reserve
// both before allocation; release the decoder's credit when handing off the
// completed vector (or abandoning an extraction).
class VideoAudioStorage {
public:
    VideoAudioStorage(std::vector<uint8_t>& output, VideoDecodeBudget& budget)
        : output_(output), budget_(budget) {}
    ~VideoAudioStorage() { budget_.release(charged_); }
    bool append(const uint8_t* data, size_t bytes) {
        if (!bytes) return true;
        if (!data || bytes > VideoDecodeLimits::inputBytes - output_.size()) {
            budget_.reject(bytes, VideoDecodeLimits::inputBytes - output_.size());
            return false;
        }
        const size_t needed = output_.size() + bytes;
        if (needed > output_.capacity()) {
            size_t capacity = output_.capacity() ? output_.capacity() : 4096;
            while (capacity < needed) capacity = (std::min)(VideoDecodeLimits::inputBytes, capacity * 2);
            if (!budget_.acquire(capacity)) {
                budget_.reject(capacity, VideoDecodeLimits::workingBytes); return false;
            }
            try {
                std::vector<uint8_t> next;
                next.reserve(capacity);
                next.insert(next.end(), output_.begin(), output_.end());
                output_.swap(next);
            } catch (const std::bad_alloc&) {
                budget_.release(capacity);
                budget_.reject(capacity, VideoDecodeLimits::workingBytes); return false;
            }
            budget_.release(charged_);
            charged_ = capacity;
        }
        output_.insert(output_.end(), data, data + bytes);
        return true;
    }
private:
    std::vector<uint8_t>& output_;
    VideoDecodeBudget& budget_;
    size_t charged_ = 0;
};

} // namespace trussc::internal
