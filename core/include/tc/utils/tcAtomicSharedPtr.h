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
// but Apple libc++ doesn't ship it yet (verified 2026-05). We fall back
// to the (C++20-deprecated) std::atomic_load / std::atomic_store free
// functions, suppressing the deprecation warning locally — when the
// specialization lands the storage type and accessors auto-switch.
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
    // Fallback: a plain shared_ptr accessed via the deprecated free-
    // function atomic API. Still lock-free for shared_ptr on common
    // platforms; the deprecation is for ergonomics only.
    template<class T>
    using AtomicSharedPtr = std::shared_ptr<T>;

    // Use the *_explicit forms with matching acquire/release ordering so
    // this path is symmetric with the C++20 specialization branch above
    // — without the explicit, the free functions default to seq_cst and
    // we'd silently take a stronger fence on Apple while GCC / MSVC ran
    // with the weaker order. Identical observable behavior for our 1
    // producer (UI) / 1 consumer (audio) usage, but keeps the two
    // branches honest.
    template<class T>
    inline std::shared_ptr<T> sharedLoad(const std::shared_ptr<T>& p) {
#       if defined(__clang__) || defined(__GNUC__)
#           pragma GCC diagnostic push
#           pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#       elif defined(_MSC_VER)
#           pragma warning(push)
#           pragma warning(disable: 4996)
#       endif
        return std::atomic_load_explicit(&p, std::memory_order_acquire);
#       if defined(__clang__) || defined(__GNUC__)
#           pragma GCC diagnostic pop
#       elif defined(_MSC_VER)
#           pragma warning(pop)
#       endif
    }
    template<class T>
    inline void sharedStore(std::shared_ptr<T>& p, std::shared_ptr<T> v) {
#       if defined(__clang__) || defined(__GNUC__)
#           pragma GCC diagnostic push
#           pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#       elif defined(_MSC_VER)
#           pragma warning(push)
#           pragma warning(disable: 4996)
#       endif
        std::atomic_store_explicit(&p, std::move(v), std::memory_order_release);
#       if defined(__clang__) || defined(__GNUC__)
#           pragma GCC diagnostic pop
#       elif defined(_MSC_VER)
#           pragma warning(pop)
#       endif
    }
#endif

} // namespace internal

} // namespace trussc
