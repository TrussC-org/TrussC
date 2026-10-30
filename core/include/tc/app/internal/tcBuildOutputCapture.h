#pragma once

#include "../../utils/internal/tcBoundedInput.h"
#include <cstdint>
#include <string>

namespace trussc::internal {

class BuildOutputCapture {
public:
    // Bound (security): 64 KiB at each end per child stream preserves context and final errors within 256 KiB total. Overflow omits the middle and is counted.
    static constexpr size_t endBytes = 64 * 1024;
    explicit BuildOutputCapture(InputStorage::Reallocate allocate = std::realloc)
        : head_(allocate), tail_(allocate) {}
    void append(const char* bytes, size_t count) {
        totalBytes_ += count;
        const size_t initial = headClosed_ ? 0 : (std::min)(count, endBytes - headSize_);
        if (initial) {
            if (head_.reserve(headSize_ + initial, endBytes)) {
                std::memcpy(head_.data() + headSize_, bytes, initial);
                headSize_ += initial;
            } else { headClosed_ = true; }
            bytes += initial;
            count -= initial;
        }
        if (!count) return;
        if (!tail_.reserve((std::min)(endBytes, tailSize_ + (std::min)(count, endBytes)), endBytes)) {
            tailSize_ = cursor_ = 0;
            return;
        }
        // Keep only the end of a large input, then use the same ring copy for
        // every chunk size. A separate constant-size memcpy here made GCC
        // diagnose an impossible 64 KiB read from the process reader's 4 KiB
        // buffer when append() was inlined in optimized host builds.
        const size_t retained = (std::min)(count, endBytes);
        bytes += count - retained;
        count = retained;
        const size_t first = (std::min)(count, endBytes - cursor_);
        std::memcpy(tail_.data() + cursor_, bytes, first);
        std::memcpy(tail_.data(), bytes + first, count - first);
        cursor_ = (cursor_ + count) % endBytes;
        tailSize_ = (std::min)(endBytes, tailSize_ + count);
    }
    uint64_t omittedBytes() const {
        return totalBytes_ - headSize_ - tailSize_ + headTrim() + tailTrim();
    }
    std::string text() const {
        std::string result;
        const size_t headLength = headSize_ - headTrim(), skip = tailTrim();
        if (headLength) result.append(head_.data(), headLength);
        if (omittedBytes()) result += "\n… " + std::to_string(omittedBytes()) + " bytes omitted …\n";
        for (size_t offset = skip; offset < tailSize_;) {
            const size_t start = (tailStart() + offset) % endBytes;
            const size_t count = (std::min)(tailSize_ - offset, endBytes - start);
            result.append(tail_.data() + start, count);
            offset += count;
        }
        return result;
    }
private:
    bool truncated() const { return totalBytes_ > headSize_ + tailSize_; }
    static bool continuation(unsigned char byte) { return (byte & 0xc0) == 0x80; }
    size_t tailStart() const { return tailSize_ == endBytes ? cursor_ : 0; }
    size_t headTrim() const {
        if (!truncated() || !headSize_) return 0;
        size_t start = headSize_ - 1;
        while (start && continuation(static_cast<unsigned char>(head_.data()[start]))) --start;
        const auto lead = static_cast<unsigned char>(head_.data()[start]);
        const size_t width = lead >= 0xf0 ? 4 : lead >= 0xe0 ? 3 : lead >= 0xc0 ? 2 : 1;
        return headSize_ - start < width ? headSize_ - start : 0;
    }
    size_t tailTrim() const {
        if (!truncated()) return 0;
        size_t skip = 0;
        while (skip < tailSize_ && continuation(static_cast<unsigned char>(
                tail_.data()[(tailStart() + skip) % endBytes]))) ++skip;
        return skip;
    }
    InputStorage head_, tail_;
    bool headClosed_ = false;
    uint64_t totalBytes_ = 0;
    size_t headSize_ = 0, tailSize_ = 0, cursor_ = 0;
};

// Counters belong to the host process, across rebuilds; querying never resets.
struct BuildOutputLoss {
    uint64_t omittedBytes = 0;
    uint64_t truncatedBuilds = 0;
    bool warned = false;
    uint64_t unavailableBuilds = 0;
    uint64_t forwardingFailures = 0;
    void record(uint64_t omitted);
    void recordStorageFailure();
    void recordForwardingFailure();
};
BuildOutputLoss& buildOutputLoss();

} // namespace trussc::internal
