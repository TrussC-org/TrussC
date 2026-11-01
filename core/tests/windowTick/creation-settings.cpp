// Native APIs are recording seams; descriptor builders, constructors and
// fullscreen transitions are extracted from production by creation-settings-test.py.
#include "sokol_app_tc.h"
#include "util/sokol_window_native.h"
#include "util/sokol_window_size.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>
using std::isfinite;
int checks=0, failures=0;
void check(const char* label, bool pass) {
    ++checks;
    if (!pass) { ++failures; std::printf("creation-settings: FAIL: %s\n", label); }
}
constexpr int _SAPP_TC_MAX_WINDOWS=32;
struct _sapp_tc_window_t;
std::vector<std::string> calls;
struct Rect { int left=120,top=120,right=760,bottom=600; };
using RECT=Rect;
using DWORD=unsigned; using UINT=unsigned; using WCHAR=wchar_t; using LONG=long;
constexpr int SWP_HIDEWINDOW=1,SWP_SHOWWINDOW=2,SWP_FRAMECHANGED=4,SW_SHOW=1;
constexpr int WS_POPUP=1,WS_SYSMENU=2,WS_VISIBLE=4,WS_CLIPSIBLINGS=8,WS_CLIPCHILDREN=16,
    WS_CAPTION=32,WS_MINIMIZEBOX=64,WS_MAXIMIZEBOX=128,WS_SIZEBOX=256,
    WS_EX_APPWINDOW=1,WS_EX_WINDOWEDGE=2,GWL_STYLE=1,GWL_EXSTYLE=2,
    MONITOR_DEFAULTTONEAREST=1,HWND_TOP=1,TRUE=1;
constexpr int DXGI_FORMAT_R10G10B10A2_UNORM=1,DXGI_FORMAT_B8G8R8A8_UNORM=2;
struct NativeWindow {
    Rect rect; LONG style=0,ex_style=0;
    bool releasedWhenClosed=false,acceptsMouseMovedEvents=false,restorable=false;
    float backingScaleFactor=1; const char* title=nullptr;
    struct View* contentView=nullptr; struct Delegate* delegate=nullptr;
};
struct Range { float minimum=0,maximum=0,preferred=0; };
Range CAFrameRateRangeMake(float a,float b,float c) { return {a,b,c}; }
struct Layer { const void* device=nullptr; int pixelFormat=0,magnificationFilter=0; bool opaque=false,framebufferOnly=true; };
struct View { _sapp_tc_window_t* w=nullptr; bool wantsLayer=false; Layer* layer=nullptr; };
void register_drop(View*) {}
struct Delegate { _sapp_tc_window_t* w=nullptr; };
struct Link { Range preferredFrameRateRange; };
struct DeviceContext { void ClearRenderTargetView(int,const float*) {} };
using HRESULT=int;
constexpr int DXGI_PRESENT_DO_NOT_WAIT=1,DXGI_STATUS_OCCLUDED=2;
bool SUCCEEDED(HRESULT hr) { return hr>=0; }
struct SwapChain {
    unsigned interval=99;
    HRESULT Present(unsigned value,unsigned) { interval=value; return 0; }
};
struct _sapp_tc_window_t : _sapp_tc_native_window_t {
    int color_fmt=0,rtv=0,drawable=0,xwin=0;
    NativeWindow *hwnd=nullptr,*window=nullptr;
    Rect stored_window_rect; LONG stored_style=0,stored_ex_style=0;
    float window_scale=1,mouse_scale=1;
    View* view=nullptr; Layer* layer=nullptr; Link* link=nullptr; Delegate* delegate=nullptr;
    bool initialized=false,bound=false,source=false,shown=false,key=false,nativeFullscreen=false;
    int applied_interval=-1;
    SwapChain chain; SwapChain* swap_chain=&chain;
    bool from_modal=false,credit_held=false;
    int frame_wait=1;
};
DeviceContext deviceContext;
struct {
    unsigned next_id=0; _sapp_tc_window_t* windows[32]{};
    int constructing=0, callbacks_active=0; bool deferred_blocked=false;
    size_t window_slots=32; const char* create_error=nullptr;
    int display=1,ctx=1,NET_WM_STATE=1,NET_WM_STATE_FULLSCREEN=2; float dpi=96;
    struct { sapp_desc desc{}; bool valid=true; const void* device=(void*)1;
        DeviceContext* device_context=&deviceContext; _sapp_tc_window_t* main=nullptr; } app;
} _sapp_tc;
_sapp_tc_window_t* _sapp_tc_lookup(sapp_window h) {
    for (auto* w:_sapp_tc.windows) if (w && w->win_id==h.id) return w;
    return nullptr;
}
_sapp_tc_window_t* _sapp_tc_resolve(sapp_window h) { return _sapp_tc_lookup(h); }
namespace trussc::internal { bool frameworkShutdown() { return false; } }
static void _sapp_tc_mac_schedule_deferred() {}
static _sapp_tc_window_t* _sapp_tc_mac_live(unsigned id) { return _sapp_tc_lookup({id}); }
void order_out(NativeWindow*) {}
bool _sapp_tc_app_events_enabled() { return true; }
void _sapp_tc_main_event_tramp(const sapp_event*,sapp_window,void*) {}
void _sapp_tc_main_tick_tramp(sapp_window,void*) {}
void _sapp_tc_timing_reset(_sapp_tc_timing_t*) {}
void dispose(_sapp_tc_window_t* w) {
    for (auto*& slot:_sapp_tc.windows) if (slot==w) slot=nullptr;
    free(w->title); delete w->hwnd; delete w->window; delete w->view;
    delete w->layer; delete w->link; delete w->delegate;
#if defined(TEST_EGL)
    free(w);
#else
    delete w;
#endif
}
void _sapp_tc_win32_unwind(_sapp_tc_window_t* w) { dispose(w); }
void _sapp_tc_x11_unwind(_sapp_tc_window_t* w) { dispose(w); }
_sapp_tc_window_t* _sapp_tc_mac_fail(_sapp_tc_window_t* w,const char*) { dispose(w); return nullptr; }
bool _sapp_tc_win32_trust_hidden_signals() { return true; }
bool _sapp_tc_mac_trust_hidden_signals() { return true; }
bool _sapp_tc_x11_trust_hidden_signals() { return true; }
int _sapp_tc_mac_pixel_format(sapp_pixel_format) { return 1; }
void initialize(_sapp_tc_window_t* w) {
    w->initialized=true; w->win_width=w->fb_width=w->desc.width;
    w->win_height=w->fb_height=w->desc.height; calls.push_back("dimensions");
}
bool _sapp_tc_win32_initialize_dimensions(_sapp_tc_window_t* w) { initialize(w); return true; }
bool _sapp_tc_x11_initialize_dimensions(_sapp_tc_window_t* w) { initialize(w); return true; }
bool _sapp_tc_mac_initialize_dimensions(_sapp_tc_window_t* w) { initialize(w); return true; }
void _sapp_tc_mac_update_dimensions(_sapp_tc_window_t*) {}
void _sapp_tc_x11_update_dimensions(_sapp_tc_window_t*) {}
void shown(_sapp_tc_window_t* w) {
    check("show after publication and binding",w->ready&&(w->bound||!w->desc.bind_cb)&&_sapp_tc_lookup({w->win_id})==w);
    w->shown=true; calls.push_back("show");
}
// Win32 API seams retain the actual fullscreen style/restore implementation.
struct MONITORINFO { unsigned cbSize; Rect rcMonitor{0,0,1920,1080}; };
int MonitorFromWindow(NativeWindow*,int) { return 1; }
bool GetMonitorInfoW(int,MONITORINFO*) { return true; }
void GetWindowRect(NativeWindow* w,Rect* r) { *r=w->rect; }
LONG GetWindowLongPtrW(NativeWindow* w,int key) { return key==GWL_STYLE?w->style:w->ex_style; }
void SetWindowLongPtrW(NativeWindow* w,int key,LONG value) { (key==GWL_STYLE?w->style:w->ex_style)=value; }
void SetWindowPos(NativeWindow* n,int,int x,int y,int width,int height,unsigned flags) {
    n->rect={x,y,x+width,y+height};
    if (flags&SWP_HIDEWINDOW) calls.push_back("fullscreen");
}
void ShowWindow(NativeWindow* n,int) {
    for (auto* w:_sapp_tc.windows) if(w&&w->hwnd==n) shown(w);
}
void DragAcceptFiles(NativeWindow*,int) {}
bool _sapp_tc_win32_utf8_to_wide(const char*,WCHAR*,int) { return true; }
bool _sapp_tc_win32_create_native_window(_sapp_tc_window_t* w,const WCHAR*,DWORD style,DWORD ex,int x,int y) {
    w->hwnd=new NativeWindow; w->hwnd->rect={x,y,x+w->desc.width,y+w->desc.height};
    w->hwnd->style=style; w->hwnd->ex_style=ex; return true;
}
bool _sapp_tc_d3d11_create_swapchain(_sapp_tc_window_t* w) {
    check("swapchain sized after fullscreen staging",!w->desc.fullscreen||(w->fullscreen&&w->hwnd->rect.right==1920&&!w->shown));
    calls.push_back("swapchain"); return true;
}
// macOS message/property syntax is translated only to these native API seams.
using NSRect=struct MacRect { struct {double x=0,y=0;} origin; struct {double width=0,height=0;} size; };
using NSWindowStyleMask=int;
constexpr int NSWindowStyleMaskTitled=1,NSWindowStyleMaskClosable=2,NSWindowStyleMaskMiniaturizable=4,
    NSWindowStyleMaskResizable=8,NSWindowStyleMaskBorderless=0,kCAFilterNearest=1;
NSRect NSMakeRect(double x,double y,double w,double h) { return {{x,y},{w,h}}; }
struct Size { double width,height; };
Size NSMakeSize(double w,double h) { return {w,h}; }
NativeWindow* make_window(NSRect,int) { return new NativeWindow; }
void set_content_size(NativeWindow*,Size) {}
NSRect content_rect(NativeWindow*) { return {}; }
NSRect get_screen_rect() { return NSMakeRect(0,0,1920,1080); }
void make_first_responder(NativeWindow*,View*) {}
void center(NativeWindow*) {}
void add_link(_sapp_tc_window_t* w) { w->source=true; calls.push_back("source"); }
void mac_show(_sapp_tc_window_t* w) {
    // Same construction-only filter as nativeWindow's AppKit recordShow seam.
    if (_sapp_tc.constructing == 0) return;
    check("mac constructor show has initialized published dimensions", w->initialized && w->ready &&
          _sapp_tc_lookup({w->win_id})==w && w->fb_width>0 && w->fb_height>0);
    check("mac no fullscreen request before show", !w->fullscreen_state.desired &&
          !w->fullscreen_state.reconcilePending && !w->fullscreen_state.transitioning());
    shown(w);
    w->key=true;
}
void native_toggle(_sapp_tc_window_t* w) {
    check("mac native fullscreen ordering",w->initialized&&w->bound&&w->delegate&&w->delegate->w==w&&w->ready&&w->source&&w->shown&&w->key&&_sapp_tc.constructing==0);
    const bool destination = !w->nativeFullscreen;
    check("mac desired matches native transition", w->fullscreen_state.desired == destination);
    w->fullscreen_state.will(destination);
    w->nativeFullscreen=destination;
    calls.push_back("fullscreen");
}
void _sapp_tc_init_keytable() {}
// X11 APIs leave the full common constructor and per-window timer policy intact.
struct _sapp_tc_x11_surface_scope {};
size_t _sapp_tc_x11_reserve_slot() {
    for (size_t i=0;i<32;i++) if (!_sapp_tc.windows[i]) return i;
    return SIZE_MAX;
}
void* _sapp_tc_x11_alloc(size_t n,bool zero) { return zero?calloc(1,n):malloc(n); }
bool _sapp_tc_x11_create_native_window(_sapp_tc_window_t* w,const char*,int,int,bool) {
    w->drawable=1; w->xwin=w->win_id; w->refresh_period=1.0/60.0; return true;
}
bool _sapp_tc_x11_make_current(_sapp_tc_window_t*) { return true; }
void _sapp_tc_x11_set_swapinterval(_sapp_tc_window_t* w,int interval) { w->applied_interval=interval; }
void _sapp_tc_x11_show_window(_sapp_tc_window_t* w) { shown(w); }
void _sapp_tc_x11_send_event(unsigned xwin,int,long,long,long,long,long) {
    auto* w=_sapp_tc_lookup({xwin});
    check("X11 fullscreen after map and bind",w->shown&&w->bound&&w->initialized);
    calls.push_back("fullscreen");
}
void XFlush(int) {}
double _sapp_tc_now() { return 10; }
bool _sapp_tc_d3d11_check_device_loss(HRESULT) { return false; }
void _sapp_tc_win32_observe_occlusion(_sapp_tc_window_t*,bool) {}
void _sapp_tc_log(...) {}
// PRODUCTION_NATIVE

namespace trussc {
// Keep the production WindowSettings declaration, including defaults/setters.
// PRODUCTION_SETTINGS
struct Window {
    void* native_=nullptr; std::string title_;
    struct { double width=0,height=0; bool pixel=false; } geometry;
    void setQuitOnClose(bool) {}
    ~Window();
};
namespace mcp::internal { std::string markedWindowTitle(const std::string& title) { return title; } }
namespace headless { bool isActive() { return false; } }
struct Log { template<class T> Log& operator<<(const T&) { return *this; } };
Log logError(const char*) { return {}; } Log logWarning(const char*) { return {}; }
bool pixelPerfectMode() { return false; }
namespace internal {
struct WindowLifecycleScope { explicit WindowLifecycleScope(Window&) {} };
struct WindowRequestAccess {
    struct Binding { decltype(Window::geometry) geometry; };
    static void setHostOwner(Window&,const std::shared_ptr<Window>&) {}
    static Binding& binding(Window&) { static Binding b; return b; }
    static void unbind(Window&) {}
};
void windowTick(sapp_window,void*) {} void windowEvent(const sapp_event*,sapp_window,void*) {}
void windowClosed(sapp_window,void*) {}
bool allowWindowClose(sapp_window,void*) { return true; }
void retainRuntimeWindow(const std::shared_ptr<Window>&) {}
void refreshWindowMetadata(Window&) {}
bool bindNativeWindow(uint32_t id,void*) {
    auto* w=_sapp_tc_lookup({id}); check("binding after dimensions before ready",w&&w->initialized&&!w->ready);
    if(w) w->bound=true; calls.push_back("bind"); return true;
}
std::shared_ptr<Window> createSecondaryWindow(const WindowSettings&);
}
struct { struct { const void* device=(void*)1; } metal; } environment;
auto sglue_environment() { return environment; }
int sapp_x11_get_display() { return _sapp_tc.display; }
struct AdapterState { sapp_window win{0}; };
// PRODUCTION_BIND
Window::~Window() {
    auto* st=static_cast<AdapterState*>(native_);
    if (st) { if (auto* w=_sapp_tc_lookup(st->win)) dispose(w); delete st; }
}
// PRODUCTION_ADAPTER
}

int main() {
    using namespace trussc;
    _sapp_tc.app.desc.sample_count=4;
    // All combinations, multiple simultaneously live peers and a zero-initialized
    // native descriptor. No OS SDK, display or GPU is claimed by this fixture.
    for (int interval:{0,1,2,3}) for (bool full:{false,true}) for (bool decorated:{false,true}) {
        calls.clear();
        WindowSettings settings; settings.setFullscreen(full).setSwapInterval(interval);
        settings.width=640;settings.height=480;settings.decorated=decorated;
        auto first=internal::createSecondaryWindow(settings);
        check("secondary creation succeeded",bool(first)); if(!first) continue;
        auto* firstState=static_cast<AdapterState*>(first->native_);
        auto* w=_sapp_tc_lookup(firstState->win);
        check("descriptor fullscreen forwarded",w->desc.fullscreen==full);
        check("descriptor interval forwarded",w->desc.swap_interval==interval);
#if !defined(TEST_MAC)
        check("native fullscreen applied",w->fullscreen==full);
#endif
        check("restore geometry retains request",w->desc.width==640&&w->desc.height==480);
#if defined(TEST_MAC)
        int effective=interval>0?interval:1;
        check("mac per-window interval",w->swap_interval==effective);
        check("mac display link rate applied",w->link->preferredFrameRateRange.preferred==120.0f/effective);
        check("mac deferred desired matches creation", w->fullscreen_state.desired==full);
        check("mac creation defers until normal tick", !w->nativeFullscreen && !w->fullscreen &&
              w->fullscreen_state.phase==_sapp_tc_fullscreen_phase::Windowed &&
              w->fullscreen_state.reconcilePending==full);
        _sapp_tc.constructing=1;
        _sapp_tc_reconcile_fullscreen(w);
        _sapp_tc.constructing=0;
        check("mac nested constructor tick keeps request deferred", !w->nativeFullscreen &&
              w->fullscreen_state.reconcilePending==full);
        _sapp_tc_reconcile_fullscreen(w);
        check("mac deferred native fullscreen applied", w->nativeFullscreen==full && w->fullscreen==full);
        if(full) {
            _sapp_tc_mac_fullscreen_terminal(w,true);
            check("mac terminal state and desired agree",w->fullscreen_state.phase==_sapp_tc_fullscreen_phase::Fullscreen &&
                  w->fullscreen_state.desired && !w->fullscreen_state.reconcilePending);
            const auto count=calls.size();
            _sapp_tc_reconcile_fullscreen(w);
            check("mac successful entry does not retry or flicker", calls.size()==count && w->fullscreen);
            sapp_tc_window_set_fullscreen(firstState->win,false);
            check("mac later runtime exit starts native transition", !w->nativeFullscreen && !w->fullscreen &&
                  !w->fullscreen_state.desired && w->fullscreen_state.phase==_sapp_tc_fullscreen_phase::Exiting);
            _sapp_tc_mac_fullscreen_terminal(w,false);
            _sapp_tc_reconcile_fullscreen(w);
            check("mac later runtime exit stays windowed", !w->fullscreen && !w->nativeFullscreen &&
                  !w->fullscreen_state.desired && !w->fullscreen_state.reconcilePending &&
                  w->fullscreen_state.phase==_sapp_tc_fullscreen_phase::Windowed);
            sapp_tc_window_set_fullscreen(firstState->win,true);
            _sapp_tc_mac_fullscreen_terminal(w,true);
        }
#elif defined(TEST_WIN)
        check("Win32 per-window interval",w->swap_interval==(interval>0?interval:1));
        _sapp_tc_win32_swap(w);
        check("Win32 Present receives own interval",w->chain.interval==unsigned(interval>0?interval:1));
        if(full) {
            check("Win32 saved windowed geometry",w->restore_valid&&w->stored_window_rect.left==120&&w->stored_window_rect.right==760);
            LONG expectedStyle=w->stored_style,expectedEx=w->stored_ex_style;
            _sapp_tc_win32_set_fullscreen(w,false,SWP_SHOWWINDOW);
            check("Win32 exact geometry/style restore",!w->fullscreen&&w->hwnd->rect.left==120&&w->hwnd->rect.right==760&&w->hwnd->rect.bottom==600&&w->hwnd->style==expectedStyle&&w->hwnd->ex_style==expectedEx);
            _sapp_tc_win32_set_fullscreen(w,true,SWP_SHOWWINDOW);
        }
#else
        check("X11 deliberate nonblocking interval",w->swap_interval==0&&w->applied_interval==0&&!w->wake_on_input);
        _sapp_tc_x11_pace_periodic(w,w->refresh_period);
        check("X11 refresh timer retained",w->earliest_next>10);
#endif
        WindowSettings peer; peer.fullscreen=!full;peer.swapInterval=4;
        auto second=internal::createSecondaryWindow(peer);
        check("independent peer creation",bool(second));
        auto* p=second?_sapp_tc_lookup(static_cast<AdapterState*>(second->native_)->win):nullptr;
#if defined(TEST_MAC)
        if(p) { _sapp_tc_reconcile_fullscreen(p); _sapp_tc_mac_fullscreen_terminal(p,!full); }
#endif
        check("peer fullscreen independent",p&&p!=w&&p->fullscreen!=w->fullscreen);
#if defined(TEST_MAC) || defined(TEST_WIN)
        check("peer interval independent",p&&p->swap_interval==4&&w->swap_interval==(interval>0?interval:1));
#endif
    }
    sapp_window_desc legacy{};
    auto legacyHandle=sapp_create_window(&legacy);
    auto* legacyWindow=_sapp_tc_lookup(legacyHandle);
    check("zero descriptor creates windowed",legacyWindow&&!legacyWindow->fullscreen);
#if defined(TEST_MAC) || defined(TEST_WIN)
    check("zero descriptor keeps default interval",legacyWindow&&legacyWindow->swap_interval==1);
#endif
    if(legacyWindow) dispose(legacyWindow);
    std::printf("creation-settings: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
