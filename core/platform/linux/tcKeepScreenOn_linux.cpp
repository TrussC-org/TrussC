// Optional X server and desktop session screen-saver inhibition.
#if defined(__linux__) && !defined(__ANDROID__)
#include "TrussC.h"
#include <X11/Xlib.h>
#include <dlfcn.h>

namespace trussc {
namespace {
// Private libdbus ABI: opaque handles, 32-bit unsigned booleans/cookies,
// int type tags and bus enum. No libdbus development headers or link dependency.
struct DBusConnection;
struct DBusMessage;
struct DBusError;
enum DBusBusType { sessionBus = 0 };
struct IdleSession {
    void* (*openLibrary)(const char*, int) = &dlopen;
    void* (*findSymbol)(void*, const char*) = &dlsym;
    void* library = nullptr;
    DBusConnection* (*busGetPrivate)(DBusBusType, DBusError*) = nullptr;
    void (*setExitOnDisconnect)(DBusConnection*, uint32_t) = nullptr;
    void (*closeConnection)(DBusConnection*) = nullptr;
    void (*unrefConnection)(DBusConnection*) = nullptr;
    DBusMessage* (*newMethodCall)(const char*, const char*, const char*, const char*) = nullptr;
    uint32_t (*appendArgs)(DBusMessage*, int, ...) = nullptr;
    uint32_t (*getArgs)(DBusMessage*, DBusError*, int, ...) = nullptr;
    DBusMessage* (*sendWithReply)(DBusConnection*, DBusMessage*, int, DBusError*) = nullptr;
    void (*unrefMessage)(DBusMessage*) = nullptr;
    DBusConnection* connection = nullptr;
    uint32_t cookie = 0;
    bool inhibited = false;
    bool requested = false; // Remember failed attempts too; retry only after off/on.

    bool load() {
        // Keep the optional library mapped for process lifetime, like libXss.
        if (!library) library = openLibrary("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL);
        if (!library) return false;
#define TC_DBUS_LOAD(member, symbol) \
        member = reinterpret_cast<decltype(member)>(findSymbol(library, symbol)); \
        if (!member) return false
        TC_DBUS_LOAD(busGetPrivate, "dbus_bus_get_private");
        TC_DBUS_LOAD(setExitOnDisconnect, "dbus_connection_set_exit_on_disconnect");
        TC_DBUS_LOAD(closeConnection, "dbus_connection_close");
        TC_DBUS_LOAD(unrefConnection, "dbus_connection_unref");
        TC_DBUS_LOAD(newMethodCall, "dbus_message_new_method_call");
        TC_DBUS_LOAD(appendArgs, "dbus_message_append_args");
        TC_DBUS_LOAD(getArgs, "dbus_message_get_args");
        TC_DBUS_LOAD(sendWithReply, "dbus_connection_send_with_reply_and_block");
        TC_DBUS_LOAD(unrefMessage, "dbus_message_unref");
#undef TC_DBUS_LOAD
        return true;
    }
    DBusMessage* call(bool enable) {
        auto* message = newMethodCall("org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver",
                                      "org.freedesktop.ScreenSaver", enable ? "Inhibit" : "UnInhibit");
        if (!message) return nullptr;
        const char* app = "TrussC";
        const char* reason = "Keep the exhibition display on";
        const bool appended = enable ? appendArgs(message, 's', &app, 's', &reason, 0)
                                     : appendArgs(message, 'u', &cookie, 0);
        // Bound (main-loop responsiveness): one second for method replies only, including cleanup.
        // busGetPrivate blocks through authentication/registration; a stalled bus can stall the main loop.
        auto* reply = appended ? sendWithReply(connection, message, 1000, nullptr) : nullptr;
        unrefMessage(message);
        return reply;
    }
    void disconnect() {
        if (connection) {
            closeConnection(connection);
            unrefConnection(connection);
            connection = nullptr;
        }
    }
    bool acquire() {
        if (inhibited) return true;
        if (!load()) return false;
        connection = busGetPrivate(sessionBus, nullptr);
        if (!connection) return false;
        setExitOnDisconnect(connection, false);
        if (auto* reply = call(true)) {
            inhibited = getArgs(reply, nullptr, 'u', &cookie, 0);
            unrefMessage(reply);
        }
        if (!inhibited) disconnect();
        return inhibited;
    }
    void warnUnavailable() {
        logWarning("Platform") << "No screen-saver inhibit service on this session; on Raspberry Pi turn off Screen Blanking in raspi-config.";
    }
    void release() {
        bool released = true;
        if (inhibited) {
            auto* reply = call(false);
            released = reply != nullptr;
            if (reply) unrefMessage(reply);
        }
        inhibited = false;
        cookie = 0;
        disconnect();
        if (!released) warnUnavailable();
    }
    void apply(bool enabled, bool headless) {
        if (enabled == requested) return;
        requested = enabled;
        if (!enabled) { release(); return; }
        if (headless || !acquire()) warnUnavailable();
    }
};
IdleSession& idleSession() {
    static IdleSession backend;
    return backend;
}
struct IdleX11 {
    Display* display = nullptr; // Host main thread only; released before cleanup closes it.
    void* (*openLibrary)(const char*, int) = &dlopen;
    void* (*findSymbol)(void*, const char*) = &dlsym;
    void* library = nullptr;
    // Private libXss ABI: Xlib Bool and Status are int; no development header required.
    Bool (*queryExtension)(Display*, int*, int*) = nullptr;
    Status (*queryVersion)(Display*, int*, int*) = nullptr;
    void (*suspendSaver)(Display*, Bool) = nullptr;
    bool suspended = false;
    bool reported = false;

    bool acquire(Display* host) {
        if (suspended) return true;
        if (!host) return false;
        // Xlib keeps extension callbacks into this library, even after release.
        // Retain its code for process lifetime; never dlclose a live Xlib callback.
        if (!library) library = openLibrary("libXss.so.1", RTLD_NOW | RTLD_LOCAL);
        if (!library) return false;
        queryExtension = reinterpret_cast<decltype(queryExtension)>(findSymbol(library, "XScreenSaverQueryExtension"));
        queryVersion = reinterpret_cast<decltype(queryVersion)>(findSymbol(library, "XScreenSaverQueryVersion"));
        suspendSaver = reinterpret_cast<decltype(suspendSaver)>(findSymbol(library, "XScreenSaverSuspend"));
        int event = 0, error = 0, major = 0, minor = 0;
        if (!queryExtension || !queryVersion || !suspendSaver ||
            !queryExtension(host, &event, &error) ||
            !queryVersion(host, &major, &minor) || major < 1 || (major == 1 && minor < 1))
            return false;
        display = host;
        suspendSaver(display, True);
        suspended = true;
        return true;
    }
    void release() {
        if (suspended) suspendSaver(display, False);
        suspended = false;
        display = nullptr;
    }
    void apply(bool enabled, Display* host) {
        if (!enabled) { release(); return; }
        if (!acquire(host) && !reported) {
            reported = true;
            logNotice("Platform") << "Keep-screen-on unavailable; disable screen blanking in your OS or desktop settings.";
        }
    }
};
IdleX11& idleX11() {
    static IdleX11 backend;
    return backend;
}
} // namespace
namespace internal {
void applyKeepScreenOn(bool enabled, bool headless) {
    Display* display = nullptr;
    if (enabled && !headless)
        display = static_cast<Display*>(const_cast<void*>(sapp_x11_get_display()));
    idleX11().apply(enabled, display);
    idleSession().apply(enabled, headless);
}
} // namespace internal
} // namespace trussc
#endif
