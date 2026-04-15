#pragma once

// =============================================================================
// TrussC Headless Mode State
// Minimal header for headless mode detection (included early in TrussC.h)
// =============================================================================

#include <atomic>

namespace trussc {
namespace headless {

// Headless mode active flag (true when running in headless mode)
inline std::atomic<bool> active{false};

// Check if currently running in headless mode
inline bool isActive() { return active.load(); }

} // namespace headless
} // namespace trussc
