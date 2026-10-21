#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include "../utils/tcOnceGate.h"

namespace trussc::internal {

// Shared receive policy for desktop and Android. Callers hold mutex while
// touching storage; the counter remains readable after close/flush. A new
// connection resets the counter and both warning gates. Deliberate flushes
// are not lost bytes. Chunks are freed as soon as consumed, including on drain.
struct SerialRxBuffer {
    // Bound (security): device input or a stalled reader can exhaust memory; the 64 MiB cap keeps storage finite. Overflow drops the oldest bytes and is counted.
    static constexpr size_t capacity = 64 * 1024 * 1024;
    static constexpr size_t backlogThreshold = 1024 * 1024;
    static constexpr size_t chunkSize = 16 * 1024;
    mutable std::mutex mutex;
    std::atomic<size_t> dropped{0};

    // Internal allocation seam: a null result must work without exceptions,
    // including Web builds. Successful allocations must be free()-compatible.
    using Allocate = void* (*)(size_t);
    explicit SerialRxBuffer(Allocate allocate = std::malloc) : allocate_(allocate) {}
    ~SerialRxBuffer() { clear(); }

    size_t size() const { return size_; }
    size_t allocatedBytes() const { return chunks_ * sizeof(Chunk); }

    void clear() {
        discard(size_);
    }

    void resetConnection() {
        clear();
        dropped = 0;
        backlogWarned_.reset();
        dropWarned_.reset();
        pending_ = {};
        dropPending_ = allocationFailed_ = false;
    }

    void append(const uint8_t* data, size_t length) {
        if (!length) return;
        // Bytes this append needs buffered at once (saturating), reported with
        // its first loss; not the connection's historical peak.
        const size_t needed = length > std::numeric_limits<size_t>::max() - size_
                            ? std::numeric_limits<size_t>::max() : size_ + length;
        // Subtract first: even a single enormous driver chunk cannot wrap.
        const size_t incomingSkip = length > capacity ? length - capacity : 0;
        const size_t retained = length - incomingSkip;
        const size_t fromBuffer = size_ > capacity - retained ? size_ - (capacity - retained) : 0;
        discard(fromBuffer);
        countLoss(fromBuffer + incomingSkip);
        data += incomingSkip;
        length = retained;
        while (length) {
            if (!tail_ || tail_->end == chunkSize) {
                auto* chunk = static_cast<Chunk*>(allocate_(sizeof(Chunk)));
                if (!chunk) {
                    // Preserve already buffered bytes, report every byte not
                    // admitted, and keep the reader alive for later input.
                    allocationFailed_ = true;
                    countLoss(length);
                    break;
                }
                chunk->next = nullptr;
                chunk->begin = chunk->end = 0;
                if (tail_) tail_->next = chunk;
                else head_ = chunk;
                tail_ = chunk;
                ++chunks_;
            }
            const size_t n = std::min(length, chunkSize - tail_->end);
            std::memcpy(tail_->data + tail_->end, data, n);
            tail_->end += n;
            size_ += n;
            data += n;
            length -= n;
        }
        if (size_ > backlogThreshold && backlogWarned_.isFirstTime()) {
            pending_.backlog = true;
            pending_.buffered = size_;
        }
        if (dropPending_ && dropWarned_.isFirstTime()) {
            pending_.loss = true;
            pending_.allocationFailed = allocationFailed_;
            pending_.dropped = dropped.load();
            pending_.needed = needed;
        }
        dropPending_ = false;
    }

    int read(void* out, int length) {
        if (length <= 0) return 0;
        const size_t n = std::min(size_, static_cast<size_t>(length));
        auto* data = static_cast<uint8_t*>(out);
        size_t remaining = n;
        while (remaining) {
            const size_t part = std::min(remaining, head_->end - head_->begin);
            std::memcpy(data, head_->data + head_->begin, part);
            discard(part);
            data += part;
            remaining -= part;
        }
        return static_cast<int>(n);
    }

    struct Warnings {
        bool backlog = false;
        bool loss = false;
        bool allocationFailed = false;
        size_t buffered = 0;
        size_t dropped = 0;
        size_t needed = 0;
    };

    Warnings takeWarnings() {
        const Warnings result = pending_;
        pending_ = {};
        return result;
    }

    bool hasWarnings() const { return pending_.backlog || pending_.loss; }

private:
    struct Chunk {
        Chunk* next;
        size_t begin, end;
        uint8_t data[chunkSize];
    };
    Chunk* head_ = nullptr;
    Chunk* tail_ = nullptr;
    size_t size_ = 0, chunks_ = 0;
    Allocate allocate_;
    OnceGate backlogWarned_, dropWarned_;
    // Captured by append, under mutex; flush/read cannot change a pending
    // diagnostic. The main thread takes these plain values; the worker
    // allocates nothing to report them.
    Warnings pending_;
    bool dropPending_ = false, allocationFailed_ = false;

    void countLoss(size_t count) {
        if (!count) return;
        dropped.fetch_add(count);
        dropPending_ = true;
    }

    void discard(size_t count) {
        size_ -= count;
        while (count) {
            const size_t n = std::min(count, head_->end - head_->begin);
            head_->begin += n;
            count -= n;
            if (head_->begin == head_->end) {
                Chunk* next = head_->next;
                std::free(head_);
                head_ = next;
                --chunks_;
                if (!head_) tail_ = nullptr;
            }
        }
    }
};

} // namespace trussc::internal
