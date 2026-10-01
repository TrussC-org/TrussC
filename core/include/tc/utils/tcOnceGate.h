#pragma once

// =============================================================================
// tcOnceGate.h - OnceGate: "only the first time" gate (e.g. for a log line)
// =============================================================================
// Usage:
//   static OnceGate overflowWarned;              // once
//   if (overflowWarned.isFirstTime()) logError("Fbo") << "...";
//
//   static OnceGate underrun{5.0};               // at most once per 5 s
//   if (underrun.isFirstTime()) logWarning("Audio") << "...";
//
//   class Foo { OnceGate notWaitedWarned_; };    // once per object
// =============================================================================

#include <atomic>
#include <chrono>
#include <cstdint>
#include <type_traits>

namespace trussc {

// isFirstTime() returns true the first time it is called. With an interval
// (seconds, constructor argument) it returns true again once that much time
// has passed since the last true (steady clock). The gate object is the key:
// one gate per call site (a `static`), or a member for once per object.
// Thread-safe and lock-free: when several threads call it at once, exactly
// one of them gets true. constexpr-constructible and trivially destructible,
// so a `static` gate has no initialization-order issue and still works
// during static destruction. Not copyable, not movable.
class OnceGate {
public:
    constexpr OnceGate() noexcept = default;
    constexpr explicit OnceGate(double intervalSeconds) noexcept
        : intervalNs_(toNanoseconds(intervalSeconds)) {}

    OnceGate(const OnceGate&) = delete;
    OnceGate& operator=(const OnceGate&) = delete;

    bool isFirstTime() noexcept {
        int64_t last = lastTrueNs_.load(std::memory_order_acquire);
        if (intervalNs_ <= 0) {
            if (last != kNever) return false;
            return lastTrueNs_.compare_exchange_strong(last, 0, std::memory_order_acq_rel);
        }
        const int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        if (last != kNever && now - last < intervalNs_) return false;
        // Only one caller can swap `last` for `now`; the others see a changed
        // value and get false.
        return lastTrueNs_.compare_exchange_strong(last, now, std::memory_order_acq_rel);
    }

private:
    static constexpr int64_t kNever = INT64_MIN;

    // Seconds -> nanoseconds; 0 (once) for 0, negative or NaN, and the
    // largest int64 for an interval too long to represent.
    static constexpr int64_t toNanoseconds(double seconds) noexcept {
        if (!(seconds > 0)) return 0;
        if (seconds >= 9.2e9) return INT64_MAX;
        return static_cast<int64_t>(seconds * 1e9);
    }

    int64_t intervalNs_ = 0;                  // 0: once
    std::atomic<int64_t> lastTrueNs_{kNever}; // steady-clock time of the last true
};

static_assert(std::is_trivially_destructible_v<OnceGate>,
              "OnceGate must stay usable during static destruction");

} // namespace trussc
