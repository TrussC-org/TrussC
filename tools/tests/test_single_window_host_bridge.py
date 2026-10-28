#!/usr/bin/env python3
"""Execute the common single-window bridge and Android swap boundary with stubs.
Also link the production Web sokol implementation with the common tick body.
Run through tcbuild.sh. No mobile SDK or browser/device run is claimed.
"""
from pathlib import Path
import json
import os
import re
import shutil
import subprocess
import tempfile
from test_web_window_controls import body

ROOT = Path(__file__).resolve().parents[2]
RUNTIME = Path(os.environ.get('TC_SINGLE_WINDOW_RUNTIME', ROOT/'core/include/tc/app/tcWindowRuntime.cpp')).read_text(encoding='utf-8')
NATIVE = (ROOT/'core/include/sokol/sokol_app_tc.h').read_text(encoding='utf-8')
common = '\n'.join(body(RUNTIME, signature) for signature in (
    'void refreshMainWindowMetadata(', 'bool enterWindowTick(', 'void windowTick(', 'void singleWindowFrame('))
parts = []
# Use each real platform's implementation of the legacy skip endpoint.
skips = list(re.finditer(r'SOKOL_API_IMPL void sapp_skip_present\(void\) \{', NATIVE))
assert len(skips) == 3
for platform, match in zip(('ios','android','web'), skips):
    skip = body(NATIVE[match.start():], 'SOKOL_API_IMPL void sapp_skip_present(') if platform == 'web' else body(NATIVE[match.start():NATIVE.index('}',match.start())+1], 'SOKOL_API_IMPL void sapp_skip_present(')
    parts.append(f'namespace {platform} {{\n' + r'''
struct sapp_window { unsigned id=0; };
struct sapp_event {};
struct sapp_tc_window_metadata { int fb_width=0,fb_height=0,sample_count=0,color_format=0,depth_format=0; float dpi_scale=0; bool trust_hidden_signals=false,occluded=false,drawable_available=false; double source_interval=0; };
struct Context { double tickDuration=0; float mouseX=3,mouseY=4,pmouseX=0,pmouseY=0; struct { bool empty() { return true; } } pendingScreenshotPaths; struct { decltype(pendingScreenshotPaths) pending; } asyncScreenshots; void* deferredResponseTarget=nullptr; bool drawWhileHidden=true; int fbWidth=100,fbHeight=50; };
struct Binding { sapp_window handle; sapp_tc_window_metadata metadata; };
struct Window { Binding binding; Context ctx; Context& context() { return ctx; } bool isOccluded() { return false; } } window;
struct WindowRequestAccess { static Binding& binding(Window& w) { return w.binding; } };
Window* mainWindow() { return &window; }
constexpr bool supportsSecondaryWindows() { return false; }
bool sapp_isvalid() { return true; }
int sapp_width() { return 100; } int sapp_height() { return 50; }
float sapp_dpi_scale() { return 2; } int sapp_sample_count() { return 1; }
int sapp_color_format() { return 1; } int sapp_depth_format() { return 2; }
bool sapp_window_occluded(sapp_window) { return false; }
double nativeDuration=1.0/120.0;
double sapp_frame_duration() { return nativeDuration; }
int stubReads=0, desktopSkips=0;
double sapp_window_frame_duration(sapp_window) { ++stubReads; return 1.0/60.0; }
void sapp_tc_window_skip_present(sapp_window) { ++desktopSkips; }
void publishWindowMetadata(Window& w,const sapp_tc_window_metadata& m) { w.binding.metadata=m; }
bool refreshWindowMetadata(Window&) { return true; }
void syncWindowAppSize(Window&) {}
struct WindowEntryScope { WindowEntryScope(Window&,bool) { refreshMainWindowMetadata(); } operator bool() { return true; } static void refreshMainWindowMetadata(); };
struct TeardownSteps { template<class F> void run(const char*,F fn) { fn(); } };
struct WindowTickPresentation {};
struct CrashPhaseScope { CrashPhaseScope(const char*) {} };
void crashFrame(int) {} int sapp_frame_count() { return 0; }
int windowClockNow() { return 0; }
bool admitDraw=false;
int ticks=0, turns=0;
void beforeHostTurn() { ++turns; } void afterHostTurn() { --turns; }
namespace mcp::internal { bool hasDeferredResponsesForTarget(void*) { return false; } }
bool runWindowTick(int,double duration,bool,const WindowTickPresentation*,bool) { ++ticks; window.ctx.tickDuration=duration; return admitDraw; }
struct { bool skip_present=false; int timing=0; struct { int display=1,context=1,surface=1; struct { void* window=nullptr; } current; } android; } _sapp_tc;
''' + skip + '\n' + common.replace('std::chrono::steady_clock::time_point', 'int') + r'''
void WindowEntryScope::refreshMainWindowMetadata() { NAMESPACE::refreshMainWindowMetadata(); }
int swaps=0;
constexpr int EGL_NO_DISPLAY=0,EGL_NO_CONTEXT=0,EGL_NO_SURFACE=0;
void _sapp_tc_timing_update(int*,double) {}
void _sapp_tc_android_update_dimensions(void*,bool) {}
void _sapp_tc_frame() { singleWindowFrame(); }
void eglSwapBuffers(int,int) { ++swaps; }
'''.replace('NAMESPACE', platform) + (body(NATIVE,'_SOKOL_PRIVATE void _sapp_tc_android_frame(') if platform == 'android' else '') + r'''
void run() {
    admitDraw=false; singleWindowFrame();
    check("120 Hz native interval reaches common tick", std::abs(window.ctx.tickDuration-1.0/120.0)<1e-12);
    check("metadata publishes native source interval", std::abs(window.binding.metadata.source_interval-1.0/120.0)<1e-12);
    check("secondary-window timing stub is never read",stubReads==0);
    check("no-draw reaches the native single-window skip endpoint",_sapp_tc.skip_present && desktopSkips==0);
    check("single-window host turn is balanced",turns==0 && ticks==1);
    _sapp_tc.skip_present=false; admitDraw=true; nativeDuration=1.0/144.0; singleWindowFrame();
    check("changing native source is preserved",std::abs(window.ctx.tickDuration-1.0/144.0)<1e-12);
    check("draw does not request presentation suppression",!_sapp_tc.skip_present && desktopSkips==0);
''' + (r'''
    admitDraw=false; _sapp_tc_android_frame(0);
    check("Android no-draw returns before eglSwapBuffers",swaps==0 && !_sapp_tc.skip_present);
    admitDraw=true; _sapp_tc_android_frame(1);
    check("Android one-shot skip leaves next admitted swap intact",swaps==1 && !_sapp_tc.skip_present);
''' if platform=='android' else '') + '}\n}\n')
prefix = r'''
#include <cstdio>
#include <cmath>
#define SOKOL_API_IMPL
#define _SOKOL_PRIVATE
#define SOKOL_ASSERT(x) check("native EGL handles valid",x)
int failures=0,checks=0;
void check(const char* label,bool ok) { ++checks; if (!ok) { ++failures; printf("single-window: FAIL: %s\n",label); } }
'''
source = prefix + ''.join(parts) + r'''
int main() { ios::run();android::run();web::run(); printf("single-window: %d checks, %d failures\n",checks,failures); return failures?1:0; }
'''
# Forward declarations are necessary because extraction retains production order.
source = source.replace('const WindowTickPresentation* presentation)', 'const WindowTickPresentation* presentation = nullptr)')
source = source.replace('struct WindowEntryScope {', 'void refreshMainWindowMetadata();\nstruct WindowEntryScope {')
variants=[('baseline',source)]
if os.environ.get('TC_SINGLE_WINDOW_MUTATIONS'):
    for name, old, new in (
        ('source_metadata_zero','metadata.source_interval = sapp_frame_duration();','metadata.source_interval = 0;'),
        ('source_uses_secondary_stub','supportsSecondaryWindows() ? sapp_window_frame_duration(handle) : sapp_frame_duration()','sapp_window_frame_duration(handle)'),
        ('skip_uses_desktop_endpoint','else sapp_skip_present();','else sapp_tc_window_skip_present(handle);'),
        ('skip_inert','_sapp_tc.skip_present = true;','/* mutation: inert native skip */'),
        ('android_swap_ignores_skip','if (_sapp_tc.skip_present) { _sapp_tc.skip_present = false; return; }','_sapp_tc.skip_present = false;'),
    ):
        assert old in source, name
        variants.append((name,source.replace(old,new)))
results=[]
with tempfile.TemporaryDirectory(prefix='tc-single-window-') as folder:
    folder=Path(folder)
    for name,code in variants:
        cpp=folder/'probe.cpp'; binary=folder/'probe'; cpp.write_text(code,encoding='utf-8')
        subprocess.run([os.environ.get('CXX','c++'),'-std=c++20',str(cpp),'-o',str(binary)],check=True)
        result=subprocess.run([str(binary)],text=True,encoding='utf-8',stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=15)
        failed=result.stdout.count('single-window: FAIL:')
        print(name,result.stdout,flush=True)
        valid=(result.returncode==0 and not failed) if name=='baseline' else (result.returncode==1 and failed>0)
        results.append(dict(name=name,returncode=result.returncode,failed_assertions=failed,valid=valid))
    compiler=os.environ.get('EMXX') or shutil.which('em++')
    if not compiler:
        candidate=Path('/home/toru/emsdk/upstream/emscripten/em++')
        compiler=str(candidate) if candidate.exists() else None
    if not compiler:
        raise SystemExit('Production Web link requires em++ (set EMXX)')
    # Production tick body, with host entry admission stubbed. Keeping its sapp
    # references live catches missing symbols even when the App never skips.
    web=r'''
#include "sokol_app_tc.h"
struct Window {};
struct TeardownSteps { template<class F> void run(const char*,F f) { f(); } };
constexpr bool supportsSecondaryWindows() { return false; }
int windowClockNow() { return 0; }
bool enterWindowTick(Window&,sapp_window,int,double) { return false; }
''' + body(RUNTIME,'void windowTick(') + r'''
int main() { Window window; windowTick({0},&window); return 0; }
'''
    cpp=folder/'web-link.cpp'; cpp.write_text(web,encoding='utf-8')
    command=[compiler,'-std=c++20','-DSOKOL_GLES3','-I'+str(ROOT/'core/include/sokol'),str(ROOT/'core/platform/web/sokol_impl.cpp'),str(cpp),'-sMIN_WEBGL_VERSION=2','-sMAX_WEBGL_VERSION=2','-sENVIRONMENT=node','-o',str(folder/'web-link.js')]
    subprocess.run(command,check=True)
    print('Production WebGL2 sokol + common tick link: PASS',flush=True)
    results.append(dict(name='production_web_link',returncode=0,valid=True))
if output:=os.environ.get('TC_SINGLE_WINDOW_OUTPUT'):
    Path(output).write_text(json.dumps(results,indent=2),encoding='utf-8')
raise SystemExit(any(not x['valid'] for x in results))
