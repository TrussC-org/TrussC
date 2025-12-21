#pragma once

#include <string>
#include <filesystem>

// =============================================================================
// Platform-specific features
// =============================================================================

namespace trussc {

// Forward declaration
class Pixels;

namespace platform {

// Get DPI scale of main display (available before window creation)
// macOS: 1.0 (normal) or 2.0 (Retina)
// Other: 1.0
float getDisplayScaleFactor();

// Change window size (specified in logical size)
// macOS: Uses NSWindow
void setWindowSize(int width, int height);

// Get absolute path of executable
std::string getExecutablePath();

// Get directory containing executable (with trailing /)
std::string getExecutableDir();

// ---------------------------------------------------------------------------
// Screenshot functionality
// ---------------------------------------------------------------------------

// Capture current window and store in Pixels
// Returns true on success, false on failure
bool captureWindow(Pixels& outPixels);

// Capture current window and save to file
// Returns true on success, false on failure
bool saveScreenshot(const std::filesystem::path& path);

} // namespace platform
} // namespace trussc
