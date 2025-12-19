#pragma once

// =============================================================================
// tcTime.h - 時間・日付ユーティリティ
// =============================================================================
// oFの時間関連関数と同じAPI
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
// 内部実装
// ---------------------------------------------------------------------------
namespace internal {

// 経過時間計測用のクロック
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

// 文字列置換（getTimestampString 用）
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
// localtime のプラットフォーム分岐（Windows: localtime_s, その他: localtime）
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
// 経過時間
// ---------------------------------------------------------------------------

/// 経過時間カウンターをリセット
inline void resetElapsedTimeCounter() {
    internal::getElapsedClock().reset();
}

/// 経過時間を秒（float）で取得
inline float getElapsedTimef() {
    return std::chrono::duration<float>(internal::getElapsedClock().getElapsed()).count();
}

/// 経過時間をミリ秒で取得
inline uint64_t getElapsedTimeMillis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        internal::getElapsedClock().getElapsed()).count();
}

/// 経過時間をマイクロ秒で取得
inline uint64_t getElapsedTimeMicros() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        internal::getElapsedClock().getElapsed()).count();
}

// ---------------------------------------------------------------------------
// システム時刻
// ---------------------------------------------------------------------------

/// システム時刻をミリ秒で取得（Unix時刻）
inline uint64_t getSystemTimeMillis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

/// システム時刻をマイクロ秒で取得
inline uint64_t getSystemTimeMicros() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// ---------------------------------------------------------------------------
// スリープ
// ---------------------------------------------------------------------------

/// ミリ秒単位でスリープ
inline void sleepMillis(int millis) {
    std::this_thread::sleep_for(std::chrono::milliseconds(millis));
}

/// マイクロ秒単位でスリープ
inline void sleepMicros(int micros) {
    std::this_thread::sleep_for(std::chrono::microseconds(micros));
}

// ---------------------------------------------------------------------------
// タイムスタンプ文字列
// ---------------------------------------------------------------------------

/// フォーマット指定でタイムスタンプ文字列を取得
/// フォーマット: strftime互換 + %i（ミリ秒）
/// 例: "%Y-%m-%d-%H-%M-%S-%i" → "2024-01-15-18-29-35-299"
inline std::string getTimestampString(const std::string& timestampFormat) {
    std::stringstream str;
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::chrono::duration<double> s = now - std::chrono::system_clock::from_time_t(t);
    int ms = static_cast<int>(s.count() * 1000);
    auto tm = internal::safeLocaltime(&t);
    constexpr int bufsize = 256;
    char buf[bufsize];

    // %i（ミリ秒）を置換（strftimeは %i をサポートしていないため）
    auto tmpFormat = timestampFormat;
    std::ostringstream msStr;
    msStr << std::setfill('0') << std::setw(3) << ms;
    internal::stringReplace(tmpFormat, "%i", msStr.str());

    if (strftime(buf, bufsize, tmpFormat.c_str(), &tm) != 0) {
        str << buf;
    }
    return str.str();
}

/// タイムスタンプ文字列を取得（デフォルトフォーマット: "2024-01-15-18-29-35-299"）
inline std::string getTimestampString() {
    return getTimestampString("%Y-%m-%d-%H-%M-%S-%i");
}

// ---------------------------------------------------------------------------
// 現在時刻の各要素
// ---------------------------------------------------------------------------

/// 現在の秒（0-59）
inline int getSeconds() {
    time_t curr;
    time(&curr);
    tm local = internal::safeLocaltime(&curr);
    return local.tm_sec;
}

/// 現在の分（0-59）
inline int getMinutes() {
    time_t curr;
    time(&curr);
    tm local = internal::safeLocaltime(&curr);
    return local.tm_min;
}

/// 現在の時（0-23）
inline int getHours() {
    time_t curr;
    time(&curr);
    tm local = internal::safeLocaltime(&curr);
    return local.tm_hour;
}

// ---------------------------------------------------------------------------
// 現在日付の各要素
// ---------------------------------------------------------------------------

/// 現在の年（例: 2024）
inline int getYear() {
    time_t curr;
    time(&curr);
    tm local = internal::safeLocaltime(&curr);
    return local.tm_year + 1900;
}

/// 現在の月（1-12）
inline int getMonth() {
    time_t curr;
    time(&curr);
    tm local = internal::safeLocaltime(&curr);
    return local.tm_mon + 1;
}

/// 現在の日（1-31）
inline int getDay() {
    time_t curr;
    time(&curr);
    tm local = internal::safeLocaltime(&curr);
    return local.tm_mday;
}

/// 現在の曜日（0=日曜, 1=月曜, ... 6=土曜）
inline int getWeekday() {
    time_t curr;
    time(&curr);
    tm local = internal::safeLocaltime(&curr);
    return local.tm_wday;
}

} // namespace trussc
