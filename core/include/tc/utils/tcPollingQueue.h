#pragma once

#include "tcMainThread.h"
#include "tcLog.h"
#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>

namespace trussc::internal {

struct StoreLoss {
    size_t limit = 0;
    size_t needed = 0;
    const char* units = nullptr;
};

// One bounded pending report per store lifetime, never one job per lost input.
// The job owns only values: destruction/reopening of the source is harmless.
class FirstStoreLoss {
public:
    void report(const char* module, const char* store, StoreLoss loss) {
        if (!loss.units || reported_.exchange(true)) return;
        runOnMainThread([module, store, loss] {
            logWarning(module) << store << ": first loss (limit " << loss.limit
                               << ", needed " << loss.needed << ' ' << loss.units << ')';
        });
    }
private:
    std::atomic<bool> reported_{false};
};

inline size_t storageAdd(size_t a, size_t b) {
    const size_t largest = (std::numeric_limits<size_t>::max)();
    return b > largest - a ? largest : a + b;
}
inline size_t storageMultiply(size_t a, size_t b) {
    const size_t largest = (std::numeric_limits<size_t>::max)();
    return a && b > largest / a ? largest : a * b;
}

// Minimum copy storage, used only to reject obviously over-budget input.
// Measure the constructed copy for admission: STL heap rounding and inline
// capacities differ. Inline storage is included in sizeof(message/argument).
inline size_t copiedStringStorage(const std::string& value) {
    return value.size() <= std::string().capacity() ? 0 : storageAdd(value.size(), 1);
}
inline std::string pollingStringCopy(const std::string& value) {
    return std::string(value);
}
inline size_t retainedStringStorage(const std::string& value) {
    const auto data = reinterpret_cast<uintptr_t>(value.data());
    const auto object = reinterpret_cast<uintptr_t>(&value);
    return data >= object && data - object < sizeof(value) ? 0 : storageAdd(value.capacity(), 1);
}

// Traits give a minimum copy size and measure actual retained capacities.
// A linked node makes container storage explicit (no hidden deque block/map
// slack). One candidate copy is temporary working storage until admission.
template<class T, class Traits> class PollingQueue {
    struct Node {
        explicit Node(const T& source) : message(Traits::copy(source)) {}
        T message;
        std::unique_ptr<Node> next;
        size_t charge = 0;
    };
public:
    // Bound (security): stalled consumers of external input must retain finite storage. Overflow evicts oldest whole messages, or rejects one over-budget message, and is counted.
    static constexpr size_t storageLimit = 64 * 1024 * 1024;
    ~PollingQueue() { while (head_) pop(); }
    bool hasNewMessage() {
        enabled = true;
        std::lock_guard<std::mutex> lock(mutex_);
        return count_ != 0;
    }
    bool getNextMessage(T& message) {
        enabled = true;
        std::lock_guard<std::mutex> lock(mutex_);
        if (!head_) return false;
        message = std::move(head_->message);
        pop();
        return true;
    }
    void setBufferSize(size_t size) {
        StoreLoss loss;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (count_ > size) loss = {size, count_, "messages"};
            bufferMax_ = size;
            while (count_ > size) { pop(); ++dropped_; }
        }
        warning_.report(Traits::module, Traits::store, loss);
    }
    size_t getBufferSize() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return bufferMax_;
    }
    uint64_t getDroppedMessageCount() const { return dropped_.load(); }
    // Allocation-free lower bound; admission charges the constructed node.
    static size_t copyCharge(const T& message) {
        return storageAdd(sizeof(Node), Traits::copyStorage(message));
    }
    size_t retainedBytes() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return bytes_;
    }
    size_t messageCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return count_;
    }
    void enqueue(const T& message) {
        if (!enabled.load()) return;
        const size_t charge = copyCharge(message);
        StoreLoss loss;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (charge > storageLimit || bufferMax_ == 0) {
                ++dropped_;
                loss = charge > storageLimit ? StoreLoss{storageLimit, charge, "bytes"}
                                             : StoreLoss{0, 1, "messages"};
            } else {
                std::unique_ptr<Node> node;
                try {
                    node = std::make_unique<Node>(message);
                } catch (const std::bad_alloc&) {
                    ++dropped_;
                    loss = {storageLimit, charge, "bytes (allocation failed)"};
                }
                if (node) {
                    const size_t actual = storageAdd(sizeof(Node), Traits::retainedStorage(node->message));
                    // Bound (security): actual retained capacity must fit the storage budget. Overflow rejects the whole copy before eviction and is counted.
                    if (actual > storageLimit) {
                        ++dropped_;
                        loss = {storageLimit, actual, "bytes"};
                    } else {
                        if (count_ >= bufferMax_) loss = {bufferMax_, storageAdd(count_, 1), "messages"};
                        else if (actual > storageLimit - bytes_)
                            loss = {storageLimit, storageAdd(bytes_, actual), "bytes"};
                        // Charge the measured copy, evicting oldest whole messages
                        // before publication; never assume a library's growth rule.
                        while (head_ && (count_ >= bufferMax_ || actual > storageLimit - bytes_)) {
                            pop();
                            ++dropped_;
                        }
                        node->charge = actual;
                        bytes_ += actual;
                        auto* nextTail = node.get();
                        if (tail_) tail_->next = std::move(node);
                        else head_ = std::move(node);
                        tail_ = nextTail;
                        ++count_;
                    }
                }
            }
        }
        warning_.report(Traits::module, Traits::store, loss);
    }
    std::atomic<bool> enabled{false};
private:
    void pop() {
        bytes_ -= head_->charge;
        auto removed = std::move(head_);
        head_ = std::move(removed->next);
        if (!head_) tail_ = nullptr;
        --count_;
    }
    std::unique_ptr<Node> head_;
    Node* tail_ = nullptr;
    size_t count_ = 0;
    size_t bytes_ = 0;
    mutable std::mutex mutex_;
    // Bound (security): external input needs a finite message count as well as a storage budget. Overflow evicts oldest whole messages and is counted.
    size_t bufferMax_ = 1024;
    std::atomic<uint64_t> dropped_{0};
    FirstStoreLoss warning_;
};

} // namespace trussc::internal
