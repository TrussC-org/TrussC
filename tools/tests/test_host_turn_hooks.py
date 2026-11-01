#!/usr/bin/env python3
"""Execute native event guards, X11 turns and AppKit turn arbitration with stubs.
No native SDK/device claim. Optional mutations must compile and fail assertions.
"""
from pathlib import Path
import json, os, re, subprocess, tempfile
from test_web_window_controls import body
ROOT=Path(__file__).resolve().parents[2]
NATIVE=(ROOT/'core/include/sokol/sokol_app_tc.h').read_text(encoding="utf-8")
def extract(source, marker): return body(source,marker)
for adapter in ('linux/tcWindowLinux.cpp','win/tcWindowWin.cpp','mac/tcWindowMac.mm'):
    text=(ROOT/'core/platform'/adapter).read_text(encoding="utf-8")
    assert 'd.tick_cb = &internal::windowTick;' in text, adapter
    assert 'runWindowTick(' not in text and 'windowTickImpl(' not in text, adapter
parts=[]
for index,match in enumerate(re.finditer(r'static (?:void|_sapp_tc_event_result) _sapp_tc_send\(_sapp_tc_window_t\* w, sapp_event\* ev\) \{',NATIVE)):
    implementation=body(NATIVE[match.start():],match.group())
    parts.append(f'namespace event_{index} {{\n'+r'''
struct _sapp_tc_window_t {
    uint32_t win_id=23; uint64_t frame_count=7; float dpi_scale=2;
    int win_width=80,win_height=40,fb_width=160,fb_height=80;
    bool ready=true,in_tick=false,wake_on_input=true,input_delivered=false,native_closed=false; int callback_depth=0;
    bool (*events_enabled)()=nullptr;
    struct { void (*event_cb)(const sapp_event*,sapp_window,void*)=nullptr; void* user_data=nullptr; } desc;
};
struct { int callbacks_active=0; bool deferred_blocked=false; bool mouse_locked=false; _sapp_tc_window_t* lock_owner=nullptr; struct { _sapp_tc_window_t* main=nullptr; } app; } _sapp_tc;
_sapp_tc_window_t* live=nullptr;
_sapp_tc_window_t* _sapp_tc_lookup(sapp_window h) { return live && live->win_id==h.id ? live : nullptr; }
''' + ('''
void _sapp_tc_mac_contained(const char*) {}
void _sapp_tc_mac_schedule_deferred() {}
''' + extract(NATIVE,'struct _sapp_tc_mac_callback_scope {')+';\n' if index==0 else '')+implementation+r'''
void callback(const sapp_event* e,sapp_window h,void* p) {
    auto& w=*static_cast<_sapp_tc_window_t*>(p);
    check("native event callback excludes its own native tick", w.in_tick);
    check("event metadata and receiver survive common binding", h.id==23 && e->framebuffer_width==160 && e->frame_count==7);
}
void run() {
    _sapp_tc_window_t w, first; first.win_id=24; live=&w; _sapp_tc.app.main=&first; w.frame_count=99; w.desc={callback,&w};
    for (bool outer : {false,true}) {
        w.in_tick=outer;
        sapp_event e{}; e.type=SAPP_EVENTTYPE_KEY_DOWN;
        _sapp_tc_send(&w,&e);
        check("event guard restores outer native callback state",w.in_tick==outer);
    }
    live=nullptr;
}
}
''')
observer=extract(NATIVE,'static void _sapp_tc_mac_host_turn(')
launch=extract(NATIVE,'- (void)applicationDidFinishLaunching:')
depth_init=launch[launch.index('    CFStringRef current_mode ='):launch.index('    _sapp_tc.app.host_turn_observer =')]
mac=r'''
namespace mac {
using CFRunLoopObserverRef=void*; using CFRunLoopActivity=unsigned;
using CFStringRef=void*;
bool loopRunning=false;
int modeReleases=0;
void* CFRunLoopGetMain() { return nullptr; }
CFStringRef CFRunLoopCopyCurrentMode(void*) { return loopRunning ? &loopRunning : nullptr; }
void CFRelease(CFStringRef mode) { check("AppKit releases the sampled running mode",mode==&loopRunning); ++modeReleases; }
enum { kCFRunLoopEntry=1,kCFRunLoopExit=2,kCFRunLoopBeforeSources=4,kCFRunLoopBeforeWaiting=8 };
struct _sapp_tc_window_t { unsigned win_id=1; int callback_depth=0; bool in_tick=false; };
constexpr int _SAPP_TC_MAX_WINDOWS=1;
struct { int callbacks_active=0,constructing=0; bool deferred_blocked=false; _sapp_tc_window_t* windows[1]{};
    struct { _sapp_tc_window_t* main=nullptr; unsigned host_loop_depth=1; bool host_turn_open=false;
    struct { void (*_tc_before_turn)()=nullptr; void (*_tc_after_turn)()=nullptr; } desc; } app; } _sapp_tc;
void _sapp_tc_mac_init_client() {}
_sapp_tc_window_t* _sapp_tc_lookup(sapp_window) { return nullptr; }
void _sapp_tc_mac_schedule_deferred() {}
int before=0,after=0;
void begin() { ++before; check("AppKit process callbacks share native busy scope",_sapp_tc.callbacks_active>0); } void end() { ++after; check("AppKit end callbacks share native busy scope",_sapp_tc.callbacks_active>0); }
'''+extract(NATIVE,'struct _sapp_tc_mac_callback_scope {')+';\n'+observer+r'''
void observe(unsigned activity) { _sapp_tc_mac_host_turn(nullptr,activity,nullptr); }
void installDepth() {
'''+depth_init+r'''
}
void run() {
    _sapp_tc.app.desc={begin,end};
    // CLI finishLaunching can install the observer before NSApplication starts
    // its first loop. Compile the production initialization, then deliver the
    // real Entry/Sources/Waiting/Exit ordering without a window tick.
    installDepth();
    observe(kCFRunLoopEntry); observe(kCFRunLoopBeforeSources);
    check("AppKit launch before the first loop services its first outer turn",before==1 && after==0);
    observe(kCFRunLoopBeforeWaiting); observe(kCFRunLoopExit);
    check("AppKit first loop returns to depth zero",after==1 && _sapp_tc.app.host_loop_depth==0 && modeReleases==0);
    before=after=0; loopRunning=true; installDepth();
    check("AppKit installation inside a loop retains its current depth",_sapp_tc.app.host_loop_depth==1 && modeReleases==1);
    observe(kCFRunLoopBeforeSources);
    check("AppKit starts one turn without a window draw",before==1 && after==0);
    observe(kCFRunLoopEntry); observe(kCFRunLoopBeforeSources); observe(kCFRunLoopBeforeWaiting);
    observe(kCFRunLoopBeforeSources); observe(kCFRunLoopExit);
    check("nested AppKit loops leave outer turn serialized",before==1 && after==0 && _sapp_tc.app.host_turn_open);
    observe(kCFRunLoopBeforeSources);
    check("non-waiting AppKit iteration closes preceding turn",before==2 && after==1);
    observe(kCFRunLoopBeforeWaiting); observe(kCFRunLoopExit);
    check("AppKit waiting and exit end outer turn only once",before==2 && after==2 && !_sapp_tc.app.host_turn_open);
}
}
'''
x11=r'''
namespace x11 {
struct sapp_window { unsigned id; };
struct _sapp_tc_window_t { uint32_t win_id=27; bool constructing=false,surface_resize_pending=false,ready=true, in_tick=false, close_requested=false,input_delivered=false; int drawable=1,swap_interval=1; double earliest_next=100,last_present=0; struct {double last=0;}timing; };
struct Descriptor { void (*init_cb)()=nullptr; void (*init_userdata_cb)(void*)=nullptr; void* user_data=nullptr;
    void (*_tc_before_turn)()=nullptr; void (*_tc_after_turn)()=nullptr; bool (*_tc_callbacks_active)()=nullptr;
    void (*_tc_event_cb)(const sapp_event*,sapp_window,void*)=nullptr; };
_sapp_tc_window_t window;
struct { int display=0; double refresh_period=1.0/60.0; size_t window_slots=1; _sapp_tc_window_t* windows[1]={&window};
    struct { bool init_called=false,quit_ordered=false,quit_requested=false; Descriptor desc; _sapp_tc_window_t* main=&window; } app; } _sapp_tc;
_sapp_tc_window_t* _sapp_tc_lookup(sapp_window h) { return h.id==window.win_id ? &window : nullptr; }
int before=0,after=0,init=0,ticks=0,clockCalls=0;
void initialize() { ++init; check("native bootstrap excludes its own tick", window.in_tick); }
void begin() { ++before; }
void end() { if (++after==3) _sapp_tc.app.quit_ordered=true; }
double _sapp_tc_now() {
    // Bound (probe termination): a missing after-turn hook still returns a runtime assertion result.
    if (++clockCalls>8) _sapp_tc.app.quit_ordered=true;
    return clockCalls*.01;
}
void XFlush(int) {} int XPending(int) { return 0; } int ConnectionNumber(int) { return -1; }
int fake_poll(pollfd*,unsigned long,int) { return 0; }
struct XEvent {};
void XNextEvent(int,XEvent*) {} void _sapp_tc_x11_process_event(XEvent*) {}
void _sapp_tc_x11_wait_until(double) {}
void _sapp_tc_x11_tick(_sapp_tc_window_t*) { ++ticks; }
bool _sapp_tc_native_hidden(_sapp_tc_window_t*) { return false; }
bool _sapp_tc_native_iconified(_sapp_tc_window_t*) { return false; }
void _sapp_tc_x11_close(_sapp_tc_window_t*) {}
void _sapp_tc_x11_app_event(_sapp_tc_window_t*,int) {}
'''+extract(NATIVE,'static void _sapp_tc_x11_request_exit(')+'\n'+extract(NATIVE,'static bool _sapp_tc_x11_window_due(')+'\n'+extract(NATIVE,'static void _sapp_tc_x11_run_loop(').replace('poll(&pfd','fake_poll(&pfd')+r'''
void run() {
    _sapp_tc.app.desc={initialize,nullptr,nullptr,begin,end};
    _sapp_tc_x11_run_loop();
    check("X11 idle turns do not initialize before the first due tick",init==0);
    check("X11 services once per native turn with no due window",before==3 && after==3 && ticks==0);
}
}
'''
# Native RAII scopes and Runtime activity both exclude nested client entry.
assert '_tc_callbacks_active' in extract(NATIVE, 'static void _sapp_tc_x11_close(')
assert '._tc_callbacks_active()' in extract(NATIVE, 'static bool _sapp_tc_mac_busy(')
quit_gate=r'''
namespace quit_gate {
using NSApplication=void; using NSApplicationTerminateReply=int; using AEEventID=unsigned;
constexpr int NSTerminateCancel=0,NSTerminateNow=1,_SAPP_TC_MAX_WINDOWS=1;
struct _sapp_tc_window_t { unsigned win_id=3; bool in_tick=false; int callback_depth=0; } window;
struct { int callbacks_active=0,constructing=0; bool deferred_blocked=false,terminate_pending=false;
    _sapp_tc_window_t* windows[1]={&window};
    struct { bool quit_requested=false,quit_ordered=false; struct { bool (*_tc_callbacks_active)()=nullptr; } desc; } app; } _sapp_tc;
_sapp_tc_window_t* _sapp_tc_lookup(sapp_window h) { return _sapp_tc.windows[0] && h.id==window.win_id?&window:nullptr; }
void _sapp_tc_mac_schedule_deferred() {}
AEEventID _sapp_tc_mac_quit_reason() { return 0; }
bool _sapp_tc_mac_is_session_end(AEEventID) { return false; }
void _sapp_tc_mac_session_end(AEEventID) {}
int endings=0;
bool runtimeBusy=false;
namespace trussc::internal {
void setExitReason(const char*) {} void setExitReasonIfEmpty(const char*) {}
bool querySessionEnd() { return true; }
void cancelSessionEnd() {}
void shutdownRuntime() { ++endings; }
}
int requests=0;
bool _sapp_tc_request_exit() { ++requests; return true; }
'''+extract(NATIVE,'struct _sapp_tc_mac_callback_scope {')+';\n'+extract(NATIVE,'static bool _sapp_tc_mac_busy(')+extract(NATIVE,'- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender').replace(
    '- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender','int should_terminate(void* sender)')+r'''
void run() {
    _sapp_tc.app.desc._tc_callbacks_active=[] { return runtimeBusy; };
    {
        _sapp_tc_mac_callback_scope host(nullptr); // process callback after native unbind
        _sapp_tc.windows[0]=nullptr;
        check("AppKit quit defers for host ending callbacks after native unbind",should_terminate(nullptr)==NSTerminateCancel && requests==0 && _sapp_tc.terminate_pending);
    }
    runtimeBusy=true; _sapp_tc.terminate_pending=false;
    check("AppKit quit defers for Runtime callback activity without native windows",should_terminate(nullptr)==NSTerminateCancel && requests==0 && _sapp_tc.terminate_pending && endings==0);
    runtimeBusy=false;
    _sapp_tc.windows[0]=&window; window.in_tick=true;
    check("AppKit quit defers for native tick activity",should_terminate(nullptr)==NSTerminateCancel && requests==0);
    window.in_tick=false;
    check("AppKit quit reaches the existing endpoint at the next safe boundary",should_terminate(nullptr)==NSTerminateNow && requests==1 && endings==1);
}
}
'''
modal=r'''
namespace modal {
struct sapp_window { unsigned id; };
struct _sapp_tc_window_t { unsigned win_id=39; bool in_tick=false,from_modal=false; } window;
_sapp_tc_window_t* live=&window;
_sapp_tc_window_t* _sapp_tc_lookup(sapp_window h) { return live && live->win_id==h.id ? live : nullptr; }
int before=0,after=0,ticks=0;
bool release=false;
void begin() { ++before; if (release) live=nullptr; }
void end() { ++after; }
bool busy=false;
bool callbacksActive() { return busy; }
struct { struct { struct { void (*_tc_before_turn)()=begin; void (*_tc_after_turn)()=end;
    bool (*_tc_callbacks_active)()=callbacksActive; } desc; } app; } _sapp_tc;
void _sapp_tc_win32_tick(_sapp_tc_window_t* receiver) {
    ++ticks;
    check("modal source revalidates receiver after process queue callbacks",receiver==live);
}
'''+extract(NATIVE,'static void _sapp_tc_win32_modal_tick(')+r'''
void run() {
    busy=true;
    _sapp_tc_win32_modal_tick(&window);
    check("modal source defers native work during a foreign framework callback",before==0 && after==0 && ticks==0 && !window.from_modal);
    busy=false;
    _sapp_tc_win32_modal_tick(&window);
    check("modal source brackets live tick and restores its source flag",before==1 && after==1 && ticks==1 && !window.from_modal);
    release=true;
    // Keep storage so omission of revalidation fails assertions without a dangling access.
    _sapp_tc_win32_modal_tick(&window);
    check("modal queue may release receiver without a stale tick",before==2 && after==2 && ticks==1 && !live);
}
}
'''
# Windows loop has normal and early-exit end hooks; modal source brackets a nested turn.
for marker in ('static void _sapp_tc_win32_run_loop(', 'static void _sapp_tc_win32_modal_tick('):
    implementation=extract(NATIVE,marker)
    assert '._tc_before_turn()' in implementation and '._tc_after_turn()' in implementation, marker
prefix=r'''
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <initializer_list>
#include <poll.h>
int failures=0,checks=0;
void check(const char* label,bool ok) { ++checks; if (!ok) { ++failures; printf("host-turn: FAIL: %s\n",label); } }
'''
prefix+='#include "sokol_app_tc.h"\n'
prefix+=(ROOT/'core/include/sokol/util/sokol_window_event.h').read_text(encoding='utf-8').replace('#pragma once','')+'\n'
source=prefix+''.join(parts)+mac+x11+quit_gate+modal+r'''
int main() { event_0::run();event_1::run();event_2::run();mac::run();x11::run();quit_gate::run();modal::run();
    printf("host-turn: %d checks, %d failures\n",checks,failures);return failures?1:0; }
'''
variants=[('baseline',source)]
if os.environ.get('TC_HOST_TURN_MUTATIONS'):
    old='_sapp_tc.app.host_loop_depth = current_mode ? 1 : 0;'
    assert old in source
    variants.append(('appkit_launch_assumes_running_loop',source.replace(old,'_sapp_tc.app.host_loop_depth = 1;')))
    old='    w->in_tick = true;'
    assert source.count(old)==1
    variants.append(('common_event_guard',source.replace(old,'    /* mutation: event callback unguarded */')))
    old='if (auto* live = _sapp_tc_lookup(source.handle)) _sapp_tc_win32_tick(live);'
    assert old in modal
    variants.append(('modal_uses_stale_receiver',source.replace(modal,modal.replace(old,'_sapp_tc_win32_tick(w);'))))
    quit_condition='if (_sapp_tc_mac_busy())'
    assert quit_condition in quit_gate
    variants.append(('native_quit_ignores_host_callbacks',source.replace(quit_gate,quit_gate.replace(quit_condition,'if (false)'))))
    old='    if (_sapp_tc.app.host_loop_depth > 1) return;'
    assert old in source
    variants.append(('nested_appkit_turn',source.replace(old,'')))
    old='if (_sapp_tc.app.desc._tc_before_turn) _sapp_tc.app.desc._tc_before_turn();'
    assert old in x11
    end_hook='if (_sapp_tc.app.desc._tc_after_turn) _sapp_tc.app.desc._tc_after_turn();'
    assert end_hook in x11
    variants.append(('x11_no_end_services',source.replace(x11,x11.replace(end_hook,'/* mutation: omitted turn end */'))))
    variants.append(('x11_no_draw_services',source.replace(x11,x11.replace(old,'/* mutation: omitted turn service */'))))
results=[]
with tempfile.TemporaryDirectory(prefix='tc-host-turn-') as folder:
    for name,code in variants:
        for backend in ('SOKOL_GLCORE','SOKOL_GLES3'):
            cpp=Path(folder)/'probe.cpp'; binary=Path(folder)/'probe'; cpp.write_text(code,encoding="utf-8")
            subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-I'+str(ROOT/'core/include/sokol'),'-D'+backend,str(cpp),'-o',str(binary)],check=True)
            result=subprocess.run([str(binary)],text=True,encoding="utf-8",stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=15)
            print(name,backend,result.stdout,flush=True)
            failed=result.stdout.count('host-turn: FAIL:')
            valid=(result.returncode==0 and not failed) if name=='baseline' else (result.returncode==1 and failed>0)
            results.append({'name':name,'backend':backend,'returncode':result.returncode,'failed_assertions':failed,'valid':valid})
if output:=os.environ.get('TC_HOST_TURN_OUTPUT'):
    Path(output).write_text(json.dumps(results,indent=2),encoding="utf-8")
raise SystemExit(any(not x['valid'] for x in results))
