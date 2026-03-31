// =============================================================================
// Android Platform-specific Functions
// =============================================================================

#include "TrussC.h"

#ifdef __ANDROID__

#include <android/log.h>
#include <android/native_activity.h>
#include <jni.h>
#include "sokol_app.h"

// GLES3 for screen capture
#include <GLES3/gl3.h>

namespace trussc {
namespace platform {

static bool immersiveMode_ = false;

float getDisplayScaleFactor() {
    return sapp_dpi_scale();
}

// ---------------------------------------------------------------------------
// Immersive mode (Sticky Immersive via JNI)
// ---------------------------------------------------------------------------
void setImmersiveMode(bool enabled) {
    immersiveMode_ = enabled;
    auto* activity = (ANativeActivity*)sapp_android_get_native_activity();
    if (!activity) return;

    JNIEnv* env = nullptr;
    activity->vm->AttachCurrentThread(&env, nullptr);
    if (!env) return;

    // Get the window's DecorView
    jclass activityClass = env->GetObjectClass(activity->clazz);
    jmethodID getWindow = env->GetMethodID(activityClass, "getWindow", "()Landroid/view/Window;");
    jobject window = env->CallObjectMethod(activity->clazz, getWindow);

    jclass windowClass = env->GetObjectClass(window);
    jmethodID getDecorView = env->GetMethodID(windowClass, "getDecorView", "()Landroid/view/View;");
    jobject decorView = env->CallObjectMethod(window, getDecorView);

    jclass viewClass = env->GetObjectClass(decorView);
    jmethodID setSystemUiVisibility = env->GetMethodID(viewClass, "setSystemUiVisibility", "(I)V");

    if (enabled) {
        // SYSTEM_UI_FLAG_IMMERSIVE_STICKY | SYSTEM_UI_FLAG_FULLSCREEN |
        // SYSTEM_UI_FLAG_HIDE_NAVIGATION | SYSTEM_UI_FLAG_LAYOUT_STABLE |
        // SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
        const int flags = 0x00001000 | 0x00000004 | 0x00000002 |
                          0x00000100 | 0x00000200 | 0x00000400;
        env->CallVoidMethod(decorView, setSystemUiVisibility, flags);
    } else {
        env->CallVoidMethod(decorView, setSystemUiVisibility, 0);
    }

    env->DeleteLocalRef(viewClass);
    env->DeleteLocalRef(decorView);
    env->DeleteLocalRef(windowClass);
    env->DeleteLocalRef(window);
    env->DeleteLocalRef(activityClass);

    activity->vm->DetachCurrentThread();
}

bool getImmersiveMode() {
    return immersiveMode_;
}

void setWindowSize(int width, int height) {
    // Android apps are fullscreen — window size is determined by the device
    (void)width;
    (void)height;
}

std::string getExecutablePath() {
    // Android doesn't have a traditional executable path.
    // Return empty — use getExecutableDir() for asset access.
    return "";
}

std::string getExecutableDir() {
    // On Android, assets are accessed via AAssetManager, not filesystem paths.
    // For files that need a real path, use internal storage.
    auto* activity = (ANativeActivity*)sapp_android_get_native_activity();
    if (activity && activity->internalDataPath) {
        return std::string(activity->internalDataPath) + "/";
    }
    return "/data/local/tmp/";
}

// ---------------------------------------------------------------------------
// Screenshot Functions (GLES3)
// ---------------------------------------------------------------------------

bool captureWindow(Pixels& outPixels) {
    int width = sapp_width();
    int height = sapp_height();

    if (width <= 0 || height <= 0) {
        logError("Screenshot") << "Invalid window dimensions";
        return false;
    }

    outPixels.allocate(width, height, 4);

    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, outPixels.getData());

    // OpenGL reads from bottom-left, flip vertically
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
// System sensors (stubs — TODO: implement with ASensorManager / JNI)
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

} // namespace platform
} // namespace trussc

#endif // __ANDROID__
