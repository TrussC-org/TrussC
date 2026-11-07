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
refresh = body(runtime, 'bool refreshWindowMetadata(Window& window)')
global_source = (root / 'core/include/tc/app/tcGlobal.cpp').read_text(encoding='utf-8')
def seed(source, marker):
    return source.split(marker, 1)[1].split(';', 1)[0]
seed_main = seed(global_source, 'binding(*window).geometry = ')
seed_secondary = seed(adapter, 'binding(*win).geometry = ')
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
bool nativeHidden=false, mapComplete=true;
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
struct sapp_tc_window_metadata { bool hidden=false; };
struct Operations {
    bool (*readMetadata)(int, void*, sapp_tc_window_metadata&) =
        +[](int, void*, sapp_tc_window_metadata& out) { out.hidden=nativeHidden; return true; };
} operations;
struct Window {
    struct Binding {
        int handle=7;
        sapp_tc_window_metadata metadata;
        bool bound=true;
        Operations* operations=&trussc::operations;
        void* user=nullptr;
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
    nativeHidden=!visible; mapComplete=false;
    // Withdrawal makes the raw position wrong; the snapshot must precede it.
    if (!visible) { liveX=1; liveY=2; }
}
bool sapp_tc_window_map_complete(int) { return !nativeHidden && mapComplete; }
namespace internal {
constexpr bool supportsSecondaryWindows() { return true; }
struct WindowRequestAccess {
    static const auto& binding(const Window& w) { return w.binding_; }
    static auto& binding(Window& w) { return w.binding_; }
};
int handle(const Window& w) { return w.binding_.handle; }
Window* resolveWindow() { return selected; }
void publishWindowMetadata(Window& w, const sapp_tc_window_metadata& m) { w.binding_.metadata=m; }
REFRESH_BODY
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
        mapComplete=true; refreshWindowMetadata(window);
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
        liveX=0; liveY=0; // Fresh frame: client placement is not ready yet.
        refreshWindowMetadata(window);
        assert(readPosition().x==1200 && readPosition().y==500);
        assert(reads==readsAtHide);
        x11Geometry(display,7,geometry,true,800,300,0,0,1);
        assert(readPosition().x==800 && readPosition().y==300);
        window.setVisible(false); // A hide during the pending show retains the snapshot.
        mapComplete=true; refreshWindowMetadata(window);
        assert(readPosition().x==800 && readPosition().y==300);
        window.setVisible(true);
        refreshWindowMetadata(window);
        assert(readPosition().x==800 && readPosition().y==300);
        liveX=455; liveY=266;
        mapComplete=true; refreshWindowMetadata(window);
        assert(readPosition().x==450 && readPosition().y==240);
        assert(reads>readsAtHide);
    }
    for (bool mainWindow : {false,true}) {
        struct { int width=100, height=80; bool pixelPerfect=false, visible=false; } settings;
        const auto pixelPerfectMode=[] { return false; };
        auto& geometry=window.binding_.geometry;
        if (mainWindow) geometry = SEED_MAIN; else geometry = SEED_SECONDARY;
        nativeHidden=true; mapComplete=false;
        liveX=5; liveY=26; // Initially 0,0, including without the snapshot.
        const auto readPosition=[&] { return mainWindow ? singleWindowPosition() : window.getPosition(); };
        assert(readPosition().x==0 && readPosition().y==0);
        x11Geometry(display,7,geometry,true,400,300,0,0,1);
        assert(readPosition().x==400 && readPosition().y==300);
    }
    puts("native-gates: X11 hidden snapshot, accepted hidden move, live read after show (main/secondary): PASS");
    puts("native-gates: X11 explicit position hints before show and after resize: PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='tc-x11-position-') as temp:
    scratch = Path(temp)
    native = header.read_text(encoding='utf-8')
    baseline = dict(native=native, visibility=visibility, main_position=main_position,
                    refresh=refresh, seed_main=seed_main, seed_secondary=seed_secondary)
    variants = {'baseline': baseline}
    if '--mutations' in sys.argv[1:]:
        for name, key, old, new in (
            ('no-hidden-move-update', 'native', 'requested.hiddenX = x;', 'requested.hiddenX = 0;'),
            ('no-hidden-read', 'native', 'if (requested && requested->hiddenPosition)', 'if (false)'),
            ('no-position-flags', 'native', 'USPosition | PPosition', '0'),
            ('no-position-x', 'native', 'hints.x = x;', 'hints.x = 0;'),
            ('no-position-y', 'native', 'hints.y = y;', 'hints.y = 0;'),
            ('no-position-update', 'native', 'if (!move && hints.win_gravity', 'if (hints.win_gravity'),
            ('no-move-forwarding', 'native', 'x11PinMoveGravity(dpy, window, move, x, y);', 'x11PinMoveGravity(dpy, window, false, x, y);'),
            ('no-pre-hide-snapshot', 'visibility', 'binding_.geometry.hiddenY = position.y;', 'binding_.geometry.hiddenY = 0;'),
            ('clear-at-request', 'visibility', 'if (!visible) binding_.geometry.hiddenPosition = true;',
             'binding_.geometry.hiddenPosition = !visible;'),
            ('no-live-read-after-show', 'refresh', 'if (sapp_tc_window_map_complete(b.handle))', 'if (false)'),
            ('no-main-hidden-read', 'main_position', 'if (geometry.hiddenPosition)', 'if (false)'),
            ('no-start-hidden-main', 'seed_main', '!settings.visible', 'false'),
            ('no-start-hidden-secondary', 'seed_secondary', '!settings.visible', 'false'),
        ):
            assert baseline[key].count(old) == 1, name
            variants[name] = dict(baseline, **{key: baseline[key].replace(old, new)})
    for name, variant in variants.items():
        private = scratch / (name + '.h')
        private.write_text(variant['native'].replace('"../../include/tc/app/internal/tcWindowGeometryState.h"',
            '"' + str(root / 'core/include/tc/app/internal/tcWindowGeometryState.h') + '"'), encoding='utf-8')
        cpp = scratch / 'probe.cpp'
        contents = source.replace('GEOMETRY_HEADER', str(private))
        for marker, key in (('VISIBILITY_BODY', 'visibility'), ('MAIN_POSITION_BODY', 'main_position'),
                            ('REFRESH_BODY', 'refresh'), ('SEED_MAIN', 'seed_main'), ('SEED_SECONDARY', 'seed_secondary')):
            contents = contents.replace(marker, variant[key])
        contents = contents.replace('POSITION_BODY', position)
        cpp.write_text(contents, encoding='utf-8')
        binary = scratch / 'probe'
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-include', 'initializer_list', str(cpp), '-o', str(binary)], check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True, encoding='utf-8')
        if (name == 'baseline') != (result.returncode == 0) or (name != 'baseline' and 'Assertion' not in result.stderr):
            raise SystemExit(f'{name}: unexpected exit {result.returncode}\n{result.stdout}{result.stderr}')
        expected_assertion = {
            'no-hidden-move-update': 'readPosition().x==1200 && readPosition().y==500',
            'no-hidden-read': 'readPosition().x==350 && readPosition().y==140',
            'no-pre-hide-snapshot': 'readPosition().x==350 && readPosition().y==140',
            'clear-at-request': 'readPosition().x==1200 && readPosition().y==500',
            'no-live-read-after-show': 'readPosition().x==450 && readPosition().y==240',
            'no-main-hidden-read': 'readPosition().x==350 && readPosition().y==140',
            'no-start-hidden-main': 'readPosition().x==400 && readPosition().y==300',
            'no-start-hidden-secondary': 'readPosition().x==400 && readPosition().y==300',
        }.get(name)
        if expected_assertion and expected_assertion not in result.stderr:
            raise SystemExit(f'{name}: did not fail the intended assertion\n{result.stderr}')
        print(result.stdout.strip() if name == 'baseline' else f'native-gates: position mutation {name}: detected', flush=True)
