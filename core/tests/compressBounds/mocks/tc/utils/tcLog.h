#pragma once
#include <string>
// Isolate the byte-buffer unit from the framework logger and GPU backends.
namespace trussc {
enum class LogLevel { Error, Notice };
struct CompressTestLogger { int notices = 0; void log(LogLevel level, const std::string&) { if (level == LogLevel::Notice) ++notices; } };
inline CompressTestLogger& getLogger() { static CompressTestLogger logger; return logger; }
}
