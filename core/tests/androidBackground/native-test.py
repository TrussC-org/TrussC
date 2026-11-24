"""Compile the Android background source against a host looper seam (no SDK)."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
header = (root / 'core/include/sokol/sokol_app_tc.h').read_text(encoding='utf-8')


def body(marker):
    start = header.index(marker)
    end = header.index('{', start) + 1
    depth = 1
    while depth:
        depth += (header[end] == '{') - (header[end] == '}')
        end += 1
    return header[start:end]


code = r'''
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#define _SOKOL_PRIVATE
#define SOKOL_API_IMPL
constexpr int EGL_NO_SURFACE=0;
struct { bool quit_ordered=false,init_called=false,cleanup_called=false;
    struct { bool has_resumed=false,has_focus=false,is_thread_stopping=false;
        int surface=0; void (*background_tick)()=nullptr; } android;
} _sapp_tc;
''' + '\n'.join(body(marker) for marker in (
    '_SOKOL_PRIVATE bool _sapp_tc_android_should_update(',
    'SOKOL_API_IMPL void sapp_tc_android_set_background_tick(',
    '_SOKOL_PRIVATE bool _sapp_tc_android_should_background_update(',
    '_SOKOL_PRIVATE void _sapp_tc_android_background_update(')) + r'''
int ticks=0;
void update() { ++ticks; }
int main() {
    _sapp_tc.init_called=true;
    _sapp_tc_android_background_update(); assert(ticks==0);
    sapp_tc_android_set_background_tick(update);
    _sapp_tc_android_background_update(); assert(ticks==1);
    _sapp_tc.android.has_resumed=true; _sapp_tc.android.has_focus=true;
    _sapp_tc_android_background_update(); assert(ticks==2); // no surface
    _sapp_tc.android.surface=1;
    _sapp_tc_android_background_update(); assert(ticks==2); // Choreographer owns visible ticks
    _sapp_tc.android.has_focus=false;
    _sapp_tc_android_background_update(); assert(ticks==3);
    _sapp_tc.cleanup_called=true;
    _sapp_tc_android_background_update(); assert(ticks==3);
    _sapp_tc.cleanup_called=false; _sapp_tc.quit_ordered=true;
    _sapp_tc_android_background_update(); assert(ticks==3);
    _sapp_tc.quit_ordered=false; _sapp_tc.android.is_thread_stopping=true;
    _sapp_tc_android_background_update(); assert(ticks==3);
    _sapp_tc.android.is_thread_stopping=false; _sapp_tc.init_called=false;
    _sapp_tc_android_background_update(); assert(ticks==3); // no setup without a surface
    _sapp_tc.init_called=true; sapp_tc_android_set_background_tick(nullptr);
    _sapp_tc_android_background_update(); assert(ticks==3);
    puts("androidBackground: source admission, surface loss, opt-out, quit, cleanup: PASS");
}
'''
# Compile the actual loop as well, for both the legacy and Choreographer paths.
loop = body('_SOKOL_PRIVATE void* _sapp_tc_android_loop(')
loop_code = r'''
#include <cassert>
#include <cstdint>
#include <cstddef>
#define _SOKOL_PRIVATE
#define _SOKOL_UNUSED(x) ((void)(x))
#define _SAPP_INFO(x) ((void)0)
#define ALOOPER_POLL_CALLBACK -2
#define ALOOPER_POLL_TIMEOUT -3
#define ALOOPER_EVENT_INPUT 1
struct { bool quit_requested=false,quit_ordered=false;
    struct { int timestamp=0; } timing;
    struct { void* looper=nullptr; void* choreographer=nullptr; bool frame_callback_in_flight=false;
        bool is_thread_started=false,is_thread_stopping=false,is_thread_stopped=false;
        struct { int read_from_main_fd=0,mutex=0,cond=0; } pt;
        struct { void* input=nullptr; } current;
    } android;
} _sapp_tc;
int ALooper_prepare(int) { return 1; }
int _sapp_tc_android_main_cb(int,int,void*) { return 0; }
void ALooper_addFd(void*,int,int,int,int(*)(int,int,void*),void*) {}
void pthread_mutex_lock(int*) {} void pthread_mutex_unlock(int*) {}
void pthread_cond_broadcast(int*) {} void AInputQueue_detachLooper(void*) {}
void* AChoreographer_getInstance() { return (void*)1; }
void _sapp_tc_android_frame_callback(int64_t,void*) {}
void AChoreographer_postFrameCallback64(void*,void(*)(int64_t,void*),void*) {}
constexpr int SAPP_EVENTTYPE_QUIT_REQUESTED=1;
void _sapp_tc_android_app_event(int) {} void _sapp_tc_android_shutdown() {}
bool _sapp_tc_android_should_update() { return false; }
bool _sapp_tc_android_should_background_update() { return !_sapp_tc.android.is_thread_stopping; }
int ticks=0,polls=0;
double fakeTime=0;
double _sapp_tc_timestamp_now(int*) { return fakeTime; }
void _sapp_tc_android_background_update() { if (_sapp_tc_android_should_background_update()) ++ticks; }
void _sapp_tc_android_frame(double) { assert(false); }
int ALooper_pollOnce(int timeout,void*,void*,void*) {
    assert(timeout==16);
    fakeTime += 0.016;
    if (++polls==3) _sapp_tc.android.is_thread_stopping=true;
    return ALOOPER_POLL_CALLBACK; // continuous events must not starve updates
}
'''.replace('int ALooper_prepare(int) { return 1; }', 'void* ALooper_prepare(int) { return (void*)1; }') + loop + r'''
int main() { _sapp_tc_android_loop(nullptr); assert(polls==3 && ticks==2); }
'''
platform = (root / 'core/platform/android/tcPlatform_android.cpp').read_text(encoding='utf-8')
start = platform.index('void setAndroidForegroundService(bool enabled)')
end = platform.index('\n// ---------------------------------------------------------------------------', start)
jni_code = r'''
#include <cassert>
#include <cstring>
#include <sstream>
using jobject=int;
using jclass=int;
bool hidden=false;
bool isWindowOccluded() { return hidden; }
int starts=0,stops=0,frames=0,requests=0;
bool available=true,failRequest=false,exception=false;
std::ostringstream warnings;
std::ostringstream& logWarning(const char*) { return warnings; }
struct Env {
    int PushLocalFrame(int n) { assert(n>=7); ++frames; return 0; }
    void PopLocalFrame(void*) { --frames; }
    int GetObjectClass(int a) { assert(a==7); return 1; }
    int FindClass(const char* name) { assert(!strcmp(name,"android/content/Intent") || !strcmp(name,"java/lang/ClassLoader")); return 2; }
    const char* GetMethodID(int,const char* name,const char*) { return name; }
    int NewObject(int,const char*) { return 3; }
    int NewStringUTF(const char* name) { assert(!strcmp(name,"org.trussc.core.ForegroundService") || !strcmp(name,"org.trussc.core.NotificationPermission")); return 4; }
    int CallObjectMethod(int,const char* method,int=0,int=0) {
        if (!strcmp(method,"startForegroundService")) { ++starts; exception=failRequest; }
        return 5;
    }
    const char* GetStaticMethodID(int,const char* name,const char* signature) {
        assert(!strcmp(name,"request") && !strcmp(signature,"(Landroid/app/Activity;)V"));
        return name;
    }
    void CallStaticVoidMethod(int,const char*,int activity) {
        assert(activity==7 && starts>0); ++requests;
    }
    bool CallBooleanMethod(int,const char* method,int) { assert(!strcmp(method,"stopService")); ++stops; return true; }
    bool ExceptionCheck() { return exception; }
    void ExceptionClear() { exception=false; }
} environment;
struct JniScope {
    Env* env=available ? &environment : nullptr;
    explicit operator bool() const { return env; }
    int activity() const { return 7; }
};
''' + platform[start:end] + r'''
int main() {
    setAndroidForegroundService(true); assert(starts==1 && !stops && !frames && requests==1);
    setAndroidForegroundService(false); assert(stops==1 && !frames && requests==1);
    hidden=true;
    setAndroidForegroundService(true); assert(starts==2 && requests==1 && !frames);
    hidden=false;
    failRequest=true;
    setAndroidForegroundService(true); assert(starts==3 && requests==1 && !exception && !frames && !warnings.str().empty());
    available=false;
    setAndroidForegroundService(true); assert(starts==3 && requests==1 && !frames);
}
'''
with tempfile.TemporaryDirectory(prefix='android-background-', dir=root / '.build') as folder:
    for name, source_text, version in [('admission', code, 29), ('loop26', loop_code, 26), ('loop29', loop_code, 29), ('jni', jni_code, 29)]:
        source, binary = Path(folder) / (name + '.cpp'), Path(folder) / name
        source.write_text(source_text, encoding='utf-8')
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                        '-D__ANDROID_API__=' + str(version), str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
print('androidBackground: looper API 26/29+ and JNI start/stop/error body: PASS')
