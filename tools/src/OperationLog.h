#pragma once

#include <functional>
#include <string>
#include <utility>

// Interfaces install their diagnostic sink; an operation temporarily captures
// resolver messages for its result and progress callback.
namespace projectOps {
enum class MessageKind { Log, Warning, Diagnostic };
using DiagnosticCallback = std::function<void(const std::string&, MessageKind)>;
inline thread_local DiagnosticCallback diagnosticCallback;
class ScopedDiagnostics {
public:
    explicit ScopedDiagnostics(DiagnosticCallback callback)
        : previous_(std::move(diagnosticCallback)) {
        diagnosticCallback = std::move(callback);
    }
    ~ScopedDiagnostics() { diagnosticCallback = std::move(previous_); }
    ScopedDiagnostics(const ScopedDiagnostics&) = delete;
    ScopedDiagnostics& operator=(const ScopedDiagnostics&) = delete;
private:
    DiagnosticCallback previous_;
};
inline void diagnostic(const std::string& message, MessageKind kind = MessageKind::Log) {
    if (diagnosticCallback) diagnosticCallback(message, kind);
}
}
