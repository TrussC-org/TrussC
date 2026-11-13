#!/usr/bin/env python3
"""Execute production event gates, injection and drawable cancellation with SDK stubs."""
from pathlib import Path
import os, subprocess, tempfile
from test_web_window_controls import body
ROOT=Path(__file__).resolve().parents[2]
source=(ROOT/'core/include/sokol/sokol_app_tc.h').read_text(encoding='utf-8')
global_source=(ROOT/'core/include/tc/app/tcGlobal.cpp').read_text(encoding='utf-8')
shared=(ROOT/'core/include/sokol/util/sokol_window_event.h').read_text(encoding='utf-8').replace('#pragma once','')
code=r'''
#include "sokol_app_tc.h"
#include <cstdio>
#include <stdexcept>
#include <limits>
int checks=0, failures=0;
void check(const char* name,bool ok){++checks;if(!ok){++failures;printf("event-gates: FAIL: %s\n",name);}}
'''+shared+r'''
namespace native {
struct Record {
    uint32_t win_id=91; sapp_window_desc desc{};
    uint64_t frame_count=9,close_recorded_frame=0; bool ready=true,in_tick=false,close_requested=false,close_recorded=false,is_main=true;
    int link=0,callback_depth=0; bool native_closed=false,closed_pending=false;
    bool input_delivered=false,wake_on_input=true;bool (*events_enabled)()=nullptr;
    int win_width=80,win_height=40,fb_width=160,fb_height=80;float dpi_scale=2;
} record;
Record first;
using _sapp_tc_window_t=Record;
Record* live=&record;
constexpr int _SAPP_TC_MAX_WINDOWS=1;
'''+body(source,'struct _sapp_tc_mac_pending_key {')+r''';
struct { bool mouse_locked=false;Record* lock_owner=nullptr; Record* windows[1]={&record};
    _sapp_tc_mac_pending_key pending_keys[_SAPP_TC_MAX_WINDOWS]{}; int pending_key_count=0;
    int callbacks_active=0,constructing=0; bool deferred_blocked=false;
    struct {Record* main=&first; struct { bool (*_tc_callbacks_active)()=nullptr; } desc;}app;} _sapp_tc;
void _sapp_tc_mac_contained(const char*) {}
void _sapp_tc_mac_schedule_deferred() {}
Record* _sapp_tc_lookup(sapp_window h){return live&&h.id==live->win_id?live:nullptr;}
'''
import re
send=next(re.finditer(r'static void _sapp_tc_send\(_sapp_tc_window_t\* w, sapp_event\* ev\) \{',source))
code+=body(source,'struct _sapp_tc_mac_callback_scope {')+';\n'+body(source,'static bool _sapp_tc_mac_busy(')+'\n'
code+=body(source[send.start():],send.group())+'\n'
code+='#define __APPLE__ 1\n#define TARGET_OS_OSX 1\n'
for marker in ['extern "C" bool sapp_window_inject_event(', 'extern "C" bool sapp_tc_window_close_recorded(']:
    code+=body(source,marker).replace('extern "C" ','')+'\n'
code+=body(source,'static void _sapp_tc_mac_forget_key(')+'\n'
closed=(body(source,'static void _sapp_tc_mac_closed_now(')+body(source,'static void _sapp_tc_mac_closed(')).replace('[NSApp terminate:nil]','terminate()').replace('[w->link invalidate]','invalidate(w->link)').replace('= nil','= 0')
code+='void terminate(); void invalidate(int); void _sapp_tc_mac_stop_fallback(Record*);\n'+closed+'\n'
code+=r'''
int terminated=0,closedCallbacks=0;
void terminate(){++terminated;}void invalidate(int){}void _sapp_tc_mac_stop_fallback(Record*){}
int calls=0;bool throwCallback=false,removeRecord=false;
sapp_event last{};
void callback(const sapp_event* e,sapp_window h,void*){
    ++calls;last=*e;
    check("native busy state precedes callback",record.in_tick);
    check("wake flag precedes callback",!_sapp_tc_event_wakes(e->type)||record.input_delivered);
    check("stamped metadata targets receiver and preserves first-window frame count",h.id==91&&e->frame_count==77&&e->framebuffer_width==160);
    if(removeRecord)live=nullptr;
    if(throwCallback)throw 1015;
}
bool disabled(){return false;}
void run(){
    first.frame_count=77;first.win_id=90;
    record.desc.event_cb=callback;
    for(auto type:{_SAPP_EVENTTYPE_NUM,_SAPP_EVENTTYPE_FORCE_U32}) {
        sapp_event sentinel{};sentinel.type=type;
        check("sentinel event cannot wake",!_sapp_tc_event_wakes(type));
        check("sentinel event cannot be injected",!_sapp_tc_valid_injected_event(sentinel));
    }
    record.desc.close_cb=[](sapp_window,void*){++closedCallbacks;};
    _sapp_tc_mac_closed(&record);
    check("direct main close records frame cancellation without a queued close",record.close_recorded&&!record.close_requested&&!record.ready&&terminated==0&&closedCallbacks==1&&sapp_tc_window_close_recorded({91}));
    sapp_event closedInput{};closedInput.type=SAPP_EVENTTYPE_MOUSE_MOVE;
    check("physically closed target rejects input in its closing frame",!sapp_window_inject_event({91},&closedInput));
    ++record.frame_count;check("native close persists beyond its recorded frame",sapp_tc_window_close_recorded({91}));--record.frame_count;
    record.native_closed=false; record.ready=true; // Isolate the shared frame marker contract too.
    check("shared physical close marker rejects closing-frame input",!sapp_window_inject_event({91},&closedInput));
    ++record.frame_count;check("physical close marker is limited to its native frame",!sapp_tc_window_close_recorded({91}));--record.frame_count;
    record.close_recorded=false;record.is_main=false;record.desc.close_cb=[](sapp_window,void*){++closedCallbacks;};
    _sapp_tc_mac_closed(&record);
    check("direct secondary close stops admission and reports once",record.close_recorded&&!record.ready&&!record.close_requested&&closedCallbacks==2);
    record.close_recorded=false;record.native_closed=false;record.ready=true;record.is_main=false;
    sapp_event e{};e.type=SAPP_EVENTTYPE_MOUSE_MOVE;e.mouse_x=40;e.mouse_y=20;
    _sapp_tc_send(&record,&e);check("real source is zero",last.source==SAPP_EVENTSOURCE_REAL&&!record.in_tick);
    e.frame_count=999;
    check("injection accepted",sapp_window_inject_event({91},&e));
    check("injection copies const input and marks source",last.source==SAPP_EVENTSOURCE_INJECTED&&e.frame_count==999);
    e.source=SAPP_EVENTSOURCE_REPLAYED;sapp_window_inject_event({91},&e);check("replay source preserved",last.source==SAPP_EVENTSOURCE_REPLAYED);
    int before=calls;
    check("stale and null targets rejected",!sapp_window_inject_event({92},&e)&&!sapp_window_inject_event({0},&e)&&!sapp_window_inject_event({91},nullptr)&&calls==before);
    e.mouse_x=std::numeric_limits<float>::quiet_NaN();check("NaN rejected",!sapp_window_inject_event({91},&e)&&calls==before);e.mouse_x=0;
    e.mouse_dx=std::numeric_limits<float>::infinity();check("infinite delta rejected",!sapp_window_inject_event({91},&e)&&calls==before);e.mouse_dx=0;
    e.type=SAPP_EVENTTYPE_TOUCHES_MOVED;e.num_touches=33;check("oversized touches rejected",!sapp_window_inject_event({91},&e));e.num_touches=-1;check("negative touches rejected",!sapp_window_inject_event({91},&e));
    e.num_touches=1;e.touches[0].pos_y=std::numeric_limits<float>::infinity();check("nonfinite touch rejected",!sapp_window_inject_event({91},&e));
    e.touches[0].pos_y=0;e.mouse_dx=std::numeric_limits<float>::infinity();check("nonfinite locked touch movement rejected",!sapp_window_inject_event({91},&e));
    e={};e.type=SAPP_EVENTTYPE_CHAR;
    for(uint32_t c:{0xd800u,0xdfffu,0x110000u}){e.char_code=c;check("invalid Unicode rejected",!sapp_window_inject_event({91},&e));}
    e.char_code=0;check("explicit NUL text remains a valid Unicode scalar",sapp_window_inject_event({91},&e)&&last.char_code==0);
    e.char_code=0x1f600;check("explicit Unicode text accepted",sapp_window_inject_event({91},&e)&&last.char_code==0x1f600);
    for(auto type:{SAPP_EVENTTYPE_ICONIFIED,SAPP_EVENTTYPE_SUSPENDED,SAPP_EVENTTYPE_RESUMED,SAPP_EVENTTYPE_FILES_DROPPED,SAPP_EVENTTYPE_CLIPBOARD_PASTED,SAPP_EVENTTYPE_QUIT_REQUESTED,SAPP_EVENTTYPE_TC_DEVICE_LOST}){e.type=type;check("injection cannot forge native observations or borrowed payload",!sapp_window_inject_event({91},&e));}
    e={};e.type=SAPP_EVENTTYPE_KEY_DOWN;int validKeys=0;
    for(int key=0;key<SAPP_MAX_KEYCODES;++key){e.key_code=static_cast<sapp_keycode>(key);validKeys+=_sapp_tc_valid_injected_event(e);}
    check("native injection key domain retains exactly 120 defined codes",validKeys==120);
    e={};e.type=SAPP_EVENTTYPE_MOUSE_DOWN;e.mouse_button=SAPP_MOUSEBUTTON_INVALID;check("invalid button rejected",!sapp_window_inject_event({91},&e));
    e.type=SAPP_EVENTTYPE_MOUSE_MOVE;
    record.ready=false;check("pre-ready injection rejected",!sapp_window_inject_event({91},&e));record.ready=true;
    record.events_enabled=disabled;check("client admission preserved",!sapp_window_inject_event({91},&e));record.events_enabled=nullptr;
    record.close_requested=true;check("pending close preserves admitted input until native unpublication",sapp_tc_window_close_recorded({91})&&sapp_window_inject_event({91},&e));record.close_requested=false;
    check("live target not closing",!sapp_tc_window_close_recorded({91}));check("stale target query fails closed",sapp_tc_window_close_recorded({999}));
    throwCallback=true;record.in_tick=true;sapp_window_inject_event({91},&e);check("throwing native callback restores outer busy state",record.in_tick);throwCallback=false;record.in_tick=false;
    removeRecord=true;_sapp_tc_send(&record,&e);check("destroyed receiver not touched on unwind",live==nullptr);
}
}
namespace presentation {
struct Target {
    bool inPass=false,resumable=true,passStartedThisFrame=true;
    unsigned ownerId=3;struct {unsigned generation=4;}resource;
    auto& resources(){return resource;}void flushLayers(){}void endPass(){inPass=false;}
};
struct Events;
struct HostWindow { bool isVisible() const { return true; } };
struct Window {HostWindow* hostWindow=nullptr;bool drawWhileHidden=false;Events* coreEvents=nullptr;Target swapchainTarget;unsigned renderEpoch=1;bool framePending=true;
    struct Stack {size_t size(){return 1;}}targetStack;} window;
struct Event {int listenerCount(){return 1;}void notify();};
struct Events {Event onRender;} hub;
Events& events(){return hub;}
bool closed=false,authorized=false;int cancels=0,commits=0;
struct Logger {template<class T>Logger& operator<<(T){return *this;}};
Logger logWarning(const char*){return {};}
void Event::notify(){closed=true;}
void sg_tc_mtl_cancel_drawables(unsigned){++cancels;authorized=false;}
bool sg_tc_mtl_authorize_drawable(unsigned,unsigned,unsigned){authorized=true;return true;}
void sgl_tc_reset_matrix_stacks(){}
namespace headless{bool isActive(){return false;}}
namespace internal {
Window& currentWindowContext(){return window;}void ensureTargetStack(Window&){}bool ensureCurrentPass(){window.swapchainTarget.inPass=true;return true;}
void popTarget(...){}bool currentWindowCloseRecorded(){return closed;}bool finishWindowCaptureSource(Target&){return true;}
void endGpuFrame(){++commits;check("acquired drawable not authorized after onRender close",!authorized);}
struct Context{void resetStacksAtFrameEnd(){}}context;
Context& getDefaultContext(){return context;}
}
'''
# Unreachable scope cleanup branches still need a compilable stack entry.
code=code.replace('struct Stack {size_t size(){return 1;}}targetStack;', 'struct Stack {struct Entry {Target* target=nullptr;unsigned token=0;};size_t size(){return 1;}Entry back(){return {};}}targetStack;')
code+=body(global_source,'void present()')+r'''
void run(){window.coreEvents=&hub;present();check("production present cancels drawable after onRender close",closed&&cancels==1&&!authorized&&commits==1&&!window.framePending);}
}
int main(){native::run();presentation::run();printf("event-gates: %d checks, %d failures\n",checks,failures);return failures?1:0;}
'''
code=code.replace('sapp_window_inject_event(', 'inject(').replace('sapp_tc_window_close_recorded(', 'closeRecorded(')
# The removed stamp assertions now exercise the production metadata query.
metadata_query=body(source,'extern "C" bool sapp_tc_window_read_metadata(').replace('extern "C" ','').replace('sapp_tc_window_read_metadata(', 'readMetadata(').replace('sapp_tc_window_visible(', 'visible(').replace('sapp_window_occluded(', 'occluded(')
metadata_parts=[]
for platform,defines in [
    ('mac','#define __APPLE__ 1\n#define TARGET_OS_OSX 1'),
    ('win','#define _WIN32 1'),
    ('glx','#define __linux__ 1'),
    ('egl','#define __linux__ 1\n#define SOKOL_GLES3 1'),
    ('ios','#define __APPLE__ 1\n#define TARGET_OS_IPHONE 1'),
    ('android','#define __ANDROID__ 1'),
    ('web','#define __EMSCRIPTEN__ 1')]:
    desktop=platform in ('mac','win','glx','egl')
    metadata_parts.append('''
#undef __APPLE__
#undef TARGET_OS_OSX
#undef TARGET_OS_IPHONE
#undef __linux__
#undef _WIN32
#undef __EMSCRIPTEN__
#undef __ANDROID__
#undef SOKOL_GLES3
'''+defines+'\nnamespace metadata_'+platform+r''' {
struct Record {
    int fb_width=302,fb_height=152,win_width=201,win_height=101;
    float dpi_scale=1.5f;
    sapp_window_desc desc{};
    sapp_pixel_format color_format=SAPP_PIXELFORMAT_RGBA8,depth_format=SAPP_PIXELFORMAT_DEPTH;
    bool trust_hidden_signals=true,iconified=false,occluded=false;
    double frame_duration=.01,refresh_period=.02;
} record,other;
struct {
    bool valid=true,cleanup_called=false,mouse_locked=true;
    Record* lock_owner=&record;
    int framebuffer_width=302,framebuffer_height=152,window_width=201,window_height=101;
    float dpi_scale=1.5f;
    struct {bool locked=true;} mouse;
} _sapp_tc;
Record* _sapp_tc_lookup(sapp_window h){return h.id==41?&record:nullptr;}
bool visible(sapp_window){return true;}
bool _sapp_tc_native_hidden(const Record*){return false;}
int sapp_sample_count(){return 4;}
sapp_pixel_format sapp_color_format(){return SAPP_PIXELFORMAT_RGBA8;}
sapp_pixel_format sapp_depth_format(){return SAPP_PIXELFORMAT_DEPTH;}
bool occluded(sapp_window){return false;}
double sapp_frame_duration(){return .01;}
'''+metadata_query+r'''
void run(){
    sapp_tc_window_metadata metadata{};
    record.desc.sample_count=4;
'''+f'    const sapp_window handle{{{41 if desktop else 0}}};\n'+r'''
    check("metadata query rejects null output",!readMetadata(handle,nullptr));
    check("metadata query accepts target handle",readMetadata(handle,&metadata));
    check("target metadata retains fractional DPI and dimensions",metadata.dpi_scale==1.5f&&metadata.fb_width==302&&metadata.fb_height==152&&metadata.width==201&&metadata.height==101);
    check("target metadata retains formats and source interval",metadata.sample_count==4&&metadata.color_format==SAPP_PIXELFORMAT_RGBA8&&metadata.depth_format==SAPP_PIXELFORMAT_DEPTH&&metadata.source_interval==.01);
'''+f'    check("platform lock and button reliability",metadata.pointer_locked=={str(platform in ("egl","web")).lower()}&&metadata.reliable_button_bits=={str(platform in ("win","glx","egl","web")).lower()});\n'+(r'''
    _sapp_tc.lock_owner=&other;
    check("another window's lock is not the target's lock",readMetadata(handle,&metadata)&&!metadata.pointer_locked&&metadata.reliable_button_bits);
''' if platform=='egl' else '')+(r'''
    _sapp_tc.mouse.locked=false;
    check("Web unlock updates real metadata",readMetadata(handle,&metadata)&&!metadata.pointer_locked&&metadata.reliable_button_bits);
''' if platform=='web' else '')+r'''
    check("metadata rejects stale target handles",!readMetadata({999},&metadata));
'''+(r'''
    _sapp_tc.valid=false;check("single-window metadata rejects absent native state",!readMetadata(handle,&metadata));
    _sapp_tc.valid=true;_sapp_tc.cleanup_called=true;check("single-window metadata rejects retired state",!readMetadata(handle,&metadata));
''' if not desktop else '')+'}\n}\n')
code=code.replace('int main(){native::run();',''.join(metadata_parts)+'\nint main(){'+''.join('metadata_'+p+'::run();' for p in ('mac','win','glx','egl','ios','android','web'))+'native::run();')

variants=[('baseline',code)]
if os.environ.get('TC_EVENT_GATE_MUTATIONS'):
    edits=[
        ('target_identity','w->desc.event_cb(event, {id}, w->desc.user_data);','w->desc.event_cb(event, {0}, w->desc.user_data);'),
        ('target_frame_as_process_frame','_sapp_tc.app.main ? _sapp_tc.app.main->frame_count : 0','w->frame_count'),
        ('legacy_frame_count','event->frame_count = frame;','event->frame_count = 0;'),
        ('dpi_metadata','out->dpi_scale = w->dpi_scale;','out->dpi_scale = 1;'),
        ('target_lock_ownership','_sapp_tc.mouse_locked && _sapp_tc.lock_owner == w;','_sapp_tc.mouse_locked;'),
        ('web_lock_metadata','out->pointer_locked = _sapp_tc.mouse.locked;','out->pointer_locked = false;'),
        ('single_window_dpi','out->dpi_scale = _sapp_tc.dpi_scale;','out->dpi_scale = 1;'),
        ('wake_before_callback','if (_sapp_tc_event_wakes(event->type)) w->input_delivered = w->wake_on_input;','/* omit input wake */'),
        ('sentinel_wake','case _SAPP_EVENTTYPE_NUM: case _SAPP_EVENTTYPE_FORCE_U32: return false;','case _SAPP_EVENTTYPE_NUM: case _SAPP_EVENTTYPE_FORCE_U32: return true;'),
        ('injected_source','event.source = input->source == SAPP_EVENTSOURCE_REPLAYED ? SAPP_EVENTSOURCE_REPLAYED : SAPP_EVENTSOURCE_INJECTED;','event.source = SAPP_EVENTSOURCE_REAL;'),
        ('touch_relative_validation','if (!std::isfinite(e.mouse_dx) || !std::isfinite(e.mouse_dy)) return false;','/* omit touch relative validation */'),
        ('input_validation','!_sapp_tc_valid_injected_event(*input)','false'),
        ('const_copy','sapp_event event = *input;','sapp_event& event = *const_cast<sapp_event*>(input);'),
        ('native_direct_close_record','w->close_recorded = true;','/* omit physical close observation */'),
        ('close_epoch','(w->close_recorded && w->close_recorded_frame == w->frame_count)','w->close_recorded'),
        ('input_closing_frame','        (w->close_recorded && w->close_recorded_frame == w->frame_count) ||','        false ||'),
        ('close_query','return !w || !w->ready || w->close_requested ||\n        (w->close_recorded && w->close_recorded_frame == w->frame_count);','return false;'),
        ('no_present_closed_drawable','const bool closing = internal::currentWindowCloseRecorded();','const bool closing = false;'),
    ]
    for name,old,new in edits:
        assert old in code,name
        variants.append((name,code.replace(old,new)))
results=[]
with tempfile.TemporaryDirectory(prefix='tc-event-gates-') as folder:
    for name,text in variants:
        cpp=Path(folder)/'probe.cpp';binary=Path(folder)/'probe';cpp.write_text(text,encoding='utf-8')
        subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-I'+str(ROOT/'core/include/sokol'),str(cpp),'-o',str(binary)],check=True)
        r=subprocess.run([str(binary)],capture_output=True,text=True,encoding="utf-8",timeout=15);print(name,r.stdout,flush=True)
        failed=r.stdout.count('event-gates: FAIL:');valid=(r.returncode==0 and not failed) if name=='baseline' else (r.returncode==1 and failed>0)
        results.append(dict(name=name,returncode=r.returncode,failed_assertions=failed,valid=valid))
if out:=os.environ.get('TC_EVENT_GATE_RESULTS'):
    import json
    Path(out).write_text(json.dumps(results,indent=2),encoding='utf-8')
raise SystemExit(any(not x['valid'] for x in results))
