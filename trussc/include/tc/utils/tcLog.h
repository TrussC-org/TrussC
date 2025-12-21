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

// Uses Event system
#include "../events/tcEvent.h"
#include "../events/tcEventListener.h"

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
#ifdef _WIN32
        std::tm tm_buf;
        localtime_s(&tm_buf, &time);
        oss << std::put_time(&tm_buf, "%H:%M:%S")
#else
        oss << std::put_time(std::localtime(&time), "%H:%M:%S")
#endif
            << '.' << std::setfill('0') << std::setw(3) << ms.count();
        timestamp = oss.str();
    }
};

// ---------------------------------------------------------------------------
// Logger - Logger core
// ---------------------------------------------------------------------------
class Logger {
public:
    // Log event (notifies all listeners)
    Event<LogEventArgs> onLog;

    Logger() {
        // Register console listener by default
        onLog.listen(consoleListener_, [this](LogEventArgs& e) {
            if (e.level >= consoleLevel_ && consoleLevel_ != LogLevel::Silent) {
                std::ostream& out = (e.level >= LogLevel::Warning) ? std::cerr : std::cout;
                out << "[" << e.timestamp << "] "
                    << "[" << logLevelToString(e.level) << "] "
                    << e.message << std::endl;
            }
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
        consoleLevel_ = level;
    }

    LogLevel getConsoleLogLevel() const {
        return consoleLevel_;
    }

    // === File settings ===

    bool setLogFile(const std::string& path) {
        closeFile();

        fileStream_.open(path, std::ios::app);
        if (!fileStream_.is_open()) {
            log(LogLevel::Error, "Failed to open log file: " + path);
            return false;
        }

        filePath_ = path;

        // Register file listener
        onLog.listen(fileListener_, [this](LogEventArgs& e) {
            if (fileStream_.is_open() &&
                e.level >= fileLevel_ && fileLevel_ != LogLevel::Silent) {
                fileStream_ << "[" << e.timestamp << "] "
                           << "[" << logLevelToString(e.level) << "] "
                           << e.message << std::endl;
                fileStream_.flush();
            }
        });

        return true;
    }

    void closeFile() {
        fileListener_.disconnect();
        if (fileStream_.is_open()) {
            fileStream_.close();
        }
        filePath_.clear();
    }

    void setFileLogLevel(LogLevel level) {
        fileLevel_ = level;
    }

    LogLevel getFileLogLevel() const {
        return fileLevel_;
    }

    const std::string& getLogFilePath() const {
        return filePath_;
    }

    bool isFileOpen() const {
        return fileStream_.is_open();
    }

private:
    // Console
    EventListener consoleListener_;
    LogLevel consoleLevel_ = LogLevel::Notice;

    // File
    EventListener fileListener_;
    std::ofstream fileStream_;
    std::string filePath_;
    LogLevel fileLevel_ = LogLevel::Notice;
};

// ---------------------------------------------------------------------------
// Global logger
// ---------------------------------------------------------------------------
inline Logger& tcGetLogger() {
    static Logger logger;
    return logger;
}

// ---------------------------------------------------------------------------
// Convenience functions
// ---------------------------------------------------------------------------
inline void tcSetConsoleLogLevel(LogLevel level) {
    tcGetLogger().setConsoleLogLevel(level);
}

inline void tcSetFileLogLevel(LogLevel level) {
    tcGetLogger().setFileLogLevel(level);
}

inline bool tcSetLogFile(const std::string& path) {
    return tcGetLogger().setLogFile(path);
}

inline void tcCloseLogFile() {
    tcGetLogger().closeFile();
}

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
            tcGetLogger().log(level_, msg);
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
//   tcLog() << "message";                    // Default (Notice)
//   tcLog(LogLevel::Warning) << "warning";   // Level specified
//   tcLogNotice("ClassName") << "message";   // With module name
//   tcLogNotice() << "message";              // Without module name
// ---------------------------------------------------------------------------
inline LogStream tcLog(LogLevel level = LogLevel::Notice) {
    return LogStream(level);
}

inline LogStream tcLogVerbose(const std::string& module = "") {
    return LogStream(LogLevel::Verbose, module);
}

inline LogStream tcLogNotice(const std::string& module = "") {
    return LogStream(LogLevel::Notice, module);
}

inline LogStream tcLogWarning(const std::string& module = "") {
    return LogStream(LogLevel::Warning, module);
}

inline LogStream tcLogError(const std::string& module = "") {
    return LogStream(LogLevel::Error, module);
}

inline LogStream tcLogFatal(const std::string& module = "") {
    return LogStream(LogLevel::Fatal, module);
}

} // namespace trussc
