"""Generate SDK-free admission seams from production native sends/init/loops.
The generated code calls the production-linked host converter and listeners.
Only OS APIs and draw bodies are replaced. Run via host-entry-test, or run this
script through tcbuild.sh for the macOS init Quit/close exception and mutations.
"""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools/tests'))
from test_web_window_controls import body


def generate(native, early=False):
    parts = []
    predicates = list(__import__('re').finditer(r'static bool _sapp_tc_app_events_enabled\(void\) \{', native))
    for platform, index, backend in [('mac', 0, 'SOKOL_GLCORE'), ('win32', 1, 'SOKOL_GLCORE'),
                                      ('x11_glx', 2, 'SOKOL_GLCORE'), ('x11_egl', 2, 'SOKOL_GLES3')]:
        port = platform.split('_')[0]
        start = predicates[index].start()
        send_start = native.rindex('static void _sapp_tc_send(_sapp_tc_window_t* w, sapp_event* ev) {', 0, start)
        send = body(native[send_start:start], 'static void _sapp_tc_send(')
        predicate = body(native[start:native.index('static void _sapp_tc_main_event_tramp', start)], 'static bool _sapp_tc_app_events_enabled(')
        init = body(native, f'static void _sapp_tc_{port}_init_client(')
        code = r'''
struct Descriptor {
    void (*init_cb)()=nullptr; void (*init_userdata_cb)(void*)=nullptr; void* user_data=nullptr;
    void (*_tc_before_turn)()=nullptr; void (*_tc_after_turn)()=nullptr; bool (*_tc_callbacks_active)()=nullptr;
    void (*_tc_event_cb)(const sapp_event*,sapp_window,void*)=nullptr;
    void (*event_cb)(const sapp_event*)=nullptr; void (*event_userdata_cb)(const sapp_event*,void*)=nullptr;
};
struct _sapp_tc_window_t {
    unsigned win_id=717; bool ready=true,in_tick=false,is_main=true,cancel_tick=false;
    bool close_requested=false,input_delivered=false,wake_on_input=true,credit_held=false,waitable_ready=true;
    int drawable=1,swap_chain=1; void* frame_wait=nullptr; double earliest_next=0;
    uint64_t frame_count=0; int win_width=100,win_height=50,fb_width=200,fb_height=100;
    void* hwnd=nullptr; bool (*events_enabled)()=nullptr; sapp_window_desc desc{};
} record;
struct { int display=0; size_t window_slots=1; _sapp_tc_window_t* windows[1]={&record};
    struct { bool init_called=false,quit_requested=false,quit_ordered=false;
        unsigned host_loop_depth=1; bool host_turn_open=false;
        Descriptor desc; _sapp_tc_window_t* main=&record; } app; } _sapp_tc;
_sapp_tc_window_t* _sapp_tc_lookup(sapp_window h) { return h.id==record.win_id ? &record : nullptr; }
std::function<void()> initializeHost;
int drained=0,initCount=0,ticks=0;
bool queued=true;
double _sapp_tc_now() { return 1; }
void _sapp_tc_d3d11_dispatch_pending_device_loss() {}
''' + predicate + '\n' + send + r'''
void emit(sapp_event_type type) {
    sapp_event event{}; event.type=type; event.key_code=SAPP_KEYCODE_B; event.char_code='b';
    _sapp_tc_send(&record,&event);
}
void initialize() {
    ++initCount;
    check("first native drain precedes init", drained==1);
    check("native init flag is true throughout desktop init",_sapp_tc.app.init_called);
    check("init retains the native busy guard",record.in_tick);
    internal::mainWindowBootstrap([] { initializeHost(); });
}
''' + init + '\n'
        if port == 'x11':
            loop = body(native, 'static void _sapp_tc_x11_run_loop(').replace('poll(&pfd', 'fake_poll(&pfd')
            if early == platform:
                loop = loop.replace('    XFlush(_sapp_tc.display);', '    XFlush(_sapp_tc.display);\n    _sapp_tc_x11_init_client();', 1)
            # Execute the production callback boundary after stubbed drawing prerequisites.
            tick = body(native, 'static void _sapp_tc_x11_tick(')
            start = tick.rindex('    if (w->is_main) _sapp_tc_x11_init_client();')
            end = tick.index('\n    ++w->frame_count;', start)
            callback = tick[start:end]
            code += r'''
void XFlush(int) {} int XPending(int) { return queued?1:0; } int ConnectionNumber(int) { return -1; }
int fake_poll(pollfd*,unsigned long,int) { return 0; }
struct XEvent {};
void XNextEvent(int,XEvent*) { queued=false; }
void _sapp_tc_x11_process_event(XEvent*) { ++drained; emit(SAPP_EVENTTYPE_KEY_DOWN); }
void _sapp_tc_x11_wait_until(double) {}
void _sapp_tc_x11_close(_sapp_tc_window_t*) {}
void _sapp_tc_x11_app_event(_sapp_tc_window_t*,sapp_event_type type) { emit(type); }
void _sapp_tc_x11_tick(_sapp_tc_window_t* w) {
    const sapp_window handle{w->win_id}; w->in_tick=true;
''' + callback + r'''
    ++ticks; w->in_tick=false; _sapp_tc.app.quit_ordered=true;
}
''' + body(native, 'static bool _sapp_tc_x11_window_due(') + '\n' + loop
            run = '_sapp_tc_x11_run_loop();'
        elif port == 'win32':
            loop = body(native, 'static void _sapp_tc_win32_run_loop(')
            if early == platform:
                loop = loop.replace('    bool done = false;', '    bool done = false;\n    _sapp_tc_win32_init_client();', 1)
            tick = body(native, 'static void _sapp_tc_win32_tick(')
            start = tick.index('    if (w->is_main) _sapp_tc_win32_init_client();')
            end = tick.index('\n    if (!(w = _sapp_tc_lookup(tick_handle)) || w->cancel_tick) return;', start + 100)
            callback = tick[start:end]
            query = native[native.index('        case WM_QUERYENDSESSION:'):native.index('        case WM_ENDSESSION:')]
            code += r'''
using HANDLE=void*; using DWORD=unsigned; using LONGLONG=long long;
struct LARGE_INTEGER { long long QuadPart; };
constexpr int CREATE_WAITABLE_TIMER_HIGH_RESOLUTION=2,TIMER_MODIFY_STATE=1,SYNCHRONIZE=2;
constexpr int QS_ALLINPUT=1,MWMO_INPUTAVAILABLE=2,WAIT_OBJECT_0=0,PM_REMOVE=1,WM_QUIT=2,WM_CLOSE=3;
constexpr int _SAPP_TC_MAX_WINDOWS=1,TRUE=1,FALSE=0,CP_UTF8=65001,WM_QUERYENDSESSION=4;
HANDLE CreateWaitableTimerExW(...) { return nullptr; } bool SetWaitableTimer(...) { return false; }
void CloseHandle(...) {} void PostMessageW(...) {}
DWORD MsgWaitForMultipleObjectsEx(...) { return 3; }
struct MSG { int message=0; };
bool PeekMessageW(MSG*,void*,int,int,int) { if (!queued) return false; queued=false;return true; }
void TranslateMessage(MSG*) {} void DispatchMessageW(MSG*) { ++drained; emit(SAPP_EVENTTYPE_KEY_DOWN); }
void ShutdownBlockReasonDestroy(...) {} void ShutdownBlockReasonCreate(...) {}
int MultiByteToWideChar(...) { return 0; }
void _sapp_tc_win32_app_event(_sapp_tc_window_t*,sapp_event_type type) { emit(type); }
''' + body(native, 'static bool _sapp_tc_win32_request_exit(') + r'''
int querySession() {
    auto* w=&record; void* hwnd=nullptr;
    switch (WM_QUERYENDSESSION) {
''' + query + r'''
    } return -1;
}
bool _sapp_tc_win32_window_due(_sapp_tc_window_t* w) { return w && !w->in_tick; }
void _sapp_tc_win32_tick(_sapp_tc_window_t* w) {
    const sapp_window tick_handle{w->win_id}; w->in_tick=true;
''' + callback + r'''
    ++ticks; w->in_tick=false; _sapp_tc.app.quit_ordered=true;
}
''' + loop
            run = '_sapp_tc_win32_run_loop();'
        else:
            observer = body(native, 'static void _sapp_tc_mac_host_turn(')
            if early == platform:
                observer = observer.replace('    if (activity == kCFRunLoopBeforeSources) {', '    if (activity == kCFRunLoopBeforeSources) {\n        _sapp_tc_mac_init_client();')
            tick = body(native, 'static void _sapp_tc_mac_tick(')
            start = tick.index('        w->in_tick = true;')
            end = tick.index('\n        w = _sapp_tc_lookup(handle);', start)
            code += r'''
using CFRunLoopObserverRef=void*;using CFRunLoopActivity=unsigned;
constexpr int kCFRunLoopEntry=1,kCFRunLoopExit=2,kCFRunLoopBeforeSources=4;
''' + observer + r'''
void tick() {
    auto* w=&record; const sapp_window handle{w->win_id}; bool admitted=true;
''' + tick[start:end].replace('        w->draw_admitted = admitted;', '        (void)admitted;') + r'''
    ++ticks; w->in_tick=false;
}
'''
            run = '''_sapp_tc_mac_host_turn(nullptr,kCFRunLoopBeforeSources,nullptr);
    ++drained; emit(SAPP_EVENTTYPE_KEY_DOWN); tick();'''
        code += r'''
void run(std::function<void()> host) {
    initializeHost=host; _sapp_tc.app.desc.init_cb=initialize;
    _sapp_tc.app.desc._tc_event_cb=internal::mainWindowEvent;
    record.desc.event_cb=internal::mainWindowEvent; record.desc.user_data=internal::mainWindow();
    record.events_enabled=_sapp_tc_app_events_enabled;
    nativeSend=emit;
''' + (r'''
    cancelNativeQuit=[] { _sapp_tc.app.quit_requested=false; };
    queryEndSession=[] { return querySession()==FALSE; };
''' if port == 'win32' else '    queryEndSession={};\n') + r'''
    emit(SAPP_EVENTTYPE_FOCUSED); // synchronous construction notification
''' + run + r'''
    check("native startup initializes and ticks once",initCount==1 && ticks==1);
}
'''
        parts.append('#pragma push_macro("SOKOL_GLCORE")\n#pragma push_macro("SOKOL_GLES3")\n#undef SOKOL_GLCORE\n#undef SOKOL_GLES3\n#define ' + backend + '\nnamespace admission_' + platform + ' {\n' + code + '\n}\n#pragma pop_macro("SOKOL_GLES3")\n#pragma pop_macro("SOKOL_GLCORE")\n')
    return ''.join(parts)


def mac_init_exit_probe(native):
    """Execute production admission/replay with AppKit messaging stubbed in C++."""
    mac = native[:native.index('static BOOL _sapp_tc_request_exit(')]
    deferred = body(native, 'static void _sapp_tc_mac_process_deferred(')
    # Replace only Objective-C message syntax; retain the production decisions.
    deferred = deferred.replace('[nsw close]', 'nsw->close()').replace(
        '[first->window performClose:nil]', 'first->window->performClose()')
    tick = body(native, 'static void _sapp_tc_mac_tick(').replace('@autoreleasepool', '').replace('link.', 'link->')
    return r'''
#include <cstdint>
#include <cstdio>
#include <cstring>
using BOOL=bool;
constexpr bool YES=true,NO=false;
constexpr int _SAPP_TC_MAX_WINDOWS=1,SAPP_EVENTTYPE_QUIT_REQUESTED=1,SAPP_EVENTTYPE_RESIZED=2;
struct sapp_window { uint32_t id; };
struct sapp_event { int type=0; uint64_t frame_count=0;
    int window_width=0,window_height=0,framebuffer_width=0,framebuffer_height=0; };
struct NSWindow { void close(); void performClose(); } nativeWindow;
struct CADisplayLink { double timestamp=1,targetTimestamp=2; };
struct _sapp_tc_window_t {
    unsigned win_id=717; bool ready=true,in_tick=false,is_main=true,close_requested=false;
    bool draw_while_hidden=true,draw_admitted=false,wake_on_input=false,input_delivered=false;
    int timing=0,win_width=100,win_height=50,fb_width=200,fb_height=100;
    double last_tick_time=0,frame_duration=0; uint64_t frame_count=0;
    NSWindow* window=&nativeWindow; bool (*events_enabled)()=nullptr;
    struct { void (*event_cb)(const sapp_event*,sapp_window,void*)=nullptr;
        void (*tick_cb)(sapp_window,void*)=nullptr; void* user_data=nullptr; } desc;
} record;
struct { _sapp_tc_window_t* windows[1]={&record};
    struct { bool init_called=false,quit_requested=false,quit_ordered=false;
        _sapp_tc_window_t* main=&record;
        struct { void (*init_cb)()=nullptr; void (*init_userdata_cb)(void*)=nullptr;
            void* user_data=nullptr; bool (*_tc_callbacks_active)()=nullptr;
            void (*_tc_event_cb)(const sapp_event*,sapp_window,void*)=nullptr;
            void (*event_cb)(const sapp_event*)=nullptr;
            void (*event_userdata_cb)(const sapp_event*,void*)=nullptr; } desc;
    } app;
} _sapp_tc;
_sapp_tc_window_t* _sapp_tc_lookup(sapp_window h) { return h.id==record.win_id?&record:nullptr; }
namespace trussc::internal { void setExitReasonIfEmpty(const char*) {} }
int checks=0,failures=0,delivered=0,replayed=0,closed=0;
bool setupReturned=false,callbacksActive=false,requestClose=false,veto=false;
void check(const char* message,bool ok) {
    ++checks; if (!ok) { ++failures; std::printf("macOS intended exception: FAIL: %s\n",message); }
}
''' + body(mac, 'static bool _sapp_tc_app_events_enabled(') + '\n' + body(
        mac, 'static void _sapp_tc_send(') + '\n' + body(
        native, 'static BOOL _sapp_tc_request_exit(') + '\n' + body(
        native, 'static BOOL _sapp_tc_mac_close(') + '\n' + deferred + '\n' + body(
        native, 'static void _sapp_tc_mac_init_client(') + r'''
void _sapp_tc_reconcile_fullscreen(_sapp_tc_window_t*) {}
void _sapp_tc_timing_update(int*) {}
bool _sapp_tc_native_hidden(_sapp_tc_window_t*) { return false; }
bool _sapp_tc_mac_update_dimensions(_sapp_tc_window_t*) { return false; }
void _sapp_tc_send_type(_sapp_tc_window_t*,int) {}
void _sapp_tc_mac_swap(_sapp_tc_window_t*) {}
''' + tick + r'''
void NSWindow::close() { ++closed; _sapp_tc.app.main=nullptr; record.window=nullptr; }
void NSWindow::performClose() {
    ++replayed;
    // AppKit's performClose: asks windowShouldClose: before closing.
    if (_sapp_tc_mac_close(&record)) close();
}
void listener(const sapp_event* e,sapp_window,void*) {
    check("only QUIT_REQUESTED is delivered",e->type==SAPP_EVENTTYPE_QUIT_REQUESTED);
    ++delivered;
    check("QUIT_REQUESTED waits until setup returns",setupReturned);
    if (veto) _sapp_tc.app.quit_requested=false;
}
void setup() {
    callbacksActive=true;
    check("init is an active callback under the approved busy rule",record.in_tick);
    // NO is NSTerminateCancel at applicationShouldTerminate:, or the refusal
    // returned by windowShouldClose:. Both production paths must defer.
    const BOOL answer=requestClose?_sapp_tc_mac_close(&record):_sapp_tc_request_exit();
    check("Quit/close during init returns cancel",answer==NO);
    check("init Quit/close is retained for replay",requestClose?record.close_requested:_sapp_tc.app.quit_requested);
    check("no QUIT_REQUESTED during setup",delivered==0);
    _sapp_tc_mac_process_deferred(); // nested AppKit loop while setup is active
    check("nested loop cannot replay during setup",delivered==0 && replayed==0 && closed==0);
    callbacksActive=false;
    setupReturned=true;
}
int main() {
    for (bool closeRequest : {false,true}) for (bool cancel : {false,true}) {
        record={}; _sapp_tc.app={};
        delivered=replayed=closed=0; setupReturned=callbacksActive=false;
        requestClose=closeRequest; veto=cancel;
        record.events_enabled=_sapp_tc_app_events_enabled; record.desc.event_cb=listener;
        _sapp_tc.app.desc._tc_event_cb=listener;
        _sapp_tc.app.desc.init_cb=setup;
        _sapp_tc.app.desc._tc_callbacks_active=[] { return callbacksActive; };
        // Production tick supplies the init guard, unwinds it and drains replay.
        _sapp_tc_mac_tick(&record,nullptr);
        check("Quit/close delivered exactly once after setup",setupReturned && delivered==1);
        check("application Quit is replayed once through performClose",requestClose || replayed==1);
        check("close intent is consumed after setup",!record.close_requested && !record.in_tick);
        check("replayed request preserves the existing veto",_sapp_tc.app.quit_ordered==!veto && closed==int(!veto));
        _sapp_tc_mac_process_deferred();
        check("another drain cannot duplicate QUIT_REQUESTED",delivered==1 && closed==int(!veto) && (requestClose || replayed==1));
    }
    std::printf("macOS init admission: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
'''


def main():
    import os
    import subprocess
    import tempfile
    native = (Path(__file__).resolve().parents[3] / 'core/include/sokol/sokol_app_tc.h').read_text(encoding='utf-8')
    source = mac_init_exit_probe(native)
    variants = [('baseline', source)]
    request = body(source, 'static BOOL _sapp_tc_request_exit(')
    immediate_request = request[:request.index('{') + 1] + '\n' + request[request.index('    _sapp_tc_window_t* w ='):]
    close = body(source, 'static BOOL _sapp_tc_mac_close(')
    immediate_close = close[:close.index('{') + 1] + '\n' + close[close.index('    if (w->is_main)'):]
    variants += [
        ('quit_immediate_during_init', source.replace(request, immediate_request)),
        ('close_immediate_during_init', source.replace(request, immediate_request).replace(close, immediate_close)),
        ('quit_drop_replay', source.replace('first->window->performClose();', '/* mutation: drop Quit replay */')),
        ('close_drop_replay', source.replace('if (nsw && _sapp_tc_mac_close(cw)) nsw->close();', '(void)nsw; /* mutation: drop close replay */')),
    ]
    with tempfile.TemporaryDirectory(prefix='mac-init-admission-') as scratch:
        for name, code in variants:
            if name != 'baseline' and code == source:
                raise SystemExit(f'{name}: mutation marker missing')
            path = Path(scratch) / (name + '.cpp')
            binary = Path(scratch) / name
            path.write_text('#include <initializer_list>\n' + code, encoding='utf-8')
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', str(path), '-o', str(binary)], check=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True, encoding='utf-8', timeout=10)
            failed = result.stdout.count('macOS intended exception: FAIL:')
            valid = result.returncode == (0 if name == 'baseline' else 1) and (failed == 0 if name == 'baseline' else failed > 0)
            print(f'{name}: exit={result.returncode}, intended-exception failures={failed}', flush=True)
            if name == 'baseline' or not valid:
                print(result.stdout, end='')
            if not valid:
                raise SystemExit(f'{name}: unexpected result\n{result.stderr}')


if __name__ == '__main__':
    main()
