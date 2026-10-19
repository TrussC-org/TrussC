#pragma once

// =============================================================================
// tcAtomicSharedPtr.h - atomic shared_ptr shim (internal)
// =============================================================================

#include <atomic>
#include <memory>
#include <utility>

namespace trussc {

// ---------------------------------------------------------------------------
// Atomic shared_ptr shim
//
// Users: Event (tcEvent.h) keeps its listener list as a copy-on-write
// snapshot read by notify() and replaced by listen / remove / clear;
// PlayingSound (tcSound.h) stores routing snapshots (channelMap /
// channelGains) that the UI thread updates and the audio thread reads.
// Reads are acquire, writes release (the RCU pattern). We'd
// like to use the C++20 std::atomic<std::shared_ptr<T>> specialization,
// but Apple libc++ doesn't ship it yet. The fallback guards a shared_ptr
// with a short spinlock — when the specialization lands the storage type
// and accessors auto-switch.
// ---------------------------------------------------------------------------
namespace internal {

#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
    // Native C++20 specialization path — lock-free where supported,
    // no deprecation warnings.
    template<class T>
    using AtomicSharedPtr = std::atomic<std::shared_ptr<T>>;

    template<class T>
    inline std::shared_ptr<T> sharedLoad(const AtomicSharedPtr<T>& p) {
        return p.load(std::memory_order_acquire);
    }
    template<class T>
    inline void sharedStore(AtomicSharedPtr<T>& p, std::shared_ptr<T> v) {
        p.store(std::move(v), std::memory_order_release);
    }
#else
    // Only copying or swapping the shared_ptr happens under the lock.
    // In particular, store releases the old value after unlocking: its
    // destructor / deleter may do arbitrary work, including another load.
    template<class T>
    class AtomicSharedPtr {
    public:
        AtomicSharedPtr() noexcept = default;
        AtomicSharedPtr(std::shared_ptr<T> value) noexcept
            : value_(std::move(value)) {}

        AtomicSharedPtr(const AtomicSharedPtr&) = delete;
        AtomicSharedPtr& operator=(const AtomicSharedPtr&) = delete;

        std::shared_ptr<T> load() const noexcept {
            lock();
            auto value = value_;
            unlock();
            return value;
        }

        void store(std::shared_ptr<T> value) noexcept {
            lock();
            value_.swap(value);
            unlock();
        }

    private:
        void lock() const noexcept {
            while (lock_.test_and_set(std::memory_order_acquire)) {}
        }
        void unlock() const noexcept {
            lock_.clear(std::memory_order_release);
        }

        mutable std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
        std::shared_ptr<T> value_;
    };

    // The lock's acquire/release synchronizes snapshot publication, matching
    // the ordering of the native accessors above.
    template<class T>
    inline std::shared_ptr<T> sharedLoad(const AtomicSharedPtr<T>& p) {
        return p.load();
    }
    template<class T>
    inline void sharedStore(AtomicSharedPtr<T>& p, std::shared_ptr<T> v) {
        p.store(std::move(v));
    }
#endif

} // namespace internal

} // namespace trussc
