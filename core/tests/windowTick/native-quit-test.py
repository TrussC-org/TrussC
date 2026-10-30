#!/usr/bin/env python3
"""SDK-free desktop exit extraction. Run via tcbuild.sh (also in native-gates-test).
Delivery, native send, quit decision, close/session branches and exception
boundary are production bodies. Only Win32 APIs and unrelated messages are seams.
"""
from pathlib import Path
import json
import os
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT/'tools/tests'))
from test_web_window_controls import body

include = Path(os.environ.get('TC_NATIVE_GATES_INCLUDE', ROOT/'core/include/sokol'))
native = (include/'sokol_app_tc.h').read_text(encoding='utf-8')
shared = (include/'util/sokol_window_event.h').read_text(encoding='utf-8').replace('#pragma once', '')
start = native.index('static uint32_t _sapp_tc_win32_mods(')
send = re.search(r'static (?:void|_sapp_tc_event_result) _sapp_tc_send\(', native[start:])
assert send
send_start = start + send.start()
app_event = re.search(r'static (?:void|_sapp_tc_event_result) _sapp_tc_win32_app_event\(', native)
assert app_event
parts = [body(native[send_start:], send.group()), body(native[app_event.start():], app_event.group())]
win = native[send_start:]
parts += [body(win[:win.index('/*-- mouse cursor')], marker) for marker in (
    'static bool _sapp_tc_app_events_enabled(', 'static void _sapp_tc_main_event_tramp(')]
parts += [body(native, marker) for marker in (
    'static bool _sapp_tc_win32_request_exit(', 'static void _sapp_tc_win32_cleanup(',
    'static void _sapp_tc_win32_close(')]
parts += ['using BOOL = int; constexpr int YES=1, NO=0;',
          body(native, 'static BOOL _sapp_tc_request_exit('),
          'static void _sapp_tc_x11_app_event(_sapp_tc_window_t* w, sapp_event_type type) { _sapp_tc_win32_app_event(w, type); }',
          body(native, 'static void _sapp_tc_x11_request_exit(')]
proc = body(native, 'static LRESULT _sapp_tc_wndproc_impl(')
# Keep complete production cases, including else branches and all returns.
branches = proc[proc.index('        case WM_QUERYENDSESSION:'):proc.index('        case WM_SYSCOMMAND:')]
code = r'''
#include "sokol_app_tc.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <stdexcept>
#include <initializer_list>
'''+shared+r'''
using HWND=int; using UINT=unsigned; using WPARAM=unsigned long; using LPARAM=long;
using LRESULT=long;
#define CALLBACK
constexpr int TRUE=1,FALSE=0,CP_UTF8=65001;
enum { WM_QUERYENDSESSION=1,WM_ENDSESSION,WM_CLOSE,WM_OTHER };
struct _sapp_tc_window_t {
    unsigned win_id=17; bool constructing=false,ready=true,in_tick=false,is_main=true,close_requested=false;
    bool wake_on_input=false,input_delivered=false; bool (*events_enabled)()=nullptr;
    int win_width=80,win_height=40,fb_width=160,fb_height=80,hwnd=17;
    float dpi_scale=2; uint64_t frame_count=7; sapp_window_desc desc{};
} window;
struct App {
    bool quit_ordered=false,quit_requested=false,init_called=true,cleanup_called=false,event_consumed=false;
    _sapp_tc_window_t* main=&window; sapp_desc desc{};
};
struct { App app; } _sapp_tc;
_sapp_tc_window_t* live=&window;
_sapp_tc_window_t* _sapp_tc_lookup(sapp_window h) { return live && live->win_id==h.id ? live : nullptr; }
int checks=0,failures=0,calls=0,posted=0,terminated=0,cleaned=0,blocked=0,defaults=0,reasonDestroyed=0;
bool throwing=false,cancel=false,hardQuit=false,removeRecord=false,throwCleanup=false,throwReason=false;
void check(const char* label,bool ok) { ++checks;if(!ok){++failures;printf("native-quit: FAIL: %s\n",label);} }
namespace trussc::internal {
void setExitReasonIfEmpty(const char*) {} void setExitReason(const char*) {}
void clearExitReason() {} void setExitBlockReason(const char*) {}
std::string exitBlockReason() { if(throwReason)throw 239; return {}; }
bool appSetupCalled() { return true; } int appExitCode() { return 0; }
}
void ShutdownBlockReasonDestroy(HWND) { ++reasonDestroyed; }
void ShutdownBlockReasonCreate(HWND,const wchar_t*) { ++blocked; }
void PostQuitMessage(int) { ++posted; }
int MultiByteToWideChar(int,int,const char*,int,wchar_t*,int) { return 1; }
int GetCurrentProcess() { return 0; }
void TerminateProcess(int,int) { ++terminated; }
LRESULT DefWindowProcW(HWND,UINT,WPARAM,LPARAM) { ++defaults;return 23; }
'''+ '\n'.join(parts) + r'''
static LRESULT _sapp_tc_wndproc_impl(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam; auto* w=&window;
    switch(msg) {
'''+branches+r'''
        default: break;
    }
    return DefWindowProcW(hwnd,msg,wParam,lParam);
}
'''+body(native,'static LRESULT CALLBACK _sapp_tc_wndproc(')+r'''
void event(const sapp_event* e,sapp_window h,void*) {
    ++calls;
    check("production delivery stamps quit and guards receiver",e->type==SAPP_EVENTTYPE_QUIT_REQUESTED && e->window_id==17 && h.id==17 && window.in_tick);
    if(cancel)_sapp_tc.app.quit_requested=false;
    if(hardQuit)_sapp_tc.app.quit_ordered=true;
    if(removeRecord)live=nullptr;
    if(throwing)throw 239;
}
void cleanup() { ++cleaned; if(throwCleanup)throw 239; }
void reset() {
    window={};live=&window;_sapp_tc.app={};window.desc.event_cb=event;
    _sapp_tc.app.desc.cleanup_cb=cleanup;
    calls=posted=terminated=cleaned=blocked=defaults=reasonDestroyed=0;
    throwing=cancel=hardQuit=removeRecord=throwCleanup=throwReason=false;
}
int main() {
    for(bool outer:{false,true}) {
        reset();window.in_tick=outer;throwing=true;
        check("throw vetoes process quit",!_sapp_tc_win32_request_exit(&window));
        check("throw clears pending quit without a retry",calls==1 && !_sapp_tc.app.quit_requested && !_sapp_tc.app.quit_ordered);
        check("throw restores previous tick guard",window.in_tick==outer);
        throwing=false;
        check("subsequent explicit quit accepts",_sapp_tc_win32_request_exit(&window) && calls==2);
    }
    reset();throwing=true;removeRecord=true;
    check("throw with removed receiver still vetoes safely",!_sapp_tc_win32_request_exit(&window) && !live);
    reset();cancel=true;
    check("explicit cancellation still vetoes",!_sapp_tc_win32_request_exit(&window) && !_sapp_tc.app.quit_requested);
    reset();_sapp_tc.app.quit_ordered=true;throwing=true;
    check("ordered hard quit bypasses client",_sapp_tc_win32_request_exit(&window) && calls==0);
    reset();hardQuit=throwing=true;
    check("hard quit set by throwing client stays unvetoable",_sapp_tc_win32_request_exit(&window) && _sapp_tc.app.quit_ordered && !_sapp_tc.app.quit_requested);
    for(bool userdata:{false,true}) {
        reset();throwing=true;
        window.desc.event_cb=_sapp_tc_main_event_tramp;
        window.events_enabled=_sapp_tc_app_events_enabled;
        if(userdata) {
            _sapp_tc.app.desc.user_data=&window;
            _sapp_tc.app.desc.event_userdata_cb=[](const sapp_event* e,void* user) {
                check("userdata client receives original user",user==&window);
                event(e,{window.win_id},nullptr);
            };
        } else _sapp_tc.app.desc.event_cb=[](const sapp_event* e) { event(e,{window.win_id},nullptr); };
        check("throw through production main trampoline vetoes",!_sapp_tc_win32_request_exit(&window) && calls==1 && !_sapp_tc.app.quit_requested && !window.in_tick);
        throwing=false;
        check("main trampoline accepts subsequent request",_sapp_tc_win32_request_exit(&window) && calls==2);
    }
    for(UINT msg:{WM_QUERYENDSESSION}) {
        reset();throwing=true;
        const auto reply=_sapp_tc_wndproc(17,msg,0,0);
        check("throwing close/session query stays open",reply==0 && posted==0 && calls==1 && !_sapp_tc.app.quit_requested && !_sapp_tc.app.quit_ordered && !window.in_tick && defaults==0);
        if(msg==WM_QUERYENDSESSION)check("throwing session query records block reason",blocked==1);
        // Idle processing has no pending quit to turn into another WM_CLOSE.
        if(_sapp_tc.app.quit_requested)_sapp_tc_wndproc(17,WM_CLOSE,0,0);
        check("idle after throw never retries",calls==1);
        throwing=false;
        const auto accepted=_sapp_tc_wndproc(17,msg,0,0);
        check("later close/session query accepts once",calls==2 && (msg==WM_CLOSE ? posted==1 && _sapp_tc.app.quit_ordered : accepted==TRUE && !_sapp_tc.app.quit_requested && !_sapp_tc.app.quit_ordered));
    }
    reset();cancel=throwReason=true;
    check("session reason failure retains outer catch veto",_sapp_tc_wndproc(17,WM_QUERYENDSESSION,0,0)==FALSE && blocked==1 && !_sapp_tc.app.quit_requested && !_sapp_tc.app.quit_ordered && defaults==0);
    for(bool initial:{false,true}) {
        reset();window.is_main=initial;
        int closes=0;
        window.desc.user_data=&closes;
        window.desc.close_cb=[](sapp_window,void* user) { ++*static_cast<int*>(user); };
        check("any Window close reaches its addressed callback", _sapp_tc_wndproc(17,WM_CLOSE,0,0)==0 && closes==1 && posted==0 && calls==0);
        window.desc.close_request_cb=[](sapp_window,void*) { return false; };
        _sapp_tc_wndproc(17,WM_CLOSE,0,0);
        check("any Window close request can defer without process quit", closes==1 && posted==0 && calls==0);
    }
    reset();window.is_main=false;window.desc.close_cb=[](sapp_window,void*) { throw 239; };
    check("throwing secondary close is contained without default destruction",_sapp_tc_wndproc(17,WM_CLOSE,0,0)==0 && defaults==0 && posted==0);
    reset();window.is_main=false;
    check("secondary session query does not consult quit client",_sapp_tc_wndproc(17,WM_QUERYENDSESSION,0,0)==TRUE && calls==0);
    reset();throwCleanup=true;
    _sapp_tc_wndproc(17,WM_ENDSESSION,TRUE,0);
    check("forced session end contains cleanup failure and terminates once",cleaned==1 && terminated==1 && _sapp_tc.app.cleanup_called && _sapp_tc.app.quit_ordered && calls==0);
    reset();_sapp_tc.app.quit_requested=_sapp_tc.app.quit_ordered=true;
    _sapp_tc_wndproc(17,WM_ENDSESSION,FALSE,0);
    check("cancelled session clears quit state without cleanup",!_sapp_tc.app.quit_requested && !_sapp_tc.app.quit_ordered && cleaned==0 && terminated==0);
    // A and B opened; A's native storage has been disposed. The Runtime
    // receiver must still ask B through its existing cancellable callback.
    for (int platform : {0, 1, 2}) for (bool veto : {false, true}) {
        reset(); _sapp_tc.app.main = nullptr; window.is_main = false;
        cancel = veto;
        _sapp_tc.app.desc._tc_event_cb = [](const sapp_event* e, sapp_window h, void* user) {
            check("process quit addresses Runtime rather than disposed A", e->type == SAPP_EVENTTYPE_QUIT_REQUESTED && h.id == 0 && user == nullptr);
            // Runtime's live-window veto is exercised by the linked ownership probe.
            sapp_event addressed = *e; addressed.window_id = 17; event(&addressed, {17}, nullptr);
        };
        // The seam callback above represents B's veto without native stamping.
        window.in_tick = true;
        bool accepted;
        if (platform == 0) accepted = _sapp_tc_win32_request_exit(_sapp_tc.app.main);
        else if (platform == 1) {
            _sapp_tc.app.quit_requested = true; _sapp_tc_x11_request_exit();
            accepted = _sapp_tc.app.quit_ordered;
        } else accepted = _sapp_tc_request_exit();
        check("surviving B is asked exactly once after A closes", calls == 1);
        check("surviving B can cancel process quit on every desktop seam", accepted == !veto);
    }
    reset();window.ready=false;
    check("gated client does not manufacture a throw veto",_sapp_tc_win32_request_exit(&window) && calls==0);
    reset();throwing=true;sapp_event e{};e.type=SAPP_EVENTTYPE_QUIT_REQUESTED;
    auto result=_sapp_tc_send(&window,&e);
    check("shared delivery distinguishes delivered throw",result.delivered && result.client_threw && !window.in_tick);
    window.ready=false;result=_sapp_tc_send(&window,&e);
    check("shared delivery distinguishes skipped event",!result.delivered && !result.client_threw);
    printf("native-quit: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
'''
variants=[('baseline',code)]
if os.environ.get('TC_NATIVE_QUIT_MUTATIONS'):
    old='result.client_threw = true;'
    assert old in code, 'shared throw-report mutation anchor missing'
    variants.append(('swallowed_unreported_throw',code.replace(old,'/* swallow without reporting */')))
    old='if (result.client_threw)'
    assert old in code, 'quit-result mutation anchor missing'
    variants.append(('ignored_quit_result',code.replace(old,'if (false)')))
results=[]
with tempfile.TemporaryDirectory(prefix='tc-an2b-cp3fu10-native-quit-') as folder:
    for backend in ('SOKOL_GLCORE','SOKOL_GLES3'):
        for name,text in variants:
            cpp=Path(folder)/'probe.cpp';binary=Path(folder)/'probe'
            cpp.write_text(text,encoding='utf-8')
            subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-D'+backend,'-I'+str(include),str(cpp),'-o',str(binary)],check=True)
            r=subprocess.run([str(binary)],capture_output=True,text=True,encoding='utf-8',timeout=15)
            print(backend,name,r.stdout,flush=True)
            failures=r.stdout.count('native-quit: FAIL:')
            valid=(r.returncode==0 and not failures) if name=='baseline' else (r.returncode==1 and failures>0)
            results.append(dict(backend=backend,name=name,returncode=r.returncode,failed_assertions=failures,valid=valid))
output=os.environ.get('TC_NATIVE_QUIT_OUTPUT')
if output:
    Path(output).write_text(json.dumps(results,indent=2)+'\n',encoding='utf-8')
assert all(row['valid'] for row in results), results
