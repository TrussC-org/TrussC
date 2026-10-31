#!/usr/bin/env python3
"""Execute desktop launch descriptor/transfer/publication/send bodies with native stubs.
Show notifications are synchronous. This is an SDK-free control-flow check.
Run through tcbuild.sh; optional mutants must compile and fail runtime assertions.
"""
from pathlib import Path
import json
import os
import re
import subprocess
import tempfile
from test_web_window_controls import body

ROOT = Path(__file__).resolve().parents[2]
NATIVE = Path(os.environ.get('TC_BOOTSTRAP_NATIVE', ROOT/'core/include/sokol/sokol_app_tc.h')).read_text(encoding='utf-8')
parts = []
for index, platform in enumerate(('mac', 'win32', 'x11')):
    start = NATIVE.index('static bool _sapp_tc_app_events_enabled(', 0 if index == 0 else start + 1)
    # Select this platform's send definition, before its readiness predicate.
    send_start = NATIVE.rfind('\nstatic ', 0, NATIVE.rindex('_sapp_tc_send(_sapp_tc_window_t* w, sapp_event* ev) {', 0, start)) + 1
    send = body(NATIVE[send_start:start], NATIVE[send_start:NATIVE.index('{', send_start)])
    # body() selects the last match, so bound it to this platform first.
    predicate = body(NATIVE[start:NATIVE.index('static void _sapp_tc_main_event_tramp', start)], 'static bool _sapp_tc_app_events_enabled(')
    main = body(NATIVE, f'static bool _sapp_tc_{platform}_create_main_window(')
    marker = f'static _sapp_tc_window_t* _sapp_tc_{platform}_create_window' + ('_ex(' if platform == 'mac' else '(')
    constructor = body(NATIVE, marker)
    # Execute production descriptor transfer through swap interval, and the entire
    # publication/show suffix. OS allocation/drawable creation is the stubbed prefix.
    transfer_start = constructor.index('    w->desc = d->window;')
    transfer_end = constructor.index('    w->swap_interval = d->swap_interval;', transfer_start) + len('    w->swap_interval = d->swap_interval;')
    transfer = constructor[transfer_start:transfer_end]
    suffix = constructor[constructor.index('    _sapp_tc_creation_transaction creation('):]
    if platform == 'mac':
        seams = {
            '[w->link addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes]': 'add_link()',
            '[window makeKeyAndOrderFront:nil]': 'show()',
            '[window orderOut:nil]': 'order_out()',
            'NSWindow* window = w->window;': 'int window=0;',
            '[w->view registerForDraggedTypes:@[NSPasteboardTypeFileURL]]': 'register_drop()',
            '(__bridge const void*)_sapp_tc.app.device': '_sapp_tc.app.device',
        }
        for old, new in seams.items():
            assert old in suffix + main, old
            suffix = suffix.replace(old, new)
            main = main.replace(old, new)
    ctor_name = f'_sapp_tc_{platform}_create_window' + ('_ex' if platform == 'mac' else '')
    args = 'const _sapp_tc_native_desc* d' + (', const _sapp_tc_mac_window_attrs*' if platform == 'mac' else '')
    parts.append(f'namespace {platform} {{\n' + r'''
using Client=void(*)(const sapp_event*,sapp_window,void*);
using Tick=void(*)(sapp_window,void*);
struct sapp_window_desc { Tick close_cb=nullptr; bool(*close_request_cb)(sapp_window,void*)=nullptr; Client event_cb=nullptr; Tick tick_cb=nullptr; void* user_data=nullptr; bool(*bind_cb)(uint32_t,void*)=nullptr; const void* mtl_device=nullptr; };
struct sapp_desc { Tick _tc_close_cb=nullptr; bool(*_tc_close_request_cb)(sapp_window,void*)=nullptr; int logger=0; Client _tc_event_cb=nullptr; Tick _tc_tick_cb=nullptr; void(*event_cb)(const sapp_event*)=nullptr; void(*event_userdata_cb)(const sapp_event*,void*)=nullptr; int swap_interval=1; bool fullscreen=false; int _tc_draw_while_hidden=1; const char* window_title=nullptr; };
struct _sapp_tc_native_desc { sapp_window_desc window; bool(*events_enabled)()=nullptr; int swap_interval=0; bool fullscreen=false,draw_while_hidden=false,center_on_create=false; int title_limit=0,color_format=0,depth_format=0; };
struct _sapp_tc_mac_window_attrs { bool restorable=false,nearest_magnification=false,take_key_focus_on_create=false; };
struct _sapp_tc_window_t { unsigned win_id=23; float dpi_scale=2; bool input_delivered=false,wake_on_input=true; sapp_window_desc desc; bool constructing=false, ready=false,in_tick=false,is_main=false,native_closed=false; int callback_depth=0; bool draw_while_hidden=false,trust_hidden_signals=true; int swap_interval=0; bool(*events_enabled)()=nullptr; uint64_t frame_count=7; int win_width=80,win_height=40,fb_width=160,fb_height=80; void* hwnd=nullptr; const char* title="test"; };
struct { size_t window_slots=1; int callbacks_active=0,constructing=0; bool deferred_blocked=false; _sapp_tc_window_t* windows[1]{}; uint32_t gl_framebuffer=0; struct { sapp_desc desc; bool init_called=false,valid=false; _sapp_tc_window_t* main=nullptr; void* device=nullptr; int logger=0; } app; const char* create_error=nullptr; } _sapp_tc;
_sapp_tc_window_t record;
_sapp_tc_window_t* _sapp_tc_lookup(sapp_window h) { auto* w=_sapp_tc.windows[0]; return w && w->win_id==h.id ? w : nullptr; }
constexpr int SAPP_PIXELFORMAT_RGB10A2=1,SAPP_PIXELFORMAT_DEPTH_STENCIL=2,SAPP_PIXELFORMAT_RGBA8=3,SW_SHOW=1,TRUE=1,SAPP_LOGITEM_OK=0,GL_FRAMEBUFFER_BINDING=1;
using GLint=int;
constexpr auto nil=nullptr;
void glGetIntegerv(int,int* out) { *out=0; }
void* MTLCreateSystemDefaultDevice() { return &record; }
void _sapp_tc_log(...) {}
void _sapp_tc_main_tick_tramp(sapp_window,void*) {}
int callbacks=0,bound=0;
bool graphics=false,app=false;
void client(const sapp_event* e,sapp_window h,void*) {
    ++callbacks;
    check("desktop listener admission uses native init flag, including init in progress",_sapp_tc.app.init_called);
    check("delivered target and metadata are bound before input",h.id==23 && bound==1 && e->framebuffer_width==160);
}
''' + predicate + '\n' + r'''
void _sapp_tc_main_event_tramp(const sapp_event* e,sapp_window h,void* u) { if (_sapp_tc_app_events_enabled()) client(e,h,u); }
bool bind(uint32_t id,void*) { ++bound; check("bind precedes show",id==23 && callbacks==0); return true; }
sapp_window_desc _sapp_tc_normalize_main_window(const sapp_desc*) { sapp_window_desc d; d.bind_cb=bind; return d; }
void _sapp_tc_win32_unwind(_sapp_tc_window_t*) {}
void _sapp_tc_x11_unwind(_sapp_tc_window_t*) {}
_sapp_tc_window_t* _sapp_tc_mac_fail(_sapp_tc_window_t*,const char*) { return nullptr; }
bool _sapp_tc_win32_trust_hidden_signals() { return true; }
void DragAcceptFiles(void*,int) {}
void add_link() {} void register_drop() {} void order_out() {}
void _sapp_tc_mac_contained(const char*) {}
void _sapp_tc_mac_schedule_deferred() {}
_sapp_tc_window_t* _sapp_tc_mac_live(unsigned id) { return _sapp_tc_lookup({id}); }
void _sapp_tc_request_fullscreen(_sapp_tc_window_t*,bool) {}
void _sapp_tc_mac_update_dimensions(_sapp_tc_window_t*) {}
void _sapp_tc_x11_update_dimensions(_sapp_tc_window_t*) {}
void _sapp_tc_x11_set_fullscreen(_sapp_tc_window_t*,bool) {}
bool _sapp_tc_x11_make_current(_sapp_tc_window_t*) { return true; }
void sapp_set_icon(...) {}
''' + (body(NATIVE,'struct _sapp_tc_mac_callback_scope {')+';\n' if platform=='mac' else '') + send + r'''
void show() {
    check("native dimensions and host binding ready at synchronous show",record.ready && bound==1);
    for (int type : {SAPP_EVENTTYPE_FOCUSED,SAPP_EVENTTYPE_RESIZED,SAPP_EVENTTYPE_KEY_DOWN,SAPP_EVENTTYPE_FILES_DROPPED}) {
        sapp_event e{}; e.type=static_cast<sapp_event_type>(type); _sapp_tc_send(&record,&e);
    }
}
void ShowWindow(void*,int) { show(); }
void _sapp_tc_x11_show_window(_sapp_tc_window_t*) { show(); }
''' + f'\n_sapp_tc_window_t* {ctor_name}({args}) {{\n' + r'''
    int slot=0; unsigned id=record.win_id;
    auto* w=&record;
''' + (body(constructor,'    struct constructing_scope {')+' constructing;\n' if platform=='mac' else '') + transfer + '\n' + suffix + '\n' + main + r'''
void run() {
    for (bool direct : {true,false}) {
        _sapp_tc={}; record={}; callbacks=bound=0; graphics=app=false;
        _sapp_tc.app.desc._tc_event_cb=direct ? client : nullptr;
        // Legacy callback presence enables the same production readiness predicate.
        _sapp_tc.app.desc.event_cb=[](const sapp_event*) {};
        check("launch creation succeeds",CREATE_MAIN());
        check("construction focus/resize/key/drop stay suppressed as with old trampoline",callbacks==0 && bound==1);
        _sapp_tc.app.init_called=true; // desktop sets this BEFORE entering init
        sapp_event during{}; during.type=SAPP_EVENTTYPE_RESIZED;
        _sapp_tc_send(&record,&during);
        check("desktop init-in-progress notification reaches host with App absent",callbacks==1 && !app);
        graphics=app=true;
        sapp_event e{}; e.type=SAPP_EVENTTYPE_KEY_DOWN;
        _sapp_tc_send(&record,&e);
        check("ready input reaches host before any first frame",callbacks==2 && record.frame_count==7);
    }
}
}
'''.replace('CREATE_MAIN()', f'_sapp_tc_{platform}_create_main_window()'))

# These ports retain sokol's opposite flag ordering: init_called becomes true
# AFTER init returns, so their native predicate suppresses init-time events.
for index, platform in enumerate(('web', 'ios', 'android')):
    starts = list(re.finditer(r'_SOKOL_PRIVATE void _sapp_tc_call_init\(void\) \{', NATIVE))
    predicates = list(re.finditer(r'_SOKOL_PRIVATE bool _sapp_tc_events_enabled\(void\) \{', NATIVE))
    init = body(NATIVE[starts[index].start():starts[index+1].start() if index<2 else len(NATIVE)], '_SOKOL_PRIVATE void _sapp_tc_call_init(')
    enabled = body(NATIVE[predicates[index].start():predicates[index+1].start() if index<2 else len(NATIVE)], '_SOKOL_PRIVATE bool _sapp_tc_events_enabled(')
    parts.append('namespace single_' + platform + ' {\n' + r'''
struct { bool constructing=false,init_called=false; struct {
    void (*init_cb)()=nullptr; void (*init_userdata_cb)(void*)=nullptr; void* user_data=nullptr;
    void (*event_cb)()=nullptr; void (*event_userdata_cb)()=nullptr;
} desc; } _sapp_tc;
''' + enabled.replace('_SOKOL_PRIVATE ', '') + '\n' + init.replace('_SOKOL_PRIVATE ', '') + r'''
void run() {
    _sapp_tc.desc.event_cb=[] {};
    check("single-window construction predicate suppresses notifications",!_sapp_tc_events_enabled());
    _sapp_tc.desc.init_cb=[] {
        check("single-window init still suppresses native notifications as base",!_sapp_tc.init_called && !_sapp_tc_events_enabled());
    };
    _sapp_tc_call_init();
    check("single-window post-init predicate admits notifications",_sapp_tc_events_enabled());
}
}
''')

prefix = r'''
#include <cstdio>
#include <cstdint>
#include <initializer_list>
int checks=0,failures=0;
void check(const char* label,bool ok) { ++checks; if (!ok) { ++failures; printf("bootstrap: FAIL: %s\n",label); } }
'''
prefix+='#include "sokol_app_tc.h"\n#include "util/sokol_window_creation.h"\n'
prefix+=(ROOT/'core/include/sokol/util/sokol_window_event.h').read_text(encoding='utf-8').replace('#pragma once','')+'\n'
source = prefix + ''.join(parts) + r'''
int main() { mac::run();win32::run();x11::run();single_web::run();single_ios::run();single_android::run(); printf("bootstrap: %d checks, %d failures\n",checks,failures); return failures?1:0; }
'''
variants = [('baseline', source)]
if os.environ.get('TC_BOOTSTRAP_MUTATIONS'):
    for index, part in enumerate(parts[:3]):
        for name, old in [('constructor', 'w->events_enabled = d->events_enabled;'), ('launch', 'native.events_enabled = _sapp_tc_app_events_enabled;')]:
            assert part.count(old) == 1
            variants.append((f'{index}_{name}', source.replace(part, part.replace(old, '/* mutation: readiness omitted */'))))
results = []
with tempfile.TemporaryDirectory(prefix='tc-bootstrap-') as folder:
    for name, code in variants:
        cpp=Path(folder)/'probe.cpp'; binary=Path(folder)/'probe'; cpp.write_text(code,encoding='utf-8')
        subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-I'+str(ROOT/'core/include/sokol'),str(cpp),'-o',str(binary)],check=True)
        result=subprocess.run([str(binary)],text=True,encoding='utf-8',stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=15)
        failed=result.stdout.count('bootstrap: FAIL:')
        print(name,result.stdout,flush=True)
        valid=(result.returncode==0 and not failed) if name=='baseline' else (result.returncode==1 and failed>0)
        results.append(dict(name=name,returncode=result.returncode,failed_assertions=failed,valid=valid))
if output := os.environ.get('TC_BOOTSTRAP_OUTPUT'):
    Path(output).write_text(json.dumps(results,indent=2),encoding='utf-8')
raise SystemExit(any(not x['valid'] for x in results))
