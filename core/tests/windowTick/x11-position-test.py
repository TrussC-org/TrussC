#!/usr/bin/env python3
"""Record production X11 geometry and visibility bodies, including hidden reads."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[3]
header = root / 'core/platform/linux/tcWindowGeometryX11.h'


def body(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


runtime = (root / 'core/include/tc/app/tcWindowRuntime.cpp').read_text(encoding='utf-8')
adapter = (root / 'core/platform/linux/tcWindowLinux.cpp').read_text(encoding='utf-8')
platform = (root / 'core/platform/linux/tcPlatform_linux.cpp').read_text(encoding='utf-8')
visibility = body(runtime, 'void Window::setVisible(bool visible)')
position = body(adapter, 'IVec2 nativeWindowPosition(const Window& w)')
main_position = body(platform, 'IVec2 internal::singleWindowPosition()')
source = r'''
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <cassert>
#include <cstdio>
#include <cstdint>
namespace trussc {
struct IVec2 { int x, y; };
struct Log { template<class T> Log& operator<<(T) { return *this; } };
Log logWarning(const char*) { return {}; }
Log logNotice(const char*) { return {}; }
}
XSizeHints hints{};
int moves=0, writes=0, reads=0;
int liveX=155, liveY=66;
bool nativeHidden=false;
int translate(Display*, Window, Window, int, int, int* x, int* y, Window*) {
    ++reads; *x=liveX; *y=liveY; return 1;
}
int property(Display*, Window, Atom, long, long, Bool, Atom,
    Atom* type, int* format, unsigned long* count, unsigned long*, unsigned char** data) {
    static unsigned long extents[]={5,5,26,5};
    *type=XA_CARDINAL; *format=32; *count=4;
    *data=reinterpret_cast<unsigned char*>(extents); return Success;
}
int release(void*) { return 1; }
int getHints(Display*, Window, XSizeHints* out, long*) { *out=hints; return 1; }
int setHints(Display*, Window, XSizeHints* in) { hints=*in; ++writes; return 1; }
Atom atom(Display*, const char*, Bool) { return 1; }
int sendEvent(Display*, Window, Bool, long, XEvent*) { return 1; }
int moveWindow(Display*, Window, int x, int y) {
    assert((hints.flags & (USPosition|PPosition)) == (USPosition|PPosition));
    assert(hints.x==x && hints.y==y); ++moves; return 1;
}
int moveResize(Display* d, Window w, int x, int y, unsigned, unsigned) { return moveWindow(d,w,x,y); }
int resizeWindow(Display*, Window, unsigned, unsigned) { return 1; }
int flush(Display*) { return 1; }
#undef DefaultRootWindow
#define DefaultRootWindow(dpy) 1
#define XGetWMNormalHints getHints
#define XSetWMNormalHints setHints
#define XInternAtom atom
#define XSendEvent sendEvent
#define XMoveWindow moveWindow
#define XMoveResizeWindow moveResize
#define XResizeWindow resizeWindow
#define XFlush flush
#define XTranslateCoordinates translate
#define XGetWindowProperty property
#define XFree release
#include "GEOMETRY_HEADER"
namespace trussc {
struct Window {
    struct Binding {
        int handle=7;
        struct { bool hidden=false; } metadata;
        internal::X11GeometryState geometry;
    } binding_;
    bool isOpen() const { return true; }
    IVec2 getPosition() const;
    void setVisible(bool);
};
Window* selected=nullptr;
void* sapp_x11_get_display() { return reinterpret_cast<void*>(1); }
const void* sapp_window_x11_get_window(int handle) { return reinterpret_cast<void*>(handle); }
const void* sapp_x11_get_window() { return sapp_window_x11_get_window(7); }
void sapp_tc_window_set_visible(int, bool visible) {
    nativeHidden=!visible;
    // Withdrawal makes the raw position wrong; the snapshot must precede it.
    if (!visible) { liveX=1; liveY=2; }
}
namespace internal {
constexpr bool supportsSecondaryWindows() { return true; }
struct WindowRequestAccess {
    static const auto& binding(const Window& w) { return w.binding_; }
};
int handle(const Window& w) { return w.binding_.handle; }
Window* resolveWindow() { return selected; }
void refreshWindowMetadata(Window& w) { w.binding_.metadata.hidden=nativeHidden; }
IVec2 windowPositionNative(const void* handle) {
    return x11FramePosition(static_cast<Display*>(sapp_x11_get_display()), (::Window)(uintptr_t)handle);
}
IVec2 singleWindowPosition();
POSITION_BODY
}
IVec2 Window::getPosition() const { return internal::nativeWindowPosition(*this); }
VISIBILITY_BODY
MAIN_POSITION_BODY
}
int main() {
    using namespace trussc::internal;
    auto* display=reinterpret_cast<Display*>(1);
    for (bool sized : {false,true}) {
        X11GeometryState requested;
        if (sized) requested={120,90};
        hints={}; hints.flags=PMinSize; hints.min_width=40;
        // Initial hidden creation, then a later hidden move. Hints must precede
        // both native moves and survive a resize before the next show.
        for (auto position : {trussc::IVec2{300,100},trussc::IVec2{400,120}}) {
            x11Geometry(display,7,requested,true,position.x,position.y,0,0,1);
            assert(hints.win_gravity==NorthWestGravity && (hints.flags&PWinGravity));
            assert((hints.flags&PMinSize) && hints.min_width==40);
            x11Geometry(display,7,requested,false,0,0,100,80,1);
            assert(hints.x==position.x && hints.y==position.y);
            assert((hints.flags & (USPosition|PPosition)) == (USPosition|PPosition));
        }
    }
    assert(moves==4 && writes>=4);
    trussc::Window window, peer;
    trussc::selected=&window;
    for (bool mainWindow : {false,true}) {
        const auto readPosition=[&] {
            return mainWindow ? singleWindowPosition() : window.getPosition();
        };
        liveX=155; liveY=66;
        window.setVisible(true);
        assert(readPosition().x==150 && readPosition().y==40);
        // A WM/user move must be sampled, rather than the last requested move.
        liveX=355; liveY=166;
        window.setVisible(false);
        const int readsAtHide=reads;
        assert(readPosition().x==350 && readPosition().y==140);
        assert(reads==readsAtHide);
        window.setVisible(false); // Repeated hiding must preserve the snapshot.
        assert(readPosition().x==350 && readPosition().y==140);
        assert(!peer.binding_.geometry.hiddenPosition);
        auto& geometry=window.binding_.geometry;
        x11Geometry(display,7,geometry,true,1200,500,0,0,1);
        assert(readPosition().x==1200 && readPosition().y==500);
        x11Geometry(display,7,geometry,false,0,0,120,90,1);
        assert(readPosition().x==1200 && readPosition().y==500);
        x11Geometry(display,7,geometry,true,50000,600,0,0,1);
        x11Geometry(display,7,geometry,true,900,600,0,0,1,true);
        assert(readPosition().x==1200 && readPosition().y==500);
        window.setVisible(true);
        liveX=455; liveY=266;
        assert(readPosition().x==450 && readPosition().y==240);
        assert(reads>readsAtHide);
    }
    puts("native-gates: X11 hidden snapshot, accepted hidden move, live read after show (main/secondary): PASS");
    puts("native-gates: X11 explicit position hints before show and after resize: PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='tc-x11-position-') as temp:
    scratch = Path(temp)
    native = header.read_text(encoding='utf-8')
    variants = {'baseline': (native, visibility, main_position)}
    if '--mutations' in sys.argv[1:]:
        for name, old, new in (
            ('no-hidden-move-update', 'requested.hiddenX = x;', 'requested.hiddenX = 0;'),
            ('no-hidden-read', 'if (requested && requested->hiddenPosition)', 'if (false)'),
            ('no-position-flags', 'USPosition | PPosition', '0'),
            ('no-position-x', 'hints.x = x;', 'hints.x = 0;'),
            ('no-position-y', 'hints.y = y;', 'hints.y = 0;'),
            ('no-position-update', 'if (!move && hints.win_gravity', 'if (hints.win_gravity'),
            ('no-move-forwarding', 'x11PinMoveGravity(dpy, window, move, x, y);', 'x11PinMoveGravity(dpy, window, false, x, y);'),
        ):
            if native.count(old) != 1:
                raise SystemExit(f'{name}: missing/ambiguous mutation marker {old!r}')
            variants[name] = (native.replace(old, new), visibility, main_position)
        for name, old, new in (
            ('no-pre-hide-snapshot', 'binding_.geometry.hiddenY = position.y;', 'binding_.geometry.hiddenY = 0;'),
            ('no-live-read-after-show', 'binding_.geometry.hiddenPosition = !visible;',
             'binding_.geometry.hiddenPosition = !visible || binding_.geometry.hiddenPosition;'),
        ):
            assert visibility.count(old) == 1
            variants[name] = (native, visibility.replace(old, new), main_position)
        old = 'if (geometry.hiddenPosition)'
        assert main_position.count(old) == 1
        variants['no-main-hidden-read'] = (native, visibility, main_position.replace(old, 'if (false)'))
    for name, (contents, visible_body, main_body) in variants.items():
        private = scratch / (name + '.h')
        private.write_text(contents.replace('"../../include/tc/app/internal/tcWindowGeometryState.h"',
            '"' + str(root / 'core/include/tc/app/internal/tcWindowGeometryState.h') + '"'), encoding='utf-8')
        cpp = scratch / 'probe.cpp'
        cpp.write_text(source.replace('GEOMETRY_HEADER', str(private)).replace('VISIBILITY_BODY', visible_body).replace('MAIN_POSITION_BODY', main_body).replace('POSITION_BODY', position), encoding='utf-8')
        binary = scratch / 'probe'
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-include', 'initializer_list', str(cpp), '-o', str(binary)], check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True, encoding='utf-8')
        if (name == 'baseline') != (result.returncode == 0) or (name != 'baseline' and 'Assertion' not in result.stderr):
            raise SystemExit(f'{name}: unexpected exit {result.returncode}\n{result.stdout}{result.stderr}')
        expected_assertion = {
            'no-hidden-move-update': 'readPosition().x==1200 && readPosition().y==500',
            'no-hidden-read': 'readPosition().x==350 && readPosition().y==140',
            'no-pre-hide-snapshot': 'readPosition().x==350 && readPosition().y==140',
            'no-live-read-after-show': 'readPosition().x==450 && readPosition().y==240',
            'no-main-hidden-read': 'readPosition().x==350 && readPosition().y==140',
        }.get(name)
        if expected_assertion and expected_assertion not in result.stderr:
            raise SystemExit(f'{name}: did not fail the intended assertion\n{result.stderr}')
        print(result.stdout.strip() if name == 'baseline' else f'native-gates: position mutation {name}: detected', flush=True)
