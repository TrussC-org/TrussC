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

    // ANativeActivity_setWindowFlags is thread-safe (posts to UI thread internally).
    // FLAG_FULLSCREEN (0x0400) hides the status bar.
    // Navigation bar hiding requires View.setSystemUiVisibility on UI thread (JNI + Runnable),
    // which is complex from pure NDK. For now, hide status bar only.
    if (enabled) {
        ANativeActivity_setWindowFlags(activity, 0x00000400 /*FLAG_FULLSCREEN*/, 0);
    } else {
        ANativeActivity_setWindowFlags(activity, 0, 0x00000400 /*FLAG_FULLSCREEN*/);
    }
}

bool getImmersiveMode() {
    return immersiveMode_;
}

IVec2 getWindowPosition() {
    logWarning("Platform") << "getWindowPosition() is not supported on Android";
    return IVec2(-1, -1);
}

void setWindowPosition(int x, int y) {
    logWarning("Platform") << "setWindowPosition() is not supported on Android";
    (void)x; (void)y;
}

void setWindowSizeLogical(int width, int height) {
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
    if (path.is_relative()) {
        return saveScreenshot(getDataPath(path.string()));
    }
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
// Sensor manager (NDK ASensorManager)
// ---------------------------------------------------------------------------
#include <android/sensor.h>
#include <math.h>

namespace {

struct SensorState {
    ASensorManager* manager = nullptr;
    ASensorEventQueue* queue = nullptr;
    ALooper* looper = nullptr;

    // Latest values
    float accel[3] = {0, 0, 0};
    float gyro[3] = {0, 0, 0};
    float mag[3] = {0, 0, 0};
    float rotVec[4] = {1, 0, 0, 0};  // w, x, y, z
    float proximity = -1.0f;
    bool initialized = false;

    void init() {
        if (initialized) return;
        manager = ASensorManager_getInstanceForPackage(nullptr);
        if (!manager) return;

        looper = ALooper_forThread();
        if (!looper) looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);

        queue = ASensorManager_createEventQueue(manager, looper, 0, nullptr, nullptr);
        if (!queue) return;

        // Enable sensors
        auto enable = [&](int type, int intervalUs) {
            const ASensor* s = ASensorManager_getDefaultSensor(manager, type);
            if (s) {
                ASensorEventQueue_enableSensor(queue, s);
                ASensorEventQueue_setEventRate(queue, s, intervalUs);
            }
        };

        enable(ASENSOR_TYPE_ACCELEROMETER, 16666);       // ~60Hz
        enable(ASENSOR_TYPE_GYROSCOPE, 16666);
        enable(ASENSOR_TYPE_MAGNETIC_FIELD, 50000);       // ~20Hz
        enable(ASENSOR_TYPE_ROTATION_VECTOR, 16666);
        enable(ASENSOR_TYPE_PROXIMITY, 100000);           // ~10Hz

        initialized = true;
    }

    void poll() {
        if (!queue) return;
        ASensorEvent event;
        while (ASensorEventQueue_getEvents(queue, &event, 1) > 0) {
            switch (event.type) {
                case ASENSOR_TYPE_ACCELEROMETER:
                    accel[0] = event.acceleration.x;
                    accel[1] = event.acceleration.y;
                    accel[2] = event.acceleration.z;
                    break;
                case ASENSOR_TYPE_GYROSCOPE:
                    gyro[0] = event.gyro.x;
                    gyro[1] = event.gyro.y;
                    gyro[2] = event.gyro.z;
                    break;
                case ASENSOR_TYPE_MAGNETIC_FIELD:
                    mag[0] = event.magnetic.x;
                    mag[1] = event.magnetic.y;
                    mag[2] = event.magnetic.z;
                    break;
                case ASENSOR_TYPE_ROTATION_VECTOR:
                    // rotation_vector: x, y, z, cos(theta/2)
                    rotVec[1] = event.data[0];  // x
                    rotVec[2] = event.data[1];  // y
                    rotVec[3] = event.data[2];  // z
                    // w = cos(theta/2), derived from xyz if not provided
                    if (event.data[3] != 0) {
                        rotVec[0] = event.data[3];
                    } else {
                        float d = 1.0f - event.data[0]*event.data[0]
                                       - event.data[1]*event.data[1]
                                       - event.data[2]*event.data[2];
                        rotVec[0] = (d > 0) ? sqrtf(d) : 0.0f;
                    }
                    break;
                case ASENSOR_TYPE_PROXIMITY:
                    proximity = event.distance;
                    break;
            }
        }
    }
};

SensorState& sensors() {
    static SensorState instance;
    if (!instance.initialized) instance.init();
    instance.poll();
    return instance;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// NDK sensor getters
// ---------------------------------------------------------------------------
Vec3 getAccelerometer() {
    auto& s = sensors();
    return Vec3(s.accel[0], s.accel[1], s.accel[2]);
}

Vec3 getGyroscope() {
    auto& s = sensors();
    return Vec3(s.gyro[0], s.gyro[1], s.gyro[2]);
}

Quaternion getDeviceOrientation() {
    auto& s = sensors();
    return Quaternion(s.rotVec[0], s.rotVec[1], s.rotVec[2], s.rotVec[3]);
}

float getCompassHeading() {
    auto& s = sensors();
    float ax = s.accel[0], ay = s.accel[1], az = s.accel[2];
    float ex = s.mag[0],   ey = s.mag[1],   ez = s.mag[2];

    // Normalize gravity
    float aN = sqrtf(ax*ax + ay*ay + az*az);
    if (aN < 0.001f) return 0.0f;
    ax /= aN; ay /= aN; az /= aN;

    // H = geomagnetic × gravity (= East direction)
    float Hx = ey*az - ez*ay;
    float Hy = ez*ax - ex*az;
    float Hz = ex*ay - ey*ax;
    float hN = sqrtf(Hx*Hx + Hy*Hy + Hz*Hz);
    if (hN < 0.001f) return 0.0f;
    Hx /= hN; Hy /= hN; Hz /= hN;

    // M = gravity × H (= North direction)
    float Mx = ay*Hz - az*Hy;
    float My = az*Hx - ax*Hz;

    // Azimuth: 0=North, positive=East (clockwise), in radians
    float heading = atan2f(Hy, My);
    if (heading < 0) heading += TAU;
    return heading;
}

bool isProximityClose() {
    auto& s = sensors();
    // Proximity sensor: 0 = close, max_range = far
    return s.proximity >= 0 && s.proximity < 1.0f;
}

// ---------------------------------------------------------------------------
// JNI helpers
// ---------------------------------------------------------------------------
namespace {

// Get a JNI env attached to current thread. Call detach() when done.
struct JniScope {
    JNIEnv* env = nullptr;
    JavaVM* vm = nullptr;
    bool attached = false;

    JniScope() {
        auto* activity = (ANativeActivity*)sapp_android_get_native_activity();
        if (!activity) return;
        vm = activity->vm;
        jint res = vm->GetEnv((void**)&env, JNI_VERSION_1_6);
        if (res == JNI_EDETACHED) {
            vm->AttachCurrentThread(&env, nullptr);
            attached = true;
        }
    }
    ~JniScope() {
        if (attached && vm) vm->DetachCurrentThread();
    }
    operator bool() const { return env != nullptr; }

    // Get the Activity object
    jobject activity() const {
        return ((ANativeActivity*)sapp_android_get_native_activity())->clazz;
    }

    // Get a system service by name
    jobject getSystemService(const char* name) {
        jclass ctxClass = env->FindClass("android/content/Context");
        jmethodID getService = env->GetMethodID(ctxClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
        jstring serviceName = env->NewStringUTF(name);
        jobject service = env->CallObjectMethod(activity(), getService, serviceName);
        env->DeleteLocalRef(serviceName);
        env->DeleteLocalRef(ctxClass);
        return service;
    }
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// Battery (via BatteryManager system service)
// ---------------------------------------------------------------------------
float getBatteryLevel() {
    JniScope jni;
    if (!jni) return -1.0f;

    jobject bm = jni.getSystemService("batterymanager");
    if (!bm) return -1.0f;

    jclass bmClass = jni.env->GetObjectClass(bm);
    // BatteryManager.getIntProperty(BATTERY_PROPERTY_CAPACITY) returns 0-100
    jmethodID getIntProp = jni.env->GetMethodID(bmClass, "getIntProperty", "(I)I");
    // BATTERY_PROPERTY_CAPACITY = 4
    jint capacity = jni.env->CallIntMethod(bm, getIntProp, 4);

    jni.env->DeleteLocalRef(bmClass);
    jni.env->DeleteLocalRef(bm);

    return (float)capacity / 100.0f;
}

bool isBatteryCharging() {
    JniScope jni;
    if (!jni) return false;

    jobject bm = jni.getSystemService("batterymanager");
    if (!bm) return false;

    jclass bmClass = jni.env->GetObjectClass(bm);
    jmethodID isCharging = jni.env->GetMethodID(bmClass, "isCharging", "()Z");
    jboolean charging = jni.env->CallBooleanMethod(bm, isCharging);

    jni.env->DeleteLocalRef(bmClass);
    jni.env->DeleteLocalRef(bm);

    return charging;
}

// ---------------------------------------------------------------------------
// App menu (macOS only) — stub
// ---------------------------------------------------------------------------
namespace internal {
void installAppMenu() {}
} // namespace internal

// ---------------------------------------------------------------------------
// Volume (via AudioManager)
// ---------------------------------------------------------------------------
float getSystemVolume() {
    JniScope jni;
    if (!jni) return -1.0f;

    jobject am = jni.getSystemService("audio");
    if (!am) return -1.0f;

    jclass amClass = jni.env->GetObjectClass(am);
    jmethodID getVol = jni.env->GetMethodID(amClass, "getStreamVolume", "(I)I");
    jmethodID getMax = jni.env->GetMethodID(amClass, "getStreamMaxVolume", "(I)I");
    // AudioManager.STREAM_MUSIC = 3
    jint vol = jni.env->CallIntMethod(am, getVol, 3);
    jint max = jni.env->CallIntMethod(am, getMax, 3);

    jni.env->DeleteLocalRef(amClass);
    jni.env->DeleteLocalRef(am);

    return (max > 0) ? (float)vol / (float)max : 0.0f;
}

void setSystemVolume(float volume) {
    JniScope jni;
    if (!jni) return;

    jobject am = jni.getSystemService("audio");
    if (!am) return;

    jclass amClass = jni.env->GetObjectClass(am);
    jmethodID getMax = jni.env->GetMethodID(amClass, "getStreamMaxVolume", "(I)I");
    jmethodID setVol = jni.env->GetMethodID(amClass, "setStreamVolume", "(III)V");
    // STREAM_MUSIC=3, FLAG_SHOW_UI=1
    jint max = jni.env->CallIntMethod(am, getMax, 3);
    jint target = (jint)(volume * max);
    if (target < 0) target = 0;
    if (target > max) target = max;
    jni.env->CallVoidMethod(am, setVol, 3, target, 0);

    jni.env->DeleteLocalRef(amClass);
    jni.env->DeleteLocalRef(am);
}

// ---------------------------------------------------------------------------
// Brightness (via Settings.System)
// ---------------------------------------------------------------------------
float getSystemBrightness() {
    JniScope jni;
    if (!jni) return -1.0f;

    // Get content resolver
    jclass ctxClass = jni.env->FindClass("android/content/Context");
    jmethodID getResolver = jni.env->GetMethodID(ctxClass, "getContentResolver",
        "()Landroid/content/ContentResolver;");
    jobject resolver = jni.env->CallObjectMethod(jni.activity(), getResolver);

    jclass settingsClass = jni.env->FindClass("android/provider/Settings$System");

    // Try float API first (Android 10+, returns 0.0-1.0)
    jmethodID getFloat = jni.env->GetStaticMethodID(settingsClass, "getFloat",
        "(Landroid/content/ContentResolver;Ljava/lang/String;F)F");
    jstring floatKey = jni.env->NewStringUTF("screen_brightness_float");
    jfloat brightness = jni.env->CallStaticFloatMethod(settingsClass, getFloat, resolver, floatKey, -1.0f);
    jni.env->DeleteLocalRef(floatKey);

    if (brightness < 0) {
        // Fallback: legacy integer API (0-255)
        jmethodID getInt = jni.env->GetStaticMethodID(settingsClass, "getInt",
            "(Landroid/content/ContentResolver;Ljava/lang/String;I)I");
        jstring intKey = jni.env->NewStringUTF("screen_brightness");
        jint intBrightness = jni.env->CallStaticIntMethod(settingsClass, getInt, resolver, intKey, -1);
        jni.env->DeleteLocalRef(intKey);
        brightness = (intBrightness >= 0) ? (float)intBrightness / 255.0f : -1.0f;
    }

    jni.env->DeleteLocalRef(resolver);
    jni.env->DeleteLocalRef(ctxClass);
    jni.env->DeleteLocalRef(settingsClass);

    return brightness;
}

void setSystemBrightness(float brightness) {
    // Requires WRITE_SETTINGS permission — just adjust window brightness instead
    (void)brightness;
    // TODO: use WindowManager.LayoutParams.screenBrightness via JNI on UI thread
}

// ---------------------------------------------------------------------------
// Location (requires runtime permission — stub)
// ---------------------------------------------------------------------------
Location getLocation() { return Location(); }

// ---------------------------------------------------------------------------
// Thermal (NDK API 30+)
// ---------------------------------------------------------------------------
#if __ANDROID_API__ >= 30
#include <android/thermal.h>
ThermalState getThermalState() {
    AThermalManager* mgr = AThermal_acquireManager();
    if (!mgr) return ThermalState::Nominal;
    AThermalStatus status = AThermal_getCurrentThermalStatus(mgr);
    AThermal_releaseManager(mgr);
    switch (status) {
        case ATHERMAL_STATUS_LIGHT:
        case ATHERMAL_STATUS_MODERATE: return ThermalState::Fair;
        case ATHERMAL_STATUS_SEVERE:   return ThermalState::Serious;
        case ATHERMAL_STATUS_CRITICAL:
        case ATHERMAL_STATUS_EMERGENCY:
        case ATHERMAL_STATUS_SHUTDOWN: return ThermalState::Critical;
        default: return ThermalState::Nominal;
    }
}
#else
ThermalState getThermalState() { return ThermalState::Nominal; }
#endif
float getThermalTemperature() { return -1.0f; }  // No NDK API for temperature value

void bringWindowToFront() {
    // no-op: Android apps are always foreground when running
}

} // namespace trussc

#endif // __ANDROID__
