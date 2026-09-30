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

// ---------------------------------------------------------------------------
// The elapsed-time clock (#229)
// ---------------------------------------------------------------------------
// One clock for everything: std::chrono::steady_clock with a single origin
// taken at program start (during static initialization of tcGlobal.cpp), not
// at whichever call happens to come first. It is never reset: framework code
// that needs a timestamp (ScreenRecorder, the tc_get_health uptime) reads
// getUptime(), the loops measure steady_clock intervals, and Node timers
// count down by getDeltaTime().
//
// resetElapsedTimeCounter() does not move the origin: it only sets a display
// offset that the public getElapsedTime*() family subtracts, so resetting the
// counter can never delay or starve framework timing.
//
// Non-inline (tcGlobal.cpp): the hot-reload Host and Guest share one clock.

// Time since program start. Monotonic, never reset.
std::chrono::steady_clock::duration getUptime();

// Display offset subtracted by getElapsedTime*() (set by resetElapsedTimeCounter()).
std::chrono::steady_clock::duration getElapsedTimeOffset();
void setElapsedTimeOffset(std::chrono::steady_clock::duration offset);

// Time since program start in seconds (double). Monotonic, never reset.
inline double getUptimeSeconds() {
    return std::chrono::duration<double>(getUptime()).count();
}

// What getElapsedTime*() report: uptime minus the display offset. Clamped at
// zero so a reset racing with a read on another thread never goes negative.
inline std::chrono::steady_clock::duration getElapsedDuration() {
    auto d = getUptime() - getElapsedTimeOffset();
    return d.count() < 0 ? std::chrono::steady_clock::duration::zero() : d;
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

/// Restart the counter that getElapsedTime(), getElapsedTimef(),
/// getElapsedTimeMillis(), getElapsedTimeMicros() and getFrameElapsedTime()
/// report. Display only: framework timing (Node timers, the loop, recording)
/// keeps running on the underlying clock and is not affected. A difference of
/// two getElapsedTime*() readings taken across a reset is wrong (negative, or
/// wrapped for the unsigned Millis/Micros): measure durations with
/// getSystemTimeMicros() differences as int64_t.
inline void resetElapsedTimeCounter() {
    internal::setElapsedTimeOffset(internal::getUptime());
}

/// Get elapsed time in seconds (float). Same clock as getElapsedTime(); float
/// loses precision after about a day of uptime (7.8 ms steps at 18 h), so use
/// it for animation and display; getElapsedTime() (double) keeps full
/// precision.
inline float getElapsedTimef() {
    return std::chrono::duration<float>(internal::getElapsedDuration()).count();
}

/// Get elapsed time in milliseconds
inline uint64_t getElapsedTimeMillis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        internal::getElapsedDuration()).count();
}

/// Get elapsed time in microseconds
inline uint64_t getElapsedTimeMicros() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        internal::getElapsedDuration()).count();
}

// ---------------------------------------------------------------------------
// System time
// ---------------------------------------------------------------------------

/// Get system time in milliseconds (Unix time). Wall clock: it follows clock
/// adjustments (NTP steps, manual changes), so a later reading can be smaller.
/// Take differences as int64_t, never as unsigned.
inline uint64_t getSystemTimeMillis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

/// Get system time in microseconds (Unix time). Wall clock, like
/// getSystemTimeMillis(): take differences as int64_t (a clock step can make
/// t1 < t0, and an unsigned difference would wrap).
inline uint64_t getSystemTimeMicros() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

/// Get Unix time in seconds (since 1970-01-01 00:00:00 UTC)
inline uint64_t getUnixTime() {
    return std::chrono::duration_cast<std::chrono::seconds>(
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
