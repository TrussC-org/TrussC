"""Extract native pre-window getters and live read-through wiring, without OS SDKs.

Run through tcbuild.sh. macOS substitutes only the NSScreen query with a scale
provider; the native fallback/bound-window branches and dimension reads are exact.
"""
from pathlib import Path
import json
import os
import re
import runpy
import subprocess

ROOT = Path(__file__).resolve().parents[3]
OUT = Path(os.environ.get('TC_PREWINDOW_NATIVE_OUTPUT', ROOT / '.verification/cp3fu9/native-read-through'))
OUT.mkdir(parents=True, exist_ok=True)
body = runpy.run_path(str(ROOT / 'core/tests/windowTick/native-event-fixture.py'))['body']
native = (ROOT / 'core/include/sokol/sokol_app_tc.h').read_text(encoding='utf-8')
runtime = (ROOT / 'core/include/tc/app/tcWindowRuntime.cpp').read_text(encoding='utf-8')
global_source = (ROOT / 'core/include/tc/app/tcGlobal.cpp').read_text(encoding='utf-8')
headless = (ROOT / 'core/include/tc/app/tcHeadlessApp.h').read_text(encoding='utf-8')
header = (ROOT / 'core/include/TrussC.h').read_text(encoding='utf-8')
window_header = (ROOT / 'core/include/tc/app/tcWindow.h').read_text(encoding='utf-8')
constructor = body(runtime, 'Window::Window(bool mainWindow)')
constructor = constructor[constructor.index('{'):]
guard = body(window_header, 'inline bool usesNativeWindowDimensions(')
read_dimensions = body(window_header, 'inline WindowDimensions windowDimensions(')
launch = body(global_source, 'void createMainWindow(')
assert 'seedNativeWindowDimensions' not in runtime + global_source + headless
assert headless.index('internal::currentWindowCtx() = &ctx;') < headless.index('auto app = std::make_shared<AppClass>();')
assert header.index('openMainWindow();', header.index('inline void _setup_cb_impl')) < header.index('appSetupFunc();', header.index('inline void _setup_cb_impl'))
assert 'std::make_shared<AppClass>()' not in body(header, 'sapp_desc buildAppDescriptor(').split('internal::appSetupFunc = []()')[0]
getters = '\n'.join(body(header, marker) for marker in (
    'inline float getDpiScale()', 'inline int getFramebufferWidth()', 'inline int getFramebufferHeight()',
    'inline int getWidth()', 'inline int getHeight()', 'inline Vec2 getSize()',
    'inline int getWindowWidth()', 'inline int getWindowHeight()', 'inline Vec2 getWindowSize()', 'inline float getAspectRatio()'))
target_helpers = '\n'.join(body(header, marker) for marker in ('inline float targetPixelScale()', 'inline Vec2 targetLogicalSize()'))
conversions = '\n'.join(body(global_source, marker) for marker in ('int framebufferToLogical(', 'float appSizeDpi(', 'float appPointerScale('))
window_getters = '\n'.join(body(window_header, marker) for marker in ('inline int Window::getWidth()', 'inline int Window::getHeight()', 'inline bool Window::isOccluded()'))
stubs = r'''
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <algorithm>
#include <cmath>
struct Vec2 { float x,y; Vec2(float a,float b):x(a),y(b) {} };
struct _sapp_tc_window_t { int fb_width=0,fb_height=0; float dpi_scale=0; };
struct { struct { _sapp_tc_window_t* main=nullptr; } app; int framebuffer_width=0,framebuffer_height=0; float dpi_scale=0; } _sapp_tc;
float screenScale=2;
namespace trussc {
class Window;
struct WindowSettings { int width=1280,height=720; bool pixelPerfect=false; std::string title; };
namespace internal {
struct WindowContext {
    Window* hostWindow=nullptr; bool isMain=true,pauseUpdatesWhenHidden=false,drawWhileHidden=false,suspended=false;
    void *deferredResponseTarget=nullptr,*acquireSwapchain=nullptr,*acquireSwapchainUser=nullptr,*activateNative=nullptr;
    float dpiScale=1; int fbWidth=0,fbHeight=0;
};
struct WindowDimensions { float dpiScale; int fbWidth, fbHeight; };
struct Geometry { double width,height; bool pixels; };
struct Metadata { bool trust_hidden_signals=false, iconified=false, occluded=false; };
struct Binding { Geometry geometry; Metadata metadata; } binding;
}
class Window {
public:
    internal::WindowContext ctx_; internal::Binding binding_; bool clearColorSet_=false, bound=false;
    explicit Window(bool mainWindow);
    bool isOpen() const { return bound; }
    int getWidth() const;
    int getHeight() const;
    bool isOccluded() const;
};
namespace mcp::internal { void mainWindowTitle(const std::string&) {} }
namespace internal {
Window* selected=nullptr;
Window* mainWindow() { return selected; }
void destroyMainWindow() {}
struct WindowRequestAccess {
    static void resetForLaunch(Window&) {}
    static Binding& binding(Window&) { return internal::binding; }
};
bool pixels=false;
bool& pixelPerfectMode() { return pixels; }
int framebufferToLogical(int pixels,float dpi);
struct Target { bool isFbo=false; int width=1001,height=667; } target;
Target& currentTarget() { return target; }
WindowContext* overrideContext=nullptr;
WindowContext& currentWindowContext() { return overrideContext ? *overrideContext : selected->ctx_; }
}
}
'''
test = r'''
using namespace trussc;
int failures=0,checks=0;
void check(bool ok) { ++checks; if (!ok) { ++failures; std::puts("native-read-through: FAIL"); } }
void compare() {
    check(getDpiScale()==sapp_dpi_scale());
    check(getFramebufferWidth()==sapp_width()); check(getFramebufferHeight()==sapp_height());
    check(internal::captureWindowSize(internal::currentWindowContext()) == std::pair<size_t,size_t>(sapp_width(),sapp_height()));
    for (bool pixels : {false,true}) {
        internal::pixelPerfectMode()=pixels;
        const float dpi=pixels?1.f:sapp_dpi_scale();
        check(getWindowWidth()==int(sapp_width()/dpi)); check(getWindowHeight()==int(sapp_height()/dpi));
        check(getWindowSize().x==int(sapp_width()/dpi)); check(getWindowSize().y==int(sapp_height()/dpi));
        check(getAspectRatio()==float(sapp_width())/sapp_height());
        check(internal::framebufferToLogical(sapp_width(),getDpiScale())==int(sapp_width()/sapp_dpi_scale()));
        check(internal::appSizeDpi(getDpiScale())==dpi);
        check(internal::appPointerScale(getDpiScale())==1.f/dpi);
        for (bool fbo : {false,true}) {
            internal::target.isFbo=fbo;
            const float scale=fbo?1.f:dpi;
            check(getWidth()==int(1001/scale)); check(getHeight()==int(667/scale));
            check(getSize().x==int(1001/scale)); check(getSize().y==int(667/scale));
            check(internal::targetPixelScale()==scale);
            check(internal::targetLogicalSize().x==1001/scale); check(internal::targetLogicalSize().y==667/scale);
        }
    }
}
int main() {
    for (float dpi : {1.f,1.5f,2.f}) {
        screenScale=dpi;
        Window main(true); internal::selected=&main; compare();
        main.binding_.metadata={true,true,true};
        check(main.getWidth()==0 && main.getHeight()==0 && !main.isOccluded());
        Window secondary(false);
        check(secondary.ctx_.dpiScale==1 && secondary.ctx_.fbWidth==0 && secondary.ctx_.fbHeight==0);
        _sapp_tc_window_t nativeWindow; nativeWindow.dpi_scale=dpi; nativeWindow.fb_width=2561; nativeWindow.fb_height=1441;
        _sapp_tc.app.main=&nativeWindow;
        _sapp_tc.dpi_scale=dpi; _sapp_tc.framebuffer_width=2561; _sapp_tc.framebuffer_height=1441;
        internal::createMainWindow(WindowSettings{}); compare();
        main.ctx_.dpiScale=sapp_dpi_scale(); main.ctx_.fbWidth=sapp_width(); main.ctx_.fbHeight=sapp_height();
        main.bound=true;
        check(main.getWidth()==int(sapp_width()/sapp_dpi_scale()) && main.getHeight()==int(sapp_height()/sapp_dpi_scale()));
        nativeWindow.dpi_scale=3; nativeWindow.fb_width=3001; nativeWindow.fb_height=2001;
        _sapp_tc.dpi_scale=3; _sapp_tc.framebuffer_width=3001; _sapp_tc.framebuffer_height=2001;
        check(getDpiScale()==dpi && getFramebufferWidth()==2561 && getFramebufferHeight()==1441);
        main.bound=false; compare();
        // A headless context reads the live native values throughout the run.
        internal::WindowContext nonWindow; internal::overrideContext=&nonWindow;
        compare();
        nativeWindow.dpi_scale=1.25f; nativeWindow.fb_width=1701; nativeWindow.fb_height=951;
        _sapp_tc.dpi_scale=1.25f; _sapp_tc.framebuffer_width=1701; _sapp_tc.framebuffer_height=951;
        screenScale=1.25f; compare();
        check(nonWindow.hostWindow==nullptr);
        internal::overrideContext=nullptr;
        _sapp_tc.app.main=nullptr; _sapp_tc.dpi_scale=0; _sapp_tc.framebuffer_width=0; _sapp_tc.framebuffer_height=0;
        internal::createMainWindow(WindowSettings{}); compare();
    }
    std::printf("native-read-through: %d checks, %d failed\n",checks,failures);
    return failures?1:0;
}
'''
rows = []
for name, index in [('macOS', 0), ('iOS', 1), ('Win32', 2), ('Linux', 3), ('Android', 4), ('Web', 5)]:
    functions = []
    for function in ('sapp_dpi_scale', 'sapp_width', 'sapp_height'):
        matches = list(re.finditer(r'^(?:SOKOL_API_IMPL )?(?:int|float) ' + function + r'\(void\) \{', native, re.M))
        assert len(matches) == 6
        functions.append(body(native[matches[index].start():], matches[index].group(0)))
    exact = '\n'.join(functions)
    (OUT / (name + '-native.txt')).write_text(exact, encoding='utf-8')
    if name == 'macOS':
        assert exact.count('[NSScreen mainScreen].backingScaleFactor') == 1
        exact = exact.replace('[NSScreen mainScreen].backingScaleFactor', 'screenScale')
    code = '#define SOKOL_API_IMPL\n' + stubs + exact
    code += '\nnamespace trussc {\nWindow::Window(bool mainWindow) ' + constructor
    platform = dict(macOS=('mac', 'mm'), iOS=('ios', 'mm'), Win32=('win', 'cpp'),
                    Linux=('linux', 'cpp'), Android=('android', 'cpp'), Web=('web', 'cpp'))[name]
    adapter = (ROOT / f'core/platform/{platform[0]}/tcPlatform_{platform[0]}.{platform[1]}').read_text(encoding='utf-8')
    capture = body(adapter, 'std::pair<size_t, size_t> internal::captureWindowSize(')
    code += '\nnamespace internal {\n' + guard + read_dimensions + launch + conversions + '\nstd::pair<size_t,size_t> captureWindowSize(const WindowContext&);\n}\n' + capture + getters
    code += '\nnamespace internal {\n' + target_helpers + '\n}\n' + window_getters + '\n}\n' + test
    source = OUT / (name + '.cpp')
    source.write_text(code, encoding='utf-8')
    binary = OUT / (name + '-probe')
    subprocess.run(['c++', '-std=c++20', str(source), '-o', str(binary)], check=True)
    with (OUT / (name + '.log')).open('w', encoding='utf-8') as log:
        result = subprocess.run([str(binary)], stdout=log, stderr=subprocess.STDOUT)
    binary.unlink()
    rows.append(dict(adapter=name, returncode=result.returncode))
    print(rows[-1], flush=True)
(OUT / 'results.json').write_text(json.dumps(rows, indent=2), encoding='utf-8')
raise SystemExit(any(row['returncode'] for row in rows))
