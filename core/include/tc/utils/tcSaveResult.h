#pragma once
#include <string>
#include <utility>

namespace trussc {

enum class SaveError { None = 0, AccessDenied, NoSpace, WriteFailed, Unknown };

// Success acknowledges this operation: checked save, stream open, recorder
// submission accepted by the backend, or checked finalization on close/stop.
struct SaveResult {
    SaveError error = SaveError::None;
    std::string message;
    bool ok() const { return error == SaveError::None; }
    explicit operator bool() const { return ok(); }
    static SaveResult success() { return {}; }
    static SaveResult fail(SaveError error, std::string message = {}) {
        return {error, std::move(message)};
    }
};

} // namespace trussc
