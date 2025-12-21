#pragma once

#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>

namespace trussc {

// ---------------------------------------------------------------------------
// Thread - Thread base class (ofThread compatible)
// ---------------------------------------------------------------------------
//
// Usage:
// 1. Create a class inheriting from Thread
// 2. Override threadedFunction() with implementation
// 3. Start thread with startThread()
// 4. Signal thread stop with stopThread()
// 5. Wait for thread to finish with waitForThread()
//
// Example:
//   class MyThread : public tc::Thread {
//   protected:
//       void threadedFunction() override {
//           while (isThreadRunning()) {
//               // Processing
//           }
//       }
//   };
//
// Mutex usage:
//   As documented, no custom wrappers provided.
//   std::mutex and std::lock_guard are recommended.
//
// ---------------------------------------------------------------------------

class Thread {
public:
    Thread() : threadRunning_(false) {}

    virtual ~Thread() {
        // If thread is running, stop and wait
        if (isThreadRunning()) {
            stopThread();
            waitForThread(false);
        }
    }

    // No copy
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;

    // Move allowed (but not while thread is running)
    Thread(Thread&& other) noexcept : threadRunning_(false) {
        // Ensure source is not running
        if (other.isThreadRunning()) {
            // Cannot move running thread
            return;
        }
    }

    Thread& operator=(Thread&& other) noexcept {
        if (this != &other) {
            if (isThreadRunning()) {
                stopThread();
                waitForThread(false);
            }
        }
        return *this;
    }

    // ---------------------------------------------------------------------------
    // Thread control
    // ---------------------------------------------------------------------------

    // Start thread
    void startThread() {
        if (isThreadRunning()) return;

        // Join previous thread if still exists
        if (thread_.joinable()) {
            thread_.join();
        }

        threadRunning_ = true;
        thread_ = std::thread([this]() {
            threadedFunction();
            threadRunning_ = false;
        });
    }

    // Send stop signal to thread
    // isThreadRunning() will return false in threadedFunction
    void stopThread() {
        threadRunning_ = false;
    }

    // Wait for thread to finish
    // callStopThread: if true, calls stopThread() first
    void waitForThread(bool callStopThread = true) {
        if (callStopThread) {
            stopThread();
        }
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    // Whether thread is running
    bool isThreadRunning() const {
        return threadRunning_;
    }

    // Get thread ID
    std::thread::id getThreadId() const {
        return thread_.get_id();
    }

    // ---------------------------------------------------------------------------
    // Utilities
    // ---------------------------------------------------------------------------

    // Pause current thread
    static void sleep(unsigned long milliseconds) {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }

    // Yield execution to other threads
    static void yield() {
        std::this_thread::yield();
    }

    // Whether current thread is main thread
    // Note: Must call once from main thread first to record its ID
    static bool isCurrentThreadTheMainThread() {
        return std::this_thread::get_id() == getMainThreadId();
    }

    // Get/set main thread ID
    // Records current thread ID on first call
    static std::thread::id getMainThreadId() {
        static std::thread::id mainThreadId = std::this_thread::get_id();
        return mainThreadId;
    }

protected:
    // ---------------------------------------------------------------------------
    // Functions to implement in subclass
    // ---------------------------------------------------------------------------

    // Processing executed in thread
    // Override in subclass to implement
    // Recommend using while (isThreadRunning()) { ... } loop
    virtual void threadedFunction() = 0;

    // ---------------------------------------------------------------------------
    // Mutex (available to subclasses)
    // ---------------------------------------------------------------------------

    // Use when sharing data between threads
    std::mutex mutex;

private:
    std::thread thread_;
    std::atomic<bool> threadRunning_;
};

// ---------------------------------------------------------------------------
// Helper functions
// ---------------------------------------------------------------------------

// Get main thread ID (alias)
inline std::thread::id getMainThreadId() {
    return Thread::getMainThreadId();
}

// Whether current thread is main thread
inline bool isMainThread() {
    return Thread::isCurrentThreadTheMainThread();
}

} // namespace trussc
