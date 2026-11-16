// Headless native seams exercise the production Android functions, not a model.
#include "sokol_app_tc.h"
#include "util/sokol_window_event.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

namespace trussc::internal {
const char* exitReason = nullptr;
void setExitReasonIfEmpty(const char* reason) { if (!exitReason) exitReason = reason; }
}

namespace {
#define _SOKOL_PRIVATE static
#define _SOKOL_UNUSED(x) (void)(x)
#define _SAPP_INFO(x) ((void)0)
#define _SAPP_ERROR(x) check(false, #x)
#define _SAPP_WARN(x) check(false, #x)
#define _SAPP_PANIC(x) do { check(false, #x); std::abort(); } while (0)
#define SOKOL_ASSERT(x) check(x, #x)
constexpr int EGL_NO_SURFACE = 0, AINPUT_EVENT_TYPE_KEY = 1, AKEYCODE_BACK = 4;
using EGLint = int;
constexpr int EGL_WIDTH = 1, EGL_HEIGHT = 2, EGL_NONE = 0, JNI_TRUE = 1;
constexpr int AKEY_EVENT_ACTION_UP = 1, AKEY_EVENT_FLAG_CANCELED = 32;
constexpr int ALOOPER_EVENT_INPUT = 1, ALOOPER_POLL_CALLBACK = -2;
using _sapp_tc_android_msg_t = int;
enum { _SOKOL_ANDROID_MSG_CREATE, _SOKOL_ANDROID_MSG_RESUME, _SOKOL_ANDROID_MSG_PAUSE,
    _SOKOL_ANDROID_MSG_FOCUS, _SOKOL_ANDROID_MSG_NO_FOCUS, _SOKOL_ANDROID_MSG_SET_NATIVE_WINDOW,
    _SOKOL_ANDROID_MSG_SET_INPUT_QUEUE, _SOKOL_ANDROID_MSG_DESTROY };
using jclass = void*;
using jmethodID = void*;
struct JNIEnv {
    jclass GetObjectClass(void*);
    jmethodID GetMethodID(jclass, const char*, const char*);
    bool CallBooleanMethod(void*, jmethodID, int);
    void DeleteLocalRef(jclass) {}
};
struct ANativeActivity { JNIEnv* env; void* clazz; };
struct AInputEvent { int type = AINPUT_EVENT_TYPE_KEY, key = AKEYCODE_BACK, action = AKEY_EVENT_ACTION_UP, flags = 0; };
int AInputEvent_getType(const AInputEvent* e) { return e->type; }
int AKeyEvent_getKeyCode(const AInputEvent* e) { return e->key; }
int AKeyEvent_getAction(const AInputEvent* e) { return e->action; }
int AKeyEvent_getFlags(const AInputEvent* e) { return e->flags; }
struct State {
    bool quit_requested = false, quit_ordered = false, init_called = true, cleanup_called = false, valid = true;
    sapp_event event{};
    sapp_desc desc{};
    bool event_consumed = false;
    uint64_t frame_count = 0;
    int window_width = 128, window_height = 128, framebuffer_width = 128, framebuffer_height = 128;
    struct {
        bool has_resumed = true, has_focus = true, is_thread_stopping = false, has_created = false;
        bool is_thread_started = false, is_thread_stopped = false, frame_callback_in_flight = false;
        int surface = 1, display = 2, context = 3, config = 4;
        ANativeActivity* activity = nullptr;
        void* looper = nullptr;
        void* choreographer = nullptr;
        struct Resources { void* input = nullptr; void* window = nullptr; } current, pending;
        struct { int read_from_main_fd = 0, write_from_app_fd = 1, mutex = 0, cond = 0; } pt;
    } android;
} _sapp_tc;
int failures = 0, cleanups = 0, finishes = 0, requests = 0, backEvents = 0, frames = 0, polls = 0;
int backgroundPosts = 0, backgrounds = 0, pbuffers = 0, currentContext = 3;
bool onUiThread = false, destroyOnPoll = false;
bool cancelQuit = false, consumeBack = false, eventsEnabled = true, orderFromFrame = false;
bool choreographerEnabled = false, callbackPending = false;
void (*pendingCallback)(int64_t, void*) = nullptr;
void check(bool ok, const char* label) {
    if (!ok) { ++failures; std::printf("FAIL: %s\n", label); }
}
jclass JNIEnv::GetObjectClass(void*) { check(onUiThread, "JNI only on UI thread"); return this; }
jmethodID JNIEnv::GetMethodID(jclass, const char* name, const char* signature) {
    check(!std::strcmp(name, "moveTaskToBack") && !std::strcmp(signature, "(Z)Z"), "Activity background JNI method");
    return this;
}
bool JNIEnv::CallBooleanMethod(void*, jmethodID, int nonRoot) {
    check(onUiThread && nonRoot == JNI_TRUE, "moveTaskToBack(true) on UI thread");
    ++backgrounds;
    return true;
}
int write(int, const void*, size_t size) {
    check(!onUiThread && size == 1, "Back posts asynchronously from app thread");
    ++backgroundPosts;
    return static_cast<int>(size);
}
int read(int, void* buffer, size_t size) {
    if (size == sizeof(_sapp_tc_android_msg_t)) *static_cast<_sapp_tc_android_msg_t*>(buffer) = _SOKOL_ANDROID_MSG_DESTROY;
    return static_cast<int>(size);
}
int eglCreatePbufferSurface(int, int, const EGLint* attrs) {
    check(!onUiThread && attrs[1] == 1 && attrs[3] == 1, "1x1 cleanup pbuffer on app thread");
    ++pbuffers;
    return 5;
}
bool eglMakeCurrent(int, int draw, int readSurface, int context) {
    check(!onUiThread && draw && draw == readSurface, "bind cleanup surface on app thread");
    currentContext = context;
    return true;
}
bool _sapp_tc_events_enabled() { return eventsEnabled; }
void _sapp_tc_init_event(sapp_event_type type) { _sapp_tc.event.type = type; }
void eventCallback(const sapp_event* e) {
    if (e->type == SAPP_EVENTTYPE_ANDROID_BACK) { ++backEvents; _sapp_tc.event_consumed = consumeBack; }
    if (e->type == SAPP_EVENTTYPE_QUIT_REQUESTED) {
        ++requests;
        if (cancelQuit) _sapp_tc.quit_requested = false;
    }
}
bool _sapp_tc_call_event(const sapp_event* e) { return _sapp_tc_deliver_single_event(_sapp_tc, e); }
void _sapp_tc_call_cleanup() {
    check(!onUiThread && currentContext == _sapp_tc.android.context, "cleanup on app thread with its GL context");
    ++cleanups;
    _sapp_tc.cleanup_called = true;
}
void _sapp_tc_android_cleanup_egl() { _sapp_tc.android.surface = EGL_NO_SURFACE; currentContext = 0; }
void ANativeActivity_finish(void*) { ++finishes; }
void _sapp_tc_android_frame(double) {
    check(!_sapp_tc.quit_ordered, "no frame after ordered quit");
    ++frames;
    if (orderFromFrame) _sapp_tc.quit_ordered = true;
    else _sapp_tc.quit_requested = true;
}
int pthread_mutex_lock(int*) { return 0; }
int pthread_mutex_unlock(int*) { return 0; }
int pthread_cond_broadcast(int*) { return 0; }
void* ALooper_prepare(int) { return nullptr; }
int ALooper_addFd(void*, int, int, int, int (*)(int, int, void*), void*) { return 0; }
static int _sapp_tc_android_main_cb(int, int, void*);
int _sapp_tc_android_input_cb(int, int, void*) { return 1; }
bool _sapp_tc_android_init_egl() { return true; }
bool _sapp_tc_android_init_egl_surface(void*) { return true; }
void _sapp_tc_android_cleanup_egl_surface() { _sapp_tc.android.surface = EGL_NO_SURFACE; currentContext = 0; }
void _sapp_tc_android_update_dimensions(void*, bool) {}
void AInputQueue_detachLooper(void*) {}
void AInputQueue_attachLooper(void*, void*, int, int (*)(int, int, void*), void*) {}
void* AChoreographer_getInstance() { return choreographerEnabled ? &frames : nullptr; }
void AChoreographer_postFrameCallback64(void*, void (*cb)(int64_t, void*), void*) {
    pendingCallback = cb; callbackPending = true;
}
int ALooper_pollOnce(int timeout, int*, int*, void*) {
    // A bound on deterministic turns catches loops without a time-based test.
    if (++polls > 12) { std::puts("FAIL: looper failed to make progress"); std::exit(1); }
    if (destroyOnPoll) {
        _sapp_tc_android_main_cb(0, ALOOPER_EVENT_INPUT, nullptr);
        return ALOOPER_POLL_CALLBACK;
    }
    if (finishes) {
        check(timeout == -1, "accepted quit waits for Activity destroy");
        const int previousFrames = frames;
        if (callbackPending) { callbackPending = false; pendingCallback(0, nullptr); }
        check(frames == previousFrames, "ordered callback does not render");
        check(!callbackPending, "ordered callback does not schedule another frame");
        _sapp_tc_android_main_cb(0, ALOOPER_EVENT_INPUT, nullptr);
        return 0;
    }
    if (cancelQuit && requests) {
        check(!_sapp_tc.quit_ordered && !cleanups && !finishes, "veto keeps Activity and EGL alive");
        _sapp_tc.android.is_thread_stopping = true;
        return 0;
    }
    if (_sapp_tc.quit_requested || _sapp_tc.quit_ordered) {
        check(timeout == 0, "pending quit never blocks");
        return 0;
    }
    if (callbackPending) {
        callbackPending = false; pendingCallback(0, nullptr);
        return ALOOPER_POLL_CALLBACK;
    }
    return 0;
}
#include "android_functions.h"

void reset() {
    _sapp_tc = {};
    _sapp_tc.desc.event_cb = eventCallback;
    cleanups = finishes = requests = backEvents = frames = polls = 0;
    backgroundPosts = backgrounds = pbuffers = 0;
    currentContext = _sapp_tc.android.context;
    onUiThread = destroyOnPoll = false;
    cancelQuit = consumeBack = orderFromFrame = callbackPending = false;
    eventsEnabled = true;
    pendingCallback = nullptr;
}
void destroyCases(bool choreographer) {
    for (bool hasSurface : {false, true}) {
        reset();
        choreographerEnabled = choreographer;
        // Recents removal after pause/window loss must still run cleanup.
        _sapp_tc.android.has_resumed = false;
        if (!hasSurface) _sapp_tc_android_cleanup_egl_surface();
        destroyOnPoll = true;
        _sapp_tc_android_loop(nullptr);
        check(_sapp_tc.android.is_thread_stopped && _sapp_tc.quit_ordered && !_sapp_tc.valid, "destroy orders exit and completes thread handshake");
        check(cleanups == 1 && !finishes && !requests && !frames, "destroy cleanup is unconditional, with no finish/query/frame");
        check(pbuffers == (hasSurface ? 0 : 1), "pbuffer only when window surface is gone");
        _sapp_tc_android_cleanup();
        check(cleanups == 1, "destroy cleanup runs once");
    }
}
void loopCases(bool choreographer) {
    for (int mode = 0; mode < 5; ++mode) {
        reset(); choreographerEnabled = choreographer;
        cancelQuit = mode == 1;
        orderFromFrame = mode == 2;
        if (mode == 3) _sapp_tc.quit_requested = true;
        if (mode == 4) _sapp_tc.quit_ordered = true;
        _sapp_tc_android_loop(nullptr);
        check(_sapp_tc.android.is_thread_stopped, "thread completes destroy handshake");
        check(requests == ((mode == 2 || mode == 4) ? 0 : 1), "ordered quit skips query; request queries once");
        check(cleanups == (cancelQuit ? 0 : 1) && finishes == (cancelQuit ? 0 : 1), "shutdown and cleanup exactly once or vetoed");
        check(frames == (mode >= 3 ? 0 : (cancelQuit && !choreographer ? 2 : 1)), "no extra frame after request or ordered quit");
        if (cancelQuit) {
            cancelQuit = false;
            _sapp_tc.android.is_thread_stopping = false;
            _sapp_tc_android_loop(nullptr);
            check(requests == 2 && cleanups == 1 && finishes == 1, "a later quit succeeds after a veto");
        }
        // The Activity DESTROY handler also calls the production cleanup helper.
        _sapp_tc_android_cleanup();
        check(cleanups == 1, "destroy does not repeat cleanup");
    }
}
}
int main() {
    reset();
    AInputEvent event;
    check(_sapp_tc_android_key_event(&event) && backEvents == 1 && backgroundPosts == 1 && !finishes, "unconsumed Back queues background and prevents default finish");
    JNIEnv env;
    ANativeActivity activity{&env, &env};
    _sapp_tc.android.activity = &activity;
    onUiThread = true;
    _sapp_tc_android_ui_cb(0, ALOOPER_EVENT_INPUT, nullptr);
    onUiThread = false;
    check(backgrounds == 1 && !cleanups && !finishes, "background preserves app and GL state");
    consumeBack = true;
    check(_sapp_tc_android_key_event(&event) && backEvents == 2 && backgroundPosts == 1, "consumed Back never backgrounds");
    check(!_sapp_tc.event_consumed, "consumption resets after each native delivery");
    consumeBack = false;
    check(_sapp_tc_android_key_event(&event) && backEvents == 3 && backgroundPosts == 2, "next unconsumed Back backgrounds");
    event.action = 0;
    check(_sapp_tc_android_key_event(&event) && backEvents == 3 && backgroundPosts == 2, "Back down is handled without notification or backgrounding");
    event.action = AKEY_EVENT_ACTION_UP; event.flags = AKEY_EVENT_FLAG_CANCELED;
    check(_sapp_tc_android_key_event(&event) && backEvents == 3 && backgroundPosts == 2, "canceled Back up is handled without notification or backgrounding");
    event.flags = 0; event.key = 7;
    check(!_sapp_tc_android_key_event(&event), "other keys remain unhandled");
    event.key = AKEYCODE_BACK; event.type = 2;
    check(!_sapp_tc_android_key_event(&event), "non-key input remains unhandled");
    event.type = AINPUT_EVENT_TYPE_KEY; eventsEnabled = false;
    check(_sapp_tc_android_key_event(&event) && backEvents == 3 && backgroundPosts == 3, "no callback backgrounds");
    loopCases(false);
    destroyCases(false);
#if __ANDROID_API__ >= 29
    loopCases(true);
    destroyCases(true);
#endif
    std::printf("androidBackQuit: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
