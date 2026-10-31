#!/usr/bin/env python3
"""Compile the actual native session adapters with counted OS seams on Linux."""
from pathlib import Path
import os, subprocess
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
using HWND=void*;using UINT=unsigned;using WPARAM=unsigned;using LPARAM=long;using LRESULT=long;
#define CALLBACK
constexpr int WM_QUERYENDSESSION=1,WM_ENDSESSION=2,TRUE=1,FALSE=0,ERROR_CLASS_ALREADY_EXISTS=1410,WS_OVERLAPPED=0,CP_UTF8=65001;
int MultiByteToWideChar(int,int,const char*,int,wchar_t* dest,int) { if(dest) { dest[0]=L'x';dest[1]=0; }return 2; }
struct WNDCLASSW { LRESULT (*lpfnWndProc)(HWND,UINT,WPARAM,LPARAM);void* hInstance;const wchar_t* lpszClassName; };
int failures=0,queries=0,commits=0,cancels=0,retired=0,nativeCalls=0;
bool allow=true,busy=false,frozen=false;unsigned reason=0;
void check(const char* label,bool good) { std::printf("c3-native: %s: %s\n",label,good?"PASS":"FAIL");failures+=!good; }
namespace trussc::internal {
bool querySessionEnd() { ++queries;return busy || allow; }
void cancelSessionEnd() { ++cancels; }
[[noreturn]] void commitSessionEnd() { ++commits;throw 239; }
void shutdownRuntime() { retired=3; }
std::string exitBlockReason() { return "Unsaved test document"; }
void setExitReasonIfEmpty(const char*) {}
struct RuntimeState { float logoutTimeout=3; };
RuntimeState& runtime() { static RuntimeState state;return state; }
bool& sessionEndExit() { return ::frozen; }
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
bool _sapp_tc_mac_frozen() { return frozen; }
bool _sapp_tc_mac_busy() { return busy; }
AEEventID _sapp_tc_mac_quit_reason() { return reason; }
bool _sapp_tc_request_exit() { ++queries;return allow; }
'''+body(mac,'static bool _sapp_tc_mac_session_busy(')+body(mac,'static bool _sapp_tc_win32_session_busy(')+body(mac,'static bool _sapp_tc_mac_is_session_end(')+body(mac,'static void _sapp_tc_mac_session_end(')
runtime=(root/'core/include/tc/app/tcWindowRuntime.cpp').read_text(encoding='utf-8')
code+='namespace trussc { class Runtime { public: void setLogoutTimeout(float); }; int errors=0; struct Sink { template<class T> Sink& operator<<(T&&) { return *this; } }; Sink logError(const char*) { ++errors;return {}; }\n'
code+=body(runtime,'void Runtime::setLogoutTimeout(float seconds) {')+'\n}\n'
method=body(mac,'- (NSApplicationTerminateReply)applicationShouldTerminate:')
method=method.replace('- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender','NSApplicationTerminateReply shouldTerminate(NSApplication* sender)')
power=body(mac,'- (void)sessionWillPowerOff:').replace('- (void)sessionWillPowerOff:(NSNotification*)notification','void willPowerOff(NSNotification* notification)')
launch=body(mac,'- (void)applicationDidFinishLaunching:')
assert 'NSWorkspaceWillPowerOffNotification' in launch and '@selector(sessionWillPowerOff:)' in launch
code+='struct NSNotification {};\n'+power+method+r'''
int main() {
 trussc::Runtime config;
 config.setLogoutTimeout(6);
 for(float bad:{-1.0f,-INFINITY,NAN}) { config.setLogoutTimeout(bad);check("negative/NaN retain previous value",trussc::internal::runtime().logoutTimeout==6); }
 check("invalid timeout logs Error",trussc::errors==3);
 config.setLogoutTimeout(0);check("zero numeric value",trussc::internal::runtime().logoutTimeout==0);
 config.setLogoutTimeout(INFINITY);check("positive infinity numeric value",std::isinf(trussc::internal::runtime().logoutTimeout));
 frozen=true;config.setLogoutTimeout(1);check("committed timeout immutable",std::isinf(trussc::internal::runtime().logoutTimeout));frozen=false;
 auto* authority=createSessionAuthority();
 check("authority independent of user HWND",authority!=nullptr);
 for(bool a:{false,true}) {allow=a;check("Win query forwards veto",sessionAuthorityProc(authority,WM_QUERYENDSESSION,0,0)==a);}
 sessionAuthorityProc(authority,WM_ENDSESSION,0,0);check("Win cancellation forwarded",cancels==1 && commits==0);
 try { sessionAuthorityProc(authority,WM_ENDSESSION,1,0); } catch(int) {}
 check("Win commit forwards forced ending",commits==1);
 destroySessionAuthority(authority);check("Runtime destroys authority",nativeCalls==1);
 for(unsigned r:{0x6c6f676fu,0x726c676fu,0x72727374u,0x72657374u,0x7273646eu,0x73687574u}) {
  for(bool b:{false,true}) {reason=r;busy=b;allow=true;int before=commits;try{shouldTerminate(nullptr);}catch(int){}check("every mac session reason commits idle and busy",commits==before+1);}
 }
 reason=0x73687574;busy=false;allow=false;
 check("mac query veto cancels before commit",shouldTerminate(nullptr)==NSTerminateCancel);
 int beforePower=commits;try{willPowerOff(nullptr);}catch(int){}check("power-off notification commits without veto",commits==beforePower+1);
 reason=0;busy=true;allow=true;
 check("ordinary nested terminate deferred",shouldTerminate(nullptr)==NSTerminateCancel && _sapp_tc.terminate_pending && retired==0);
 busy=false;
 check("common ending precedes AppKit remaining closes",shouldTerminate(nullptr)==NSTerminateNow && retired==3);
 frozen=true;check("repeat frozen terminate inert",shouldTerminate(nullptr)==NSTerminateCancel);
 return failures?1:0;
}
'''
global_source=(root/'core/include/tc/app/tcGlobal.cpp').read_text(encoding='utf-8')
gpu=r'''
namespace gpu {
int operations=0;bool valid=false;
bool frameworkShutdown() { return false; }
bool sg_isvalid() { return valid; }
bool sessionEndExit() { return ::frozen; }
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
 frozen=true;acquireBoundSwapchain(&w);
 check("frozen native acquisition does no GPU work",operations==0);
 frozen=false;check("live acquisition remains admitted",acquireBoundSwapchain(&w).width==7);
}
}
'''
code=code.replace('int main() {',gpu+'\nint main() {\n gpu::test();')

# The production call before NSTerminateNow is the regression for synchronous
# performClose with two remaining windows. Deleting it must fail behaviorally.
variants=[('baseline',code),('late-appkit-ending',code.replace('    trussc::internal::shutdownRuntime();','')),
 ('late-gpu-release',code.replace('frameworkShutdown() || !sg_isvalid()', 'frameworkShutdown()')),
 ('frozen-acquisition',code.replace('    if (sessionEndExit()) return {};', ''))]
labels={'late-appkit-ending':'common ending precedes AppKit remaining closes',
        'late-gpu-release':'late Window release cannot touch sgl after sg shutdown',
        'frozen-acquisition':'frozen native acquisition does no GPU work'}
for name,text in variants:
 cpp=scratch/(name+'.cpp');binary=scratch/name;cpp.write_text(text,encoding='utf-8')
 subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-DNDEBUG',str(cpp),'-o',str(binary)],check=True)
 r=subprocess.run([str(binary)],capture_output=True,text=True,encoding='utf-8',timeout=10)
 (out/('native-session-'+name+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8')
 if (name=='baseline' and r.returncode!=0) or (name!='baseline' and (r.returncode!=1 or labels[name]+': FAIL' not in r.stdout)):
  raise SystemExit(r.stdout+r.stderr)
 print(name,'PASS' if name=='baseline' else 'mutation detected',flush=True)
