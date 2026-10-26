#!/usr/bin/env python3
"""Host-check native creation, Win32/X11 pixel sizing and macOS point forwarding.

Run through tcbuild.sh. Host fixtures record native calls; the native validation fixture uses Mesa/Xvfb.
Window::control, Win32 forwarding, global sizing and X11 geometry use production code.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from test_web_window_controls import body

ROOT = Path(__file__).resolve().parents[2]


def main():
    window = (ROOT / 'core/include/tc/app/tcWindow.h').read_text(encoding='utf-8')
    win = (ROOT / 'core/platform/win/tcWindowWin.cpp').read_text(encoding='utf-8')
    platform = (ROOT / 'core/include/tcPlatform.h').read_text(encoding='utf-8')
    source = r'''
#include "core/include/sokol/util/sokol_window_size.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <string>
#define _WIN32
namespace trussc {
struct IVec2 { int x, y; };
struct Window;
struct AdapterState { int win = 2; };
float dpi = 1;
double seenWidth = 0, seenHeight = 0;
int calls = 0;
const void* seenHandle = nullptr;
const void* sapp_win32_get_hwnd() { return reinterpret_cast<void*>(1); }
const void* sapp_window_win32_get_hwnd(int win) { return reinterpret_cast<void*>(win); }
float sapp_dpi_scale() { return dpi; }
bool sapp_isvalid() { return true; }
bool sapp_is_fullscreen() { return false; }
void sapp_toggle_fullscreen() {}
void sapp_set_window_title(const char*) {}
namespace internal {
enum class WindowControl { Title, Size, PixelSize, Position, Fullscreen, PositionQuery };
constexpr bool windowSizeIsScreenControlled() { return false; }
constexpr bool supportsSecondaryWindows() { return true; }
using Hook = bool (*)(Window&, WindowControl, int, int, const std::string*, IVec2&);
Hook windowControlHookForTests() { return nullptr; }
void setMainWindowSizeLogical(int, int) { assert(false); }
void setMainWindowPosition(int, int) {}
IVec2 mainWindowPosition() { return {}; }
void setWindowSizeWin32(const void* handle, double w, double h) {
    ++calls; seenHandle = handle; seenWidth = w; seenHeight = h;
}
CLIENT_CONVERSION
}
namespace mcp::internal { void mainWindowTitle(const std::string&) {} }
struct Window {
    struct { bool isMain = false; float dpiScale = 1; } ctx_;
    void* native_ = nullptr;
    std::string title_;
    bool isOpen() const { return true; }
    void setSecondaryTitle(const std::string&) {}
    void setSecondaryPosition(int, int) {}
    IVec2 getSecondaryPosition() { return {}; }
    void setSecondaryFullscreen(bool) {}
    void setSecondarySize(double, double, bool = false);
    IVec2 control(internal::WindowControl, int, int, const std::string* = nullptr);
};
CONTROL_BODY
SECONDARY_BODY
}
int main() {
    using namespace trussc;
    AdapterState state;
    Window window; window.native_ = &state;
    for (bool mainWindow : {false, true}) {
        window.ctx_.isMain = mainWindow;
        for (float scale : {1.25f, 1.5f, 1.75f, 2.0f}) {
            dpi = window.ctx_.dpiScale = scale;
            const int previousCalls = calls;
            window.control(internal::WindowControl::PixelSize, 1001, 667);
            assert(calls == previousCalls + 1);
            assert(seenHandle == reinterpret_cast<void*>(mainWindow ? 1 : 2));
            assert(seenWidth == 1001.0 / scale && seenHeight == 667.0 / scale);
            assert(internal::windowClientExtentPixels(seenWidth, unsigned(scale * 96)) == 1001);
            assert(internal::windowClientExtentPixels(seenHeight, unsigned(scale * 96)) == 667);
        }
        const int previousCalls = calls;
        window.control(internal::WindowControl::PixelSize, -1, 667);
        window.control(internal::WindowControl::PixelSize, 0, 667);
        dpi = window.ctx_.dpiScale = std::numeric_limits<float>::min();
        window.control(internal::WindowControl::PixelSize, 1001, 667);
        assert(calls == previousCalls);
    }
}
'''
    source = source.replace('CONTROL_BODY', body(window, 'inline IVec2 Window::control('))
    source = source.replace('SECONDARY_BODY', body(win, 'void Window::setSecondarySize('))
    source = source.replace('CLIENT_CONVERSION', body(platform, 'constexpr int windowClientExtentPixels('))
    with tempfile.TemporaryDirectory(prefix='window-pixel-size-') as scratch:
        cpp = Path(scratch) / 'probe.cpp'
        cpp.write_text(source, encoding='utf-8')
        binary = Path(scratch) / 'probe'
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT), str(cpp), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print('PASS: main/secondary Win32 fractional dispatch and forwarding, 125/150/175/200%')


def creation_source(backend):
    """Replay production descriptor/creation arithmetic with native size recording.

    Only OS calls/types are replaced. Statements come from the actual launcher,
    adapters and native creators, so an old conversion compiles but fails at run time.
    """
    header = (ROOT / 'core/include/TrussC.h').read_text(encoding='utf-8')
    native = (ROOT / 'core/include/sokol/sokol_app_tc.h').read_text(encoding='utf-8')
    launcher = body(header, 'inline sapp_desc buildWindowDescriptor(').split('{', 1)[1].split('    const auto& windowTitle', 1)[0]
    normal = body(header, 'sapp_desc buildAppDescriptor(').rsplit('    return ', 1)[1].split(';', 1)[0]
    normal = 'return ' + normal + ';'
    hot_header = (ROOT / 'core/include/tc/app/tcHotReloadHost.h').read_text(encoding='utf-8')
    hot = body(hot_header, 'inline int runHotReloadApp(').split('    sapp_desc desc = ', 1)[1].split('    desc.frame_cb', 1)[0]
    hot = 'sapp_desc desc = ' + hot
    adapter_path = {'win': 'win/tcWindowWin.cpp', 'x11': 'linux/tcWindowLinux.cpp',
                    'mac': 'mac/tcWindowMac.mm', 'egl': 'linux/tcWindowLinux.cpp'}[backend]
    adapter = (ROOT / 'core/platform' / adapter_path).read_text(encoding='utf-8')
    secondary = adapter.split('    sapp_window_desc d = {};', 1)[1].split('    d.title', 1)[0]
    main_signature = {'win': 'static bool _sapp_tc_win32_create_main_window(',
                      'x11': 'static bool _sapp_tc_x11_create_main_window(',
                      'mac': 'static bool _sapp_tc_create_main_window(',
                      'egl': 'static bool _sapp_tc_x11_create_main_window('}[backend]
    main = body(native, main_signature)
    transfer = main.split('    w->desc.width = ', 1)[1].split('    w->desc.event_cb', 1)[0]
    transfer = '    w->desc.width = ' + transfer
    if backend == 'mac':
        # Main's descriptor has already chosen the non-positive native defaults.
        transfer = transfer.replace('= width;', '= d->width;').replace('= height;', '= d->height;')
    def guard(creator):
        call = '_sapp_tc_window_pixels_representable('
        return call + creator.split(call, 1)[1].split(')', 1)[0] + ')'

    validation = ''
    if backend == 'win':
        creator = body(native, 'static bool _sapp_tc_win32_create_native_window(')
        arithmetic = creator.split('        const double width = ', 1)[1].split('        RECT rect', 1)[0]
        arithmetic = 'const double width = ' + arithmetic + '\nreturn {pw, ph};'
        validation = 'if (!' + guard(creator) + ') return false;'
    elif backend == 'x11':
        creator = body(native, 'static bool _sapp_tc_x11_create_native_window(')
        arithmetic = creator.split('    const float scale = ', 1)[1].split('    if (width_pt <= 0)', 1)[0]
        native_validation = creator.split('{', 1)[1].split('    XVisualInfo*', 1)[0]
        arithmetic = native_validation + 'const float scale = ' + arithmetic + '\nreturn {px_w, px_h};'
    elif backend == 'egl':
        creator = body(native, '_SOKOL_PRIVATE void _sapp_tc_x11_create_window(')
        arithmetic = creator.split('    const float window_scale = ', 1)[1].split('    if (0 == _sapp_tc.window_width)', 1)[0]
        arithmetic = 'const float window_scale = ' + arithmetic + '\nreturn {x11_window_width, x11_window_height};'
        validation = 'if (!' + guard(creator) + ') return false;'
    else:
        creator = body(native, main_signature)
        call = creator.split('[w->window setContentSize:NSMakeSize(', 1)[1].split(')];', 1)[0]
        secondary_creator = native[native.index('sapp_window sapp_create_window(const sapp_window_desc* desc) {'):]
        secondary_call = secondary_creator.split('[w->window setContentSize:NSMakeSize(', 1)[1].split(')];', 1)[0]
        validation = ('if (!(mainWindow ? ' + guard(creator) + ' : ' +
                      guard(secondary_creator) + ')) return false;')
        arithmetic = 'if (mainWindow) return roundMac(' + call + ');\nreturn roundMac(' + secondary_call + ');'
    template = (ROOT / 'tools/tests/window_creation_size.cpp').read_text(encoding='utf-8')
    return (template.replace('EGL_DIMENSIONS', body(native, '_SOKOL_PRIVATE void _sapp_tc_x11_update_dimensions('))
            .replace('NORMAL_LAUNCHER', normal).replace('HOT_LAUNCHER', hot)
            .replace('LAUNCHER', launcher).replace('SECONDARY', secondary)
            .replace('TRANSFER', transfer).replace('CREATION_GUARD', validation).replace('ARITHMETIC', arithmetic)
            .replace('GLX_DIMENSIONS', body(native, 'static bool _sapp_tc_x11_update_dimensions(')))


class WindowPixelSizeTests(unittest.TestCase):
    def test_creation_matches_resize(self):
        with tempfile.TemporaryDirectory(prefix='window-create-') as scratch:
            cpp = Path(scratch) / 'probe.cpp'
            binary = Path(scratch) / 'probe'
            for backend in ('win', 'x11', 'egl', 'mac'):
                with self.subTest(backend=backend):
                    cpp.write_text(creation_source(backend), encoding='utf-8')
                    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall',
                                    '-Wextra', '-Werror', '-fsanitize=undefined,float-cast-overflow',
                                    '-fno-sanitize-recover=all', '-I', str(ROOT),
                                    '-DTEST_' + backend.upper(), str(cpp), '-o', str(binary)], check=True)
                    subprocess.run([str(binary)], check=True)

    def test_native_invalid_sizes_match_base(self):
        # Isolate expected assertion failures in child processes and compare real
        # native creation/default selection to the target branch, per dimension.
        header = (ROOT / 'core/include/TrussC.h').read_text(encoding='utf-8')
        launcher = body(header, 'inline sapp_desc buildWindowDescriptor(').split('{', 1)[1].split('    const auto& windowTitle', 1)[0]
        normal = 'return ' + body(header, 'sapp_desc buildAppDescriptor(').rsplit('    return ', 1)[1].split(';', 1)[0] + ';'
        source = (ROOT / 'tools/tests/window_creation_native.cpp').read_text(encoding='utf-8')
        adapter = (ROOT / 'core/platform/linux/tcWindowLinux.cpp').read_text(encoding='utf-8')
        secondary = adapter.split('    sapp_window_desc d = {};', 1)[1].split('    d.title', 1)[0]
        source = source.replace('NORMAL_LAUNCHER', normal).replace('LAUNCHER', launcher).replace('SECONDARY', secondary)
        runner = r'''import re, subprocess, sys
for binary in sys.argv[1:-1]:
    for pixels in (False, True):
        for width, height in ((-1,667),(0,667),(667,-1),(0,0)):
            results = [subprocess.run([binary, mode, str(width), str(height), str(int(pixels))],
                capture_output=True, text=True) for mode in ('base','current')]
            base, current = results
            assert base.returncode == current.returncode, (binary,pixels,width,height,base,current)
            for dimension, extent in enumerate((width,height)):
                if extent > 0: continue
                for label in ('descriptor','native'):
                    values = [re.search(label+r'=(\d+|-\d+)x(\d+|-\d+)', r.stdout) for r in results]
                    assert bool(values[0]) == bool(values[1]), (label,results)
                    if values[0]:
                        assert values[0].group(dimension+1) == values[1].group(dimension+1), (label,results)
            if base.returncode:
                assert 'Assertion' in base.stderr and 'Assertion' in current.stderr, results
            else:
                assert 'native=' in current.stdout, results
            print('PASS native invalid input:',binary.rsplit('/',1)[-1],pixels,width,height,base.returncode)
subprocess.run(['xrdb','-merge'], input='Xft.dpi: 9.6e-7\n', text=True, check=True)
subprocess.run([sys.argv[-1],'tiny','640','400','1'], check=True)
'''
        with tempfile.TemporaryDirectory(prefix='window-native-') as scratch:
            cpp = Path(scratch) / 'probe.cpp'; cpp.write_text(source, encoding='utf-8')
            binaries = []
            for backend, libraries in (('SOKOL_GLES3', ['-lEGL','-lGLESv2']),
                                       ('SOKOL_GLCORE', ['-lGL'])):
                for release in (False, True):
                    binary = Path(scratch) / (backend + ('-release' if release else ''))
                    flags = ['-DNDEBUG'] if release else []
                    subprocess.run([os.environ.get('CXX','c++'), '-std=c++20', '-D'+backend,
                        *flags, '-fsanitize=undefined,float-cast-overflow', '-fno-sanitize-recover=all',
                        '-I', str(ROOT / 'core/include/sokol'), str(cpp), *libraries,
                        '-lX11','-lXi','-lXcursor','-lpthread','-ldl','-o',str(binary)], check=True)
                    binaries.append(str(binary))
            # Keep the debug GLX binary last so the native rejection assertion is active.
            binaries.append(binaries[2])
            subprocess.run(['xvfb-run','-a','-s','-screen 0 1280x720x24 -noreset',
                'bash','-c', 'ulimit -c 0; printf "Xft.dpi: 192\\n" | xrdb -merge; exec "$@"',
                'native-validation', 'python3','-c',runner,*binaries], check=True,
                env=dict(os.environ, LIBGL_ALWAYS_SOFTWARE='1',
                    __EGL_VENDOR_LIBRARY_FILENAMES='/usr/share/glvnd/egl_vendor.d/50_mesa.json',
                    __GLX_VENDOR_LIBRARY_NAME='mesa', GALLIUM_DRIVER='llvmpipe'))

    def test_mac_main_secondary_points(self):
        window = (ROOT / 'core/include/tc/app/tcWindow.h').read_text(encoding='utf-8')
        # Reuse the actual dispatcher with recording AppKit endpoints, including
        # a main build with secondary support disabled.
        template = (ROOT / 'tools/tests/window_mac_size.cpp').read_text(encoding='utf-8')
        source = template.replace('CONTROL_BODY', body(window, 'inline IVec2 Window::control('))
        for token, path, signature in (
                ('MAC_MAIN_CALL', 'core/platform/mac/tcPlatform_mac.mm', 'void internal::setMainWindowSizeFractionalMac('),
                ('MAC_SECONDARY_CALL', 'core/platform/mac/tcWindowMac.mm', 'void Window::setSecondarySize(')):
            endpoint = body((ROOT / path).read_text(encoding='utf-8'), signature)
            arguments = endpoint.split('NSMakeSize(', 1)[1].split(')];', 1)[0]
            source = source.replace(token, arguments)
        with tempfile.TemporaryDirectory(prefix='window-mac-size-') as scratch:
            cpp = Path(scratch) / 'probe.cpp'; cpp.write_text(source, encoding='utf-8')
            binary = Path(scratch) / 'probe'
            for secondary in (0, 1):
                subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall',
                                '-Wextra', '-Werror', '-I', str(ROOT),
                                '-DTEST_SECONDARY=' + str(secondary), str(cpp), '-o', str(binary)], check=True)
                subprocess.run([str(binary)], check=True)

    def test_win32_fractional_forwarding(self):
        main()

    def test_x11_main_independent_of_secondary_support(self):
        source = (ROOT / 'tools/tests/window_pixel_size_x11.cpp').read_text(encoding='utf-8')
        source = source.replace('CONTROL_BODY', body(
            (ROOT / 'core/include/tc/app/tcWindow.h').read_text(encoding='utf-8'),
            'inline IVec2 Window::control('))
        source = source.replace('GLOBAL_BODY', body(
            (ROOT / 'core/include/TrussC.h').read_text(encoding='utf-8'),
            'inline void setWindowSize('))
        seed = body((ROOT / 'core/platform/linux/tcPlatform_linux.cpp').read_text(encoding='utf-8'),
                    'internal::X11GeometryState& mainGeometryState(')
        seed = 'geometry =' + seed.split('mainGeometry =', 1)[1].split(';', 1)[0] + ';'
        source = source.replace('CREATION_GEOMETRY_SEED', seed)
        with tempfile.TemporaryDirectory(prefix='window-pixel-x11-') as scratch:
            cpp = Path(scratch) / 'probe.cpp'
            cpp.write_text(source, encoding='utf-8')
            binary = Path(scratch) / 'probe'
            for backend in ('SOKOL_GLCORE', 'SOKOL_GLES3'):
                for secondary in (0, 1):
                    with self.subTest(backend=backend, secondary=secondary):
                        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20',
                                        '-Wall', '-Wextra', '-Werror', '-I', str(ROOT),
                                        '-D' + backend, '-DTEST_SECONDARY=' + str(secondary),
                                        str(cpp), '-o', str(binary)], check=True)
                        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
