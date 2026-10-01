#pragma once

// =============================================================================
// tcLog.h - Logging system
// =============================================================================

#include <sstream>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <atomic>
#include <cstdint>
#ifdef __ANDROID__
#include <android/log.h>
#endif
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

// The platform log sink (internal::writeSystemLog, in tcGlobal.cpp): Apple's
// unified log (os_log, Console.app) and Windows' debug output
// (OutputDebugStringW: the Visual Studio output window, DebugView). Android
// has logcat in place of the console below.
#if defined(__APPLE__) || defined(_WIN32)
#define TC_LOG_SYSTEM_SINK 1
#else
#define TC_LOG_SYSTEM_SINK 0
#endif
// On iOS (and the other embedded Apple platforms) os_log replaces the
// stdout/stderr console: stdout of an app not started from Xcode is not
// kept, and Xcode's console shows os_log, so writing both would print every
// line twice there.
#if defined(__APPLE__) && TARGET_OS_IPHONE
#define TC_LOG_SYSTEM_SINK_REPLACES_CONSOLE 1
#else
#define TC_LOG_SYSTEM_SINK_REPLACES_CONSOLE 0
#endif
// Uses Event system
#include "../events/tcEvent.h"
#include "../events/tcEventListener.h"
#include "tcFileIO.h"   // fs alias + pathToUtf8

namespace trussc {

// ---------------------------------------------------------------------------
// Log level
// ---------------------------------------------------------------------------
enum class LogLevel {
    Verbose,    // Detailed info (for debugging); hidden by default
    Notice,     // Normal info; the default level of every output
    Warning,    // Something unexpected that the app recovers from
    Error,      // An operation failed
    Fatal,      // The app cannot continue
    Silent      // As an output's level: that output is off
};

// Convert log level to string
inline const char* logLevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Verbose: return "VERBOSE";
        case LogLevel::Notice:  return "NOTICE";
        case LogLevel::Warning: return "WARNING";
        case LogLevel::Error:   return "ERROR";
        case LogLevel::Fatal:   return "FATAL";
        case LogLevel::Silent:  return "SILENT";
    }
    return "UNKNOWN";
}

// ---------------------------------------------------------------------------
// LogEventArgs - Log event arguments
// ---------------------------------------------------------------------------
struct LogEventArgs {
    LogLevel level;
    std::string message;
    std::string timestamp;

    LogEventArgs(LogLevel lvl, const std::string& msg)
        : level(lvl), message(msg) {
        // Generate timestamp
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        std::ostringstream oss;
        std::tm tm_buf{};
#ifdef _WIN32
        localtime_s(&tm_buf, &time);
#else
        // Not std::localtime: it returns one static buffer shared by every
        // thread, and log() runs on any thread.
        localtime_r(&time, &tm_buf);
#endif
        oss << std::put_time(&tm_buf, "%H:%M:%S")
            << '.' << std::setfill('0') << std::setw(3) << ms.count();
        timestamp = oss.str();
    }
};

namespace internal {
// True while the current thread logs from a panic path (the sokol bridge,
// internal::sokolLog): the Logger's sinks then only try the lock, and write
// the line to the console (stderr) and the system log alone when another
// thread holds it, so a panic never waits for the lock. Per thread, in tcGlobal.cpp.
bool isLogNonBlocking();

#if TC_LOG_SYSTEM_SINK
// Writes one log line to the platform log (see TC_LOG_SYSTEM_SINK above). In
// tcGlobal.cpp. The Logger applies the level first: the system level on macOS
// and Windows; on iOS, where it is the console output, the console level.
// - Apple: os_log, subsystem "org.trussc", category "TrussC", as
//   "[LEVEL] message" (Verbose -> debug, Notice / Warning -> default,
//   Error -> error, Fatal -> fault). On macOS it is skipped while os_log is
//   mirrored into the console that already shows stdout (run from Xcode:
//   OS_ACTIVITY_DT_MODE, with any value, or IDE_DISABLED_OS_ACTIVITY_DT_MODE
//   set), so Xcode's console shows each line once.
// - Windows: one OutputDebugStringW call per line, "[time] [LEVEL] message".
void writeSystemLog(const LogEventArgs& e);
#endif
} // namespace internal

// ---------------------------------------------------------------------------
// Logger - Logger core
//
// Three outputs, each with its own level (default Notice; Silent turns that
// output off). setLogLevel() sets all three at once; a later per-output call
// wins.
//   - console: stdout (Verbose / Notice) and stderr (Warning and up) on
//     desktop and web; os_log on iOS; logcat on Android.
//   - file: the file opened with setLogFile() (or TRUSSC_LOG_FILE).
//   - system: os_log on macOS, OutputDebugStringW on Windows. Elsewhere
//     there is none (on iOS and Android the OS log is the console output),
//     and the system level has no effect.
// onLog listeners receive every line, whatever the levels.
//
// Thread-safe: log() and the settings below may be called from any thread.
// The console and file sinks write under one mutex, so every line lands
// whole, and closeFile() / setLogFile() wait for a write in progress. The
// other onLog listeners (yours) run outside that mutex, so a listener may
// log again without deadlocking. One exception: setLogFile() with a relative
// path resolves it through getDataPath(), which reads the data path state
// without a lock, so it must not race setDataPathRoot() or the first
// getDataPath() call. An absolute path does not touch that state.
// ---------------------------------------------------------------------------
class Logger {
public:
    // Log event (notifies all listeners)
    Event<LogEventArgs> onLog;

    Logger() {
        // Console (stderr/stdout; logcat on Android, os_log on iOS), system
        // (os_log on macOS, OutputDebugStringW on Windows) and file, as one
        // listener that takes mutex_ itself.
        sinkListener_ = onLog.listen([this](LogEventArgs& e) {
            writeSinks(e);
        });
    }

    ~Logger() {
        closeFile();
    }

    // === Log output ===

    void log(LogLevel level, const std::string& message) {
        LogEventArgs args(level, message);
        onLog.notify(args);
    }

    // === All outputs ===

    // Sets the console, file and system levels to level. It overwrites them
    // when called; a later setConsoleLogLevel() / setFileLogLevel() /
    // setSystemLogLevel() call wins. No getter: there is no single level.
    void setLogLevel(LogLevel level) {
        consoleLevel_.store(level);
        fileLevel_.store(level);
        systemLevel_.store(level);
    }

    // === Console settings ===

    void setConsoleLogLevel(LogLevel level) {
        consoleLevel_.store(level);
    }

    LogLevel getConsoleLogLevel() const {
        return consoleLevel_.load();
    }

    // === File settings ===

    // Open a log file (append mode). A relative path resolves against the
    // data folder (getDataPath), and a missing parent folder is created. On
    // failure it logs the reason and returns false, and the current log file
    // (if any) stays open. After a successful call, getLogFilePath()
    // returns the resolved path.
    // In tcGlobal.cpp: getDataPath (tcUtils.h) cannot be included here.
    bool setLogFile(const fs::path& path);

    void closeFile() {
        TC_LOCK_GUARD(mutex_);
        closeFileLocked();
    }

    void setFileLogLevel(LogLevel level) {
        fileLevel_.store(level);
    }

    LogLevel getFileLogLevel() const {
        return fileLevel_.load();
    }

    // === System (platform log) settings ===

    // The level of the platform log output: os_log on macOS,
    // OutputDebugStringW on Windows. No effect on iOS and Android (the OS log
    // is the console output there, set with setConsoleLogLevel()) or on
    // Linux and web (no platform log output).
    void setSystemLogLevel(LogLevel level) {
        systemLevel_.store(level);
    }

    LogLevel getSystemLogLevel() const {
        return systemLevel_.load();
    }

    // A copy taken under the lock (empty when no file is open).
    std::string getLogFilePath() const {
        TC_LOCK_GUARD(mutex_);
        return filePath_;
    }

    bool isFileOpen() const {
        TC_LOCK_GUARD(mutex_);
        return fileStream_.is_open();
    }

private:
    // The sink listener. Takes mutex_ (only tries it on a panic path, see
    // internal::isLogNonBlocking) and writes to the system log, the console
    // and the file.
    void writeSinks(const LogEventArgs& e) {
        if (internal::isLogNonBlocking()) {
            if (!mutex_.try_lock()) {
                // Another thread holds the lock: the system log and the
                // console alone, unlocked.
                writeSystem(e);
                writeConsole(e);
                return;
            }
            struct Unlock {
                TC_MUTEX& m;
                ~Unlock() { m.unlock(); }
            } unlock{mutex_};
            writeSystem(e);
            writeConsole(e);
            writeFile(e);
            return;
        }
        TC_LOCK_GUARD(mutex_);
        writeSystem(e);
        writeConsole(e);
        writeFile(e);
    }

    static bool passes(LogLevel lineLevel, LogLevel level) {
        return level != LogLevel::Silent && lineLevel >= level;
    }

    // macOS: os_log; Windows: OutputDebugStringW, by the system level. Not on
    // iOS, where os_log is the console output (writeConsole).
    void writeSystem(const LogEventArgs& e) {
#if TC_LOG_SYSTEM_SINK && !TC_LOG_SYSTEM_SINK_REPLACES_CONSOLE
        if (!passes(e.level, systemLevel_.load())) return;
        internal::writeSystemLog(e);
#else
        (void)e;
#endif
    }

    void writeConsole(const LogEventArgs& e) {
        if (!passes(e.level, consoleLevel_.load())) return;
#ifdef __ANDROID__
        // Android: logcat via __android_log_write
        int prio;
        switch (e.level) {
            case LogLevel::Verbose: prio = ANDROID_LOG_VERBOSE; break;
            case LogLevel::Warning: prio = ANDROID_LOG_WARN; break;
            case LogLevel::Error:   prio = ANDROID_LOG_ERROR; break;
            case LogLevel::Fatal:   prio = ANDROID_LOG_FATAL; break;
            default:                prio = ANDROID_LOG_INFO; break;   // Notice
        }
        __android_log_write(prio, "TrussC", e.message.c_str());
#elif TC_LOG_SYSTEM_SINK_REPLACES_CONSOLE
        // iOS: os_log is the console output
        internal::writeSystemLog(e);
#else
        // Desktop/web: stderr/stdout
        std::ostream& out = (e.level >= LogLevel::Warning) ? std::cerr : std::cout; // log-check: allow (Logger console sink)
        out << "[" << e.timestamp << "] "
            << "[" << logLevelToString(e.level) << "] "
            << e.message << std::endl;
#endif
    }

    void writeFile(const LogEventArgs& e) {
        if (!fileStream_.is_open() || !passes(e.level, fileLevel_.load())) return;
        fileStream_ << "[" << e.timestamp << "] "
                    << "[" << logLevelToString(e.level) << "] "
                    << e.message << std::endl;
        fileStream_.flush();
    }

    void closeFileLocked() {
        if (fileStream_.is_open()) {
            fileStream_.close();
        }
        filePath_.clear();
    }

    // Guards fileStream_ / filePath_ and serializes the console and file
    // writes. Recursive (TC_MUTEX), and a no-op in single-threaded web builds.
    mutable TC_MUTEX mutex_;

    // Console (stdout/stderr; logcat on Android, os_log on iOS)
    std::atomic<LogLevel> consoleLevel_{LogLevel::Notice};

    // System (os_log on macOS, OutputDebugStringW on Windows)
    std::atomic<LogLevel> systemLevel_{LogLevel::Notice};

    // File
    std::ofstream fileStream_;
    std::string filePath_;
    std::atomic<LogLevel> fileLevel_{LogLevel::Notice};

    // Last: disconnected first on destruction, before the state it writes.
    EventListener sinkListener_;
};

// ---------------------------------------------------------------------------
// Global logger
// ---------------------------------------------------------------------------
// Non-inline: Host/Guest share the same logger on Windows hot-reload
Logger& getLogger();

// ---------------------------------------------------------------------------
// sokol -> Logger bridge
// ---------------------------------------------------------------------------
namespace internal {
// The logger.func TrussC passes to the sokol modules it sets up (sapp, sg,
// sgl, simgui), in place of sokol's slog_func: their messages go through the
// Logger (console, log file, onLog listeners). The tag is the module name.
// panic -> Fatal, written without waiting for the Logger's lock and then
// handed on to slog_func, which aborts as before; error -> Error;
// warning -> Warning; info -> Verbose (hidden by default). In tcGlobal.cpp.
void sokolLog(const char* tag, uint32_t logLevel, uint32_t logItem,
              const char* message, uint32_t lineNr, const char* filename,
              void* userData);

// The level mapping and the message sokolLog() logs: "[tag] message", or
// "[tag] id:<item> line:<line>" when sokol passes no message (release builds).
LogLevel sokolLogLevel(uint32_t logLevel);
std::string sokolLogMessage(const char* tag, uint32_t logItem,
                            const char* message, uint32_t lineNr);
} // namespace internal

// ---------------------------------------------------------------------------
// Convenience functions
// ---------------------------------------------------------------------------
inline void setLogLevel(LogLevel level) {
    getLogger().setLogLevel(level);
}

inline void setConsoleLogLevel(LogLevel level) {
    getLogger().setConsoleLogLevel(level);
}

inline void setFileLogLevel(LogLevel level) {
    getLogger().setFileLogLevel(level);
}

inline void setSystemLogLevel(LogLevel level) {
    getLogger().setSystemLogLevel(level);
}

inline bool setLogFile(const fs::path& path) {
    return getLogger().setLogFile(path);
}

inline void closeLogFile() {
    getLogger().closeFile();
}

// ---------------------------------------------------------------------------
// Deprecated tc-prefixed aliases (legacy, pre-namespace naming).
// Removed in v1.0.0 — the tc:: namespace makes the prefix redundant.
// ---------------------------------------------------------------------------
[[deprecated("Use getLogger() instead. Will be removed in v1.0.0")]]
inline Logger& tcGetLogger() { return getLogger(); }

[[deprecated("Use setConsoleLogLevel() instead. Will be removed in v1.0.0")]]
inline void tcSetConsoleLogLevel(LogLevel level) { setConsoleLogLevel(level); }

[[deprecated("Use setFileLogLevel() instead. Will be removed in v1.0.0")]]
inline void tcSetFileLogLevel(LogLevel level) { setFileLogLevel(level); }

[[deprecated("Use setLogFile() instead. Will be removed in v1.0.0")]]
inline bool tcSetLogFile(const fs::path& path) { return setLogFile(path); }

[[deprecated("Use closeLogFile() instead. Will be removed in v1.0.0")]]
inline void tcCloseLogFile() { closeLogFile(); }

// ---------------------------------------------------------------------------
// LogStream - Stream-based log output
// ---------------------------------------------------------------------------
class LogStream {
public:
    LogStream(LogLevel level, const std::string& module = "")
        : level_(level), module_(module) {}

    ~LogStream() {
        if (!moved_) {
            std::string msg = stream_.str();
            if (!module_.empty()) {
                msg = "[" + module_ + "] " + msg;
            }
            getLogger().log(level_, msg);
        }
    }

    // Move only allowed
    LogStream(LogStream&& other) noexcept
        : level_(other.level_)
        , module_(std::move(other.module_))
        , stream_(std::move(other.stream_)) {
        other.moved_ = true;
    }

    LogStream(const LogStream&) = delete;
    LogStream& operator=(const LogStream&) = delete;
    LogStream& operator=(LogStream&&) = delete;

    template<typename T>
    LogStream& operator<<(const T& value) {
        stream_ << value;
        return *this;
    }

    // fs::path is written as UTF-8 text, without quotes. The std::ostream
    // inserter goes through path::string(), which on Windows converts to the
    // active code page and throws for characters it cannot represent, and
    // it quotes the path (std::quoted). pathToDisplayUtf8, not pathToUtf8:
    // logging a path never throws, not even for a name that is not valid
    // UTF-16 (it runs in catch blocks and destructors).
    LogStream& operator<<(const fs::path& path) {
        stream_ << internal::pathToDisplayUtf8(path);
        return *this;
    }

    // Support for manipulators like std::endl
    LogStream& operator<<(std::ostream& (*manip)(std::ostream&)) {
        manip(stream_);
        return *this;
    }

private:
    LogLevel level_;
    std::string module_;
    std::ostringstream stream_;
    bool moved_ = false;
};

// ---------------------------------------------------------------------------
// Log output functions (stream-based)
// Usage:
//   logAt(LogLevel::Warning) << "warning";   // Runtime-selected level
//   logNotice("ClassName") << "message";     // With module name
//   logNotice() << "message";                // Without module name
// ---------------------------------------------------------------------------
inline LogStream logAt(LogLevel level = LogLevel::Notice) {
    return LogStream(level);
}

inline LogStream logVerbose(const std::string& module = "") {
    return LogStream(LogLevel::Verbose, module);
}

inline LogStream logNotice(const std::string& module = "") {
    return LogStream(LogLevel::Notice, module);
}

inline LogStream logWarning(const std::string& module = "") {
    return LogStream(LogLevel::Warning, module);
}

inline LogStream logError(const std::string& module = "") {
    return LogStream(LogLevel::Error, module);
}

inline LogStream logFatal(const std::string& module = "") {
    return LogStream(LogLevel::Fatal, module);
}

// ---------------------------------------------------------------------------
// Deprecated tc-prefixed aliases (legacy, pre-namespace naming). v1.0.0 removal.
// ---------------------------------------------------------------------------
[[deprecated("Use logAt() instead. Will be removed in v1.0.0")]]
inline LogStream tcLog(LogLevel level = LogLevel::Notice) { return logAt(level); }
[[deprecated("Use logVerbose() instead. Will be removed in v1.0.0")]]
inline LogStream tcLogVerbose(const std::string& module = "") { return logVerbose(module); }
[[deprecated("Use logNotice() instead. Will be removed in v1.0.0")]]
inline LogStream tcLogNotice(const std::string& module = "") { return logNotice(module); }
[[deprecated("Use logWarning() instead. Will be removed in v1.0.0")]]
inline LogStream tcLogWarning(const std::string& module = "") { return logWarning(module); }
[[deprecated("Use logError() instead. Will be removed in v1.0.0")]]
inline LogStream tcLogError(const std::string& module = "") { return logError(module); }
[[deprecated("Use logFatal() instead. Will be removed in v1.0.0")]]
inline LogStream tcLogFatal(const std::string& module = "") { return logFatal(module); }

} // namespace trussc
