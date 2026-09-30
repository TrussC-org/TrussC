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
// Uses Event system
#include "../events/tcEvent.h"
#include "../events/tcEventListener.h"
#include "tcFileIO.h"   // fs alias + pathToUtf8

namespace trussc {

// ---------------------------------------------------------------------------
// Log level
// ---------------------------------------------------------------------------
enum class LogLevel {
    Verbose,    // Detailed info (for debugging)
    Notice,     // Normal info
    Warning,    // Warning
    Error,      // Error
    Fatal,      // Fatal error
    Silent      // No output (for filtering)
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
// internal::sokolLog): the Logger's console and file sinks then only try the
// lock, and write the line to the console (stderr) alone when another thread
// holds it, so a panic never waits for the lock. Per thread, in tcGlobal.cpp.
bool isLogNonBlocking();
} // namespace internal

// ---------------------------------------------------------------------------
// Logger - Logger core
//
// Thread-safe: log() and the settings below may be called from any thread.
// The console and file sinks write under one mutex, so every line lands
// whole, and closeFile() / setLogFile() wait for a write in progress. The
// other onLog listeners (yours) run outside that mutex, so a listener may
// log again without deadlocking.
// ---------------------------------------------------------------------------
class Logger {
public:
    // Log event (notifies all listeners)
    Event<LogEventArgs> onLog;

    Logger() {
        // Console (stderr/stdout; logcat on Android) and file, as one
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
    // internal::isLogNonBlocking) and writes to the console and the file.
    void writeSinks(const LogEventArgs& e) {
        if (internal::isLogNonBlocking()) {
            if (!mutex_.try_lock()) {
                // Another thread holds the lock: the console alone, unlocked.
                writeConsole(e);
                return;
            }
            struct Unlock {
                TC_MUTEX& m;
                ~Unlock() { m.unlock(); }
            } unlock{mutex_};
            writeConsole(e);
            writeFile(e);
            return;
        }
        TC_LOCK_GUARD(mutex_);
        writeConsole(e);
        writeFile(e);
    }

    void writeConsole(const LogEventArgs& e) {
        const LogLevel level = consoleLevel_.load();
        if (e.level < level || level == LogLevel::Silent) return;
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
#else
        // Desktop/web: stderr/stdout
        std::ostream& out = (e.level >= LogLevel::Warning) ? std::cerr : std::cout;
        out << "[" << e.timestamp << "] "
            << "[" << logLevelToString(e.level) << "] "
            << e.message << std::endl;
#endif
    }

    void writeFile(const LogEventArgs& e) {
        const LogLevel level = fileLevel_.load();
        if (!fileStream_.is_open() || e.level < level || level == LogLevel::Silent) return;
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

    // Console (desktop/web; logcat on Android)
    std::atomic<LogLevel> consoleLevel_{LogLevel::Notice};

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
inline void setConsoleLogLevel(LogLevel level) {
    getLogger().setConsoleLogLevel(level);
}

inline void setFileLogLevel(LogLevel level) {
    getLogger().setFileLogLevel(level);
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
