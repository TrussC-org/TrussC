// Native boundaries for the production functions in before.inc/current header.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <new>
#include <string>
#include <vector>
using std::size_t; using std::isfinite;
struct sapp_window { uint32_t id=0; };
using GLXDrawable=int; using EGLSurface=int;
struct sapp_window_desc {
    int width=64,height=64,swap_interval=0; bool hidden=false,borderless=false,no_high_dpi=false;
    const char* title="probe"; void* user_data=nullptr;
    bool (*bind_cb)(uint32_t,void*)=nullptr;
    void (*tick_cb)(sapp_window,void*)=nullptr;
};
struct _sapp_tc_native_desc {
    sapp_window_desc window; bool events_enabled=false,draw_while_hidden=false,fullscreen=false;
    int swap_interval=1,color_format=0,depth_format=0; size_t title_limit=0;
};
struct _sapp_tc_window_t {
    sapp_window_desc desc; bool events_enabled=false,draw_while_hidden=false,trust_hidden_signals=true;
    bool ready=false,constructing=false,in_tick=false,is_main=false,skip_present=false,last_tick_presented=false;
    bool close_requested=false,input_delivered=false,surface_resize_pending=false,fullscreen_requested=false,wake_on_input=false;
    bool iconified=false,occluded=false;
    uint32_t win_id=0; int drawable=0,swap_interval=0,requested_swap_interval=1,pacing_ticks=0;
    int frame_count=0,visibility_op=0,color_format=0,depth_format=0,fb_width=64,fb_height=64;
    float dpi_scale=1; size_t title_limit=0; char* title=nullptr;
    double earliest_next=0,last_present=0,frame_duration=0;
    struct { double last=0,smooth_dt=1.0/60; } timing;
};
struct {
    _sapp_tc_window_t* windows[4]{}; size_t window_slots=4; uint32_t next_id=0;
    double refresh_period=1.0/60; float dpi=96; int display=1,egl_display=1,ctx=1;
    sapp_window pacer;
    void (*SwapIntervalEXT)(int,int,int)=nullptr; int (*SwapIntervalMESA)(int)=nullptr;
    struct { bool quit_ordered=false,quit_requested=false;
        _sapp_tc_window_t* main=nullptr;
        struct { void (*_tc_before_turn)()=nullptr; void (*_tc_after_turn)()=nullptr;
            bool (*_tc_callbacks_active)()=nullptr; } desc;
    } app;
} _sapp_tc;
static double clockValue=10,refresh=1.0/60,work=0;
static int currentDrawable=0,turns=0,turnLimit=12,swaps[5]{},nativeIntervals[5]{};
static bool trace=false,fast=false;
static std::vector<int> order;
static std::vector<std::string> intervalCalls;
void record(const char* op,int target,int value=0) { if(trace) std::printf("%s %d %d\n",op,target,value); }
_sapp_tc_window_t* _sapp_tc_lookup(sapp_window h) {
    for(auto* w:_sapp_tc.windows) if(w && w->win_id==h.id) return w;
    return nullptr;
}
void nativeInterval(int,int drawable,int value) {
    nativeIntervals[drawable]=value;
    intervalCalls.push_back(std::to_string(drawable)+":"+std::to_string(value));
    record("interval",drawable,value);
}
int mesaInterval(int value) { nativeInterval(1,currentDrawable,value); return 0; }
int glXGetCurrentDrawable() { return currentDrawable; }
int glXGetCurrentContext() { return 1; }
int glXMakeCurrent(int,int drawable,int) { currentDrawable=drawable;record("current",drawable);return 1; }
int eglMakeCurrent(int,int drawable,int,int) { return glXMakeCurrent(1,drawable,1); }
int eglSwapInterval(int,int value) { nativeInterval(1,currentDrawable,value);return 1; }
void nativeSwap(int drawable) {
    record("swap",drawable,nativeIntervals[drawable]); ++swaps[drawable];
    clockValue+=fast?.00005:refresh*nativeIntervals[drawable];
}
void glXSwapBuffers(int,int drawable) { nativeSwap(drawable); }
int eglSwapBuffers(int,int drawable) { nativeSwap(drawable);return 1; }
constexpr int EGL_TRUE=1,True=1;
bool sapp_tc_gl_activate_idle() { return true; }
struct _sapp_tc_x11_surface_scope {
    int saved=currentDrawable;
    ~_sapp_tc_x11_surface_scope() { if(saved!=currentDrawable) glXMakeCurrent(1,saved,1); }
};
double _sapp_tc_now() { return clockValue; }
bool _sapp_tc_native_iconified(_sapp_tc_window_t* w) { return w->trust_hidden_signals && w->iconified; }
bool _sapp_tc_native_hidden(_sapp_tc_window_t* w) { return w->desc.hidden || (w->trust_hidden_signals && (w->iconified||w->occluded)); }
void _sapp_tc_x11_init_client() {}
void _sapp_tc_timing_update(decltype(_sapp_tc_window_t::timing)* t) { t->last=clockValue; }
bool _sapp_tc_x11_update_dimensions(_sapp_tc_window_t*) { return false; }
bool _sapp_tc_x11_resize_surface(_sapp_tc_window_t*) { return true; }
void sapp_destroy_window(sapp_window h) { if(_sapp_tc.pacer.id==h.id) _sapp_tc.pacer={}; for(auto*& w:_sapp_tc.windows) if(w && w->win_id==h.id) w=nullptr; }
constexpr int SAPP_EVENTTYPE_RESIZED=1,SAPP_EVENTTYPE_TC_RESIZE_REDRAW_REQUESTED=2,_SAPP_TC_X11_VISIBILITY_NONE=0,POLLIN=1;
void _sapp_tc_x11_app_event(_sapp_tc_window_t*,int) {}
void _sapp_tc_x11_sync_visibility(_sapp_tc_window_t*) {}
void _sapp_tc_x11_close(_sapp_tc_window_t*) {}
void _sapp_tc_x11_request_exit() { _sapp_tc.app.quit_ordered=true; }
bool sapp_tc_gl_ensure_context() { return true; }
struct XEvent {}; struct pollfd { int fd,events,revents; };
int XPending(int) { return 0; } void XFlush(int) {} void XNextEvent(int,XEvent*) {}
void _sapp_tc_x11_process_event(XEvent*) {} int ConnectionNumber(int) { return 0; }
int poll(pollfd*,int,int ms) { clockValue+=ms*.001; return 0; }
void _sapp_tc_x11_wait_until(double deadline) { clockValue=deadline; }
size_t _sapp_tc_x11_reserve_slot() { for(size_t i=0;i<4;++i) if(!_sapp_tc.windows[i]) return i; return SIZE_MAX; }
void* _sapp_tc_x11_alloc(size_t bytes,bool) { return std::calloc(1,bytes); }
void _sapp_tc_x11_unwind(_sapp_tc_window_t*) {}
bool _sapp_tc_next_window_id(uint32_t* counter,uint32_t* id) { *id=++*counter;return true; }
bool _sapp_tc_x11_trust_hidden_signals() { return true; }
bool _sapp_tc_x11_create_native_window(_sapp_tc_window_t* w,const char*,int,int,bool) { w->drawable=w->win_id;return true; }
bool _sapp_tc_x11_initialize_dimensions(_sapp_tc_window_t*) { return true; }
void _sapp_tc_x11_show_window(_sapp_tc_window_t*) {}
void _sapp_tc_x11_set_fullscreen(_sapp_tc_window_t*,bool) {}
template<class Record,class Undo> struct _sapp_tc_creation_transaction {
    _sapp_tc_creation_transaction(Record*,Undo) {} void commit() {} void removed() {}
};
PRODUCTION_FUNCTIONS
static void check(const char* name,bool ok) {
    if(!ok) { std::fprintf(stderr,"FAIL %s\n",name);std::exit(1); }
}
static void tick(sapp_window h,void*) { order.push_back(h.id);clockValue+=work; }
static void afterTurn() {
    ++turns;
    if(trace) for(auto* w:_sapp_tc.windows) if(w)
        std::printf("state %u %d %.17g %.17g %.17g %d\n",w->win_id,w->frame_count,
                    _sapp_tc.refresh_period,w->earliest_next,w->last_present,w->last_tick_presented);
    if(turns==turnLimit) _sapp_tc.app.quit_ordered=true;
}
static void reset() {
    _sapp_tc={};_sapp_tc.SwapIntervalEXT=nativeInterval;_sapp_tc.SwapIntervalMESA=mesaInterval;
    _sapp_tc.app.desc._tc_after_turn=afterTurn;clockValue=10;refresh=1.0/60;turns=0;work=0;currentDrawable=0;
    std::fill(std::begin(swaps),std::end(swaps),0);std::fill(std::begin(nativeIntervals),std::end(nativeIntervals),0);
    intervalCalls.clear();order.clear();
}
static _sapp_tc_window_t* create(int interval=1,bool hidden=false) {
    _sapp_tc_native_desc d;d.swap_interval=interval;d.window.hidden=hidden;d.window.tick_cb=tick;
    auto* w=_sapp_tc_x11_create_window(&d);check("created",w);return w;
}
int main(int argc,char** argv) {
    reset();
    if(argc>1 && std::string(argv[1])=="single") {
        if(argc>4 && std::string(argv[4])=="MESA") _sapp_tc.SwapIntervalEXT=nullptr;
        // A native period different from the initial estimate exercises feedback,
        // including interval-N normalization, rather than a no-op sample.
        refresh=1.0/62;
        trace=true;auto* w=create(std::atoi(argv[2]));w->is_main=true;_sapp_tc.app.main=w;
        fast=std::string(argv[3])=="fast";
        if(std::string(argv[3])=="skip") w->desc.tick_cb=[](sapp_window h,void*) {
            auto* target=_sapp_tc_lookup(h);target->skip_present=target->frame_count%3==1;
        };
        _sapp_tc_x11_run_loop();return 0;
    }
#if CURRENT
    auto* first=create(2);auto* second=create();auto* third=create();
    intervalCalls.clear();
    check("steady role",_sapp_tc_x11_update_pacer()==first && intervalCalls.empty());
    first->iconified=true;second->wake_on_input=false; // As sapp_create_window creates secondaries.
    check("minimized handoff",_sapp_tc_x11_update_pacer()==second);
    check("new pacer wakes on input",second->wake_on_input);
    check("demote then promote once",intervalCalls==std::vector<std::string>{"1:0","2:1"});
    _sapp_tc_x11_update_pacer();check("no interval reapply",intervalCalls.size()==2);
    // Close the second and reuse its slot: the older third must beat the fourth.
    sapp_destroy_window({second->win_id});intervalCalls.clear();auto* fourth=create();
    check("close and slot reuse",_sapp_tc_x11_update_pacer()==third);
    check("closed surface not demoted; existing visible precedes new window",
        intervalCalls==std::vector<std::string>{"3:1","4:0"});
    first->iconified=false;intervalCalls.clear();
    check("earliest returns",_sapp_tc_x11_update_pacer()==first);
    check("requested native N restored",intervalCalls==std::vector<std::string>{"3:0","1:2"});
    // Visibility wins over drawing policy; all hidden run base timers even at N=2.
    for(auto* w:_sapp_tc.windows) if(w) { w->desc.hidden=true;w->draw_while_hidden=w!=third; }
    check("no visible pacer",!_sapp_tc_x11_update_pacer());intervalCalls.clear();
    _sapp_tc_x11_run_loop();
    check("all hidden never swap",swaps[1]+swaps[2]+swaps[3]+swaps[4]==0);
    check("all hidden ON/OFF update at base refresh",first->frame_count==turnLimit && third->frame_count==turnLimit && fourth->frame_count==turnLimit);
    check("all hidden intervals remain zero",intervalCalls.empty());
    // Common callback hide must move the role before the old surface can swap.
    reset();first=create();second=create();first->draw_while_hidden=true;
    first->desc.tick_cb=[](sapp_window h,void*) { _sapp_tc_lookup(h)->desc.hidden=true; };
    _sapp_tc_x11_tick(first);
    check("late hide skips swap and promotes",swaps[1]==0 && _sapp_tc.pacer.id==2 && nativeIntervals[2]==1);
    // Step 2: both hidden, callback work does not move periodic timer phase.
    reset();first=create();second=create();first->desc.hidden=second->desc.hidden=true;
    first->draw_while_hidden=true;work=.0001;turnLimit=120;
    _sapp_tc_x11_run_loop();
    check("step2 both hidden full admission",first->frame_count==120 && second->frame_count==120);
    check("step2 periodic deadlines",std::abs(first->earliest_next-second->earliest_next)<.0003 && swaps[1]+swaps[2]==0);
    // Visible second pacer runs before the earlier hidden follower, restoring credit.
    reset();first=create();second=create();first->desc.hidden=true;first->draw_while_hidden=false;
    _sapp_tc_x11_update_pacer();second->last_present=10;
    first->earliest_next=10+_sapp_tc.refresh_period+.0001;
    refresh-=.0001;work=.00002;turnLimit=120;_sapp_tc_x11_run_loop();
    check("replacement pacer first",order[0]==2 && order[1]==1);
    check("earlier follower regains cadence",first->frame_count==second->frame_count && first->frame_count==120);
    // Readiness and drawability are requirements, including a hidden-signal opt-out.
    reset();first=create();second=create();first->ready=false;
    check("not ready",_sapp_tc_x11_update_pacer()==second);first->ready=true;first->drawable=0;
    check("not drawable",_sapp_tc_x11_update_pacer()==second);first->drawable=1;first->occluded=true;
    check("effective occlusion",_sapp_tc_x11_update_pacer()==second);first->trust_hidden_signals=false;
    check("untrusted occlusion ignored",_sapp_tc_x11_update_pacer()==first);
#if defined(SOKOL_GLCORE)
    reset();_sapp_tc.SwapIntervalEXT=nullptr;first=create();second=create();
    check("MESA multiwindow uses interval zero",intervalCalls.back()=="2:0");
    first->desc.hidden=true;intervalCalls.clear();_sapp_tc_x11_update_pacer();
    check("MESA handoff targets each drawable at zero",intervalCalls==std::vector<std::string>{"1:0","2:0"});
#endif
#endif
    return 0;
}
