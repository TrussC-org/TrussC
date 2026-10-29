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
using GLXDrawable=unsigned long; using GLXPbuffer=unsigned long;
using Display=int; using GLXContext=void*; using Window=unsigned long;
using GLXFBConfig=int;
constexpr int GLX_RENDER_TYPE=3, GLX_DRAWABLE_TYPE=4, GLX_DOUBLEBUFFER=5,
GLX_RED_SIZE=6, GLX_GREEN_SIZE=7, GLX_BLUE_SIZE=8, GLX_ALPHA_SIZE=9,
GLX_DEPTH_SIZE=10, GLX_STENCIL_SIZE=11, GLX_SAMPLES=12,
GLX_RGBA_BIT=1, GLX_WINDOW_BIT=1, GLX_PBUFFER_BIT=4,
SAPP_LOGITEM_LINUX_GLX_NO_GLXFBCONFIGS=1, SAPP_LOGITEM_LINUX_GLX_NO_SUITABLE_GLXFBCONFIG=2,
InputOutput=1, CWColormap=1, CWBorderPixel=2;
static bool windowOnly;
static int configCount=1, configSamples[2]={4,4}, configTypes[2]={5,5};
struct XVisualInfo {int depth=24; void* visual=(void*)7;};
struct XSetWindowAttributes { unsigned long colormap=0, border_pixel=0; };
static XVisualInfo visual;
static bool helperLive, helperGLXLive;
static int helperFailure, helperCreates, helperDestroys;
constexpr int None=0, False=0, GLX_PBUFFER_WIDTH=1, GLX_PBUFFER_HEIGHT=2;
struct sapp_window { uint32_t id; };
struct XErrorEvent { int error_code; };
using Handler=int(*)(Display*,XErrorEvent*);
struct _sapp_tc_window_t {
    uint32_t win_id=1; GLXDrawable drawable=11; bool drawable_is_pbuffer=false;
    unsigned long xwin=9, recovery_xwin=0, colormap=8; int swap_interval=1;
};
static Display display;
static struct {
    Display* display=&::display; GLXFBConfig fbconfig=0; Window root=1;
    struct {struct {int sample_count=4; int logger=0;} desc;} app; GLXContext ctx=(void*)1;
    int screen=0; int error_code=0; Handler prev_error_handler=nullptr;
    size_t window_slots=1; _sapp_tc_window_t* windows[1]{};
} _sapp_tc;
static void _sapp_tc_log(...){}
static GLXFBConfig* glXGetFBConfigs(Display*,int,int* count){static int configs[]={0,1};*count=configCount;return configs;}
static void XFree(void*){}
static int glXGetFBConfigAttrib(Display*,int cfg,int name,int* value){
    switch(name){
    case GLX_RENDER_TYPE:*value=GLX_RGBA_BIT;break;
    case GLX_DRAWABLE_TYPE:*value=windowOnly?GLX_WINDOW_BIT:configTypes[cfg];break;
    case GLX_DOUBLEBUFFER:*value=1;break;
    case GLX_DEPTH_SIZE:*value=24;break;
    case GLX_STENCIL_SIZE:*value=8;break;
    case GLX_SAMPLES:*value=configSamples[cfg];break;
    default:*value=8;
    }return 0;
}
static bool oldLive, windowLive, pbufferLive, bindFailure;
static bool bindAsyncError;
static int windowFailure, recoveryFailure, pendingError, syncs, oldDestroys, recoveryDestroys, illegalCreates, invalidDestroys;
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
    if (bindFailure && (a==20||b==20||a==30||b==30)) { pendingError=8; return false; }
    if ((a==12||b==12)&&!windowLive) return false;
    draw=a;read=b;context=c;
    if(bindAsyncError&&(a==20||a==30)){pendingError=8;}
    return true;
}
static void glXDestroyWindow(Display*,GLXDrawable w){
    if(w==30 && helperGLXLive) {helperGLXLive=false; ++recoveryDestroys;}
    else if(w==11 && oldLive) { if((draw!=20&&draw!=30)||_sapp_tc.error_code||pendingError) ++illegalCreates; oldLive=false; ++oldDestroys; }
    else if(w==12 && windowLive) windowLive=false;
    else ++invalidDestroys;
}
static GLXDrawable glXCreateWindow(Display*,GLXFBConfig,unsigned long xwin,const int*) {
    if(xwin==29){
        if(!helperLive||helperGLXLive){++illegalCreates;return 0;}
        if(recoveryFailure){pendingError=11;return recoveryFailure==1?0:30;}
        helperGLXLive=true;return 30;
    }
    // GLX 1.4 section 3.3.4: a duplicate association must fail with BadAlloc.
    if(oldLive||windowLive) { ++illegalCreates; pendingError=11; return windowFailure==1?0:12; }
    if(windowFailure) { pendingError=11; return windowFailure==1?0:12; }
    windowLive=true;return 12;
}
static GLXPbuffer glXCreatePbuffer(Display*,GLXFBConfig,const int*) {
    if(windowOnly){++illegalCreates;return 0;}
    if(recoveryFailure) { pendingError=11; return recoveryFailure==1?0:20; }
    pbufferLive=true;return 20;
}
static void glXDestroyPbuffer(Display*,GLXPbuffer p) {
    if(p==20 && pbufferLive) { pbufferLive=false; ++recoveryDestroys; }
    else ++invalidDestroys;
}
static XVisualInfo* glXGetVisualFromFBConfig(Display*,GLXFBConfig){return &visual;}
static Window XCreateWindow(Display*,Window,int,int,unsigned int width,unsigned int height,
 unsigned int border,int depth,unsigned int kind,void* v,unsigned long mask,XSetWindowAttributes* a){
    ++helperCreates;
    if(width!=1||height!=1||border||depth!=24||kind!=InputOutput||v!=visual.visual||mask!=(CWColormap|CWBorderPixel)||a->colormap!=8) ++illegalCreates;
    if(helperFailure){pendingError=11;return helperFailure==1?0:29;}
    helperLive=true;return 29;
}
static void XDestroyWindow(Display*,Window w){if(w==29&&helperLive){helperLive=false;++helperDestroys;}else ++invalidDestroys;}
static void _sapp_tc_x11_set_swapinterval(_sapp_tc_window_t*,int){}
static _sapp_tc_window_t* _sapp_tc_lookup(sapp_window h){auto*w=_sapp_tc.windows[0];return w&&h.id==w->win_id?w:nullptr;}
using _sapp_tc_x11_surface_t=GLXDrawable;
'''
code += '\n'.join(body(marker) for marker in (
    'static int _sapp_tc_glx_attrib(',
    'static bool _sapp_tc_glx_choose_fbconfig(',
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
    bindAsyncError=false;helperLive=helperGLXLive=false;helperFailure=helperCreates=helperDestroys=0;
    w={};_sapp_tc.windows[0]=&w;oldLive=true;windowLive=pbufferLive=bindFailure=false;
    windowFailure=recoveryFailure=pendingError=syncs=oldDestroys=recoveryDestroys=illegalCreates=invalidDestroys=0;
    draw=read=11;context=(void*)1;handler=nullptr;_sapp_tc.error_code=0;
}
int main(){
    _sapp_tc_window_t w;
    for(int mode : {1,2}) {
        reset(w);recoveryFailure=mode;
        check("recovery allocation failure retains old current drawable",!_sapp_tc_x11_resize_surface(&w)&&oldLive&&w.drawable==11&&draw==11&&read==11&&context&&(syncs>0)&&oldDestroys==0);
        recoveryFailure=0;
        check("recovery allocation failure retries legally",_sapp_tc_x11_resize_surface(&w)&&windowLive&&draw==12&&!oldLive&&!pbufferLive&&!illegalCreates&&!invalidDestroys);
        reset(w);windowFailure=mode;
        {
            _sapp_tc_x11_surface_scope outer;
            check("zero or nonzero XID with synchronized error never succeeds",!_sapp_tc_x11_resize_surface(&w)&&_sapp_tc.error_code==11);
        }
        check("failed replacement retains current recovery context",!oldLive&&pbufferLive&&w.drawable_is_pbuffer&&w.drawable==20&&draw==20&&read==20&&context&&oldDestroys==1&&recoveryDestroys==0&&!illegalCreates&&!invalidDestroys);
        check("repeated failure preserves same recovery drawable",!_sapp_tc_x11_resize_surface(&w)&&w.drawable==20&&draw==20&&context&&recoveryDestroys==0);
        windowFailure=0;
        check("replacement retry restores current renewed window",_sapp_tc_x11_resize_surface(&w)&&windowLive&&!pbufferLive&&!w.drawable_is_pbuffer&&draw==12&&read==12&&context&&recoveryDestroys==1&&!illegalCreates&&!invalidDestroys);
        reset(w);windowFailure=mode;_sapp_tc_x11_resize_surface(&w);_sapp_tc_x11_destroy_surface(&w);
        check("failed recovery is released by teardown",!w.drawable&&!pbufferLive&&recoveryDestroys==1&&!invalidDestroys);
    }
    reset(w);bindFailure=true;
    check("recovery bind error preserves usable old drawable",!_sapp_tc_x11_resize_surface(&w)&&oldLive&&draw==11&&read==11&&context&&!pbufferLive&&oldDestroys==0&&!invalidDestroys);
    bindFailure=false;
    check("bind failure retries successfully",_sapp_tc_x11_resize_surface(&w)&&draw==12&&context&&!illegalCreates&&!invalidDestroys);
    // Keep all original sixteen pbuffer checks above, then exercise selection
    // and the same renewal protocol on a legal window-only configuration.
    windowOnly=true;_sapp_tc.app.desc.sample_count=4;
    check("window-only config startup succeeds",_sapp_tc_glx_choose_fbconfig()&&_sapp_tc.app.desc.sample_count==4);
    windowOnly=false;configCount=2;configTypes[0]=1;configTypes[1]=5;
    check("equally suitable pbuffer config preferred",_sapp_tc_glx_choose_fbconfig()&&_sapp_tc.fbconfig==1);
    configSamples[1]=8;_sapp_tc.app.desc.sample_count=4;
    check("better window-only config retains baseline score priority",_sapp_tc_glx_choose_fbconfig()&&_sapp_tc.fbconfig==0);
    configCount=1;configSamples[1]=4;configTypes[0]=5;windowOnly=true;
    for(int mode : {1,2}) {
        reset(w);helperFailure=mode;
        check("helper X error retains original drawable",!_sapp_tc_x11_resize_surface(&w)&&oldLive&&draw==11&&!oldDestroys&&!helperLive&&!invalidDestroys);
        helperFailure=0;
        check("helper allocation retries legally",_sapp_tc_x11_resize_surface(&w)&&draw==12&&!helperLive&&!helperGLXLive&&!invalidDestroys&&!illegalCreates);
        reset(w);recoveryFailure=mode;
        check("helper GLX error retains original and releases X window",!_sapp_tc_x11_resize_surface(&w)&&oldLive&&draw==11&&!helperLive&&!helperGLXLive&&helperDestroys==1&&!invalidDestroys);
        recoveryFailure=0;
        check("helper GLX allocation retries legally",_sapp_tc_x11_resize_surface(&w)&&draw==12&&!helperLive&&!helperGLXLive&&!invalidDestroys&&!illegalCreates);
        reset(w);windowFailure=mode;
        { _sapp_tc_x11_surface_scope outer;
          check("window-backed replacement synchronized error rejected",!_sapp_tc_x11_resize_surface(&w)&&_sapp_tc.error_code==11); }
        check("window-backed recovery remains usable",helperLive&&helperGLXLive&&w.recovery_xwin==29&&!w.drawable_is_pbuffer&&draw==30&&read==30&&context&&oldDestroys==1&&!pbufferLive);
        check("failed retries reuse helper",!_sapp_tc_x11_resize_surface(&w)&&draw==30&&helperCreates==1&&!helperDestroys&&!recoveryDestroys);
        windowFailure=0;
        check("helper retry restores renewed drawable and frees recovery",_sapp_tc_x11_resize_surface(&w)&&draw==12&&read==12&&context&&!helperLive&&!helperGLXLive&&!w.recovery_xwin&&helperDestroys==1&&recoveryDestroys==1&&!illegalCreates&&!invalidDestroys);
        reset(w);windowFailure=mode;_sapp_tc_x11_resize_surface(&w);_sapp_tc_x11_destroy_surface(&w);
        check("window-backed teardown releases recovery",!w.drawable&&!w.recovery_xwin&&!helperLive&&!helperGLXLive&&helperDestroys==1&&recoveryDestroys==1&&!invalidDestroys);
    }
    reset(w);bindFailure=true;
    check("helper bind error retains old and releases recovery",!_sapp_tc_x11_resize_surface(&w)&&oldLive&&draw==11&&read==11&&context&&!helperLive&&!helperGLXLive&&!oldDestroys&&!invalidDestroys);
    bindFailure=false;
    check("helper bind failure retries legally",_sapp_tc_x11_resize_surface(&w)&&draw==12&&!illegalCreates&&!invalidDestroys);
    for(bool only : {false,true}) {
        windowOnly=only;reset(w);bindAsyncError=true;
        check("successful bind with synchronized X error retains old drawable",!_sapp_tc_x11_resize_surface(&w)&&oldLive&&draw==11&&read==11&&context&&!pbufferLive&&!helperLive&&!helperGLXLive&&!oldDestroys&&!illegalCreates&&!invalidDestroys);
        bindAsyncError=false;
        check("synchronized bind error retries legally",_sapp_tc_x11_resize_surface(&w)&&draw==12&&!illegalCreates&&!invalidDestroys);
    }
    printf("glx-renewal: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
'''
with tempfile.TemporaryDirectory(prefix='tc-glx-renewal-', dir=os.environ.get('TMPDIR')) as tmp:
    p=Path(tmp);(p/'probe.cpp').write_text(code, encoding='utf-8')
    subprocess.run(['c++','-std=c++20',str(p/'probe.cpp'),'-o',str(p/'probe')],check=True)
    subprocess.run([str(p/'probe')],check=True)
