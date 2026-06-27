#pragma once
#include "tc/utils/tcAnnotations.h"

#include <string>
#include <filesystem>
#include "tcMath.h"

#if defined(__APPLE__)
    #include <TargetConditionals.h>
#endif

// =============================================================================
// Platform-specific features
// =============================================================================

namespace trussc {

// Forward declaration
class Pixels;

// ---------------------------------------------------------------------------
// Compile-time platform detection
// ---------------------------------------------------------------------------
struct Platform {
    // OS family
    static constexpr bool isWeb() {
#if defined(__EMSCRIPTEN__)
        return true;
#else
        return false;
#endif
    }
    static constexpr bool isMacOS() {
#if defined(__APPLE__) && (TARGET_OS_OSX || (TARGET_OS_MAC && !TARGET_OS_IPHONE))
        return true;
#else
        return false;
#endif
    }
    static constexpr bool isIOS() {
#if defined(__APPLE__) && TARGET_OS_IOS
        return true;
#else
        return false;
#endif
    }
    static constexpr bool isWindows() {
#if defined(_WIN32)
        return true;
#else
        return false;
#endif
    }
    static constexpr bool isAndroid() {
#if defined(__ANDROID__)
        return true;
#else
        return false;
#endif
    }
    static constexpr bool isLinux() {
        // __linux__ is also defined on Android, so exclude Android explicitly
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
        return true;
#else
        return false;
#endif
    }

    // Aggregates
    static constexpr bool isApple()   { return isMacOS() || isIOS(); }
    static constexpr bool isMobile()  { return isIOS() || isAndroid(); }
    static constexpr bool isDesktop() { return isMacOS() || isWindows() || isLinux(); }

    // String form — "web" / "macos" / "ios" / "windows" / "android" / "linux" / "unknown"
    static constexpr const char* name() {
        if (isWeb())     return "web";
        if (isMacOS())   return "macos";
        if (isIOS())     return "ios";
        if (isWindows()) return "windows";
        if (isAndroid()) return "android";
        if (isLinux())   return "linux";
        return "unknown";
    }
};

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

// Get window position in screen coordinates (logical pixels, top-left origin)
// macOS/Windows: returns top-left corner of the window frame
// Linux/Mobile/Web: stub, returns (-1, -1)
TC_PLATFORMS("macos,windows") IVec2 getWindowPosition();

// Set window position in screen coordinates (logical pixels, top-left origin)
// macOS/Windows: moves the window
// Linux/Mobile/Web: no-op (stub)
TC_PLATFORMS("macos,windows") void setWindowPosition(int x, int y);

// Set whether the window shows standard decorations (title bar, borders,
// buttons). false = borderless/chromeless, but the window stays key-focusable
// (keyboard works) and closable (exitApp() works). Usually driven once at
// startup by WindowSettings::setDecorated(), but can be toggled at runtime.
// Desktop only; no-op on mobile/web.
void setWindowDecorated(bool decorated);

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
TC_PLATFORMS("android,ios") void setImmersiveMode(bool enabled);
TC_PLATFORMS("android,ios") bool getImmersiveMode();

// ---------------------------------------------------------------------------
// Keep screen on (prevent display sleep / auto-lock)
// Android: AWINDOW_FLAG_KEEP_SCREEN_ON
// iOS: UIApplication.idleTimerDisabled
// macOS: IOPMAssertion (kIOPMAssertionTypeNoDisplaySleep)
// Windows: SetThreadExecutionState(ES_DISPLAY_REQUIRED | ES_CONTINUOUS)
// Linux / Web: no-op
// ---------------------------------------------------------------------------
TC_PLATFORMS("macos,windows,android,ios") void setKeepScreenOn(bool enabled);
TC_PLATFORMS("macos,windows,android,ios") bool getKeepScreenOn();

// ---------------------------------------------------------------------------
// Screenshot functionality
// ---------------------------------------------------------------------------

// Capture current window and store in Pixels
// Returns true on success, false on failure
TC_PLATFORMS("macos,windows,linux,ios,android") bool captureWindow(Pixels& outPixels);

// Internal: synchronous capture + file write (the actual worker behind the
// public deferred saveScreenshot()). Must run at a safe readback point, i.e.
// AFTER present() (see the afterFrame drain in TrussC.h). Callers pass an
// already-resolved absolute path; the parent directory is assumed to exist.
// Returns true on success, false on failure.
// Supported formats: .png, .jpg/.jpeg, .tiff/.tif, .bmp
namespace internal {
bool captureWindowToFile(const std::filesystem::path& path);
}

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
TC_PLATFORMS("macos,android,ios") float getSystemBrightness();
TC_PLATFORMS("macos,android,ios") void setSystemBrightness(float brightness);

// ---------------------------------------------------------------------------
// Thermal monitoring
// ---------------------------------------------------------------------------
TC_PLATFORMS("macos,ios,android") ThermalState getThermalState();
TC_PLATFORMS("ios") float getThermalTemperature();  // Celsius, -1 if unavailable

// ---------------------------------------------------------------------------
// Battery
// ---------------------------------------------------------------------------
TC_PLATFORMS("macos,android,ios") float getBatteryLevel();      // 0.0-1.0, -1 if unavailable (e.g. desktop without battery)
TC_PLATFORMS("macos,android,ios") bool isBatteryCharging();

// ---------------------------------------------------------------------------
// Motion sensors (iOS/Android; desktop returns zero)
// ---------------------------------------------------------------------------
TC_PLATFORMS("android,ios") Vec3 getAccelerometer();       // g-force (1.0 = Earth gravity)
TC_PLATFORMS("android,ios") Vec3 getGyroscope();           // angular velocity (rad/s)
TC_PLATFORMS("android,ios") Quaternion getDeviceOrientation();  // fused attitude (accel+gyro+mag)
TC_PLATFORMS("android,ios") float getCompassHeading();     // radians (0 = north, clockwise)

// ---------------------------------------------------------------------------
// Proximity sensor
// ---------------------------------------------------------------------------
TC_PLATFORMS("android,ios") bool isProximityClose();

// ---------------------------------------------------------------------------
// Location (GPS / WiFi)
// Starts location updates on first call. Returns most recent fix.
// ---------------------------------------------------------------------------
TC_PLATFORMS("macos,android,ios") Location getLocation();

// ---------------------------------------------------------------------------
// Internal — called by the framework, not user code
// ---------------------------------------------------------------------------
namespace internal {
    // Install the standard macOS application menu (About / Hide / Hide Others /
    // Show All / Quit). Called automatically from _setup_cb on macOS.
    // No-op on other platforms.
    void installAppMenu();
}

} // namespace trussc
