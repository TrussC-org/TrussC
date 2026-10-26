// =============================================================================
// Linux Platform-specific Functions
// =============================================================================

#include "TrussC.h"

#if defined(__linux__)

#include <unistd.h>
#include <pwd.h>
#include <cstdlib>
#include <linux/limits.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include "tcWindowGeometryX11.h"
#include "sokol_app_tc.h"

// OpenGL for screen capture. GL_GLEXT_PROTOTYPES makes glext.h declare the
// GL 3.0 function prototypes (glBindFramebuffer); without it only the enum
// tokens are visible. Mesa's libGL exports the symbols, so linking is fine.
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>   // glBindFramebuffer / GL_READ_FRAMEBUFFER (per-window capture)

#include "../gl/tcGLReadbackState.h"

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

IVec2 internal::windowPositionNative(const void* handle) {
    auto* dpy = (Display*)sapp_x11_get_display();
    if (!dpy || !handle) {
        logWarning("Platform") << "getWindowPosition() requires X11 (unsupported on headless KMS)";
        return {-1, -1};
    }
    return x11FramePosition(dpy, (::Window)(uintptr_t)handle);
}
namespace {
// Main-window storage belongs to the platform TU, never to an inline header.
// Each secondary owns the same state in its AdapterState.
internal::X11GeometryState mainGeometry;
::Window mainGeometryWindow = 0;
internal::X11GeometryState& mainGeometryState() {
    const auto window = (::Window)(uintptr_t)sapp_x11_get_window();
    if (window != mainGeometryWindow) {
        const auto desc = sapp_query_desc();
        mainGeometry = {double(desc.width), double(desc.height)};
        mainGeometryWindow = window;
    }
    return mainGeometry;
}
}
IVec2 internal::mainWindowPosition() { return windowPositionNative(sapp_x11_get_window()); }
void internal::setMainWindowPosition(int x, int y) {
    x11Geometry((Display*)sapp_x11_get_display(), (::Window)(uintptr_t)sapp_x11_get_window(),
        mainGeometryState(), true, x, y, 0, 0, sapp_dpi_scale(), sapp_is_fullscreen());
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

void internal::setMainWindowSizeFractionalX11(double width, double height) {
    x11Geometry((Display*)sapp_x11_get_display(), (::Window)(uintptr_t)sapp_x11_get_window(),
        mainGeometryState(), false, 0, 0, width, height, sapp_dpi_scale(), sapp_is_fullscreen(), true);
}

void internal::setMainWindowSizeLogical(int width, int height) {
    internal::x11Geometry((Display*)sapp_x11_get_display(), (::Window)(uintptr_t)sapp_x11_get_window(),
        mainGeometryState(), false, 0, 0, width, height, sapp_dpi_scale(), sapp_is_fullscreen());
}

fs::path getExecutablePath() {
    // Bound (platform): the OS executable path uses PATH_MAX storage. Overflow returns the truncated path from readlink.
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
// User data / temp folders (getUserDataPath / getTempPath)
// ---------------------------------------------------------------------------
namespace {
// The executable name names the app's folder.
fs::path appFolderName() {
    fs::path name = getExecutablePath().filename();
    return name.empty() ? fs::path("TrussC") : name;
}

// An environment variable as a path, when set to an absolute path (the XDG
// spec ignores relative values).
fs::path absoluteEnvPath(const char* name) {
    const char* value = std::getenv(name);
    if (!value || !*value) return {};
    fs::path p(value);
    return p.is_absolute() ? p : fs::path();
}
} // namespace

fs::path internal::platformUserDataRoot() {
    fs::path base = absoluteEnvPath("XDG_DATA_HOME");
    if (base.empty()) {
        fs::path home = absoluteEnvPath("HOME");
        if (home.empty()) {
            if (const passwd* pw = getpwuid(getuid())) {
                if (pw->pw_dir) home = pw->pw_dir;
            }
        }
        if (home.empty()) return platformTempRoot();   // no home folder at all
        base = home / ".local" / "share";
    }
    return base / appFolderName();
}

fs::path internal::platformTempRoot() {
    fs::path base = absoluteEnvPath("TMPDIR");
    if (base.empty()) base = "/tmp";
    return base / appFolderName();
}

fs::path internal::platformAppBundlePath() {
    return {};
}

// ---------------------------------------------------------------------------
// Screenshot Functions (OpenGL)
// ---------------------------------------------------------------------------

std::pair<size_t, size_t> internal::captureWindowSize(const internal::WindowContext& ctx) {
    if (!ctx.isMain && ctx.fbWidth > 0 && ctx.fbHeight > 0)
        return {size_t(ctx.fbWidth), size_t(ctx.fbHeight)};
    return {size_t(std::max(0, sapp_width())), size_t(std::max(0, sapp_height()))};
}

bool captureWindow(Pixels& outPixels) {
    if (!sg_isvalid()) return false;
    if (internal::hasPublicFboActivation()) return false;
    internal::CapturePrefixScope capturePhase;
    const auto source = internal::windowCaptureSource();
    const int width = source.width, height = source.height;

    size_t rowBytes = 0, totalBytes = 0;
    if (!internal::captureLayout(width, height, 4, rowBytes, totalBytes)) {
        logError("Screenshot") << "Invalid window dimensions";
        return false;
    }

    mcp::internal::reconcileWindowCapture(size_t(width), size_t(height));

    internal::GLReadbackState nativeState;
    nativeState.bindSource(source.swapchain.gl.framebuffer);
    // Allocate pixel buffer
    const auto layout = internal::recorderPackedLayout(width, height, internal::RecorderPixelFormat::RGBA8);
    // Bound (abi): packed capture bytes must fit size_t. Overflow drops the recorder frame and is counted.
    if (!layout.byteSize) return false;
    // Capture overwrites every byte; preserve a matching RGBA8 buffer.
    if (!outPixels.isAllocated() || outPixels.getWidth() != width ||
        outPixels.getHeight() != height || outPixels.getChannels() != 4 ||
        outPixels.getFormat() != PixelFormat::U8) {
        outPixels.allocate(width, height, 4);
    }
    // Bound (abi): the pixel allocation must hold every captured row. Overflow drops the recorder frame and is counted.
    if (!outPixels.getData() || outPixels.getTotalBytes() < layout.byteSize) return false;

    // Read pixels from OpenGL framebuffer
    // GLReadbackState selects the source and normalizes/restores pack state.
    // Match Fbo readback: only this read's error determines success.
    while (glGetError() != GL_NO_ERROR) {}
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, outPixels.getData());
    const GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        logError("Screenshot") << "glReadPixels failed (GL error 0x" << std::hex << error << ")";
        return false;
    }

    // OpenGL reads from bottom-left, so we need to flip vertically
    unsigned char* data = outPixels.getData();
    std::vector<unsigned char> rowBuffer(layout.rowBytes);
    for (int y = 0; y < height / 2; y++) {
        size_t topRow = (size_t)y * layout.rowBytes;
        size_t bottomRow = (size_t)(height - 1 - y) * layout.rowBytes;
        memcpy(rowBuffer.data(), &data[topRow], layout.rowBytes);
        memcpy(&data[topRow], &data[bottomRow], layout.rowBytes);
        memcpy(&data[bottomRow], rowBuffer.data(), layout.rowBytes);
    }

    return true;
}

bool internal::captureWindowToFile(const std::filesystem::path& requestedPath) {
    const auto path = internal::resolveScreenshotPath(requestedPath);
    Pixels pixels;
    if (!captureWindow(pixels)) return false;
    return internal::saveScreenshotPixels(pixels, path);
}

bool internal::saveScreenshotPixels(const Pixels& pixels, const std::filesystem::path& requestedPath) {
    return writeScreenshotPixels(pixels, requestedPath).ok();
}

SaveResult internal::writeScreenshotPixels(const Pixels& pixels, const std::filesystem::path& requestedPath) {
    const auto path = internal::resolveScreenshotPath(requestedPath);
    return internal::atomicWrite(path, [&](std::FILE* file) {
        return internal::writePixelsEncoded(file, pixels, toLower(getFileExtension(path)), path);
    }, "Screenshot", true);
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
