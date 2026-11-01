#!/usr/bin/env python3
"""Compile the actual native session adapters with counted OS seams on Linux."""
from pathlib import Path
import os, subprocess, sys

def policyRegression():
    from pathlib import Path
    import os, re, subprocess, sys, tempfile
    ROOT = Path(__file__).resolve().parents[3]
    def source(path):
        return (ROOT / path).read_text(encoding='utf-8')
    def body(text, name):
        match = re.search(r'^(?:bool|void|static bool) ' + name + r'\([^;]*?\) noexcept \{|^static bool ' + name + r'\([^;]*?\) \{', text, re.M)
        if not match:
            raise AssertionError(name)
        i = text.index('{', match.start()); end = i + 1; depth = 1
        while depth:
            depth += (text[end] == '{') - (text[end] == '}'); end += 1
        return text[match.start():end]
    runtime = source('core/include/tc/app/tcWindowRuntime.cpp')
    native = source('core/include/sokol/sokol_app_tc.h')
    code = r'''
    #include <atomic>
    #include <cstdio>
    #include <cstdlib>
    #include <string>
    #include <utility>
    #include <unistd.h>
    #include <sys/wait.h>
    bool busy=false,hostBusy=false;
    struct { struct {bool init_called=true;} app; } _sapp_tc;
    bool _sapp_tc_mac_busy() { return busy; }
    ''' + body(native, '_sapp_tc_mac_session_busy') + r'''
    namespace trussc::internal {
    struct State {
        bool sessionCommitted=false,stopping=false,sessionQueried=false,sessionAllowed=true;
        void* constructor=nullptr;
        bool (*nativeSessionActivity)()=_sapp_tc_mac_session_busy;
        float logoutTimeout=3;
    } state;
    int exits=0,arms=0,cleanups=0,queries=0;
    auto& runtime() { return state; }
    bool hostCallbacksActive() { return hostBusy; }
    void setExitReason(const char*) {}
    bool dispatchExitRequested() { ++queries;return false; }
    void armLogoutWatchdog(float) { ++arms; }
    void shutdownRuntime() { if(!state.stopping){state.stopping=true;exits+=3;} }
    void _cleanup_cb() { shutdownRuntime(); ++cleanups; }
    int appExitCode() { return 17; }
    void closeLogFile() {}
    [[noreturn]] void terminateSessionProcess(int code) { ::_exit(code); }
    void processExit() { ::write(STDOUT_FILENO, arms==1 && exits==3 && cleanups==1 ? "A" : "!", 1); }
    struct StaticExitProbe { ~StaticExitProbe() { ::write(STDOUT_FILENO, "D", 1); } };
    [[noreturn]] void macExit(int code) { ::_exit(code==0 && arms==0 && exits==0 && cleanups==0 ? 71 : 91); }
    #define _exit macExit
    ''' + '\n'.join(body(runtime, n) for n in ['querySessionEnd', 'cancelSessionEnd', 'endRuntimeForSession', 'commitSessionEnd']) + r'''
    #undef _exit
    }
    int main() {
        using namespace trussc::internal;
        int failures=0;
        for (int scenario : {0,1,2,3,4}) {
            std::fflush(stdout);
            int pipefd[2]; if (pipe(pipefd)) return 2;
            pid_t pid=fork();
            if (pid<0) return 2;
            if (pid==0) {
                close(pipefd[0]); dup2(pipefd[1],STDOUT_FILENO); close(pipefd[1]);
                static StaticExitProbe staticExit;
                std::atexit(processExit);
                if (scenario==0) {
                    bool veto=!querySessionEnd() && queries==1;
                    cancelSessionEnd();
                    veto=veto && !querySessionEnd() && queries==2;
                    ::_exit(veto?0:91);
                }
                if (scenario==1) busy=true;
                if (scenario==3) hostBusy=true;
                if (scenario==2) state.constructor=&state;
                if (scenario==4) { commitSessionEnd(); ::_exit(91); }
                if (!querySessionEnd() || queries) ::_exit(91);
                commitSessionEnd(); commitSessionEnd();
                ::_exit(exits==0 && arms==0 && cleanups==0 ? 0 : 91);
            }
            close(pipefd[1]);
            std::string markers; char buffer[32]; ssize_t count;
            while ((count=read(pipefd[0],buffer,sizeof(buffer)))>0) markers.append(buffer,count);
            close(pipefd[0]);
            int status; waitpid(pid,&status,0);
    #ifdef __APPLE__
            const int expected=scenario==0?0:scenario==4?17:71;
    #else
            const int expected=scenario==4?17:0;
    #endif
            const bool ok=WIFEXITED(status) && WEXITSTATUS(status)==expected && markers==(scenario==4?"AD":"");
            std::printf("session-policy %d: %s (status=%d, handlers=%s)\n",scenario,ok?"PASS":"FAIL",status,markers.c_str());
            failures+=!ok;
        }
        return failures?1:0;
    }
    '''
    with tempfile.TemporaryDirectory(prefix='tc-session-policy-') as folder:
        folder = Path(folder); cpp = folder/'probe.cpp'; cpp.write_text(code, encoding='utf-8')
        failed = False
        anchor = 'std::exit(appExitCode());'
        assert code.count(anchor)==1, 'idle exit mutation anchor missing'
        for name, text in [('baseline',code), ('idle-immediate-exit',code.replace(anchor,'terminateSessionProcess(appExitCode());'))]:
            cpp.write_text(text, encoding='utf-8')
            for platform in ['win-return', 'mac-exit']:
                binary = folder/platform
                subprocess.run([os.environ.get('CXX','c++'), '-std=c++20', *(['-D__APPLE__'] if platform=='mac-exit' else []), str(cpp), '-o', str(binary)], check=True)
                result = subprocess.run([str(binary)], text=True, encoding='utf-8', capture_output=True, timeout=10)
                output = result.stdout + result.stderr
                out = ROOT/os.environ.get('TC_C3_EVIDENCE','.verification/c3')
                (out/f'native-policy-{platform}-{name}.log').write_text(output,encoding='utf-8')
                good = result.returncode==0 if name=='baseline' else result.returncode==1 and 'session-policy 4: FAIL' in output
                print(platform, name, 'PASS' if good else 'FAIL', output, flush=True)
                failed |= not good
        return int(failed)

def orderingRegression():
    from pathlib import Path
    import os, subprocess, sys, tempfile
    ROOT = Path(__file__).resolve().parents[3]
    def read(path):
        return (ROOT/path).read_text(encoding='utf-8')
    def body(text, marker):
        a=text.index(marker); end=text.index('{',a)+1; depth=1
        while depth:
            depth+=(text[end]=='{')-(text[end]=='}');end+=1
        return text[a:end]
    runtime=read('core/include/tc/app/tcWindowRuntime.cpp')
    appHeader=read('core/include/tcBaseApp.h')
    prefix=r'''
    #include <atomic>
    #include <chrono>
    #include <cstdio>
    #include <memory>
    #include <stdexcept>
    #include <utility>
    int failures=0;
    void check(const char* label,bool ok) { printf("ordering: %s: %s\n",label,ok?"PASS":"FAIL"); failures+=!ok; }
    namespace headless { bool active=true;std::atomic<bool> quitRequested{false};bool running=true;bool isActive(){return active;} }
    namespace setup {
    int callbacks=0,vetoes=0;bool allow=true;
    struct State {
     int entries=0,turns=0;bool servicing=false,stopping=false,servicedTurn=false,pollingReload=false,quitPending=false;
     void (*reload)()=nullptr;void* sessionAuthority=nullptr;
    } state;
    auto& runtime(){return state;} auto& coordinator(){return state;}
    bool dispatchExitRequested(){++vetoes;return allow;}
    void shutdownRuntime(){state.stopping=true;}
    void sapp_quit(){} void sapp_cancel_quit(){}
    void pollSessionAuthority(void*){}
    int* currentWindowCtx(){return nullptr;}
    void restoreWindowContext(int*){}
    struct Saved{};Saved sapp_tc_gl_save(){return {};}
    void sapp_tc_gl_restore(Saved){}
    struct sgl_context{int id=0;}; bool sg_isvalid(){return false;}
    sgl_context sgl_get_context(){return {}; } void sgl_set_context(sgl_context){}
    bool tc_internal_gpu_frame_has_work(){return false;} void endGpuFrame(){}
    struct ProcessDrawingScope{};
    struct TeardownSteps{template<class F>void run(const char*,F f){f();}};
    struct Operations{void (*queues)()=+[]{++callbacks;};} operations;
    auto& hostServiceOperationsForTests(){return operations;}
    '''
    # currentWindowCtx is an lvalue reference in the real host.
    prefix=prefix.replace('int* currentWindowCtx(){return nullptr;}','int*& currentWindowCtx(){static int* p=nullptr;return p;}')
    setup=prefix
    if 'bool processPendingQuit() noexcept {' in runtime:
        setup+=body(runtime,'bool processPendingQuit() noexcept {')
    setup+=body(runtime,'void beforeHostTurn() noexcept {')+r'''
    namespace internal=::setup;
    struct AppOwnerScope{explicit AppOwnerScope(void*){}};
    int audioSubscriptions=0;
    template<class T>void attachAppAudio(T&){++audioSubscriptions;}
    namespace mcp::internal{void initializeMcp(){}}
    struct App {
    App* getRootNode(){return this;}
    '''+body(appHeader,'    void onSetupDone() final {').replace(' final {',' {')+r'''
    };
    void test(){
     for(bool headlessMode:{true,false}) for(bool veto:{false,true}) {
      state={};callbacks=vetoes=0;allow=!veto;
      headless::active=headlessMode;headless::running=true;
      headless::quitRequested=headlessMode;state.quitPending=!headlessMode;
      audioSubscriptions=0;App app;app.onSetupDone();
      if (!state.stopping) beforeHostTurn();
      check("setup quit reaches veto before first queued console delivery",vetoes==1 && callbacks==int(veto));
      check("accepted setup quit stops callbacks; veto continues normally",state.stopping==!veto);
      check("accepted setup quit never subscribes App audio",audioSubscriptions==int(veto));
     }
    }
    }
    namespace resize {
    struct Context{int fbWidth=200,fbHeight=200;float dpiScale=1;} context;
    struct App{int getWidth(){return 100;}int getHeight(){return 100;}void handleWindowResized(int,int){throw 1;}};
    struct Window{
     std::shared_ptr<App> app=std::make_shared<App>();
     bool appSizeDelivering_=false,appSizeSeeded_=true,open=true;
     int appSizeWidth_=100,appSizeHeight_=100,calls=0;
     auto getApp(){return app;}auto& context(){return resize::context;}bool isOpen(){return open;}
     void syncRootSize(float,float){++calls;throw 1;}
    };
    struct WindowRequestAccess{static auto& context(Window& w){return w.context();}static void syncRootSize(Window& w,float x,float y){w.syncRootSize(x,y);}static bool appStarted(Window&){return true;}static void teardown(Window& w){w.open=false;w.app.reset();}};
    int appOwner(App&){return 0;}struct AppCallbackScope{AppCallbackScope(int){}};
    enum class AppEntry{Event};struct EntryStackGuard{EntryStackGuard(AppEntry,const char*){}};
    struct WindowCallbackTimeScope{};
    float appSizeDpi(float v){return v;}struct Size{int width,height;};Size framebufferResizeArgs(int w,int h,float){return{w,h};}
    '''+body(runtime,'void syncWindowAppSize(Window& window, bool initial) {')+r'''
    void test(){
     Window window;
     for(int i=0;i<3;++i)try{syncWindowAppSize(window,false);}catch(...){}
     check("throwing resize fails the Window with no replay",!window.isOpen() && window.calls==1);
    }
    }
    int main(){setup::test();resize::test();return failures?1:0;}
    '''
    with tempfile.TemporaryDirectory(prefix='tc-session-order-') as tmp:
        folder=Path(tmp);cpp=folder/'probe.cpp';cpp.write_text(setup,encoding='utf-8');binary=folder/'probe'
        subprocess.run([os.environ.get('CXX','c++'),'-std=c++20',str(cpp),'-o',str(binary)],check=True)
        result=subprocess.run([str(binary)],timeout=10)
        return int(result.returncode != 0)

regressionFailures = policyRegression() + orderingRegression()
if regressionFailures:
    raise SystemExit(1 if regressionFailures else 0)

root=Path.cwd();out=root/ os.environ.get('TC_C3_EVIDENCE','.verification/c3');scratch=root/ os.environ.get('TC_C3_SCRATCH','.build/c3') /'native';scratch.mkdir(parents=True,exist_ok=True)
def body(s,m):
 a=s.index(m);i=s.index('{',a)+1;depth=1;b=i
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]
win=(root/'core/platform/win/tcPlatform_win.cpp').read_text(encoding='utf-8')
mac=(root/'core/include/sokol/sokol_app_tc.h').read_text(encoding='utf-8')
code=r'''
#include <cstdio>
#include <cmath>
#include <string>
#include <unordered_map>
#include <memory>
#include <stdexcept>
#include <thread>
#include <functional>
using HWND=void*;using UINT=unsigned;using WPARAM=unsigned;using LPARAM=long;using LRESULT=long;
#define CALLBACK
constexpr int WM_QUERYENDSESSION=1,WM_ENDSESSION=2,TRUE=1,FALSE=0,ERROR_CLASS_ALREADY_EXISTS=1410,WS_OVERLAPPED=0,CP_UTF8=65001;
int MultiByteToWideChar(int,int,const char*,int,wchar_t* dest,int) { if(dest) { dest[0]=L'x';dest[1]=0; }return 2; }
struct WNDCLASSW { LRESULT (*lpfnWndProc)(HWND,UINT,WPARAM,LPARAM);void* hInstance;const wchar_t* lpszClassName; };
int failures=0,queries=0,commits=0,cancels=0,retired=0,nativeCalls=0;
bool allow=true,busy=false;unsigned reason=0;
void check(const char* label,bool good) { std::printf("c3-native: %s: %s\n",label,good?"PASS":"FAIL");failures+=!good; }
namespace trussc::internal {
bool querySessionEnd() { ++queries;return busy || allow; }
void cancelSessionEnd() { ++cancels; }
void commitSessionEnd() { ++commits;throw 239; }
void shutdownRuntime() { retired=3; }
std::string exitBlockReason() { return "Unsaved test document"; }
void setExitReasonIfEmpty(const char*) {}
struct RuntimeState { float logoutTimeout=3; };
RuntimeState& runtime() { static RuntimeState state;return state; }
}
void ShutdownBlockReasonDestroy(HWND) {}
void ShutdownBlockReasonCreate(HWND,const wchar_t*) {}
LRESULT DefWindowProcW(HWND,UINT,WPARAM,LPARAM) { return 9; }
void* GetModuleHandleW(void*) { return nullptr; }
bool RegisterClassW(WNDCLASSW*) { return true; }
int GetLastError() { return 0; }
HWND CreateWindowExW(int,const wchar_t*,const wchar_t*,int style,int,int,int,int,HWND parent,void*,void*,void*) {
 check("hidden top-level authority (no message-only parent)",parent==nullptr && style==WS_OVERLAPPED);
 return reinterpret_cast<HWND>(17);
}
void DestroyWindow(HWND) { ++nativeCalls; }
'''+'using namespace trussc::internal;\n'+body(win,'LRESULT CALLBACK sessionAuthorityProc(')+body(win,'void* createSessionAuthority(')+body(win,'void destroySessionAuthority(')+r'''
using AEEventID=unsigned;struct NSApplication {};using NSApplicationTerminateReply=int;
constexpr int NSTerminateCancel=0,NSTerminateNow=1;
constexpr int _SAPP_TC_MAX_WINDOWS=3;
struct NativeWindow { bool constructing=false,in_tick=false; } nativeWindow;
struct { bool terminate_pending=false,deferred_blocked=false;
 struct { bool init_called=true; } app;
 NativeWindow* windows[_SAPP_TC_MAX_WINDOWS]{};
} _sapp_tc;
bool _sapp_tc_mac_busy() { return busy; }
AEEventID _sapp_tc_mac_quit_reason() { return reason; }
bool _sapp_tc_request_exit() { ++queries;return allow; }
'''+body(mac,'static bool _sapp_tc_mac_session_busy(')+body(mac,'static bool _sapp_tc_win32_session_busy(')+body(mac,'static bool _sapp_tc_mac_is_session_end(')+body(mac,'static void _sapp_tc_mac_session_end(')
runtime=(root/'core/include/tc/app/tcWindowRuntime.cpp').read_text(encoding='utf-8')
code+='namespace trussc { class Runtime { public: void setLogoutTimeout(float); }; int errors=0; struct Sink { template<class T> Sink& operator<<(T&&) { return *this; } }; Sink logError(const char*) { ++errors;return {}; }\n'
code+=body(runtime,'void Runtime::setLogoutTimeout(float seconds) {')+'\n}\n'
method=body(mac,'- (NSApplicationTerminateReply)applicationShouldTerminate:')
method=method.replace('- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender','NSApplicationTerminateReply shouldTerminate(NSApplication* sender)')
launch=body(mac,'- (void)applicationDidFinishLaunching:')
assert 'NSWorkspaceWillPowerOffNotification' in launch and 'queue:[NSOperationQueue mainQueue]' in launch
begin=launch.index('usingBlock:^(NSNotification* notification) {')
power=body(launch[begin:].replace('usingBlock:^(NSNotification* notification)', 'void willPowerOff(NSNotification* notification)',1),'void willPowerOff(')
code+='struct NSNotification {};\n'+power+method+r'''
int main() {
 trussc::Runtime config;
 config.setLogoutTimeout(6);
 for(float bad:{-1.0f,-INFINITY,NAN}) { config.setLogoutTimeout(bad);check("negative/NaN retain previous value",trussc::internal::runtime().logoutTimeout==6); }
 check("invalid timeout logs Error",trussc::errors==3);
 config.setLogoutTimeout(0);check("zero numeric value",trussc::internal::runtime().logoutTimeout==0);
 config.setLogoutTimeout(INFINITY);check("positive infinity numeric value",std::isinf(trussc::internal::runtime().logoutTimeout));
 auto* authority=createSessionAuthority();
 check("authority independent of user HWND",authority!=nullptr);
 for(bool a:{false,true}) {allow=a;check("Win query forwards veto",sessionAuthorityProc(authority,WM_QUERYENDSESSION,0,0)==a);}
 sessionAuthorityProc(authority,WM_ENDSESSION,0,0);check("Win cancellation forwarded",cancels==1 && commits==0);
 try { sessionAuthorityProc(authority,WM_ENDSESSION,1,0); } catch(int) {}
 check("Win commit forwards session ending",commits==1);
 destroySessionAuthority(authority);check("Runtime destroys authority",nativeCalls==1);
 for(unsigned r:{0x6c6f676fu,0x726c676fu,0x72727374u,0x72657374u,0x7273646eu,0x73687574u}) {
  for(bool b:{false,true}) {reason=r;busy=b;allow=true;int before=commits;try{shouldTerminate(nullptr);}catch(int){}check("every mac session reason commits idle and busy",commits==before+1);}
 }
 reason=0x73687574;busy=false;allow=false;
 check("mac query veto cancels before commit",shouldTerminate(nullptr)==NSTerminateCancel);
 int beforePower=commits;
 std::function<void()> onMain;
 std::thread worker([&] { onMain=[] { willPowerOff(nullptr); }; });worker.join();
 check("worker workspace post waits for main operation queue", commits==beforePower);
 try { onMain(); } catch(int) {}
 check("power-off notification commits on main without veto",commits==beforePower+1);
 reason=0;busy=true;allow=true;
 check("ordinary nested terminate deferred",shouldTerminate(nullptr)==NSTerminateCancel && _sapp_tc.terminate_pending && retired==0);
 busy=false;
 check("common ending precedes AppKit remaining closes",shouldTerminate(nullptr)==NSTerminateNow && retired==3);
 return failures?1:0;
}
'''
global_source=(root/'core/include/tc/app/tcGlobal.cpp').read_text(encoding='utf-8')
gpu=r'''
namespace gpu {
int operations=0;bool valid=false;
bool frameworkShutdown() { return false; }
bool sg_isvalid() { return valid; }
struct Handle { int id=0; };
void sgl_destroy_pipeline(Handle) { ++operations; }
void sgl_destroy_context(Handle) { ++operations; }
Handle sgl_default_context() { ++operations;return {1}; }
struct RenderTargetResources {
 std::unordered_map<int,Handle> cache;std::unordered_map<int,int> selections;
 std::shared_ptr<void> rawCache;Handle context;bool contextFailed=false;
 void release();
};
'''+body(global_source,'void RenderTargetResources::release() {')+r'''
struct sg_swapchain { int width=0,height=0; };
struct Metadata { int fb_width=0,fb_height=0; };
struct Operations { sg_swapchain (*acquireSwapchain)(int,void*);bool (*readMetadata)(int,void*,Metadata&); };
struct Window { struct Binding { bool bound=true;int handle=1;void* user=nullptr;Operations* operations;Metadata metadata; } binding; };
struct WindowRequestAccess { static auto& binding(Window& w) { return w.binding; } };
void publishWindowMetadata(Window&,const Metadata&) {}
'''+body(runtime,'sg_swapchain acquireBoundSwapchain(void* user) {')+r'''
void test() {
 RenderTargetResources resources[3];
 for(auto& r:resources) { r.cache[1]={9};r.context={7}; }
 for(int i:{1,2,0,2}) resources[i].release();
 check("late Window release cannot touch sgl after sg shutdown",operations==0);
 Operations ops{[](int,void*) { ++operations;return sg_swapchain{7,8}; },nullptr};
 Window w;w.binding.operations=&ops;
 check("live acquisition remains admitted",acquireBoundSwapchain(&w).width==7);
}
}
'''
code=code.replace('int main() {',gpu+'\nint main() {\n gpu::test();')

# The production call before NSTerminateNow is the regression for synchronous
# performClose with two remaining windows. Deleting it must fail behaviorally.
variants=[('baseline',code),('late-appkit-ending',code.replace('    trussc::internal::shutdownRuntime();','')),
 ('late-gpu-release',code.replace('frameworkShutdown() || !sg_isvalid()', 'frameworkShutdown()'))]
labels={'late-appkit-ending':'common ending precedes AppKit remaining closes',
        'late-gpu-release':'late Window release cannot touch sgl after sg shutdown'}
for name,text in variants:
 cpp=scratch/(name+'.cpp');binary=scratch/name;cpp.write_text(text,encoding='utf-8')
 subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-DNDEBUG',str(cpp),'-o',str(binary)],check=True)
 r=subprocess.run([str(binary)],capture_output=True,text=True,encoding='utf-8',timeout=10)
 (out/('native-session-'+name+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8')
 if (name=='baseline' and r.returncode!=0) or (name!='baseline' and (r.returncode!=1 or labels[name]+': FAIL' not in r.stdout)):
  raise SystemExit(r.stdout+r.stderr)
 print(name,'PASS' if name=='baseline' else 'mutation detected',flush=True)
