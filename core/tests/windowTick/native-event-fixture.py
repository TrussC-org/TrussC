"""SDK-free production visibility and XI2 construction used by the linked converter probe."""
from pathlib import Path
import re


def body(source, marker):
    start = source.index(marker)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


def declaration(source, name):
    return re.search(r'^static [^;{}\n]*\b' + re.escape(name) + r'\([^;{}]*\);', source, re.M).group(0) + '\n'


def definition(source, name):
    marker = re.search(r'^static [^;{}\n]*\b' + re.escape(name) + r'\([^;{}]*\)\s*\{', source, re.M).group(0)
    return body(source, marker)


def generate(source):
    shared = (Path(__file__).resolve().parents[2] / 'include/sokol/util/sokol_window_native.h').read_text(encoding='utf-8')
    helpers = ''.join(body(shared, 'static inline bool ' + name + '(') for name in
                      ('_sapp_tc_native_iconified', '_sapp_tc_native_occluded', '_sapp_tc_native_hidden'))
    code = r'''
namespace nativeFixture {
struct NativeRecord {
    tcCoreTest::InjectedWindow& producer;
    unsigned win_id;
    bool iconified=false,occluded=false,trust_hidden_signals=true,draw_while_hidden=false,is_main=false;
    double earliest_next=0;
    explicit NativeRecord(tcCoreTest::InjectedWindow& n):producer(n),win_id(n.win_id){}
    void send(sapp_event_type type) {
        producer.metadata.iconified=iconified;producer.metadata.occluded=occluded;
        producer.metadata.trust_hidden_signals=trust_hidden_signals;
        producer.send(event(type));
    }
};
using _sapp_tc_native_window_t=NativeRecord;
''' + helpers + r'''
namespace mac {
constexpr unsigned NSWindowOcclusionStateVisible=1;
struct NSWindow {unsigned occlusionState=1;bool miniaturized=false;};
struct Record:NativeRecord {
    using NativeRecord::NativeRecord;
    NSWindow* window=nullptr;bool link_suspended=false,suspended_sent=false;
};
using _sapp_tc_window_t=Record;
Record* live=nullptr;
Record* _sapp_tc_lookup(sapp_window h){return live&&live->win_id==h.id?live:nullptr;}
void _sapp_tc_send_type(Record* w,sapp_event_type type){w->send(type);}
void _sapp_tc_mac_select_source(Record*){}
''' + declaration(source, '_sapp_tc_mac_observe') + body(source, 'static _sapp_tc_window_t* _sapp_tc_mac_live(') + body(source, 'static void _sapp_tc_mac_update_suspension(')
    observation = definition(source, '_sapp_tc_mac_observe')
    code += observation.replace('w->window.occlusionState', 'w->window->occlusionState').replace('w->window.miniaturized', 'w->window->miniaturized')
    for name, cpp in [('Miniaturize', 'minimize'), ('Deminiaturize', 'restore')]:
        marker = '- (void)windowDid' + name + ':(NSNotification*)n'
        method = body(source, marker).replace(marker, 'void ' + cpp + '(Record* self, void* n)')
        code += method.replace('_sapp_tc_win_delegate* keep = self;', 'Record* keep = self;').replace('keep.w', 'keep')
    code += r'''
void observe(Record& w,bool iconic,bool covered) {
    w.window->miniaturized=iconic;w.window->occlusionState=covered?0:1;
    _sapp_tc_mac_observe(&w);
}
}
namespace win {
using Record=NativeRecord;using _sapp_tc_window_t=Record;
Record* live=nullptr;
Record* _sapp_tc_lookup(sapp_window h){return live&&live->win_id==h.id?live:nullptr;}
void _sapp_tc_win32_app_event(Record* w,sapp_event_type type){w->send(type);}
''' + body(source, 'static void _sapp_tc_win32_observe_occlusion(') + r'''
constexpr int WM_SIZE=1,SIZE_MINIMIZED=1;
int minimize(Record* w,bool minimized){int wParam=minimized?SIZE_MINIMIZED:0;switch(WM_SIZE){
''' + body(source, 'case WM_SIZE: {') + r'''}return 0;}
}
namespace x11 {
using Record=NativeRecord;using _sapp_tc_window_t=Record;
Record* live=nullptr;
Record* _sapp_tc_lookup(sapp_window h){return live&&live->win_id==h.id?live:nullptr;}
void _sapp_tc_x11_app_event(Record* w,sapp_event_type type){w->send(type);}
constexpr int PropertyNewValue=1,IconicState=1,NormalState=2,VisibilityUnobscured=0,VisibilityPartiallyObscured=1,VisibilityFullyObscured=2;
struct Event{struct{int state=PropertyNewValue,atom=1;}xproperty;struct{int state;}xvisibility;};
struct{int WM_STATE=1;}_sapp_tc;
int state=NormalState;
int _sapp_tc_x11_get_window_state(Record*){return state;}
void minimize(Record* w,bool iconic){state=iconic?IconicState:NormalState;Event input{};auto* ev=&input;
''' + body(source, 'if (w && w->trust_hidden_signals && (ev->xproperty.state == PropertyNewValue) &&') + r'''}
void cover(Record* w,bool covered){Event input{};input.xvisibility.state=covered?VisibilityFullyObscured:VisibilityUnobscured;auto* ev=&input;
''' + body(source, 'if (w && w->trust_hidden_signals && ev->xvisibility.state >= VisibilityUnobscured') + r'''}
}
namespace xi2 {
// Xlib stubs supply a raw cookie. Parsing, packet construction, lock stamping,
// native delivery gate and host conversion are production code.
using Window=unsigned long;
struct Record:tcCoreTest::InjectedWindow {
    using tcCoreTest::InjectedWindow::InjectedWindow;
    float mouse_x=20,mouse_y=30,mouse_dx=0,mouse_dy=0;Window xwin=1;
    unsigned frame_count=0;
};
using _sapp_tc_window_t=Record;
struct {bool mouse_locked=true;Record* lock_owner=nullptr;int display=0;
    struct{bool available=true;int major_opcode=55;}xi;struct{Record* main=nullptr;}app;
}_sapp_tc;
Record* _sapp_tc_lookup(sapp_window h){auto* w=_sapp_tc.lock_owner;return w&&h.id==w->win_id?w:nullptr;}
constexpr int GenericEvent=1,XI_RawMotion=17;
struct XIRawEvent{struct{int mask_len=1;unsigned char* mask;}valuators;double* raw_values;};
struct XEvent{int type=GenericEvent;struct{int extension=55,evtype=XI_RawMotion;void* data=nullptr;}xcookie;};
int queries=0,freed=0;
bool XGetEventData(int,void*){return true;}void XFreeEventData(int,void*){++freed;}
void XQueryPointer(int,Window,Window*,Window*,int*,int*,int*,int*,unsigned* state){++queries;*state=0;}
bool XIMaskIsSet(unsigned char* mask,int bit){return *mask&(1u<<bit);}
using std::isfinite;
uint32_t _sapp_tc_x11_mods(unsigned){return 0;}
#define SOKOL_GLES3 1
''' + body(source[source.rindex('/*-- event plumbing ---'):], 'static void _sapp_tc_send(') + '\n#undef SOKOL_GLES3\n'
    code += body(source, 'static void _sapp_tc_x11_mouse_event(')
    code += 'void process(XEvent* ev){\n' + body(source[source.index('static void _sapp_tc_x11_process_event(XEvent* ev) {'):], 'if (ev->type == GenericEvent) {') + '}\n'
    code += r'''
void motion(Record& w,double dx,double dy){
    _sapp_tc.lock_owner=&w;_sapp_tc.app.main=&w;_sapp_tc.mouse_locked=true;
    unsigned char mask=3;double values[]={dx,dy};XIRawEvent raw{{1,&mask},values};XEvent e;e.xcookie.data=&raw;process(&e);
}
}
}
'''
    return code
