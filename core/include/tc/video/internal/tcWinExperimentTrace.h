#pragma once
// Throwaway #1547 instrumentation, shared only by the Windows player and harness.
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <vector>

namespace trussc::internal::winexp {
using Clock = std::chrono::steady_clock;
inline bool switchOn(const char* name) {
    const char* value = std::getenv(name);
    return value && std::strcmp(value, "1") == 0;
}
inline bool enabled() {
    static const bool value = switchOn("TRUSSC_EXP_WIN_TRACE");
    return value;
}
inline bool acquire0() {
    static const bool value = switchOn("TRUSSC_EXP_WIN_ZEROCOPY_ACQUIRE0");
    return value;
}
struct Row {
    uint64_t frame, player, counter;
    const char* step;
    double startUs, us;
    int64_t hr, key; // -1 = no key; void API calls have no HRESULT (hasHr=false).
    bool hasHr;
};
inline std::vector<Row> rows;
inline constexpr size_t maxFrames = 600, maxRows = 65536;
inline uint64_t frame = 0, counter = 0;
inline size_t dropped = 0;
inline bool active = false;
inline Clock::time_point epoch;
inline void prepare() {
    if (enabled()) rows.reserve(maxRows);
    (void)acquire0(); // Read the new switch once, at harness startup.
}
inline void beginFrame(uint64_t updateCounter) {
    ++frame;
    counter = updateCounter;
    active = enabled() && frame <= maxFrames;
    if (frame == 1) epoch = Clock::now();
}
inline void record(uint64_t player, const char* step, Clock::time_point start,
                   Clock::time_point end, int64_t hr = 0, bool hasHr = false, int64_t key = -1) {
    if (!active) return;
    if (rows.size() >= maxRows) { ++dropped; return; }
    rows.push_back({frame, player, counter, step,
        std::chrono::duration<double, std::micro>(start - epoch).count(),
        std::chrono::duration<double, std::micro>(end - start).count(), hr, key, hasHr});
}
template<class F> auto call(uint64_t player, const char* step, F&& operation, int64_t key = -1) {
    if (!active) return operation();
    const auto start = Clock::now();
    if constexpr (std::is_void_v<decltype(operation())>) {
        operation();
        record(player, step, start, Clock::now(), 0, false, key);
    } else {
        auto hr = operation();
        record(player, step, start, Clock::now(), static_cast<int64_t>(hr), true, key);
        return hr;
    }
}
struct Scope {
    uint64_t player;
    const char* step;
    bool measuring = active;
    Clock::time_point start = measuring ? Clock::now() : Clock::time_point{};
    ~Scope() { if (measuring) record(player, step, start, Clock::now()); }
};
} // namespace trussc::internal::winexp
