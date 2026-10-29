#pragma once

namespace trussc::internal {
// Defined beside the logger; failure reporting must also survive allocation
// failure and a throwing log listener.
void reportTeardownFailure(const char* step) noexcept;

// Keep the first failing step without allocating exception storage. Callers
// split mandatory work into separate steps, including individual exit hooks.
class TeardownSteps {
public:
    template<class F> void run(const char* step, F&& work) noexcept {
        try { work(); }
        catch (...) { if (!firstFailure_) firstFailure_ = step; }
    }
    ~TeardownSteps() { if (firstFailure_) reportTeardownFailure(firstFailure_); }
private:
    const char* firstFailure_ = nullptr; // labels must be string literals
};
}
