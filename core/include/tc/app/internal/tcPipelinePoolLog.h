#pragma once
#include "sokol/sokol_gfx.h"
#include "sokol/util/sokol_gl_tc.h"
#include <chrono>
#include <cstdint>
#include <string>

namespace trussc::internal {
// Internal diagnostics only. Observe allocation/free events, never frames.
class PipelinePoolLog {
public:
    enum class Level { Notice, Warning, Error };
    using Emit = void (*)(Level, const std::string&);
    using Clock = double (*)();
    Emit emit = nullptr;
    Clock clock = steadySeconds;
    uint64_t exhaustionEvents = 0;
    bool sgAllocationExhausted = false;

    void reset() {
        sgUsed = sglUsed = 0; sgCapacity = sglCapacity = 0;
        warned = false; // Keep the Notice latch and Error gate for the run.
        sgAllocationExhausted = false;
    }
    void install() {
        reset();
        sampleSg();
    }
    void sampleSg() noexcept {
        try {
            const auto stats = sg_query_stats().total.pipelines;
            sgUsed = int(stats.alive); sgCapacity = int(stats.alive + stats.free);
            const auto gl = sgl_tc_query_pipeline_pool();
            sglUsed = gl.used; sglCapacity = gl.capacity;
            observe();
        } catch (...) {} // Diagnostics must not abort resource operations.
    }
    void exhausted(bool sgPool) {
        ++exhaustionEvents;
        if (sgPool) sgAllocationExhausted = true;
        sampleSg();
        const double now = clock();
        if (hasError && now - lastError < 1.0) { ++suppressed; return; }
        auto text = message();
        if (suppressed) text += "; " + std::to_string(suppressed) + " failures suppressed";
        suppressed = 0; lastError = now; hasError = true;
        send(Level::Error, text);
    }
private:
    int sgUsed = 0, sgCapacity = 0, sglUsed = 0, sglCapacity = 0;
    bool noticed = false, warned = false, hasError = false;
    uint64_t suppressed = 0;
    double lastError = 0;
    static double steadySeconds() {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    bool sglFuller() const {
        return sglCapacity > 0 && (sgCapacity == 0 ||
            int64_t(sglUsed) * sgCapacity > int64_t(sgUsed) * sglCapacity);
    }
    int used() const { return sglFuller() ? sglUsed : sgUsed; }
    int capacity() const { return sglFuller() ? sglCapacity : sgCapacity; }
    std::string message() const {
        const int size = capacity();
        return std::string(sglFuller() ? "sokol_gl" : "sg") + " pipeline pool " +
            std::to_string(size ? int64_t(used()) * 100 / size : 0) + "% used (" +
            std::to_string(used()) + "/" + std::to_string(size) +
            "); raise it with WindowSettings::setPipelinePoolSize() before runApp()";
    }
    void send(Level level, const std::string& text) noexcept {
        try { if (emit) emit(level, text); } catch (...) {}
    }
    void observe() {
        const bool atNotice = capacity() > 0 && int64_t(used()) * 100 >= int64_t(capacity()) * 50;
        const bool atWarning = capacity() > 0 && int64_t(used()) * 100 >= int64_t(capacity()) * 80;
        const bool noticeCrossed = atNotice && !noticed;
        const bool warningCrossed = atWarning && !warned;
        // onLog listeners run synchronously and may allocate resources too.
        noticed = noticed || atNotice; warned = atWarning;
        if (noticeCrossed || warningCrossed) {
            const auto text = message();
            if (noticeCrossed) send(Level::Notice, text);
            if (warningCrossed) send(Level::Warning, text);
        }
    }
};
}
