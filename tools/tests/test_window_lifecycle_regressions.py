#!/usr/bin/env python3
"""Run production macOS rejection and Android frame-entry control flow with stubs.

No Apple/Android SDK or device claim. TC_WINDOW_REVIEW_SOURCE selects a private
source overlay for regressions/mutations; missing files fall back to this tree.
"""
from pathlib import Path
import os
import subprocess
import tempfile
from test_web_window_controls import body

ROOT = Path(__file__).resolve().parents[2]


def read(name):
    overlay = Path(os.environ.get('TC_WINDOW_REVIEW_SOURCE', ROOT)) / name
    return (overlay if overlay.exists() else ROOT / name).read_text(encoding='utf-8')


def main():
    native = read('core/include/sokol/sokol_app_tc.h')
    runtime = read('core/include/tc/app/tcWindowRuntime.cpp')
    global_source = read('core/include/tc/app/tcGlobal.cpp')
    window = read('core/include/tc/app/tcWindow.h')
    constructor = body(native, 'static _sapp_tc_window_t* _sapp_tc_mac_create_window_ex(')
    # Keep the entire published-but-not-ready interval, including rejection.
    rejection = constructor[constructor.index('_sapp_tc.windows[slot] = w;'):
                            constructor.index('w->ready = true;')]
    android = native[native.index('/* ---- multi-window API: explicit platform gap on Android') :]
    android = android[:android.index('void sapp_window_set_title(')]
    query = body(android, 'bool sapp_window_occluded(')
    message_handler = body(native, '_SOKOL_PRIVATE int _sapp_tc_android_main_cb(')
    messages = message_handler[message_handler.index('case _SOKOL_ANDROID_MSG_RESUME:'):
                               message_handler.index('case _SOKOL_ANDROID_MSG_SET_NATIVE_WINDOW:')]
    code = r'''
#include <cstdio>
#include <cstdint>
#include <cstddef>
#define __ANDROID__ 1
int failures=0;
void check(const char* label, bool ok) {
    printf("window-lifecycle: %s: %s\n", label, ok?"PASS":"FAIL"); failures+=!ok;
}
namespace mac {
struct _sapp_tc_window_t {
    unsigned win_id=19;
    struct { bool (*bind_cb)(unsigned,void*)=nullptr; void* user_data=nullptr; } desc;
};
struct { _sapp_tc_window_t* windows[2]{}; const char* create_error=nullptr; } _sapp_tc;
int unwound=0, bound=0;
void _sapp_tc_mac_unwind(_sapp_tc_window_t* w) {
    check("macOS registry removed before failure unwind", _sapp_tc.windows[0]!=w && _sapp_tc.windows[1]!=w);
    ++unwound; delete w;
}
''' + body(native, 'static _sapp_tc_window_t* _sapp_tc_mac_fail(') + r'''
bool reject(unsigned id, void*) {
    ++bound;
    check("macOS binder sees published native identity", _sapp_tc.windows[1] && _sapp_tc.windows[1]->win_id==id);
    return false;
}
_sapp_tc_window_t* publish(_sapp_tc_window_t* w, size_t slot) {
''' + rejection + r'''
    return w;
}
void run() {
    auto* w=new _sapp_tc_window_t; w->desc.bind_cb=reject;
    auto* result=publish(w,1);
    check("macOS rejection clears slot and unwinds exactly once", !result && !_sapp_tc.windows[1] && bound==1 && unwound==1);
    // Termination only visits published entries; do not dereference a failed slot.
    int termination_visits=0;
    for (auto* record : _sapp_tc.windows) if (record) ++termination_visits;
    check("macOS termination cannot revisit rejected record", termination_visits==0);
}
}
namespace mobile {
enum sapp_event_type { SAPP_EVENTTYPE_UNFOCUSED, SAPP_EVENTTYPE_SUSPENDED,
    SAPP_EVENTTYPE_ICONIFIED, SAPP_EVENTTYPE_RESUMED, SAPP_EVENTTYPE_RESTORED };
struct sapp_window { unsigned id; };
struct sapp_tc_window_metadata {
    int fb_width=0, fb_height=0, sample_count=0, color_format=0, depth_format=0;
    float dpi_scale=1;
    bool trust_hidden_signals=false, iconified=false, occluded=false, drawable_available=false;
};
struct WindowContext {
    bool isMain=true, suspended=false, visibilityPaused=false, mainUpdateCallTimeInitialized=true;
};
struct Window {
    struct { sapp_tc_window_metadata metadata; } binding_;
    WindowContext ctx;
    int dispatchDepth_=0;
    bool isOpen() const { return true; }
    bool isOccluded() const;
    WindowContext& context() { return ctx; }
};
''' + body(window, 'inline bool Window::isOccluded() const') + r'''
Window owner;
Window* mainWindow() { return &owner; }
constexpr bool supportsSecondaryWindows() { return false; }
Window* openWindows[1]={&owner};
// Only used by the discarded desktop branch of visibility observation.
auto& windows() { return openWindows; }
struct WindowRequestAccess {
    static auto& binding(Window& w) { return w.binding_; }
    static void applyPendingApp(Window&) {}
};
void refreshWindowMetadata(Window&) {} // single-window binding has no operations
void syncWindowAppSize(Window&, bool) {}
int queries=0;
struct { struct { bool has_resumed=false, has_focus=false; } android; } _sapp_tc;
''' + query + r'''
bool sapp_isvalid() { return true; }
int sapp_width() { return 100; } int sapp_height() { return 100; }
int sapp_sample_count() { return 1; } float sapp_dpi_scale() { return 1; }
int sapp_color_format() { return 1; } int sapp_depth_format() { return 2; }
void publishWindowMetadata(Window& w, const sapp_tc_window_metadata& m) {
    ++queries; w.binding_.metadata=m;
    w.ctx.suspended=m.trust_hidden_signals && (m.iconified || m.occluded);
}
''' + body(runtime, 'void refreshMainWindowMetadata()') + r'''
struct MainWindowDispatchScope {
    Window* window;
    MainWindowDispatchScope();
    ~MainWindowDispatchScope() { --window->dispatchDepth_; }
};
''' + body(global_source, 'MainWindowDispatchScope::MainWindowDispatchScope()') + '\n' + body(global_source, 'static bool occlusionPausesUpdate(') + '\n' + body(global_source, 'static bool nativeSuspensionPausesUpdate(') + '\n' + body(global_source, 'void observeWindowVisibility(').replace('openWindows()', 'windows()') + r'''
void _sapp_tc_android_app_event(sapp_event_type type) {
    refreshMainWindowMetadata(); observeWindowVisibility(owner.ctx,type);
}
#define _SAPP_INFO(x) ((void)0)
enum { _SOKOL_ANDROID_MSG_RESUME, _SOKOL_ANDROID_MSG_PAUSE,
       _SOKOL_ANDROID_MSG_FOCUS, _SOKOL_ANDROID_MSG_NO_FOCUS };
void message(int msg) {
    switch(msg) {
''' + messages + r'''
    }
}
void frame(bool hidden) {
    const int before=queries;
    MainWindowDispatchScope dispatch;
    check("single-window frame refreshes native observation", queries==before+1);
    check("frame getter and suspension follow current native lifecycle/focus", owner.isOccluded()==hidden && owner.ctx.suspended==hidden);
    check("mobile hidden state still pauses native updates", nativeSuspensionPausesUpdate());
}
void run() {
    message(_SOKOL_ANDROID_MSG_RESUME); message(_SOKOL_ANDROID_MSG_FOCUS); frame(false);
    message(_SOKOL_ANDROID_MSG_PAUSE); message(_SOKOL_ANDROID_MSG_NO_FOCUS); frame(true);
    message(_SOKOL_ANDROID_MSG_RESUME);
    check("resume before focus retains current hidden native snapshot", owner.isOccluded());
    frame(true);
    message(_SOKOL_ANDROID_MSG_FOCUS); // emits no event and changes no dimensions
    frame(false);
    message(_SOKOL_ANDROID_MSG_PAUSE); frame(true); // focus alone must not erase pause
    message(_SOKOL_ANDROID_MSG_RESUME); frame(false);
}
}
int main() { mac::run(); mobile::run(); return failures?1:0; }
'''
    with tempfile.TemporaryDirectory(prefix='tc-window-lifecycle-') as folder:
        source, binary = Path(folder)/'probe.cpp', Path(folder)/'probe'
        source.write_text(code, encoding='utf-8')
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra',
                        str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
