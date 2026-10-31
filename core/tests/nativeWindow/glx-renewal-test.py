#!/usr/bin/env python3
"""Production GLX renewal with strict association and synchronized X-error seams."""
from pathlib import Path
import os, subprocess, tempfile
root = Path(__file__).resolve().parents[3]
include = Path(os.environ.get('TC_X11_TEST_INCLUDE', root/'core/include/sokol'))
header = (include/'sokol_app_tc.h').read_text(encoding='utf-8')
def body(marker):
    start = header.index(marker); opening = header.index('{', start)
    end, depth = opening + 1, 1
    while depth:
        depth += (header[end] == '{') - (header[end] == '}'); end += 1
    return header[start:end] + (';' if marker.startswith('struct ') else '')
code = r'''
#include <cstdio>
#include <cstdint>
#include <initializer_list>
#include <climits>
#define SOKOL_GLCORE
using GLXDrawable=unsigned long;
using Display=int; using GLXContext=void*;
using GLXFBConfig=int;
constexpr int None=0, False=0;
struct sapp_window { uint32_t id; };
struct XErrorEvent { int error_code; };
using Handler=int(*)(Display*,XErrorEvent*);
struct _sapp_tc_window_t {
    uint32_t win_id=1; GLXDrawable drawable=11;
    unsigned long xwin=9; int swap_interval=1;
};
static Display display;
static struct {
    Display* display=&::display; GLXFBConfig fbconfig=0;
    struct {struct {int sample_count=4; int logger=0;} desc;} app; GLXContext ctx=(void*)1;
    int screen=0; int error_code=0; Handler prev_error_handler=nullptr;
    size_t window_slots=1; _sapp_tc_window_t* windows[1]{};
} _sapp_tc;
static bool oldLive, windowLive;
static int windowFailure, pendingError, syncs, oldDestroys, illegalCreates, invalidDestroys;
static GLXDrawable draw,read;
static GLXContext context;
static Handler handler;
static Handler XSetErrorHandler(Handler h) { Handler prev=handler; handler=h; return prev; }
static void XSync(Display* d,int) {
    ++syncs;
    if (pendingError) { XErrorEvent ev{pendingError}; pendingError=0; if(handler) handler(d,&ev); else ++invalidDestroys; }
}
static Display* glXGetCurrentDisplay(){return context?&display:nullptr;}
static GLXContext glXGetCurrentContext(){return context;}
static GLXDrawable glXGetCurrentDrawable(){return draw;}
static GLXDrawable glXGetCurrentReadDrawable(){return read;}
static bool glXMakeContextCurrent(Display*,GLXDrawable a,GLXDrawable b,GLXContext c){
    if ((a==12||b==12)&&!windowLive) return false;
    draw=a;read=b;context=c;
    return true;
}
static void glXDestroyWindow(Display*,GLXDrawable w){
    if(w==11 && oldLive) { if(draw==11||read==11||pendingError) ++illegalCreates; oldLive=false; ++oldDestroys; }
    else if(w==12 && windowLive) windowLive=false;
    else ++invalidDestroys;
}
static GLXDrawable glXCreateWindow(Display*,GLXFBConfig,unsigned long xwin,const int*) {
    // GLX 1.4 section 3.3.4: a duplicate association must fail with BadAlloc.
    if(oldLive||windowLive) { ++illegalCreates; pendingError=11; return windowFailure==1?0:12; }
    if(windowFailure) { pendingError=11; return windowFailure==1?0:12; }
    windowLive=true;return 12;
}
static void _sapp_tc_x11_set_swapinterval(_sapp_tc_window_t*,int){}
static _sapp_tc_window_t* _sapp_tc_lookup(sapp_window h){auto*w=_sapp_tc.windows[0];return w&&h.id==w->win_id?w:nullptr;}
using _sapp_tc_x11_surface_t=GLXDrawable;
'''
code += '\n'.join(body(marker) for marker in (
    'static int _sapp_tc_x11_error_handler(',
    'static void _sapp_tc_x11_grab_error_handler(',
    'static void _sapp_tc_x11_release_error_handler(',
    'static uint32_t _sapp_tc_x11_surface_window_id(',
    'static bool _sapp_tc_x11_saved_surface_alive(',
    'struct _sapp_tc_x11_surface_scope {',
    'static void _sapp_tc_x11_destroy_surface(',
    'static bool _sapp_tc_x11_create_surface(',
    'static bool _sapp_tc_x11_resize_surface('))
code += r'''
static int failures, checks;
static void check(const char* label,bool ok){++checks;printf("glx-renewal: %s: %s\n",label,ok?"PASS":"FAIL");failures+=!ok;}
static void reset(_sapp_tc_window_t& w){
    w={};_sapp_tc.windows[0]=&w;oldLive=true;windowLive=false;
    windowFailure=pendingError=syncs=oldDestroys=illegalCreates=invalidDestroys=0;
    draw=read=11;context=(void*)1;handler=nullptr;_sapp_tc.error_code=0;
}
int main(){
    _sapp_tc_window_t w;
    reset(w);
    check("successful renewal replaces the drawable without duplicate association",
        _sapp_tc_x11_resize_surface(&w) && !oldLive && windowLive && w.drawable==12 &&
        draw==12 && read==12 && context && oldDestroys==1 && !illegalCreates && !invalidDestroys);
    _sapp_tc_x11_destroy_surface(&w);
    check("renewed surface teardown", !w.drawable && !windowLive && !invalidDestroys);
    for(int mode : {1,2}) {
        reset(w);windowFailure=mode;
        check("failed renewal is terminal without recovery or drawable",
            !_sapp_tc_x11_resize_surface(&w) && !w.drawable && !oldLive && !draw && !read && !context &&
            oldDestroys==1 && syncs>0 && !illegalCreates && !invalidDestroys);
        _sapp_tc_x11_destroy_surface(&w);
        check("failed renewal cleanup never destroys an invalid XID", !invalidDestroys);
    }
    printf("glx-renewal: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
'''
with tempfile.TemporaryDirectory(prefix='tc-glx-renewal-', dir=os.environ.get('TMPDIR')) as tmp:
    p=Path(tmp);(p/'probe.cpp').write_text(code, encoding='utf-8')
    subprocess.run(['c++','-std=c++20',str(p/'probe.cpp'),'-o',str(p/'probe')],check=True)
    subprocess.run([str(p/'probe')],check=True)
