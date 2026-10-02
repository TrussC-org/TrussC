#pragma once

#include <mutex>
#include <queue>
#include <vector>
#include <utility>
#include <condition_variable>
#include <chrono>
#include <cstddef>

namespace trussc {

// ---------------------------------------------------------------------------
// ThreadChannel - Thread-safe queue for inter-thread communication (ofThreadChannel compatible)
// ---------------------------------------------------------------------------
//
// Implementation of the Producer-Consumer pattern.
// For one-way communication. Use two channels for bidirectional communication.
//
// Usage:
//   // Sender side (worker thread)
//   channel.send(data);
//
//   // Receiver side (main thread)
//   DataType received;
//   if (channel.receive(received)) {
//       // Receive successful
//   }
//
// Data is sent/received in FIFO (first-in-first-out) order.
//
// ---------------------------------------------------------------------------

template<typename T>
class ThreadChannel {
public:
    ThreadChannel() : closed_(false) {}

    // No copy
    ThreadChannel(const ThreadChannel&) = delete;
    ThreadChannel& operator=(const ThreadChannel&) = delete;

    // ---------------------------------------------------------------------------
    // Send
    // ---------------------------------------------------------------------------

    // Send value (copy)
    // Returns false if channel is closed
    bool send(const T& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (closed_) {
            return false;
        }
        queue_.push(value);
        condition_.notify_one();
        return true;
    }

    // Send value (move)
    // Returns false if channel is closed
    // Note: value is invalidated even on failure
    bool send(T&& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (closed_) {
            return false;
        }
        queue_.push(std::move(value));
        condition_.notify_one();
        return true;
    }

    // ---------------------------------------------------------------------------
    // Receive
    // ---------------------------------------------------------------------------

    // Receive value (blocking)
    // Waits until data arrives
    // Returns false if channel is closed
    bool receive(T& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (closed_) {
            return false;
        }
        while (queue_.empty() && !closed_) {
            condition_.wait(lock);
        }
        if (!closed_ && !queue_.empty()) {
            std::swap(value, queue_.front());
            queue_.pop();
            return true;
        }
        return false;
    }

    // Receive value (non-blocking)
    // Returns false immediately if no data
    bool tryReceive(T& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (closed_ || queue_.empty()) {
            return false;
        }
        std::swap(value, queue_.front());
        queue_.pop();
        return true;
    }

    // Receive value (with timeout)
    // Waits for specified time, returns false if no data
    bool tryReceive(T& value, int64_t timeoutMs) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (closed_) {
            return false;
        }
        if (queue_.empty()) {
            condition_.wait_for(lock, std::chrono::milliseconds(timeoutMs));
            if (queue_.empty()) {
                return false;
            }
        }
        if (!closed_ && !queue_.empty()) {
            std::swap(value, queue_.front());
            queue_.pop();
            return true;
        }
        return false;
    }

    // Receive everything queued right now (non-blocking), in FIFO order.
    // Under the lock it only swaps the queue with an empty one; the values are
    // moved into the returned vector after the lock is released, so senders
    // wait only for the swap. Values sent after this call stay queued for the
    // next receive. Returns an empty vector if the channel is empty or closed.
    //
    //   for (auto& msg : channel.receiveAll()) {
    //       handle(msg);
    //   }
    std::vector<T> receiveAll() {
        std::queue<T> taken;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (!closed_) {
                taken.swap(queue_);
            }
        }
        std::vector<T> out;
        out.reserve(taken.size());
        while (!taken.empty()) {
            out.push_back(std::move(taken.front()));
            taken.pop();
        }
        return out;
    }

    // ---------------------------------------------------------------------------
    // Control
    // ---------------------------------------------------------------------------

    // Close channel
    // Wakes all waiting threads
    // After closing, send/receive return false
    void close() {
        std::unique_lock<std::mutex> lock(mutex_);
        closed_ = true;
        condition_.notify_all();
    }

    // Clear queue
    void clear() {
        std::unique_lock<std::mutex> lock(mutex_);
        queue_ = {};
    }

    // ---------------------------------------------------------------------------
    // State
    // ---------------------------------------------------------------------------

    // Whether queue is empty. Takes the lock; other threads may send or
    // receive right after it returns.
    bool empty() const {
        std::unique_lock<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    // Queue size. Takes the lock; other threads may send or receive right
    // after it returns.
    size_t size() const {
        std::unique_lock<std::mutex> lock(mutex_);
        return queue_.size();
    }

    // Whether channel is closed
    bool isClosed() const {
        return closed_;
    }

private:
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    bool closed_;
};

} // namespace trussc
