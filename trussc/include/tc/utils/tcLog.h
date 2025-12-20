#pragma once

// =============================================================================
// tcLog.h - ロギングシステム
// =============================================================================

#include <sstream>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <ctime>

// Event システムを使用
#include "../events/tcEvent.h"
#include "../events/tcEventListener.h"

namespace trussc {

// ---------------------------------------------------------------------------
// ログレベル
// ---------------------------------------------------------------------------
enum class LogLevel {
    Verbose,    // 詳細情報（デバッグ用）
    Notice,     // 通常の情報
    Warning,    // 警告
    Error,      // エラー
    Fatal,      // 致命的エラー
    Silent      // 出力しない（フィルタ用）
};

// ログレベルを文字列に変換
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
// LogEventArgs - ログイベントの引数
// ---------------------------------------------------------------------------
struct LogEventArgs {
    LogLevel level;
    std::string message;
    std::string timestamp;

    LogEventArgs(LogLevel lvl, const std::string& msg)
        : level(lvl), message(msg) {
        // タイムスタンプを生成
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
// Logger - ロガー本体
// ---------------------------------------------------------------------------
class Logger {
public:
    // ログイベント（全リスナーに通知）
    Event<LogEventArgs> onLog;

    Logger() {
        // コンソールリスナーをデフォルトで登録
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

    // === ログ出力 ===

    void log(LogLevel level, const std::string& message) {
        LogEventArgs args(level, message);
        onLog.notify(args);
    }

    // === コンソール設定 ===

    void setConsoleLogLevel(LogLevel level) {
        consoleLevel_ = level;
    }

    LogLevel getConsoleLogLevel() const {
        return consoleLevel_;
    }

    // === ファイル設定 ===

    bool setLogFile(const std::string& path) {
        closeFile();

        fileStream_.open(path, std::ios::app);
        if (!fileStream_.is_open()) {
            log(LogLevel::Error, "Failed to open log file: " + path);
            return false;
        }

        filePath_ = path;

        // ファイルリスナーを登録
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
    // コンソール
    EventListener consoleListener_;
    LogLevel consoleLevel_ = LogLevel::Notice;

    // ファイル
    EventListener fileListener_;
    std::ofstream fileStream_;
    std::string filePath_;
    LogLevel fileLevel_ = LogLevel::Notice;
};

// ---------------------------------------------------------------------------
// グローバルロガー
// ---------------------------------------------------------------------------
inline Logger& tcGetLogger() {
    static Logger logger;
    return logger;
}

// ---------------------------------------------------------------------------
// 便利関数
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
// LogStream - ストリーム形式でログ出力
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

    // ムーブのみ許可
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

    // std::endl 等のマニピュレータ対応
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
// ログ出力関数（ストリーム形式）
// 使い方:
//   tcLog() << "message";                    // デフォルト（Notice）
//   tcLog(LogLevel::Warning) << "warning";   // レベル指定
//   tcLogNotice("ClassName") << "message";   // モジュール名付き
//   tcLogNotice() << "message";              // モジュール名なし
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
