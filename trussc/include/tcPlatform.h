#pragma once

#include <string>
#include <filesystem>
#include "tcMath.h"

// =============================================================================
// Platform-specific features
// =============================================================================

namespace trussc {

// Forward declaration
class Pixels;

// ---------------------------------------------------------------------------
// Thermal state (coarse-grained, available on all platforms)
// ---------------------------------------------------------------------------
enum class ThermalState {
    Nominal,   // Normal operation
    Fair,      // Slightly elevated, performance may be reduced
    Serious,   // High temperature, should reduce workload
    Critical   // Thermal throttling active, risk of shutdown
};

// ---------------------------------------------------------------------------
// Location data
// ---------------------------------------------------------------------------
struct Location {
    double latitude = 0;
    double longitude = 0;
    double altitude = 0;
    float accuracy = -1;   // meters, -1 = not available yet
};

// Bring the application window to front and give it focus
// Desktop: activates and raises the window
// Mobile/Web: no-op (always foreground)
void bringWindowToFront();

// Get DPI scale of main display (available before window creation)
// macOS: 1.0 (normal) or 2.0 (Retina)
// Other: 1.0
float getDisplayScaleFactor();

// Change window size (specified in logical size)
// macOS: Uses NSWindow
void setWindowSizeLogical(int width, int height);

// Get absolute path of executable
std::string getExecutablePath();

// Get directory containing executable (with trailing /)
std::string getExecutableDir();

// ---------------------------------------------------------------------------
// Immersive mode (hide system UI)
// Android: Sticky Immersive (hides status bar + navigation bar)
// iOS: hides status bar + home indicator (auto-hidden)
// Desktop: no-op
// ---------------------------------------------------------------------------
void setImmersiveMode(bool enabled);
bool getImmersiveMode();

// ---------------------------------------------------------------------------
// Screenshot functionality
// ---------------------------------------------------------------------------

// Capture current window and store in Pixels
// Returns true on success, false on failure
bool captureWindow(Pixels& outPixels);

// Capture current window and save to file
// Returns true on success, false on failure
// Save screenshot (uses OS window capture feature)
// Relative paths are resolved relative to executable directory + data path
// Supported formats: .png, .jpg/.jpeg, .tiff/.tif, .bmp
bool saveScreenshot(const std::filesystem::path& path);

// ---------------------------------------------------------------------------
// System volume (0.0 - 1.0)
// ---------------------------------------------------------------------------
float getSystemVolume();
void setSystemVolume(float volume);  // iOS: logs warning (not supported by OS)

// ---------------------------------------------------------------------------
// Screen brightness (0.0 - 1.0)
// Note: the meaning of the value differs by platform.
//   iOS: linear, 0.0 = min, 1.0 = max (matches the UI slider position)
//   Android: gamma-corrected (perceptual). The OS applies a non-linear curve,
//            so the value may appear much lower than the slider position
//            (e.g. slider at max → value ~0.64). This is normal Android behavior.
//   Desktop: returns -1 (not supported)
// ---------------------------------------------------------------------------
float getSystemBrightness();
void setSystemBrightness(float brightness);

// ---------------------------------------------------------------------------
// Thermal monitoring
// ---------------------------------------------------------------------------
ThermalState getThermalState();
float getThermalTemperature();  // Celsius, -1 if unavailable

// ---------------------------------------------------------------------------
// Battery
// ---------------------------------------------------------------------------
float getBatteryLevel();      // 0.0-1.0, -1 if unavailable (e.g. desktop without battery)
bool isBatteryCharging();

// ---------------------------------------------------------------------------
// Motion sensors (iOS/Android; desktop returns zero)
// ---------------------------------------------------------------------------
Vec3 getAccelerometer();       // g-force (1.0 = Earth gravity)
Vec3 getGyroscope();           // angular velocity (rad/s)
Quaternion getDeviceOrientation();  // fused attitude (accel+gyro+mag)
float getCompassHeading();     // radians (0 = north, clockwise)

// ---------------------------------------------------------------------------
// Proximity sensor
// ---------------------------------------------------------------------------
bool isProximityClose();

// ---------------------------------------------------------------------------
// Location (GPS / WiFi)
// Starts location updates on first call. Returns most recent fix.
// ---------------------------------------------------------------------------
Location getLocation();

} // namespace trussc
