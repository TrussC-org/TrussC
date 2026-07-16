// =============================================================================
// Android Serial backend (USB Host, CDC-ACM)
//
// No Java sources required:
// - Enumeration / permission / interface claiming use the framework USB Host
//   API (android.hardware.usb.*) called via JNI.
// - The permission result is detected by polling UsbManager.hasPermission()
//   from a worker thread, so no BroadcastReceiver class (= no .dex) is needed.
//   The PendingIntent passed to requestPermission() broadcasts to nowhere.
// - Bulk data I/O bypasses JNI entirely: UsbDeviceConnection.getFileDescriptor()
//   exposes the usbfs fd, and reads/writes go through USBDEVFS_BULK ioctls.
//
// Limitations:
// - CDC-ACM class devices only (nRF, RP2040, ESP32-S2/S3 native USB, etc.).
//   Vendor protocol chips (FTDI, CP210x, CH34x) are not supported here.
// - First connect shows the system permission dialog: setup() returns false,
//   the connection completes asynchronously, and isInitialized() flips to
//   true once granted. setup() for the same device while the dialog is
//   pending is a no-op, so app-side reconnect loops are safe.
// =============================================================================

#include "TrussC.h"

#ifdef __ANDROID__

#include <jni.h>
#include <android/native_activity.h>
#include <linux/usbdevice_fs.h>
#include <sys/ioctl.h>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>
#include "sokol_app_tc.h"

namespace trussc {
namespace androidserial {

namespace {

// USB / CDC constants
constexpr int USB_CLASS_COMM     = 2;    // CDC control interface
constexpr int USB_CLASS_CDC_DATA = 10;   // CDC data interface
constexpr int EP_TYPE_BULK       = 2;    // UsbConstants.USB_ENDPOINT_XFER_BULK
constexpr int EP_DIR_IN          = 0x80; // UsbConstants.USB_DIR_IN

// CDC-ACM class requests
constexpr int CDC_REQ_TYPE               = 0x21; // host-to-device | class | interface
constexpr int CDC_SET_LINE_CODING        = 0x20;
constexpr int CDC_SET_CONTROL_LINE_STATE = 0x22;
constexpr int CDC_LINE_DTR_RTS           = 0x03;

constexpr int    PERMISSION_TIMEOUT_SEC = 60;   // give up on an unanswered dialog
constexpr int    PERMISSION_POLL_MS     = 200;
constexpr int    READ_TIMEOUT_MS        = 250;  // per-ioctl bulk read timeout
constexpr int    WRITE_TIMEOUT_MS       = 1000;
constexpr size_t RX_BUFFER_CAP          = 1 << 20;  // drop oldest beyond 1 MiB
constexpr int    READ_CHUNK             = 4096;     // multiple of bulk max packet
constexpr int    WRITE_CHUNK            = 16384;    // usbfs per-urb limit

enum class State : int { Idle = 0, Pending = 1, Connected = 2 };

// Get a JNI env attached to the current thread (same pattern as
// tcPlatform_android.cpp). Detaches on destruction if it attached.
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

    jobject activity() const {
        return ((ANativeActivity*)sapp_android_get_native_activity())->clazz;
    }

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

// Log and clear a pending Java exception. Returns true if one was pending.
bool clearJniException(JNIEnv* env, const char* what) {
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        logError() << "Serial: " << what << " threw a Java exception";
        return true;
    }
    return false;
}

std::string jstringToStd(JNIEnv* env, jstring js) {
    if (!js) return "";
    const char* chars = env->GetStringUTFChars(js, nullptr);
    std::string result = chars ? chars : "";
    if (chars) env->ReleaseStringUTFChars(js, chars);
    return result;
}

int sdkInt(JNIEnv* env) {
    jclass ver = env->FindClass("android/os/Build$VERSION");
    jfieldID f = env->GetStaticFieldID(ver, "SDK_INT", "I");
    int v = env->GetStaticIntField(ver, f);
    env->DeleteLocalRef(ver);
    return v;
}

// Find a UsbDevice by usbfs path (UsbDevice.getDeviceName()).
// Returns a local ref or nullptr.
jobject findDeviceByPath(JNIEnv* env, jobject usbManager, const std::string& path) {
    jclass umClass = env->GetObjectClass(usbManager);
    jmethodID getDeviceList = env->GetMethodID(umClass, "getDeviceList", "()Ljava/util/HashMap;");
    jobject map = env->CallObjectMethod(usbManager, getDeviceList);
    env->DeleteLocalRef(umClass);
    if (clearJniException(env, "getDeviceList") || !map) return nullptr;

    jclass mapClass = env->GetObjectClass(map);
    jmethodID get = env->GetMethodID(mapClass, "get", "(Ljava/lang/Object;)Ljava/lang/Object;");
    jstring key = env->NewStringUTF(path.c_str());
    jobject device = env->CallObjectMethod(map, get, key);
    env->DeleteLocalRef(key);
    env->DeleteLocalRef(mapClass);
    env->DeleteLocalRef(map);
    return device;
}

bool hasPermission(JNIEnv* env, jobject usbManager, jobject device) {
    jclass umClass = env->GetObjectClass(usbManager);
    jmethodID has = env->GetMethodID(umClass, "hasPermission", "(Landroid/hardware/usb/UsbDevice;)Z");
    bool result = env->CallBooleanMethod(usbManager, has, device);
    env->DeleteLocalRef(umClass);
    if (clearJniException(env, "hasPermission")) return false;
    return result;
}

// Show the system USB permission dialog. The PendingIntent broadcast goes
// nowhere (no receiver registered); the result is observed by polling
// hasPermission(). The Intent must be explicit (setPackage) because apps
// targeting API 34+ may not create mutable PendingIntents from implicit ones.
bool requestPermission(JniScope& jni, jobject usbManager, jobject device) {
    JNIEnv* env = jni.env;

    jclass ctxClass = env->FindClass("android/content/Context");
    jmethodID getPackageName = env->GetMethodID(ctxClass, "getPackageName", "()Ljava/lang/String;");
    auto packageName = (jstring)env->CallObjectMethod(jni.activity(), getPackageName);
    env->DeleteLocalRef(ctxClass);

    jclass intentClass = env->FindClass("android/content/Intent");
    jmethodID intentCtor = env->GetMethodID(intentClass, "<init>", "(Ljava/lang/String;)V");
    jmethodID setPackage = env->GetMethodID(intentClass, "setPackage", "(Ljava/lang/String;)Landroid/content/Intent;");
    jstring action = env->NewStringUTF("trussc.serial.USB_PERMISSION");
    jobject intent = env->NewObject(intentClass, intentCtor, action);
    env->DeleteLocalRef(env->CallObjectMethod(intent, setPackage, packageName));
    env->DeleteLocalRef(action);
    env->DeleteLocalRef(packageName);
    env->DeleteLocalRef(intentClass);

    // FLAG_MUTABLE (API 31+): the system fills the result extras
    int flags = (sdkInt(env) >= 31) ? 0x02000000 : 0;
    jclass piClass = env->FindClass("android/app/PendingIntent");
    jmethodID getBroadcast = env->GetStaticMethodID(piClass, "getBroadcast",
        "(Landroid/content/Context;ILandroid/content/Intent;I)Landroid/app/PendingIntent;");
    jobject pending = env->CallStaticObjectMethod(piClass, getBroadcast, jni.activity(), 0, intent, flags);
    env->DeleteLocalRef(intent);
    env->DeleteLocalRef(piClass);
    if (clearJniException(env, "PendingIntent.getBroadcast") || !pending) return false;

    jclass umClass = env->GetObjectClass(usbManager);
    jmethodID request = env->GetMethodID(umClass, "requestPermission",
        "(Landroid/hardware/usb/UsbDevice;Landroid/app/PendingIntent;)V");
    env->CallVoidMethod(usbManager, request, device, pending);
    env->DeleteLocalRef(umClass);
    env->DeleteLocalRef(pending);
    return !clearJniException(env, "requestPermission");
}

// True if the device exposes a CDC data interface (class 10)
bool isCdcDevice(JNIEnv* env, jobject device) {
    jclass devClass = env->GetObjectClass(device);
    jmethodID getInterfaceCount = env->GetMethodID(devClass, "getInterfaceCount", "()I");
    jmethodID getInterface = env->GetMethodID(devClass, "getInterface", "(I)Landroid/hardware/usb/UsbInterface;");
    int count = env->CallIntMethod(device, getInterfaceCount);

    bool found = false;
    for (int i = 0; i < count && !found; i++) {
        jobject iface = env->CallObjectMethod(device, getInterface, i);
        if (!iface) continue;
        jclass ifClass = env->GetObjectClass(iface);
        jmethodID getInterfaceClass = env->GetMethodID(ifClass, "getInterfaceClass", "()I");
        found = (env->CallIntMethod(iface, getInterfaceClass) == USB_CLASS_CDC_DATA);
        env->DeleteLocalRef(ifClass);
        env->DeleteLocalRef(iface);
    }
    env->DeleteLocalRef(devClass);
    return found;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

struct Impl {
    std::atomic<int> state{(int)State::Idle};
    std::atomic<bool> stop{false};
    std::string path;       // requested usbfs path
    int baud = 9600;
    std::thread worker;     // permission poll + bulk read loop

    jobject connection = nullptr;  // global ref to UsbDeviceConnection
    int fd = -1;                   // usbfs fd owned by `connection`
    int epIn = 0;                  // bulk IN endpoint address
    int epOut = 0;                 // bulk OUT endpoint address

    std::mutex rxMutex;
    std::deque<uint8_t> rx;
    bool rxOverflowWarned = false;
};

namespace {

// Open the device, claim the CDC interfaces, apply line coding, and fill
// impl->connection/fd/epIn/epOut. Caller must hold the permission.
bool openAndClaim(Impl* impl, JniScope& jni) {
    JNIEnv* env = jni.env;

    jobject usbManager = jni.getSystemService("usb");
    if (!usbManager) {
        logError() << "Serial: UsbManager unavailable";
        return false;
    }
    jobject device = findDeviceByPath(env, usbManager, impl->path);
    if (!device) {
        logError() << "Serial: device not found: " << impl->path;
        env->DeleteLocalRef(usbManager);
        return false;
    }

    jclass umClass = env->GetObjectClass(usbManager);
    jmethodID openDevice = env->GetMethodID(umClass, "openDevice",
        "(Landroid/hardware/usb/UsbDevice;)Landroid/hardware/usb/UsbDeviceConnection;");
    jobject connection = env->CallObjectMethod(usbManager, openDevice, device);
    env->DeleteLocalRef(umClass);
    env->DeleteLocalRef(usbManager);
    if (clearJniException(env, "openDevice") || !connection) {
        logError() << "Serial: openDevice failed for " << impl->path;
        env->DeleteLocalRef(device);
        return false;
    }

    // Locate the CDC control (class 2) and data (class 10) interfaces
    jclass devClass = env->GetObjectClass(device);
    jmethodID getInterfaceCount = env->GetMethodID(devClass, "getInterfaceCount", "()I");
    jmethodID getInterface = env->GetMethodID(devClass, "getInterface", "(I)Landroid/hardware/usb/UsbInterface;");
    int ifCount = env->CallIntMethod(device, getInterfaceCount);
    env->DeleteLocalRef(devClass);

    jobject commIface = nullptr;
    jobject dataIface = nullptr;
    for (int i = 0; i < ifCount; i++) {
        jobject iface = env->CallObjectMethod(device, getInterface, i);
        if (!iface) continue;
        jclass ifClass = env->GetObjectClass(iface);
        jmethodID getInterfaceClass = env->GetMethodID(ifClass, "getInterfaceClass", "()I");
        int cls = env->CallIntMethod(iface, getInterfaceClass);
        env->DeleteLocalRef(ifClass);
        if (cls == USB_CLASS_CDC_DATA && !dataIface) {
            dataIface = iface;
        } else if (cls == USB_CLASS_COMM && !commIface) {
            commIface = iface;
        } else {
            env->DeleteLocalRef(iface);
        }
    }
    env->DeleteLocalRef(device);

    jclass connClass = env->GetObjectClass(connection);
    jmethodID claimInterface = env->GetMethodID(connClass, "claimInterface",
        "(Landroid/hardware/usb/UsbInterface;Z)Z");
    jmethodID controlTransfer = env->GetMethodID(connClass, "controlTransfer", "(IIII[BII)I");
    jmethodID getFileDescriptor = env->GetMethodID(connClass, "getFileDescriptor", "()I");
    jmethodID connClose = env->GetMethodID(connClass, "close", "()V");

    auto fail = [&](const char* msg) {
        logError() << "Serial: " << msg << " (" << impl->path << ")";
        env->CallVoidMethod(connection, connClose);
        clearJniException(env, "close");
        if (commIface) env->DeleteLocalRef(commIface);
        if (dataIface) env->DeleteLocalRef(dataIface);
        env->DeleteLocalRef(connClass);
        env->DeleteLocalRef(connection);
        return false;
    };

    if (!dataIface) {
        return fail("no CDC data interface found (only CDC-ACM devices are supported on Android)");
    }

    // Claim (force = true detaches a kernel driver if one is bound)
    if (commIface) {
        env->CallBooleanMethod(connection, claimInterface, commIface, JNI_TRUE);
        clearJniException(env, "claimInterface(comm)");
    }
    bool claimed = env->CallBooleanMethod(connection, claimInterface, dataIface, JNI_TRUE);
    if (clearJniException(env, "claimInterface(data)") || !claimed) {
        return fail("failed to claim CDC data interface");
    }

    // Find bulk IN/OUT endpoints on the data interface
    jclass ifClass = env->GetObjectClass(dataIface);
    jmethodID getEndpointCount = env->GetMethodID(ifClass, "getEndpointCount", "()I");
    jmethodID getEndpoint = env->GetMethodID(ifClass, "getEndpoint", "(I)Landroid/hardware/usb/UsbEndpoint;");
    jmethodID getId = env->GetMethodID(ifClass, "getId", "()I");
    int commIfaceId = commIface ? env->CallIntMethod(commIface, getId) : 0;

    int epIn = 0, epOut = 0;
    int epCount = env->CallIntMethod(dataIface, getEndpointCount);
    for (int i = 0; i < epCount; i++) {
        jobject ep = env->CallObjectMethod(dataIface, getEndpoint, i);
        if (!ep) continue;
        jclass epClass = env->GetObjectClass(ep);
        jmethodID getType = env->GetMethodID(epClass, "getType", "()I");
        jmethodID getAddress = env->GetMethodID(epClass, "getAddress", "()I");
        if (env->CallIntMethod(ep, getType) == EP_TYPE_BULK) {
            int addr = env->CallIntMethod(ep, getAddress);
            if (addr & EP_DIR_IN) epIn = addr;
            else epOut = addr;
        }
        env->DeleteLocalRef(epClass);
        env->DeleteLocalRef(ep);
    }
    env->DeleteLocalRef(ifClass);

    if (!epIn || !epOut) {
        return fail("bulk endpoints not found on CDC data interface");
    }

    // CDC-ACM line coding: baud (LE32) + 1 stop bit + no parity + 8 data bits.
    // Some devices (nRF among them) ignore this; failures are non-fatal.
    {
        unsigned char coding[7] = {
            (unsigned char)(impl->baud & 0xFF),
            (unsigned char)((impl->baud >> 8) & 0xFF),
            (unsigned char)((impl->baud >> 16) & 0xFF),
            (unsigned char)((impl->baud >> 24) & 0xFF),
            0, 0, 8
        };
        jbyteArray arr = env->NewByteArray(7);
        env->SetByteArrayRegion(arr, 0, 7, (const jbyte*)coding);
        int r = env->CallIntMethod(connection, controlTransfer,
            CDC_REQ_TYPE, CDC_SET_LINE_CODING, 0, commIfaceId, arr, 7, WRITE_TIMEOUT_MS);
        clearJniException(env, "controlTransfer(SET_LINE_CODING)");
        env->DeleteLocalRef(arr);
        if (r < 0) logWarning() << "Serial: SET_LINE_CODING not accepted (continuing)";

        // Assert DTR + RTS so the device starts sending
        env->CallIntMethod(connection, controlTransfer,
            CDC_REQ_TYPE, CDC_SET_CONTROL_LINE_STATE, CDC_LINE_DTR_RTS, commIfaceId,
            (jbyteArray) nullptr, 0, WRITE_TIMEOUT_MS);
        clearJniException(env, "controlTransfer(SET_CONTROL_LINE_STATE)");
    }

    int fd = env->CallIntMethod(connection, getFileDescriptor);
    if (clearJniException(env, "getFileDescriptor") || fd < 0) {
        return fail("could not get usbfs file descriptor");
    }

    impl->connection = env->NewGlobalRef(connection);
    impl->fd = fd;
    impl->epIn = epIn;
    impl->epOut = epOut;

    if (commIface) env->DeleteLocalRef(commIface);
    env->DeleteLocalRef(dataIface);
    env->DeleteLocalRef(connClass);
    env->DeleteLocalRef(connection);
    return true;
}

// Worker thread: waits for the permission dialog result (if pending),
// connects, then runs the bulk read loop on the raw fd (no JNI in the loop).
void workerMain(Impl* impl) {
    if (impl->state.load() == (int)State::Pending) {
        JniScope jni;
        if (!jni) {
            impl->state = (int)State::Idle;
            return;
        }
        jobject usbManager = jni.getSystemService("usb");
        jobject device = usbManager ? findDeviceByPath(jni.env, usbManager, impl->path) : nullptr;
        if (!device) {
            if (usbManager) jni.env->DeleteLocalRef(usbManager);
            impl->state = (int)State::Idle;
            return;
        }

        auto deadline = std::chrono::steady_clock::now()
                      + std::chrono::seconds(PERMISSION_TIMEOUT_SEC);
        bool granted = false;
        while (!impl->stop.load()) {
            if (hasPermission(jni.env, usbManager, device)) {
                granted = true;
                break;
            }
            if (std::chrono::steady_clock::now() > deadline) {
                logWarning() << "Serial: USB permission not granted within "
                             << PERMISSION_TIMEOUT_SEC << "s for " << impl->path
                             << " (setup() will re-show the dialog)";
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(PERMISSION_POLL_MS));
        }
        jni.env->DeleteLocalRef(device);
        jni.env->DeleteLocalRef(usbManager);

        if (!granted || impl->stop.load()) {
            impl->state = (int)State::Idle;
            return;
        }
        if (!openAndClaim(impl, jni)) {
            impl->state = (int)State::Idle;
            return;
        }
        impl->state = (int)State::Connected;
        logNotice() << "Serial: connected to " << impl->path << " at " << impl->baud << " baud";
    }

    // Bulk read loop. usbfs returns -ETIMEDOUT when no data arrived within
    // the window, which doubles as our stop-flag poll interval.
    std::vector<unsigned char> buf(READ_CHUNK);
    while (!impl->stop.load()) {
        usbdevfs_bulktransfer bt{};
        bt.ep = (unsigned int)impl->epIn;
        bt.len = (unsigned int)buf.size();
        bt.timeout = READ_TIMEOUT_MS;
        bt.data = buf.data();
        int r = ioctl(impl->fd, USBDEVFS_BULK, &bt);
        if (r > 0) {
            std::lock_guard<std::mutex> lock(impl->rxMutex);
            impl->rx.insert(impl->rx.end(), buf.begin(), buf.begin() + r);
            if (impl->rx.size() > RX_BUFFER_CAP) {
                if (!impl->rxOverflowWarned) {
                    impl->rxOverflowWarned = true;
                    logWarning() << "Serial: RX buffer overflow, dropping oldest data (app is not reading fast enough)";
                }
                impl->rx.erase(impl->rx.begin(), impl->rx.begin() + (impl->rx.size() - RX_BUFFER_CAP));
            }
        } else if (r < 0 && (errno == ETIMEDOUT || errno == EAGAIN || errno == EINTR)) {
            continue;
        } else if (r < 0) {
            // ENODEV / EIO / ESHUTDOWN: device unplugged or connection broken
            logWarning() << "Serial: device lost: " << impl->path;
            impl->state = (int)State::Idle;
            return;
        }
    }
}

// Stop the worker and release the USB connection. Joins the thread, so it
// must never be called from the worker itself.
void closeImpl(Impl* impl) {
    impl->stop = true;
    if (impl->worker.joinable()) impl->worker.join();
    impl->stop = false;

    if (impl->connection) {
        JniScope jni;
        if (jni) {
            jclass connClass = jni.env->GetObjectClass(impl->connection);
            jmethodID connClose = jni.env->GetMethodID(connClass, "close", "()V");
            jni.env->CallVoidMethod(impl->connection, connClose);
            clearJniException(jni.env, "close");
            jni.env->DeleteLocalRef(connClass);
            jni.env->DeleteGlobalRef(impl->connection);
        }
        impl->connection = nullptr;
        if (impl->state.load() == (int)State::Connected) {
            logVerbose() << "Serial: disconnected from " << impl->path;
        }
    }
    impl->fd = -1;
    impl->epIn = 0;
    impl->epOut = 0;
    impl->state = (int)State::Idle;

    std::lock_guard<std::mutex> lock(impl->rxMutex);
    impl->rx.clear();
    impl->rxOverflowWarned = false;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public backend API (called from tcSerial.h)
// ---------------------------------------------------------------------------

Impl* create() {
    return new Impl();
}

void destroy(Impl* impl) {
    if (!impl) return;
    closeImpl(impl);
    delete impl;
}

std::vector<SerialDeviceInfo> listDevices() {
    std::vector<SerialDeviceInfo> devices;
    JniScope jni;
    if (!jni) return devices;
    JNIEnv* env = jni.env;

    jobject usbManager = jni.getSystemService("usb");
    if (!usbManager) return devices;

    jclass umClass = env->GetObjectClass(usbManager);
    jmethodID getDeviceList = env->GetMethodID(umClass, "getDeviceList", "()Ljava/util/HashMap;");
    jobject map = env->CallObjectMethod(usbManager, getDeviceList);
    env->DeleteLocalRef(umClass);
    env->DeleteLocalRef(usbManager);
    if (clearJniException(env, "getDeviceList") || !map) return devices;

    jclass mapClass = env->GetObjectClass(map);
    jmethodID values = env->GetMethodID(mapClass, "values", "()Ljava/util/Collection;");
    jobject collection = env->CallObjectMethod(map, values);
    env->DeleteLocalRef(mapClass);
    env->DeleteLocalRef(map);
    if (!collection) return devices;

    jclass collClass = env->GetObjectClass(collection);
    jmethodID iteratorMethod = env->GetMethodID(collClass, "iterator", "()Ljava/util/Iterator;");
    jobject iterator = env->CallObjectMethod(collection, iteratorMethod);
    env->DeleteLocalRef(collClass);
    env->DeleteLocalRef(collection);
    if (!iterator) return devices;

    jclass iterClass = env->GetObjectClass(iterator);
    jmethodID hasNext = env->GetMethodID(iterClass, "hasNext", "()Z");
    jmethodID next = env->GetMethodID(iterClass, "next", "()Ljava/lang/Object;");
    env->DeleteLocalRef(iterClass);

    int id = 0;
    while (env->CallBooleanMethod(iterator, hasNext)) {
        jobject device = env->CallObjectMethod(iterator, next);
        if (!device) continue;

        if (isCdcDevice(env, device)) {
            jclass devClass = env->GetObjectClass(device);
            jmethodID getDeviceName = env->GetMethodID(devClass, "getDeviceName", "()Ljava/lang/String;");
            jmethodID getProductName = env->GetMethodID(devClass, "getProductName", "()Ljava/lang/String;");
            auto pathStr = (jstring)env->CallObjectMethod(device, getDeviceName);
            auto productStr = (jstring)env->CallObjectMethod(device, getProductName);
            clearJniException(env, "getProductName");
            env->DeleteLocalRef(devClass);

            SerialDeviceInfo info;
            info.deviceId = id++;
            info.devicePath = jstringToStd(env, pathStr);
            std::string product = jstringToStd(env, productStr);
            info.deviceName = product.empty() ? info.devicePath : product;
            if (pathStr) env->DeleteLocalRef(pathStr);
            if (productStr) env->DeleteLocalRef(productStr);
            devices.push_back(info);
        }
        env->DeleteLocalRef(device);
    }
    env->DeleteLocalRef(iterator);
    return devices;
}

bool setup(Impl* impl, const std::string& devicePath, int baudRate) {
    // Reconnect-loop guard: while the permission dialog for this device is
    // still pending, repeated setup() calls must not re-trigger it.
    if (impl->state.load() == (int)State::Pending && impl->path == devicePath) {
        logVerbose() << "Serial: USB permission still pending for " << devicePath;
        return false;
    }

    closeImpl(impl);
    impl->path = devicePath;
    impl->baud = baudRate;

    JniScope jni;
    if (!jni) {
        logError() << "Serial: JNI unavailable";
        return false;
    }
    jobject usbManager = jni.getSystemService("usb");
    if (!usbManager) {
        logError() << "Serial: UsbManager unavailable";
        return false;
    }
    jobject device = findDeviceByPath(jni.env, usbManager, devicePath);
    if (!device) {
        logError() << "Serial: device not found: " << devicePath;
        jni.env->DeleteLocalRef(usbManager);
        return false;
    }

    if (hasPermission(jni.env, usbManager, device)) {
        jni.env->DeleteLocalRef(device);
        jni.env->DeleteLocalRef(usbManager);
        if (!openAndClaim(impl, jni)) return false;
        impl->state = (int)State::Connected;
        impl->worker = std::thread(workerMain, impl);
        logNotice() << "Serial: connected to " << devicePath << " at " << baudRate << " baud";
        return true;
    }

    // No permission yet: show the dialog and finish connecting asynchronously
    bool requested = requestPermission(jni, usbManager, device);
    jni.env->DeleteLocalRef(device);
    jni.env->DeleteLocalRef(usbManager);
    if (!requested) {
        logError() << "Serial: USB permission request failed for " << devicePath;
        return false;
    }
    impl->state = (int)State::Pending;
    impl->worker = std::thread(workerMain, impl);
    logNotice() << "Serial: requesting USB permission for " << devicePath
                << " (isInitialized() becomes true once granted)";
    return false;
}

void close(Impl* impl) {
    closeImpl(impl);
}

bool isConnected(const Impl* impl) {
    return impl->state.load() == (int)State::Connected;
}

int available(const Impl* impl) {
    std::lock_guard<std::mutex> lock(const_cast<Impl*>(impl)->rxMutex);
    return (int)impl->rx.size();
}

int readBytes(Impl* impl, void* buffer, int length) {
    if (length <= 0) return 0;
    std::lock_guard<std::mutex> lock(impl->rxMutex);
    int n = (int)std::min<size_t>((size_t)length, impl->rx.size());
    for (int i = 0; i < n; i++) {
        ((unsigned char*)buffer)[i] = impl->rx.front();
        impl->rx.pop_front();
    }
    return n;
}

int writeBytes(Impl* impl, const void* buffer, int length) {
    if (impl->state.load() != (int)State::Connected) return -1;
    if (length <= 0) return 0;

    int written = 0;
    while (written < length) {
        int chunk = std::min(WRITE_CHUNK, length - written);
        usbdevfs_bulktransfer bt{};
        bt.ep = (unsigned int)impl->epOut;
        bt.len = (unsigned int)chunk;
        bt.timeout = WRITE_TIMEOUT_MS;
        bt.data = (void*)((const unsigned char*)buffer + written);
        int r = ioctl(impl->fd, USBDEVFS_BULK, &bt);
        if (r < 0) {
            logError() << "Serial: write failed (" << strerror(errno) << ")";
            return written > 0 ? written : -1;
        }
        written += r;
        if (r == 0) break;
    }
    return written;
}

void flushInput(Impl* impl) {
    std::lock_guard<std::mutex> lock(impl->rxMutex);
    impl->rx.clear();
    impl->rxOverflowWarned = false;
}

} // namespace androidserial
} // namespace trussc

#endif // __ANDROID__
