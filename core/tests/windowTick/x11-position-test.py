#!/usr/bin/env python3
"""Native-gates position hints: compile the real X11 geometry implementation."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[3]
header = root / 'core/platform/linux/tcWindowGeometryX11.h'
source = r'''
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cassert>
#include <cstdio>
namespace trussc {
struct IVec2 { int x, y; };
struct Log { template<class T> Log& operator<<(T) { return *this; } };
Log logWarning(const char*) { return {}; }
}
XSizeHints hints{};
int moves=0, writes=0;
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
#include "GEOMETRY_HEADER"
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
    puts("native-gates: X11 explicit position hints before show and after resize: PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='tc-x11-position-') as temp:
    scratch = Path(temp)
    native = header.read_text(encoding='utf-8')
    variants = {'baseline': native}
    if '--mutations' in sys.argv[1:]:
        for name, old, new in (
            ('no-position-flags', 'USPosition | PPosition', '0'),
            ('no-position-x', 'hints.x = x;', 'hints.x = 0;'),
            ('no-position-y', 'hints.y = y;', 'hints.y = 0;'),
            ('no-position-update', 'if (!move && hints.win_gravity', 'if (hints.win_gravity'),
            ('no-move-forwarding', 'x11PinMoveGravity(dpy, window, move, x, y);', 'x11PinMoveGravity(dpy, window, false, x, y);'),
        ):
            if native.count(old) != 1:
                raise SystemExit(f'{name}: missing/ambiguous mutation marker {old!r}')
            variants[name] = native.replace(old, new)
    for name, contents in variants.items():
        private = scratch / (name + '.h')
        private.write_text(contents.replace('"../../include/tc/app/internal/tcWindowGeometryState.h"',
            '"' + str(root / 'core/include/tc/app/internal/tcWindowGeometryState.h') + '"'), encoding='utf-8')
        cpp = scratch / 'probe.cpp'
        cpp.write_text(source.replace('GEOMETRY_HEADER', str(private)), encoding='utf-8')
        binary = scratch / 'probe'
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-include', 'initializer_list', str(cpp), '-o', str(binary)], check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True, encoding='utf-8')
        if (name == 'baseline') != (result.returncode == 0):
            raise SystemExit(f'{name}: unexpected exit {result.returncode}\n{result.stdout}{result.stderr}')
        print(result.stdout.strip() if name == 'baseline' else f'native-gates: position mutation {name}: detected', flush=True)
