"""Execute production decoration changes against recording X11/Win32 boundaries.

Run through tcbuild.sh. These sequence checks do not emulate mutter or AppKit.
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest

from test_window_pixel_size import body

ROOT = Path(__file__).resolve().parents[2]
NATIVE = (ROOT / 'core/include/sokol/sokol_app_tc.h').read_text(encoding='utf-8')


def execute(source):
    with tempfile.TemporaryDirectory(prefix='decoration-') as temporary:
        cpp = Path(temporary) / 'probe.cpp'
        binary = Path(temporary) / 'probe'
        cpp.write_text(source, encoding='utf-8')
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-I', str(ROOT / 'core/include/sokol'),
                        str(cpp), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


class WindowDecoration(unittest.TestCase):
    def test_launch_native_descriptor(self):
        # The core runtimeSettings test checks WindowSettings -> sapp_desc.
        # Complete the transfer through the shared desktop normalizer here.
        execute(r'''
#include "sokol_app_tc.h"
#include "util/sokol_window_native.h"
#include <cassert>
int main() {
    sapp_desc desc{};
    assert(!_sapp_tc_normalize_main_window(&desc).borderless);
    desc._tc_borderless = true;
    assert(_sapp_tc_normalize_main_window(&desc).borderless);
}
''')
        for platform in ('mac', 'win32', 'x11'):
            creator = body(NATIVE, f'static bool _sapp_tc_{platform}_create_main_window(')
            self.assertIn('native.window = _sapp_tc_normalize_main_window(d);', creator)
        creator = body(NATIVE, 'static bool _sapp_tc_x11_create_native_window(')
        self.assertIn('_sapp_tc_x11_set_decorated(w, false);', creator)
        # Mapping is owned by the later shared constructor, after native creation.
        constructor = body(NATIVE, 'static _sapp_tc_window_t* _sapp_tc_x11_create_window(')
        self.assertLess(constructor.index('_sapp_tc_x11_create_native_window('),
                        constructor.index('_sapp_tc_x11_show_window(w)'))

    @unittest.skipUnless(sys.platform.startswith('linux'), 'X11 production branch requires Linux')
    def test_x11_size_request_order_and_count(self):
        execute(r'''
#include <cassert>
#include <vector>
using sapp_tc_window = int;
using Atom = unsigned long;
constexpr Atom XA_ATOM=4;
struct _sapp_tc_window_t {
    struct { bool borderless=false, hidden=false; } desc;
    int xwin=1;
    bool fullscreen=false, fullscreen_requested=false, decoration_pending=false, fullscreen_on_map=false;
    double fullscreen_sent=0;
} window;
struct { int display=1; Atom NET_WM_STATE=1, NET_WM_STATE_FULLSCREEN=2; } _sapp_tc;
struct XWindowAttributes { int width, height; } live{960,600};
std::vector<int> calls;
_sapp_tc_window_t* _sapp_tc_lookup(int) { return &window; }
int XGetWindowAttributes(int, int, XWindowAttributes* client) { *client=live; return 1; }
void _sapp_tc_x11_set_decorated(_sapp_tc_window_t*, bool) { calls.push_back(1); }
void XResizeWindow(int,int,unsigned width,unsigned height) {
    assert(width==unsigned(live.width) && height==unsigned(live.height));
    calls.push_back(2);
}
void XFlush(int) { calls.push_back(3); }
void XFree(void*) {}
double _sapp_tc_now() { return 10; }
unsigned long _sapp_tc_x11_get_window_property(int,Atom,Atom,unsigned char** data,unsigned long) {
    *data=nullptr;
    return 0; // The WM reports windowed on fullscreen exit.
}
''' + body(NATIVE, 'extern "C" void sapp_tc_window_set_decorated(')
    + body(NATIVE, 'static void _sapp_tc_x11_observe_fullscreen(') + r'''
int main() {
    for (bool decorated : {false,true,false,true}) {
        calls.clear();
        sapp_tc_window_set_decorated(1,decorated);
        assert((calls==std::vector<int>{1,2,3}));
        calls.clear();
        sapp_tc_window_set_decorated(1,decorated);
        assert(calls.empty());
        // A later user resize is the size preserved by the next toggle.
        live={801,503};
    }
    window.fullscreen=true;
    calls.clear();
    sapp_tc_window_set_decorated(1,false);
    assert(window.decoration_pending);
    assert((calls==std::vector<int>{1,3}));
    live={960,600}; // Current windowed client size when the WM reports exit.
    calls.clear();
    _sapp_tc_x11_observe_fullscreen(&window);
    assert(!window.decoration_pending);
    assert((calls==std::vector<int>{1,2,3}));
    calls.clear();
    _sapp_tc_x11_observe_fullscreen(&window);
    sapp_tc_window_set_decorated(1,false);
    assert(calls.empty()); // Repeated observation/request must not resize again.
}
''')

    def test_win32_client_size_at_current_dpi(self):
        execute(r'''
#include <cassert>
#include <vector>
#define _WIN32
using sapp_tc_window=int;
using LONG_PTR=long long;
using DWORD=unsigned long;
constexpr LONG_PTR WS_CAPTION=1, WS_THICKFRAME=2, WS_MINIMIZEBOX=4,
    WS_MAXIMIZEBOX=8, WS_SYSMENU=16;
constexpr int GWL_STYLE=1,GWL_EXSTYLE=2;
constexpr unsigned SWP_NOMOVE=1,SWP_NOZORDER=2,SWP_FRAMECHANGED=4,FALSE=0;
struct RECT { long left,top,right,bottom; };
struct _sapp_tc_window_t {
    struct { bool borderless=false; } desc;
    bool fullscreen=false;
    LONG_PTR stored_style=31;
    LONG_PTR stored_ex_style=0;
    RECT stored_window_rect{100,100,1076,737};
    int hwnd=1;
} window;
LONG_PTR style=31;
int clientWidth=960,clientHeight=600,adjustments=0,positions=0;
unsigned dpi=144;
unsigned getDpi(int) { return dpi; }
bool adjust(RECT* rect,DWORD value,bool,DWORD,unsigned currentDpi) {
    ++adjustments;
    if (value & WS_CAPTION) { rect->right+=16*currentDpi/96; rect->bottom+=37*currentDpi/96; }
    return true;
}
struct {
    unsigned (*GetDpiForWindow)(int)=getDpi;
    bool (*AdjustWindowRectExForDpi)(RECT*,DWORD,bool,DWORD,unsigned)=adjust;
} _sapp_tc;
_sapp_tc_window_t* _sapp_tc_lookup(int) { return &window; }
LONG_PTR GetWindowLongPtrW(int,int field) { return field==GWL_STYLE ? style : 0; }
bool GetClientRect(int,RECT* rect) { *rect={0,0,clientWidth,clientHeight}; return true; }
bool AdjustWindowRectEx(RECT* rect,DWORD value,bool menu,DWORD exStyle) {
    return adjust(rect,value,menu,exStyle,96);
}
void SetWindowLongPtrW(int,int,LONG_PTR value) { style=value; }
void SetWindowPos(int,void*,int,int,int width,int height,unsigned flags) {
    ++positions;
    assert(flags==(SWP_NOMOVE|SWP_NOZORDER|SWP_FRAMECHANGED));
    if (style & WS_CAPTION) { width-=16*dpi/96; height-=37*dpi/96; }
    assert(width==clientWidth && height==clientHeight);
}
''' + body(NATIVE, 'extern "C" void sapp_tc_window_set_decorated(') + r'''
int main() {
    for (unsigned currentDpi : {96,144,192}) {
        dpi=currentDpi;
        for (bool decorated : {false,true,false,true}) {
            const int count=positions;
            sapp_tc_window_set_decorated(1,decorated);
            assert(positions==count+1 && adjustments==positions);
            sapp_tc_window_set_decorated(1,decorated);
            assert(positions==count+1);
            clientWidth=801; clientHeight=503;
        }
    }
    _sapp_tc.GetDpiForWindow=nullptr;
    dpi=96;
    sapp_tc_window_set_decorated(1,false);
    sapp_tc_window_set_decorated(1,true);
    const int count=positions;
    window.fullscreen=true;
    sapp_tc_window_set_decorated(1,false);
    assert(positions==count && !(window.stored_style & WS_CAPTION));
}
''')

    def test_win32_fullscreen_restore_client_size(self):
        execute(r'''
#include <cstdio>
#include <cassert>
#define _WIN32
using LONG_PTR=long long;
using DWORD=unsigned long;
using UINT=unsigned;
using HWND=void*;
constexpr LONG_PTR WS_CAPTION=1, WS_THICKFRAME=2, WS_MINIMIZEBOX=4, WS_MAXIMIZEBOX=8, WS_SYSMENU=16, WS_POPUP=32, WS_VISIBLE=64;
constexpr int GWL_STYLE=1, GWL_EXSTYLE=2, MONITOR_DEFAULTTONEAREST=0;
constexpr unsigned SWP_NOMOVE=1, SWP_NOZORDER=2, SWP_FRAMECHANGED=4, SWP_NOACTIVATE=8, SWP_NOSIZE=16;
constexpr bool FALSE=false;
constexpr HWND HWND_TOP=nullptr;
struct RECT { long left,top,right,bottom; };
struct MONITORINFO { unsigned cbSize; RECT rcMonitor; };
using sapp_tc_window=int;
struct _sapp_tc_window_t {
    struct { bool borderless=false, hidden=false; } desc;
    bool fullscreen=false, restore_valid=false;
    LONG_PTR stored_style=0, stored_ex_style=0;
    RECT stored_window_rect{};
    struct { void terminal(bool) {} } fullscreen_state;
    HWND hwnd=nullptr;
} window;
LONG_PTR style=31, exStyle=0;
RECT outer{100,100,1076,739};
int adjustments=0;
_sapp_tc_window_t* _sapp_tc_lookup(int) { return &window; }
int MonitorFromWindow(HWND,int) { return 1; }
bool GetMonitorInfoW(int,MONITORINFO* info) { info->rcMonitor={0,0,1920,1080}; return true; }
bool GetWindowRect(HWND,RECT* rect) { *rect=outer; return true; }
LONG_PTR GetWindowLongPtrW(HWND,int which) { return which==GWL_STYLE ? style : exStyle; }
void SetWindowLongPtrW(HWND,int which,LONG_PTR value) { (which==GWL_STYLE ? style : exStyle)=value; }
bool GetClientRect(HWND,RECT* rect) {
    *rect={0,0,outer.right-outer.left-((style&WS_CAPTION)?16:0),outer.bottom-outer.top-((style&WS_CAPTION)?39:0)};
    return true;
}
bool adjust(RECT* rect,DWORD flags,bool,DWORD,unsigned) {
    ++adjustments;
    if (flags&WS_CAPTION) { rect->right+=16; rect->bottom+=39; }
    return true;
}
bool AdjustWindowRectEx(RECT* rect,DWORD flags,bool menu,DWORD ex) { return adjust(rect,flags,menu,ex,96); }
unsigned dpi(HWND) { return 96; }
struct {
    unsigned (*GetDpiForWindow)(HWND)=dpi;
    bool (*AdjustWindowRectExForDpi)(RECT*,DWORD,bool,DWORD,unsigned)=adjust;
} _sapp_tc;
void SetWindowPos(HWND,HWND,int x,int y,int width,int height,unsigned flags) {
    if (flags&SWP_NOMOVE) { x=outer.left; y=outer.top; }
    outer={x,y,x+width,y+height};
}
''' + body(NATIVE, 'static void _sapp_tc_win32_set_fullscreen(') + '\n' + body(NATIVE, 'extern "C" void sapp_tc_window_set_decorated(') + r'''
int main() {
    // Begin with a 960x600 client and a 16x39 decorated frame at 96 DPI.
    RECT client;
    GetClientRect(nullptr,&client);
    assert(client.right==960 && client.bottom==600);
    _sapp_tc_win32_set_fullscreen(&window,true,0);
    sapp_tc_window_set_decorated(1,false);
    _sapp_tc_win32_set_fullscreen(&window,false,0);
    GetClientRect(nullptr,&client);
    std::printf("Win32 decorated -> fullscreen -> borderless -> windowed: client %ldx%ld (expected 960x600), adjustments %d\n",client.right,client.bottom,adjustments);
    assert(client.right==960 && client.bottom==600 && adjustments==2);
    // Inverse: start with a 960x600 borderless client, change to decorated while full.
    style=WS_POPUP|WS_SYSMENU;
    window.desc.borderless=true;
    outer={100,100,1060,700};
    _sapp_tc_win32_set_fullscreen(&window,true,0);
    sapp_tc_window_set_decorated(1,true);
    _sapp_tc_win32_set_fullscreen(&window,false,0);
    GetClientRect(nullptr,&client);
    std::printf("Win32 borderless -> fullscreen -> decorated -> windowed: client %ldx%ld (expected 960x600), adjustments %d\n",client.right,client.bottom,adjustments);
    assert(client.right==960 && client.bottom==600 && adjustments==4);
}
''')


if __name__ == '__main__':
    unittest.main()
