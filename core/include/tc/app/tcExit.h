#pragma once
#include <string>

// Internal, process-wide exit bookkeeping shared by the runtime and backends.
namespace trussc::internal {
void setExitReason(const char* reason);
void setExitReasonIfEmpty(const char* reason);
std::string exitReason();
void clearExitReason();
void setExitBlockReason(const std::string& reason);
std::string exitBlockReason();
bool beginExitCleanup();
std::string exitLogMessage(bool clean);
void installWindowExitSignals();
void restoreWindowExitSignals();
int pendingWindowExitSignal();
}
