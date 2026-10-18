#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>

namespace trussc::mcp {

/// MCP admission settings; change only while the HTTP server is stopped.
struct AdmissionLimits {
    // Bound (security): control JSON must fit finite ingress storage. Overflow rejects with 413 and is counted.
    size_t bodyBytes = 1024 * 1024;
    // Bound (security): RPC nesting must fit parse and copy stacks. Overflow rejects the request and is counted.
    size_t jsonDepth = 64;
    // Bound (security): sockets and pending replies share finite admission. Overflow closes the new socket and is counted.
    size_t outstanding = 32;
    // Bound (security): network work must use a finite worker pool. Overflow queues within admission and excess admission is counted.
    size_t workers = 4;
    // Bound (security): HTTP metadata must stop growing while read. Overflow rejects with 431 and is counted.
    size_t lineBytes = 8192;
    // Bound (security): headers and trailers share finite metadata storage. Overflow rejects with 431 and is counted.
    size_t headerBytes = 65536;
    // Bound (security): header/trailer entries also consume storage. Overflow rejects with 431 and is counted.
    size_t headerCount = 128;
    // Bound (security): histogram parameters drive scratch and reply allocation. Overflow rejects the operation and is counted.
    size_t histogramBins = 4096;
    // Bound (security): grid products drive scratch and reply allocation. Overflow rejects the operation and is counted.
    size_t gridCells = 65536;
    // Bound (security): operation arrays drive repeated work and retained results. Overflow rejects the request and is counted.
    size_t operations = 256;
    // Bound (security): reply construction must fit finite output storage. Overflow returns an error and is counted.
    size_t replyBytes = 64 * 1024 * 1024;
    // Bound (security): concurrent MCP work shares finite process storage. Overflow rejects admission or returns an error and is counted.
    size_t workingBytes = 256 * 1024 * 1024;
};

/// Set positive limits while HTTP is stopped and no work is outstanding; return true on success.
/// Rejection returns false, logs one Warning per call, increments the Configuration rejection
/// counter, and preserves all previous limits. Counters persist across HTTP restarts.
bool setAdmissionLimits(const AdmissionLimits& limits);
/// Return the admission settings.
AdmissionLimits getAdmissionLimits();

namespace internal {
enum class AdmissionStore { Body, Headers, Connections, Histogram, Grid, Operations, Reply, Working, Configuration, Count };
void admissionRejected(AdmissionStore store, size_t capacity, size_t needed, const char* units = nullptr);
uint64_t admissionRejections(AdmissionStore store);
size_t admittedConnections();
size_t workingMemory();

class AdmissionError : public std::runtime_error {
public:
    AdmissionError() : std::runtime_error("MCP admission limit exceeded") {}
};

// Separate budgets own connection ingress or one request and its deferred stages.
class WorkBudget {
public:
    explicit WorkBudget(bool http = false) : http_(http) {}
    bool isHttp() const { return http_; }
    ~WorkBudget();
    void reserve(size_t bytes);
    // Silent decision for callers holding locks; report rejection after unlocking.
    bool tryReserve(size_t bytes, size_t& capacity, size_t& needed);
    void reply(size_t bytes);
private:
    friend void reserveWindowCapture(size_t, size_t, size_t, size_t);
    friend void reconcileWindowCapture(size_t, size_t);
    size_t reserved_ = 0, reply_ = 0;
    size_t captureBytes_ = 0, capturePixelBytes_ = 0, captureOverhead_ = 0;
    const bool http_;
};
std::shared_ptr<WorkBudget>& currentWorkBudget();
class WorkScope {
public:
    explicit WorkScope(std::shared_ptr<WorkBudget> budget = {});
    ~WorkScope();
private:
    std::shared_ptr<WorkBudget> previous_;
};
// Owns one bounded externally requested screenshot until the deferred job retires.
std::shared_ptr<void> acquireScreenshotCredit();
void reserveWork(size_t bytes);
// Retain the capture allowance in the request budget across deferred stages.
void reserveWindowCapture(size_t width, size_t height, size_t pixelBytes = 8, size_t overhead = 0);
// Called with the selected source extent before staging or output allocation.
// No budget or no admitted window capture leaves application capture unchanged.
void reconcileWindowCapture(size_t width, size_t height);
void reserveReply(size_t bytes);
void checkAdmission(size_t needed, size_t capacity, AdmissionStore store);
}
}
