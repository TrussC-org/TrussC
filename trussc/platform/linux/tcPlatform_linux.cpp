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
namespace platform {

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

void setWindowSize(int width, int height) {
    // TODO: Implement using X11
    // sokol_app handles window creation, so we need to access the X11 window
    // For now, this is a no-op
    tcLogWarning("Platform") << "setWindowSize not yet implemented on Linux";
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
        tcLogError("Screenshot") << "Invalid window dimensions";
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
        tcLogVerbose("Screenshot") << "Saved: " << path;
        return true;
    } else {
        tcLogError("Screenshot") << "Failed to save: " << path;
        return false;
    }
}

} // namespace platform
} // namespace trussc

#endif // __linux__
