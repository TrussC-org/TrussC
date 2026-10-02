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
// Reads are acquire, writes release (the RCU pattern).
//
// Where the library ships the C++20 std::atomic<std::shared_ptr<T>>
// specialization (__cpp_lib_atomic_shared_ptr: MSVC, libstdc++ from
// GCC 12) we use it directly. Apple libc++ does not ship it yet
// (checked 2026-10 with Apple clang 21 / libc++ 210106), so there we use
// LockedSharedPtr below: a shared_ptr guarded by a short spinlock, the
// same scheme libstdc++ and the MSVC STL use inside their
// std::atomic<std::shared_ptr>. The deprecated (C++20) and removed
// (C++26) std::atomic_load / std::atomic_store free functions for
// shared_ptr are not used. When Apple ships the specialization, the #if
// below switches over by itself.
// ---------------------------------------------------------------------------
namespace internal {

#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
    // Native C++20 specialization path.
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
    // Fallback: a shared_ptr guarded by a std::atomic_flag spinlock. The
    // critical section only copies or swaps a shared_ptr (a refcount
    // increment and two pointer writes), so spinning is cheaper than a
    // mutex and never blocks in the kernel, which matters for the audio
    // thread reader.
    //
    // Ordering: lock() is test_and_set(acquire) and unlock() is
    // clear(release), so every load() synchronizes with the store() that
    // wrote the value it reads. That gives load() acquire and store()
    // release semantics, matching the native branch above.
    template<class T>
    class LockedSharedPtr {
    public:
        LockedSharedPtr() noexcept = default;
        LockedSharedPtr(std::shared_ptr<T> v) noexcept : ptr_(std::move(v)) {}
        LockedSharedPtr(const LockedSharedPtr&) = delete;
        LockedSharedPtr& operator=(const LockedSharedPtr&) = delete;

        std::shared_ptr<T> load() const noexcept {
            lock();
            std::shared_ptr<T> copy = ptr_;
            unlock();
            return copy;
        }

        void store(std::shared_ptr<T> v) noexcept {
            lock();
            ptr_.swap(v);
            unlock();
            // v now holds the previous value; it is released here,
            // outside the lock, so a destructor never runs while the
            // lock is held.
        }

    private:
        void lock() const noexcept {
            while (flag_.test_and_set(std::memory_order_acquire)) {
#if defined(__cpp_lib_atomic_flag_test) && __cpp_lib_atomic_flag_test >= 201907L
                // Spin on a plain read until the flag looks free, so
                // waiters don't keep writing the cache line.
                while (flag_.test(std::memory_order_relaxed)) {}
#endif
            }
        }
        void unlock() const noexcept {
            flag_.clear(std::memory_order_release);
        }

        mutable std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
        std::shared_ptr<T> ptr_;
    };

    template<class T>
    using AtomicSharedPtr = LockedSharedPtr<T>;

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
