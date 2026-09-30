#pragma once
#include "tc/utils/tcAnnotations.h"

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
// Destruction:
//   A subclass must call waitForThread() in its OWN destructor:
//
//     ~MyThread() { waitForThread(); }   // stop, then join
//
//   The base destructor also stops and joins, but it runs after the subclass
//   destructor, so the subclass members are already destroyed while
//   threadedFunction() may still be using them. It logs a warning when it
//   finds the thread still running. Calling only stopThread() (the ofThread
//   exit() habit) does not wait either.
//
//   A subclass that does not wait may also be destroyed before its worker
//   has called threadedFunction(), e.g. right after startThread() (with or
//   without stopThread()). What happens then depends on when the worker
//   makes that call:
//   - while the subclass destructor is running: the real threadedFunction()
//     runs and may use members that are already destroyed. This is undefined
//     behaviour, and usually nothing aborts to point at it.
//   - at about the moment ~Thread() starts: the call can still reach the
//     pure virtual threadedFunction() ("pure virtual method called").
//   - after ~Thread() has started: the worker skips threadedFunction(), and
//     the base destructor logs the warning and joins it.
//   Only waiting in the subclass destructor (waitForThread()) rules out all
//   three, and threadedFunction() then always runs.
//
//   A subclass that waits must not be destroyed on its own worker: from its
//   threadedFunction() (delete this, or dropping the last shared_ptr there),
//   or after it returned (a thread_local owner released at thread exit). Its
//   waitForThread() would then join its own thread, which throws
//   std::system_error (resource_deadlock_would_occur) and terminates. Only a
//   subclass that does not wait survives this: the base destructor detaches.
//
// Mutex usage:
//   As documented, no custom wrappers provided.
//   std::mutex and std::lock_guard are recommended.
//
// ---------------------------------------------------------------------------

namespace internal {
// Logs the warning for a Thread destroyed while its thread is still running.
// Defined in tcGlobal.cpp: this header is included before tcLog.h
// (tcEvent.h -> tcMainThread.h -> tcThread.h), so it cannot log by itself.
void logThreadNotWaited();
} // namespace internal

class TC_PLATFORMS("macos,windows,linux,android,ios") Thread {
public:
    Thread() : threadRunning_(false) {}

    // Stops the thread and joins it, whether it is still running, was only
    // sent stopThread(), or has already returned. This is a safety net only:
    // see "Destruction" above.
    virtual ~Thread() {
        // Before anything else: the subclass part is already destroyed, so a
        // worker that has not called threadedFunction() yet must skip it (see
        // startThread()). Only a worker that has already passed that check
        // but not made the call yet can still reach the pure virtual.
        destroying_ = true;
        const bool joinable = thread_.joinable();
        const bool fromOwnThread =
            joinable && thread_.get_id() == std::this_thread::get_id();
        // The worker has not finished (threadedFunction() has not returned,
        // has not been called yet, or was skipped), yet the subclass
        // destructor has already run: the subclass did not wait.
        const bool notWaited = joinable && !fromOwnThread && workerActive_;

        stopThread();
        if (notWaited) {
            internal::logThreadNotWaited();
        }
        if (joinable) {
            if (fromOwnThread) {
                // Destroyed on the worker: from inside threadedFunction(), or
                // after it returned (e.g. a thread_local owner released at
                // thread exit). Joining itself would throw
                // resource_deadlock_would_occur, so detach. In the first case,
                // also tell the worker not to touch this object once
                // threadedFunction() returns (see startThread()). In the
                // second, selfDestroyed_ is null: its flag's frame is gone.
                if (selfDestroyed_) *selfDestroyed_ = true;
                thread_.detach();
            } else {
                thread_.join();
            }
        }
    }

    // No copy
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;

    // Moving does not move the running thread: the new object gets none. A
    // running thread can't follow a moved object in this inheritance design,
    // so e.g. vector<MyThread> growth would silently stop every worker. Hold
    // Thread subclasses via unique_ptr / shared_ptr instead.
    [[deprecated("Moving a Thread does not move the running thread. Hold it via unique_ptr. Will be removed in v1.0.0")]]
    Thread(Thread&& other) noexcept : threadRunning_(false) {
        // Ensure source is not running
        if (other.isThreadRunning()) {
            // Cannot move running thread
            return;
        }
    }

    [[deprecated("Moving a Thread does not move the running thread. Hold it via unique_ptr. Will be removed in v1.0.0")]]
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
        workerActive_ = true;
        // Held until thread_ is assigned. The worker waits for it, so
        // threadedFunction() cannot destroy this object (see the destructor)
        // while thread_ is still being assigned here.
        std::lock_guard<std::mutex> lock(startMutex_);
        thread_ = std::thread([this]() {
            { std::lock_guard<std::mutex> started(startMutex_); }
            // Once ~Thread() has started, the call would reach the pure
            // virtual: skip it and touch nothing else. workerActive_ stays
            // true, so the destructor warns (the subclass did not wait), and
            // it joins this worker.
            if (destroying_) return;
            // Lives on this worker's stack, so it outlives the object if
            // threadedFunction() destroys it (the destructor sets it).
            bool destroyed = false;
            selfDestroyed_ = &destroyed;
            threadedFunction();
            if (destroyed) return;   // this object is gone: touch nothing
            // `destroyed` dies with this lambda, but a destructor may still
            // run on this worker (a thread_local owner released at thread
            // exit): it must not write to it.
            selfDestroyed_ = nullptr;
            // Clear workerActive_ first: once isThreadRunning() reads false
            // for a worker that returned, the destructor sees it as finished.
            workerActive_ = false;
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
    // Records current thread ID on first call (the framework makes that call on
    // the main thread at startup). Defined in tcGlobal.cpp so the id is one per
    // process: header-inline, a Windows hot reload guest recorded its own, on
    // whichever thread first asked (#249).
    static std::thread::id getMainThreadId();

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
    std::mutex dataMutex_;

private:
    std::thread thread_;
    std::atomic<bool> threadRunning_;
    // True from startThread() until threadedFunction() returns. A worker that
    // skips threadedFunction() (see destroying_) leaves it true. Unlike
    // threadRunning_, stopThread() does not clear it.
    std::atomic<bool> workerActive_{false};
    // Points to a flag on the worker's stack while threadedFunction() runs,
    // null otherwise. Only the worker thread touches it: startThread()'s
    // lambda, and the destructor when it runs on that worker.
    bool* selfDestroyed_ = nullptr;
    // Held by startThread() while it assigns thread_ (see there).
    std::mutex startMutex_;
    // Set first thing in ~Thread(). A worker that has not called
    // threadedFunction() by then skips it.
    std::atomic<bool> destroying_{false};
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
