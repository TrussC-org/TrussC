// =============================================================================
// Linux Platform-specific Functions
// =============================================================================

#include "TrussC.h"

#if defined(__linux__)

#include <unistd.h>
#include <linux/limits.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include "sokol_app.h"

// OpenGL for screen capture
#include <GL/gl.h>

namespace trussc {

void bringWindowToFront() {
    Display* display = XOpenDisplay(nullptr);
    if (!display) return;
    Window window = (Window)(uintptr_t)sapp_x11_get_window();
    if (window) {
        XRaiseWindow(display, window);
        XSetInputFocus(display, window, RevertToParent, CurrentTime);
        XFlush(display);
    }
    XCloseDisplay(display);
}

float getDisplayScaleFactor() {
    // TODO: Implement proper DPI detection using X11/XRandR
    // For now, return 1.0 (no scaling)
    Display* display = XOpenDisplay(nullptr);
    if (!display) {
        return 1.0f;
    }

    // Get screen DPI from X resources (if set)
    // This is a common way to detect DPI on Linux
    char* resourceString = XResourceManagerString(display);
    float dpi = 96.0f; // default

    if (resourceString) {
        // Look for Xft.dpi setting
        char* dpiStr = strstr(resourceString, "Xft.dpi:");
        if (dpiStr) {
            dpiStr += 8; // skip "Xft.dpi:"
            while (*dpiStr == ' ' || *dpiStr == '\t') dpiStr++;
            dpi = atof(dpiStr);
        }
    }

    XCloseDisplay(display);

    // Scale factor relative to standard 96 DPI
    return dpi / 96.0f;
}

// Immersive mode (no-op on desktop)
void setImmersiveMode(bool enabled) { (void)enabled; }
bool getImmersiveMode() { return false; }

IVec2 getWindowPosition() {
    logWarning("Platform") << "getWindowPosition() not yet implemented on Linux";
    return IVec2(-1, -1);
}

void setWindowPosition(int x, int y) {
    logWarning("Platform") << "setWindowPosition() not yet implemented on Linux";
    (void)x; (void)y;
}

void setWindowSizeLogical(int width, int height) {
    // TODO: Implement using X11
    // sokol_app handles window creation, so we need to access the X11 window
    // For now, this is a no-op
    logWarning("Platform") << "setWindowSize not yet implemented on Linux";
}

std::string getExecutablePath() {
    char path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len != -1) {
        path[len] = '\0';
        return std::string(path);
    }
    return "";
}

std::string getExecutableDir() {
    std::string exePath = getExecutablePath();
    size_t lastSlash = exePath.rfind('/');
    if (lastSlash != std::string::npos) {
        return exePath.substr(0, lastSlash + 1);
    }
    return "./";
}

// ---------------------------------------------------------------------------
// Screenshot Functions (OpenGL)
// ---------------------------------------------------------------------------

bool captureWindow(Pixels& outPixels) {
    // Get window dimensions from sokol
    int width = sapp_width();
    int height = sapp_height();

    if (width <= 0 || height <= 0) {
        logError("Screenshot") << "Invalid window dimensions";
        return false;
    }

    // Allocate pixel buffer
    outPixels.allocate(width, height, 4);

    // Read pixels from OpenGL framebuffer
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, outPixels.getData());

    // OpenGL reads from bottom-left, so we need to flip vertically
    unsigned char* data = outPixels.getData();
    std::vector<unsigned char> rowBuffer(width * 4);
    for (int y = 0; y < height / 2; y++) {
        int topRow = y * width * 4;
        int bottomRow = (height - 1 - y) * width * 4;
        memcpy(rowBuffer.data(), &data[topRow], width * 4);
        memcpy(&data[topRow], &data[bottomRow], width * 4);
        memcpy(&data[bottomRow], rowBuffer.data(), width * 4);
    }

    return true;
}

bool saveScreenshot(const std::filesystem::path& path) {
    if (path.is_relative()) {
        return saveScreenshot(getDataPath(path.string()));
    }
    Pixels pixels;
    if (!captureWindow(pixels)) {
        return false;
    }

    // Use stb_image_write to save
    std::string ext = path.extension().string();
    std::string pathStr = path.string();

    int width = pixels.getWidth();
    int height = pixels.getHeight();
    unsigned char* data = pixels.getData();

    int result = 0;
    if (ext == ".png") {
        result = stbi_write_png(pathStr.c_str(), width, height, 4, data, width * 4);
    } else if (ext == ".jpg" || ext == ".jpeg") {
        result = stbi_write_jpg(pathStr.c_str(), width, height, 4, data, 90);
    } else if (ext == ".bmp") {
        result = stbi_write_bmp(pathStr.c_str(), width, height, 4, data);
    } else {
        // Default to PNG
        result = stbi_write_png(pathStr.c_str(), width, height, 4, data, width * 4);
    }

    if (result) {
        logVerbose("Screenshot") << "Saved: " << path;
        return true;
    } else {
        logError("Screenshot") << "Failed to save: " << path;
        return false;
    }
}

// ---------------------------------------------------------------------------
// App menu (macOS only) — stub
// ---------------------------------------------------------------------------
namespace internal {
void installAppMenu() {}
} // namespace internal

// ---------------------------------------------------------------------------
// System sensors (stubs)
// ---------------------------------------------------------------------------
float getSystemVolume() { return -1.0f; }
void setSystemVolume(float volume) { (void)volume; }
float getSystemBrightness() { return -1.0f; }
void setSystemBrightness(float brightness) { (void)brightness; }
ThermalState getThermalState() { return ThermalState::Nominal; }
float getThermalTemperature() { return -1.0f; }
float getBatteryLevel() { return -1.0f; }
bool isBatteryCharging() { return false; }
Vec3 getAccelerometer() { return Vec3(0, 0, 0); }
Vec3 getGyroscope() { return Vec3(0, 0, 0); }
Quaternion getDeviceOrientation() { return Quaternion(1, 0, 0, 0); }
float getCompassHeading() { return 0.0f; }
bool isProximityClose() { return false; }
Location getLocation() { return Location(); }

} // namespace trussc

#endif // __linux__
