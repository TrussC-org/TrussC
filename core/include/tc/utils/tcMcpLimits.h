#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>

namespace trussc::mcp {

/// MCP admission settings; change only while the HTTP server is stopped.
struct AdmissionLimits {
    // Bound (security): control JSON must fit finite ingress storage. Overflow rejects with 413 and is counted.
    size_t bodyBytes = 64 * 1024 * 1024;
    // Bound (security): RPC nesting must fit parse and copy stacks. Overflow rejects the request and is counted.
    size_t jsonDepth = 64;
    // Bound (security): sockets and pending replies share finite admission. Overflow closes the new socket and is counted.
    size_t outstanding = 32;
    // Bound (security): network work must use a finite worker pool. Overflow queues within admission and excess admission is counted.
    size_t workers = 4;
    // Bound (security): histogram parameters drive scratch and reply allocation. Overflow rejects the operation and is counted.
    size_t histogramBins = 4096;
    // Bound (security): grid products drive scratch and reply allocation. Overflow rejects the operation and is counted.
    size_t gridCells = 65536;
    // Bound (security): operation arrays drive repeated work and retained results. Overflow rejects the request and is counted.
    size_t operations = 256;
};

/// Set positive limits while HTTP is stopped and no connections are outstanding; return true on success.
/// Rejection returns false, logs one Warning per call, increments the Configuration rejection
/// counter, and preserves all previous limits. Counters persist across HTTP restarts.
bool setAdmissionLimits(const AdmissionLimits& limits);
/// Return the admission settings.
AdmissionLimits getAdmissionLimits();

namespace internal {
enum class AdmissionStore { Body, Connections, Histogram, Grid, Operations, Working, Headers, RequestLine, Configuration, Count };
void admissionRejected(AdmissionStore store, size_t capacity, size_t needed, const char* units = nullptr);
uint64_t admissionRejections(AdmissionStore store);
size_t admittedConnections();
class AdmissionError : public std::runtime_error {
public:
    AdmissionError() : std::runtime_error("MCP admission limit exceeded") {}
};

// Owns one bounded externally requested screenshot until the deferred job retires.
std::shared_ptr<void> acquireScreenshotCredit();
// Existing platform capture adapters no longer need a memory reservation.
inline void reconcileWindowCapture(size_t, size_t) {}
void checkAdmission(size_t needed, size_t capacity, AdmissionStore store);
}
}
