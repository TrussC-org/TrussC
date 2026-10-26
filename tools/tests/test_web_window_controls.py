#!/usr/bin/env python3
"""Compile Web title and Web/iOS/Android size bodies, run under Node.

Run through tcbuild.sh; this is a browser-free seam, not a WGPU device test.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def body(source, signature):
    start = source.rindex(signature)
    opening = source.index('{', start)
    depth = 1
    cursor = opening + 1
    while depth:
        if source[cursor] == '{':
            depth += 1
        elif source[cursor] == '}':
            depth -= 1
        cursor += 1
    return source[start:cursor]


def main():
    compiler = os.environ.get('EMXX') or shutil.which('em++')
    if not compiler:
        raise SystemExit('em++ required (set EMXX)')
    sokol = (ROOT / 'core/include/sokol/sokol_app_tc.h').read_text(encoding='utf-8')
    platform = (ROOT / 'core/platform/web/tcPlatform_web.cpp').read_text(encoding='utf-8')
    title = body(sokol, 'SOKOL_API_IMPL void sapp_set_window_title(')
    size = body(platform, 'void internal::setMainWindowSizeLogical(')
    window = (ROOT / 'core/include/tc/app/tcWindow.h').read_text(encoding='utf-8')
    public = (ROOT / 'core/include/TrussC.h').read_text(encoding='utf-8')
    global_cpp = (ROOT / 'core/include/tc/app/tcGlobal.cpp').read_text(encoding='utf-8')
    wrappers = '\n'.join([body(public, 'inline void setWindowSize('),
                          body(global_cpp, 'void setWindowSizeLogical('),
                          body(window, 'inline IVec2 Window::control('),
                          body(window, 'inline void Window::setSize('),
                          body(window, 'inline void App::setSize(')])
    platform_header = (ROOT / 'core/include/tcPlatform.h').read_text(encoding='utf-8')
    detection = body(platform_header, 'struct Platform {') + ';'
    predicate = body(platform_header, 'constexpr bool windowSizeIsScreenControlled(')
    initial = re.search(r'const char\* document_title = .*?;', sokol).group()
    init_start = sokol.index('    if (c_str_document_title !== 0)')
    init_end = sokol.index('    const target_selector_str', init_start)
    initial_js = sokol[init_start:init_end]
    source = r'''
#include <emscripten.h>
#include <emscripten/html5.h>
#include <cstring>
#include <cassert>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include "tc/app/internal/tcWindowFullscreen.h"
using trussc::internal::WindowGeometryCall;
using trussc::internal::WindowFullscreenPhase;
#define SOKOL_API_IMPL
#define SOKOL_ASSERT assert
#define _SAPP_EMSCRIPTEN
struct { char window_title[128]; } _sapp_tc;
void _sapp_tc_strcpy(const char* src, char* dst, size_t n) { std::strncpy(dst, src, n); }
struct Log { template<class T> Log& operator<<(const T&) { return *this; } };
int notices = 0;
std::string noticeText;
struct NoticeLog { NoticeLog& operator<<(const char* message) { noticeText = message; return *this; } };
NoticeLog logNotice(const char* tag) { assert(std::string(tag) == "Platform"); ++notices; return {}; }
PLATFORM_DETECTION
namespace internal { SIZE_PREDICATE }

// Stand-ins supply only native/ownership dependencies. All four public size
// entry points, their validation/conversion and Window::control below are the
// production bodies. Platform detection is separately preprocessed as Web,
// iOS or Android, without importing unavailable mobile SDK headers.
struct IVec2 { int x, y; };
bool valid = true, pixelPerfect = true;
float sapp_dpi_scale() { return 2; }
bool sapp_isvalid() { return valid; }
bool sapp_is_fullscreen() { return false; }
void sapp_toggle_fullscreen() {}
Log logWarning(const char*) { return {}; }
namespace internal {
using trussc::internal::WindowGeometryCall;
using trussc::internal::WindowFullscreenPhase;
WindowFullscreenPhase mainWindowFullscreenPhase() { return WindowFullscreenPhase::Fullscreen; }
bool ignoreFullscreenGeometry(WindowFullscreenPhase, WindowGeometryCall) { assert(false); return true; }
void setMainFullscreen(bool) {}
void setMainWindowSizeLogical(int, int);
enum class WindowControl { Title, PixelSize, Size, Position, PositionQuery, Fullscreen, ToggleFullscreen };
bool& pixelPerfectMode() { return pixelPerfect; }
void setMainWindowPosition(int, int) {}
IVec2 mainWindowPosition() { return {-1, -1}; }
}
namespace mcp::internal { void mainWindowTitle(const std::string&) {} }
struct RectNode { void setSize(float, float) { assert(false); } };
struct Window {
    struct { bool isMain = true; float dpiScale = 2; } ctx_;
    std::string title_;
    bool isOpen() const { return valid; }
    void setSecondaryTitle(const std::string&) { assert(false); }
    void setSecondarySize(double, double, bool = false) { assert(false); }
    void setSecondaryPosition(int, int) { assert(false); }
    IVec2 getSecondaryPosition() { assert(false); return {}; }
    bool isSecondaryFullscreen() const { return false; }
    void setSecondaryFullscreen(bool) { assert(false); }
    IVec2 control(internal::WindowControl, int = 0, int = 0, const std::string* = nullptr, internal::WindowGeometryCall = internal::WindowGeometryCall::WindowSize);
    void setSize(int, int);
};
struct App : RectNode, std::enable_shared_from_this<App> {
    Window* attached = nullptr;
    bool unownedSetSizeWarned_ = false;
    Window* getWindow() { return attached; }
    void setSize(float, float);
};
Window* current = nullptr;
namespace internal {
using Hook = bool (*)(Window&, WindowControl, int, int, const std::string*, IVec2&);
Hook windowControlHookForTests() { return nullptr; }
bool routeSetWindowSizeToWindow(int w, int h, WindowGeometryCall = WindowGeometryCall::GlobalLogicalSize) { if (!current) return false; current->setSize(w, h); return true; }
struct WindowRequestAccess {
    static bool ignoreGeometry(Window&, WindowGeometryCall) { assert(false); return true; }
    static void setSize(Window& win, int w, int h, WindowGeometryCall call) { win.control(WindowControl::Size, w, h, nullptr, call); }
    static void setPixelSize(Window& win, int w, int h, WindowGeometryCall = WindowGeometryCall::GlobalSize) { win.control(WindowControl::PixelSize, w, h); }
};
bool routeSetWindowPixelSizeToWindow(int w, int h, WindowGeometryCall = WindowGeometryCall::GlobalSize) { if (!current) return false; WindowRequestAccess::setPixelSize(*current, w, h); return true; }
}
void setWindowSizeLogical(int, int);
EM_JS(void, init_title, (const char* c_str_document_title), { INITIAL_JS });
TITLE_BODY
SIZE_BODY
PUBLIC_BODIES
int main() {
    EM_ASM({ globalThis.document = {title: '{{{ TITLE }}}'}; });
    std::strcpy(_sapp_tc.window_title, "Initial title");
    struct { struct { bool update_document_title = false; } html5; } descriptor;
    auto* desc = &descriptor;
    INITIAL_CPP
    init_title(document_title);
    assert(EM_ASM_INT({ return document.title === 'Initial title'; }));
    sapp_set_window_title("Unicode \xe9\x89\x84\xe5\xa1\x94 <literal>");
    assert(EM_ASM_INT({ return document.title === 'Unicode 鉄塔 <literal>'; }));
    EM_ASM({ globalThis.canvas = {}; canvas.width = 2000; canvas.height = 1314; canvas.style = {}; canvas.style.width = '100%'; canvas.style.height = '100%'; document.querySelector = (selector) => canvas; });
    internal::setMainWindowSizeLogical(800, 600);
    assert(EM_ASM_INT({ return canvas.width === 2000 && canvas.height === 1314 && canvas.style.width === '100%' && canvas.style.height === '100%'; }));
    assert(notices == 1);
    assert(noticeText.find("ENDPOINT_PLATFORM") != std::string::npos);
    Window window;
    current = &window;
    auto app = std::make_shared<App>();
    app->attached = &window;
    int expectedNotices = notices;
    auto verify = [&] {
        assert(notices == ++expectedNotices); // exactly one Notice per public call
        assert(EM_ASM_INT({ return canvas.width === 2000 && canvas.height === 1314 && canvas.style.width === '100%' && canvas.style.height === '100%'; }));
    };
    // Repeat every API, including positive framebuffer extents which truncate
    // to logical zero at DPR 2, and invalid extents which used to return early.
    const int sizes[][2] = {{800, 600}, {1, 1}, {0, 100}, {100, 0}, {-1, 100}, {100, -1}};
    // Include pre-window globals and closed Window handles: Notice comes first.
    current = nullptr;
    setWindowSize(0, 0); verify();
    setWindowSizeLogical(0, 0); verify();
    current = &window;
    valid = false;
    window.setSize(0, 0); verify();
    valid = true;
    for (bool perfect : {true, false}) {
        pixelPerfect = perfect;
        for (int repeat = 0; repeat < 2; ++repeat) {
            for (auto& extent : sizes) {
                setWindowSize(extent[0], extent[1]); verify();
                setWindowSizeLogical(extent[0], extent[1]); verify();
                window.setSize(extent[0], extent[1]); verify();
                app->setSize(extent[0], extent[1]); verify();
            }
        }
    }
    app->setSize(std::numeric_limits<float>::quiet_NaN(), 100); verify();
    app->setSize(std::numeric_limits<float>::infinity(), 100); verify();
    return 0;
}
'''.replace('INITIAL_JS', initial_js).replace('TITLE_BODY', title).replace('SIZE_BODY', size).replace('INITIAL_CPP', initial).replace('PUBLIC_BODIES', wrappers)
    with tempfile.TemporaryDirectory(prefix='web-window-controls-') as temp:
        path = Path(temp)
        for target in ('Web', 'iOS', 'Android'):
            # Compile the actual Platform and predicate with only their platform
            # macros changed. em++ remains the runner for the Node dependencies.
            if target == 'Web':
                target_detection = detection
                endpoint = size
            else:
                macros = ('#define __APPLE__ 1\n#define TARGET_OS_IOS 1\n'
                          '#define TARGET_OS_IPHONE 1\n' if target == 'iOS' else '#define __ANDROID__ 1\n')
                target_detection = '#undef __EMSCRIPTEN__\n' + macros + detection
                target_detection += f'\nstatic_assert(Platform::{"isIOS" if target == "iOS" else "isAndroid"}() && !Platform::isWeb());\n'
                target_detection += '\n#undef __APPLE__\n#undef __ANDROID__\n#define __EMSCRIPTEN__ 1\n'
                folder, extension = ('ios', 'mm') if target == 'iOS' else ('android', 'cpp')
                native = (ROOT / f'core/platform/{folder}/tcPlatform_{folder}.{extension}').read_text(encoding='utf-8')
                endpoint = body(native, 'void internal::setMainWindowSizeLogical(')
            fixture = source.replace(size, endpoint).replace('PLATFORM_DETECTION', target_detection).replace('SIZE_PREDICATE', predicate).replace('ENDPOINT_PLATFORM', target)
            (path / 'main.cpp').write_text(fixture, encoding='utf-8')
            subprocess.run([compiler, '-std=c++20', '-I' + str(ROOT / 'core/include'), str(path / 'main.cpp'), '-o', str(path / 'test.js'), '-sENVIRONMENT=node', '-sSINGLE_FILE=1', '-sASSERTIONS=1'], check=True)
            subprocess.run(['node', str(path / 'test.js')], check=True)
            print(f'PASS: {target} production predicate, endpoint and four public size APIs: every-call Notice (Node)', flush=True)
    print('PASS: production Web title and unchanged CSS/drawable; mobile macro simulation does not syntax-check SDK-dependent translation units')


if __name__ == '__main__':
    main()
