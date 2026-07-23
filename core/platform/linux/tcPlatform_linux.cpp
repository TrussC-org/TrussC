// =============================================================================
// Linux Platform-specific Functions
// =============================================================================

#include "TrussC.h"

#if defined(__linux__)

#include <unistd.h>
#include <linux/limits.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include "sokol_app_tc.h"

// OpenGL for screen capture. GL_GLEXT_PROTOTYPES makes glext.h declare the
// GL 3.0 function prototypes (glBindFramebuffer); without it only the enum
// tokens are visible. Mesa's libGL exports the symbols, so linking is fine.
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>   // glBindFramebuffer / GL_READ_FRAMEBUFFER (per-window capture)

namespace trussc {

void bringWindowToFront() {
    Display* display = XOpenDisplay(nullptr);
    if (!display) return;
    // ::Window = the X11 window id type (trussc::Window shadows it in this ns)
    ::Window window = (::Window)(uintptr_t)sapp_x11_get_window();
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

// Orientation (no-op on desktop)
void setOrientation(Orientation mask) { (void)mask; }

// Keep screen on (TODO: xdg-screensaver or systemd-inhibit — no-op for now)
void setKeepScreenOn(bool enabled) { (void)enabled; }
bool getKeepScreenOn() { return false; }

IVec2 getWindowPosition() {
    logWarning("Platform") << "getWindowPosition() not yet implemented on Linux";
    return IVec2(-1, -1);
}

void setWindowPosition(int x, int y) {
    logWarning("Platform") << "setWindowPosition() not yet implemented on Linux";
    (void)x; (void)y;
}

void setWindowDecorated(bool decorated) {
    Display* display = XOpenDisplay(nullptr);
    if (!display) return;
    // ::Window = the X11 window id type (trussc::Window shadows it in this ns)
    ::Window window = (::Window)(uintptr_t)sapp_x11_get_window();
    if (window) {
        // Motif WM hints: clear the decorations flag to drop the WM-drawn frame.
        struct {
            unsigned long flags;
            unsigned long functions;
            unsigned long decorations;
            long          input_mode;
            unsigned long status;
        } hints = {0};
        hints.flags = (1L << 1);                 // MWM_HINTS_DECORATIONS
        hints.decorations = decorated ? 1 : 0;   // 1 = all, 0 = none
        Atom prop = XInternAtom(display, "_MOTIF_WM_HINTS", False);
        XChangeProperty(display, window, prop, prop, 32, PropModeReplace,
                        (unsigned char*)&hints, 5);
        XFlush(display);
    }
    XCloseDisplay(display);
}

void setWindowSizeLogical(int width, int height) {
    // TODO: Implement using X11
    // sokol_app handles window creation, so we need to access the X11 window
    // For now, this is a no-op
    logWarning("Platform") << "setWindowSize not yet implemented on Linux";
}

fs::path getExecutablePath() {
    char path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len != -1) {
        path[len] = '\0';
        return fs::path(path);
    }
    return {};
}

fs::path getExecutableDir() {
    fs::path exePath = getExecutablePath();
    if (!exePath.empty()) return exePath.parent_path();
    return fs::path(".");
}

// ---------------------------------------------------------------------------
// Screenshot Functions (OpenGL)
// ---------------------------------------------------------------------------

bool captureWindow(Pixels& outPixels) {
    // Read back the CURRENT window's framebuffer. A secondary window's context
    // records its own swapchain (framebuffer id + size) in lastSwapchain, and the
    // capture runs inside that window's tick while its GL drawable is current, so
    // reading its framebuffer at its own size is correct. The main window keeps
    // using sapp_width()/height() and the default framebuffer — byte-identical.
    auto& winCtx = internal::currentWindowContext();
    int width, height;
    if (!winCtx.isMain && winCtx.fbWidth > 0 && winCtx.fbHeight > 0) {
        width  = winCtx.fbWidth;
        height = winCtx.fbHeight;
    } else {
        width  = sapp_width();
        height = sapp_height();
    }

    if (width <= 0 || height <= 0) {
        logError("Screenshot") << "Invalid window dimensions";
        return false;
    }

    // Allocate pixel buffer
    outPixels.allocate(width, height, 4);

    // Bind the window's swapchain framebuffer as the read source so the readback
    // targets THIS window's surface, not whatever was last bound (main window's
    // framebuffer id is 0 / lastSwapchain unset for capture-only frames, so this
    // is a no-op there).
    if (!winCtx.isMain) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, winCtx.lastSwapchain.gl.framebuffer);
    }

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

bool internal::captureWindowToFile(const std::filesystem::path& path) {
    if (path.is_relative()) {
        return internal::captureWindowToFile(getDataPath(path));
    }
    Pixels pixels;
    if (!captureWindow(pixels)) {
        return false;
    }

    // Use stb_image_write to save
    std::string ext = path.extension().string();
    std::string pathStr = internal::pathToUtf8(path);   // UTF-8 for stb (STBIW_WINDOWS_UTF8)

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
