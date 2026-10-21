#pragma once
#include "tc/utils/tcLog.h"
#include <cstdio>

namespace trussc::internal {
// Recovery must not use LogStream: its noexcept destructor can allocate.
inline void reportImportError(const char* module, const char* reason,
                              const char* field = "") noexcept {
    try {
        getLogger().log(LogLevel::Error, std::string("[") + module + "] " + reason + field);
    } catch (...) {
        // C stdio reports its own failures without throwing C++ exceptions.
        std::fprintf(stderr, "[ERROR] [%s] %s%s\n", module, reason, field);
    }
}
}
