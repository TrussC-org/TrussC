// =============================================================================
// tcLog.cpp - logOnce() state (one copy per process)
// =============================================================================

#include "tc/utils/tcLog.h"

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

namespace trussc {

namespace {

struct LogOnceState {
    std::mutex mutex;
    // key -> time of the last call that returned true
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> lastTrue;
};

// Never destroyed, so logOnce() keeps working during static destruction.
LogOnceState& logOnceState() {
    static LogOnceState* state = new LogOnceState();
    return *state;
}

} // namespace

bool logOnce(const std::string& key, double intervalSeconds) {
    const auto now = std::chrono::steady_clock::now();
    LogOnceState& s = logOnceState();
    std::lock_guard<std::mutex> lock(s.mutex);
    auto it = s.lastTrue.find(key);
    if (it == s.lastTrue.end()) {
        s.lastTrue.emplace(key, now);
        return true;
    }
    if (intervalSeconds > 0 &&
        std::chrono::duration<double>(now - it->second).count() >= intervalSeconds) {
        it->second = now;
        return true;
    }
    return false;
}

} // namespace trussc
