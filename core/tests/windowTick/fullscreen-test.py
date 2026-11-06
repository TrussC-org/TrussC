#!/usr/bin/env python3
"""Compile production fullscreen bodies against recording native seams.

No SDK/display needed. --mutations requires every behavioral mutant to compile
and fail an assertion, rather than accepting a compiler failure as coverage.
"""
from pathlib import Path
import argparse
import os
import runpy
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
body = runpy.run_path(str(Path(__file__).with_name('native-event-fixture.py')))['body']
NATIVE = (ROOT / 'core/include/sokol/sokol_app_tc.h').read_text(encoding='utf-8')
WINDOW = (ROOT / 'core/include/tc/app/tcWindow.h').read_text(encoding='utf-8')


def source(native):
    decoration = body(native, 'extern "C" void sapp_tc_window_set_decorated(')
    win = body(native, 'static void _sapp_tc_win32_set_fullscreen(')
    observe = body(native, 'static void _sapp_tc_x11_observe_fullscreen(')
    request = body(native, 'static void _sapp_tc_x11_set_fullscreen(')
    start = native.index('        case PropertyNotify:', native.index('static void _sapp_tc_x11_process_event('))
    end = native.index('            _sapp_tc_x11_send_clipboard_chunk', start)
    dispatch = native[start:end]
    mac = body(native, 'static void _sapp_tc_request_fullscreen(').replace('[w->window toggleFullScreen:nil]', 'toggle(w)')
    mac += body(native, 'static void _sapp_tc_reconcile_fullscreen(').replace('[w->window toggleFullScreen:nil]', 'toggle(w)')
    mac += body(native, 'static void _sapp_tc_mac_fullscreen_terminal(')
    web = body(native, '_SOKOL_PRIVATE void _sapp_tc_emsc_toggle_fullscreen(').replace('_SOKOL_PRIVATE ', '')
    return r'''
#include <cstdio>
#include <vector>
#include <cstdlib>
#include "util/sokol_window_fullscreen.h"
int failures=0;
void check(bool ok) { if (!ok) ++failures; }
using LONG_PTR=long; using UINT=unsigned; using Atom=unsigned long;
constexpr int WS_CAPTION=1, WS_THICKFRAME=2, WS_MINIMIZEBOX=4, WS_MAXIMIZEBOX=8,
 WS_SYSMENU=16, WS_POPUP=32, WS_VISIBLE=64, GWL_STYLE=0, GWL_EXSTYLE=1,
 SWP_NOMOVE=1, SWP_NOSIZE=2, SWP_NOZORDER=4, SWP_FRAMECHANGED=8,
 MONITOR_DEFAULTTONEAREST=0, HWND_TOP=0, PropertyNotify=28, XA_ATOM=4;
struct RECT { int left=0,top=0,right=480,bottom=300; };
struct MONITORINFO { unsigned cbSize; RECT rcMonitor{0,0,1920,1080},rcWork{0,0,1920,1040}; };
struct sapp_window { unsigned id; };
constexpr int _SAPP_TC_X11_VISIBILITY_NONE=0, _SAPP_TC_X11_VISIBILITY_MAPPING=1;
struct _sapp_tc_window_t {
 bool fullscreen=false, fullscreen_requested=false, fullscreen_on_map=false, restore_valid=false, decoration_pending=false;
 struct { bool hidden=false, borderless=false; } desc;
 int hwnd=1,xwin=1, visibility_op=0,window=1;
 LONG_PTR stored_style=0,stored_ex_style=0;
 RECT stored_window_rect;
 _sapp_tc_fullscreen_state_t fullscreen_state;
};
_sapp_tc_window_t record;
struct { int display=1,constructing=0; Atom NET_WM_STATE=10,NET_WM_STATE_FULLSCREEN=11; } _sapp_tc;
_sapp_tc_window_t* _sapp_tc_lookup(sapp_window w) { return w.id==1 ? &record : nullptr; }
LONG_PTR style=WS_CAPTION|WS_THICKFRAME|WS_SYSMENU|WS_VISIBLE,exstyle=7;
RECT current; int positions=0,requests=0; bool requested=false;
LONG_PTR GetWindowLongPtrW(int,int index) { return index==GWL_STYLE ? style : exstyle; }
void SetWindowLongPtrW(int,int index,LONG_PTR value) { (index==GWL_STYLE ? style : exstyle)=value; }
int MonitorFromWindow(int,int) { return 1; }
bool GetMonitorInfoW(int,MONITORINFO*) { return true; }
void GetWindowRect(int,RECT* rect) { *rect=current; }
void SetWindowPos(int,std::nullptr_t,int,int,int,int,int) { ++positions; }
void SetWindowPos(int,int,int x,int y,int width,int height,int) { current={x,y,x+width,y+height}; ++positions; }
#define _WIN32
''' + decoration + '\n#undef _WIN32\n' + win + r'''
std::vector<Atom> property;
unsigned long _sapp_tc_x11_get_window_property(int,Atom,Atom,unsigned char** data,long length) {
 check(length==64); *data=nullptr;
 if (property.empty()) return 0;
 *data=(unsigned char*)property.data(); return property.size();
}
void XFree(void*) {}
void XFlush(int) {}
void _sapp_tc_x11_send_event(int,Atom,long op,long,long,long,long) { ++requests; requested=op==1; }
''' + request + observe + '\nnamespace web { struct { bool fullscreen=false; } _sapp_tc;\nvoid _sapp_tc_js_toggle_fullscreen() {}\n' + web + '\n}\n' + r'''
struct XEvent { int type=PropertyNotify; struct { Atom atom; int state=0; } xproperty; };
void process(_sapp_tc_window_t* w,Atom atom,int state=0) {
 XEvent event; event.xproperty={atom,state}; auto* ev=&event;
 switch(ev->type) {
''' + dispatch + r'''
 break;
 }
}
void toggle(_sapp_tc_window_t*) {}
int decorations=0;
void _sapp_tc_mac_apply_decoration(_sapp_tc_window_t*) { ++decorations; }
''' + mac + r'''
namespace internal { using WindowFullscreenPhase=_sapp_tc_fullscreen_phase; }
struct Window {
 _sapp_tc_fullscreen_phase phase;
 _sapp_tc_fullscreen_phase fullscreenPhase() const { return phase; }
 bool isFullscreen() const;
};
''' + body(WINDOW, 'inline bool Window::isFullscreen() const {') + r'''
int main() {
 web::_sapp_tc_emsc_toggle_fullscreen(); check(!web::_sapp_tc.fullscreen);
 web::_sapp_tc.fullscreen=true; web::_sapp_tc_emsc_toggle_fullscreen(); check(web::_sapp_tc.fullscreen);
 const LONG_PTR chrome=WS_CAPTION|WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_SYSMENU;
 for (bool initial : {false,true}) for (bool decorated : {false,true}) for (bool hidden : {false,true}) {
  record={}; record.desc.hidden=hidden;
  style=WS_VISIBLE|(initial?chrome:0); exstyle=7; current={20,30,500,330}; positions=0;
  _sapp_tc_win32_set_fullscreen(&record,true,0);
  const auto fullStyle=style; const auto fullPositions=positions;
  sapp_tc_window_set_decorated({1},decorated);
  check(style==fullStyle && !(style&(WS_CAPTION|WS_THICKFRAME)) && positions==fullPositions);
  check(current.right==1920 && current.bottom==1080 && record.fullscreen);
  _sapp_tc_win32_set_fullscreen(&record,false,0);
  check(!record.fullscreen && !(style&WS_POPUP) && (style&chrome)==(decorated?chrome:0));
  check(current.left==20 && current.top==30 && current.right==500 && current.bottom==330 && exstyle==7);
  sapp_tc_window_set_decorated({1},!decorated);
  check((style&chrome)==(!decorated?chrome:0));
 }
 record={}; requests=0;
 _sapp_tc_x11_set_fullscreen(&record,true);
 check(!record.fullscreen && record.fullscreen_requested && requests==1 && requested);
 property={99}; process(&record,_sapp_tc.NET_WM_STATE);
 check(!record.fullscreen && record.fullscreen_requested); // unrelated state change cannot cancel pending entry
 property={99,_sapp_tc.NET_WM_STATE_FULLSCREEN}; process(&record,_sapp_tc.NET_WM_STATE);
 check(record.fullscreen && record.fullscreen_requested);
 property={99}; process(&record,999);
 check(record.fullscreen && record.fullscreen_requested); // unrelated properties ignored
 process(&record,_sapp_tc.NET_WM_STATE);
 check(!record.fullscreen && !record.fullscreen_requested); // WM-initiated exit wins
 property={_sapp_tc.NET_WM_STATE_FULLSCREEN}; process(&record,_sapp_tc.NET_WM_STATE);
 check(record.fullscreen && record.fullscreen_requested); // WM-initiated entry wins
 record.desc.hidden=true; property.clear(); process(&record,_sapp_tc.NET_WM_STATE,1);
 check(!record.fullscreen && record.fullscreen_requested); // withdrawal deletes real state, preserves replay
 _sapp_tc_x11_set_fullscreen(&record,false);
 check(!record.fullscreen_requested && requests==1); // hidden request waits for map
 record.fullscreen_requested=true; record.desc.hidden=false; record.fullscreen_on_map=true;
 process(&record,_sapp_tc.NET_WM_STATE,1);
 check(!record.fullscreen && record.fullscreen_requested);
 record.fullscreen_on_map=false; record.fullscreen=true; process(&record,_sapp_tc.NET_WM_STATE,1);
 check(!record.fullscreen && !record.fullscreen_requested); // deleted property means windowed
 record={}; _sapp_tc_request_fullscreen(&record,true);
 check(!record.fullscreen); // request alone cannot report real fullscreen
 _sapp_tc_mac_fullscreen_terminal(&record,false); check(!record.fullscreen && !record.fullscreen_state.desired);
 _sapp_tc_request_fullscreen(&record,true); _sapp_tc_mac_fullscreen_terminal(&record,true);
 check(record.fullscreen);
 _sapp_tc_request_fullscreen(&record,false); check(record.fullscreen);
 record.decoration_pending=true;
 _sapp_tc_mac_fullscreen_terminal(&record,false); check(!record.fullscreen && !record.decoration_pending && decorations==1);
 using Phase=_sapp_tc_fullscreen_phase;
 check(!Window{Phase::Windowed}.isFullscreen() && !Window{Phase::Entering}.isFullscreen());
 check(Window{Phase::Fullscreen}.isFullscreen() && Window{Phase::Exiting}.isFullscreen());
 std::printf("fullscreen: %d failures\n",failures); return failures?1:0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutations', action='store_true')
    args = parser.parse_args()
    variants = {'baseline': NATIVE}
    if args.mutations:
        changes = {
            'win-live-style': ('LONG_PTR style = w->fullscreen ? w->stored_style : GetWindowLongPtrW(w->hwnd, GWL_STYLE);', 'LONG_PTR style = GetWindowLongPtrW(w->hwnd, GWL_STYLE);'),
            'win-no-defer': ('if (w->fullscreen) {\n        w->stored_style = style;', 'if (false) {\n        w->stored_style = style;'),
            'win-no-restore': ('w->stored_style = style;', '(void)style;'),
            'win-work-area': ('rect = minfo.rcMonitor;', 'rect = minfo.rcWork;'),
            'x11-request-is-real': ('w->fullscreen_requested = enable;', 'w->fullscreen_requested = enable; w->fullscreen = enable;'),
            'x11-no-property': ('if (w && ev->xproperty.atom == _sapp_tc.NET_WM_STATE)', 'if (false && w && ev->xproperty.atom == _sapp_tc.NET_WM_STATE)'),
            'x11-unrelated-state-cancels-request': ('if (changed && !w->desc.hidden', 'if (!w->desc.hidden'),
            'x11-ignore-wm-choice': ('w->fullscreen_requested = full;', '(void)full;'),
            'x11-lose-hidden-replay': ('if (changed && !w->desc.hidden && w->visibility_op == _SAPP_TC_X11_VISIBILITY_NONE && !w->fullscreen_on_map)', 'if (true)'),
            'x11-always-full': ('w->fullscreen = full;', 'w->fullscreen = true;'),
            'web-request-is-real': ('// fullscreenchange reports the real state after the browser confirms it.', '_sapp_tc.fullscreen = !_sapp_tc.fullscreen;'),
            'mac-request-is-real': ('[w->window toggleFullScreen:nil];', 'w->fullscreen = w->fullscreen_state.desired; [w->window toggleFullScreen:nil];'),
        }
        for name, (old, new) in changes.items():
            assert old in NATIVE, (name, old)
            variants[name] = NATIVE.replace(old, new)
    with tempfile.TemporaryDirectory(prefix='tc-fullscreen-') as temporary:
        scratch = Path(temporary)
        for name, native in variants.items():
            cpp = scratch / (name+'.cpp'); binary = scratch / name
            cpp.write_text(source(native), encoding='utf-8')
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-I', str(ROOT/'core/include/sokol'), str(cpp), '-o', str(binary)], check=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True, encoding='utf-8', check=False)
            assert result.returncode == (0 if name=='baseline' else 1), (name, result.returncode, result.stdout)
            print(name+': PASS', flush=True)


if __name__ == '__main__':
    main()
