#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <deque>
#include <mutex>
#include "../utils/tcOnceGate.h"

namespace trussc::internal {

// Shared receive policy for desktop and Android. Callers hold mutex while
// touching bytes; the counter remains readable after close/flush. A new
// connection resets the counter. Deliberate flushes are not lost bytes.
struct SerialRxBuffer {
    static constexpr size_t capacity = 1 << 20;
    mutable std::mutex mutex;
    std::deque<uint8_t> bytes;
    std::atomic<size_t> dropped{0};
    bool warningPending = false;

    void append(const uint8_t* data, size_t length) {
        const size_t excess = bytes.size() + length > capacity
                            ? bytes.size() + length - capacity : 0;
        const size_t fromBuffer = std::min(excess, bytes.size());
        bytes.erase(bytes.begin(), bytes.begin() + fromBuffer);
        data += excess - fromBuffer;
        length -= excess - fromBuffer;
        bytes.insert(bytes.end(), data, data + length);
        if (excess) {
            dropped.fetch_add(excess);
            warningPending = true;
        }
    }

    int read(void* out, int length) {
        const int n = static_cast<int>(std::min(bytes.size(), static_cast<size_t>(length)));
        auto* data = static_cast<uint8_t*>(out);
        for (int i = 0; i < n; ++i) {
            data[i] = bytes.front();
            bytes.pop_front();
        }
        return n;
    }

    bool takeWarning(OnceGate& dropWarned) {
        if (!warningPending || !dropWarned.isFirstTime()) return false;
        warningPending = false;
        return true;
    }
};

} // namespace trussc::internal
