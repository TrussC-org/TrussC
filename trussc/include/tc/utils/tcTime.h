#pragma once

// =============================================================================
// tcTime.h - Time/Date utilities
// =============================================================================
// API compatible with oF time-related functions
// - getElapsedTimef / getElapsedTimeMillis / getElapsedTimeMicros
// - resetElapsedTimeCounter
// - sleepMillis
// - getTimestampString
// - getSeconds / getMinutes / getHours
// - getYear / getMonth / getDay / getWeekday
// =============================================================================

#include <chrono>
#include <ctime>
#include <string>
#include <sstream>
#include <iomanip>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace trussc {

// ---------------------------------------------------------------------------
// Internal implementation
// ---------------------------------------------------------------------------
namespace internal {

// Clock for elapsed time measurement
class ElapsedTimeClock {
public:
    ElapsedTimeClock() {
        reset();
    }

    void reset() {
        startTime_ = std::chrono::steady_clock::now();
    }

    std::chrono::steady_clock::duration getElapsed() const {
        return std::chrono::steady_clock::now() - startTime_;
    }

private:
    std::chrono::steady_clock::time_point startTime_;
};

inline ElapsedTimeClock& getElapsedClock() {
    static ElapsedTimeClock clock;
    return clock;
}

// String replacement (for getTimestampString)
inline void stringReplace(std::string& input, const std::string& searchStr, const std::string& replaceStr) {
    auto pos = input.find(searchStr);
    while (pos != std::string::npos) {
        input.replace(pos, searchStr.size(), replaceStr);
        pos += replaceStr.size();
        std::string nextfind(input.begin() + pos, input.end());
        auto nextpos = nextfind.find(searchStr);
        if (nextpos == std::string::npos) {
            break;
        }
        pos += nextpos;
    }
}
// Platform-specific localtime (Windows: localtime_s, others: localtime)
inline std::tm safeLocaltime(const std::time_t* t) {
    std::tm result = {};
#ifdef _WIN32
    localtime_s(&result, t);
#else
    result = *std::localtime(t);
#endif
    return result;
}

} // namespace internal

// ---------------------------------------------------------------------------
// Elapsed time
// ---------------------------------------------------------------------------

/// Reset elapsed time counter
inline void resetElapsedTimeCounter() {
    internal::getElapsedClock().reset();
}

/// Get elapsed time in seconds (float)
inline float getElapsedTimef() {
    return std::chrono::duration<float>(internal::getElapsedClock().getElapsed()).count();
}

/// Get elapsed time in milliseconds
inline uint64_t getElapsedTimeMillis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        internal::getElapsedClock().getElapsed()).count();
}

/// Get elapsed time in microseconds
inline uint64_t getElapsedTimeMicros() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        internal::getElapsedClock().getElapsed()).count();
}

// ---------------------------------------------------------------------------
// System time
// ---------------------------------------------------------------------------

/// Get system time in milliseconds (Unix time)
inline uint64_t getSystemTimeMillis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

/// Get system time in microseconds
inline uint64_t getSystemTimeMicros() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// ---------------------------------------------------------------------------
// Sleep
// ---------------------------------------------------------------------------

/// Sleep for specified milliseconds
inline void sleepMillis(int millis) {
    std::this_thread::sleep_for(std::chrono::milliseconds(millis));
}

/// Sleep for specified microseconds
inline void sleepMicros(int micros) {
    std::this_thread::sleep_for(std::chrono::microseconds(micros));
}

// ---------------------------------------------------------------------------
// Timestamp string
// ---------------------------------------------------------------------------

/// Get timestamp string with format specification
/// Format: strftime compatible + %i (milliseconds)
/// Example: "%Y-%m-%d-%H-%M-%S-%i" -> "2024-01-15-18-29-35-299"
inline std::string getTimestampString(const std::string& timestampFormat) {
    std::stringstream str;
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::chrono::duration<double> s = now - std::chrono::system_clock::from_time_t(t);
    int ms = static_cast<int>(s.count() * 1000);
    auto tm = internal::safeLocaltime(&t);
    constexpr int bufsize = 256;
    char buf[bufsize];

    // Replace %i (milliseconds) since strftime doesn't support it
    auto tmpFormat = timestampFormat;
    std::ostringstream msStr;
    msStr << std::setfill('0') << std::setw(3) << ms;
    internal::stringReplace(tmpFormat, "%i", msStr.str());

    if (strftime(buf, bufsize, tmpFormat.c_str(), &tm) != 0) {
        str << buf;
    }
    return str.str();
}

/// Get timestamp string (default format: "2024-01-15-18-29-35-299")
inline std::string getTimestampString() {
    return getTimestampString("%Y-%m-%d-%H-%M-%S-%i");
}

// ---------------------------------------------------------------------------
// Current time components
// ---------------------------------------------------------------------------

/// Current seconds (0-59)
inline int getSeconds() {
    time_t curr;
    time(&curr);
    tm local = internal::safeLocaltime(&curr);
    return local.tm_sec;
}

/// Current minutes (0-59)
inline int getMinutes() {
    time_t curr;
    time(&curr);
    tm local = internal::safeLocaltime(&curr);
    return local.tm_min;
}

/// Current hours (0-23)
inline int getHours() {
    time_t curr;
    time(&curr);
    tm local = internal::safeLocaltime(&curr);
    return local.tm_hour;
}

// ---------------------------------------------------------------------------
// Current date components
// ---------------------------------------------------------------------------

/// Current year (e.g., 2024)
inline int getYear() {
    time_t curr;
    time(&curr);
    tm local = internal::safeLocaltime(&curr);
    return local.tm_year + 1900;
}

/// Current month (1-12)
inline int getMonth() {
    time_t curr;
    time(&curr);
    tm local = internal::safeLocaltime(&curr);
    return local.tm_mon + 1;
}

/// Current day (1-31)
inline int getDay() {
    time_t curr;
    time(&curr);
    tm local = internal::safeLocaltime(&curr);
    return local.tm_mday;
}

/// Current weekday (0=Sunday, 1=Monday, ... 6=Saturday)
inline int getWeekday() {
    time_t curr;
    time(&curr);
    tm local = internal::safeLocaltime(&curr);
    return local.tm_wday;
}

} // namespace trussc
