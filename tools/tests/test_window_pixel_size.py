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
from unittest.mock import patch
from test_web_window_controls import body as source_body

ROOT = Path(__file__).resolve().parents[2]


def split_marker(source, marker, *, last=False):
    parts = source.rpartition(marker) if last else source.partition(marker)
    if not parts[1]:
        raise AssertionError(f'missing source marker: {marker!r}')
    return parts[0], parts[2]


def before(source, marker):
    return split_marker(source, marker)[0]


def after(source, marker):
    return split_marker(source, marker)[1]


def between(source, start, end):
    return before(after(source, start), end)


def body(source, signature):
    # The shared extractor uses index/rindex; report source drift explicitly.
    after(source, signature)
    after(after(source, signature), '{')
    try:
        return source_body(source, signature)
    except (ValueError, IndexError) as error:
        raise AssertionError(f'incomplete source body for marker: {signature!r}') from error


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
#include "tc/app/internal/tcWindowFullscreen.h"
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
enum class WindowControl { Title, Size, PixelSize, Position, Fullscreen, PositionQuery, ToggleFullscreen };
constexpr bool windowSizeIsScreenControlled() { return false; }
constexpr bool supportsSecondaryWindows() { return true; }
using Hook = bool (*)(Window&, WindowControl, int, int, const std::string*, IVec2&);
Hook windowControlHookForTests() { return nullptr; }
struct WindowRequestAccess {
    static bool ignoreGeometry(Window&, WindowGeometryCall) { return false; }
};
void setMainFullscreen(bool) {}
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
    bool isSecondaryFullscreen() const { return false; }
    void setSecondaryFullscreen(bool) {}
    void setSecondarySize(double, double, bool = false);
    IVec2 control(internal::WindowControl, int, int, const std::string* = nullptr, internal::WindowGeometryCall = internal::WindowGeometryCall::WindowSize);
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
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT), '-I', str(ROOT / 'core/include'), str(cpp), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print('PASS: main/secondary Win32 fractional dispatch and forwarding, 125/150/175/200%')


def creation_source(backend, *, native_path=None):
    """Replay production descriptor/creation arithmetic with native size recording.

    Native services/types are stubbed; target DPI is supplied by the fixture.
    Statements come from the actual launcher, adapters and native creators,
    so an old conversion compiles but fails at run time.
    """
    header = (ROOT / 'core/include/TrussC.h').read_text(encoding='utf-8')
    native = (native_path or ROOT / 'core/include/sokol/sokol_app_tc.h').read_text(encoding='utf-8')
    launcher = between(body(header, 'inline sapp_desc buildWindowDescriptor('), '{', '    const auto& windowTitle')
    normal = before(split_marker(body(header, 'sapp_desc buildAppDescriptor('), '    return ', last=True)[1], ';')
    normal = 'return ' + normal + ';'
    hot_header = (ROOT / 'core/include/tc/app/tcHotReloadHost.h').read_text(encoding='utf-8')
    hot = between(body(hot_header, 'inline int runHotReloadApp('), '    sapp_desc desc = ', '    desc.frame_cb')
    hot = 'sapp_desc desc = ' + hot
    adapter_path = {'win': 'win/tcWindowWin.cpp', 'x11': 'linux/tcWindowLinux.cpp',
                    'mac': 'mac/tcWindowMac.mm', 'egl': 'linux/tcWindowLinux.cpp'}[backend]
    adapter = (ROOT / 'core/platform' / adapter_path).read_text(encoding='utf-8')
    secondary = between(adapter, '    sapp_window_desc d = {};', '    d.title')
    main_signature = {'win': 'static bool _sapp_tc_win32_create_main_window(',
                      'x11': 'static bool _sapp_tc_x11_create_main_window(',
                      'mac': 'static bool _sapp_tc_mac_create_main_window(',
                      'egl': 'static bool _sapp_tc_x11_create_main_window('}[backend]
    main = body(native, main_signature)
    shared = (ROOT / 'core/include/sokol/util/sokol_window_native.h').read_text(encoding='utf-8')
    normalizers = ''
    secondary_transfer = ''
    common_transfer = ''
    mac_transfers = ''
    win_creator = ''
    if backend == 'win':
        launcher += 'desc.high_dpi = ' + between(header, '    desc.high_dpi = ', ';') + ';'
        secondary += 'd.no_high_dpi = ' + between(adapter, '    d.no_high_dpi = ', ';') + ';'
        # Both entry points feed the same constructor. Replay their production
        # normalization and its descriptor copy, then the entire native creator
        # (including the target-DPI correction condition and SetWindowPos call).
        common = body(native, 'static _sapp_tc_window_t* _sapp_tc_win32_create_window(')
        secondary_entry = body(before(after(native, main), '\nvoid sapp_destroy_window('),
                               'sapp_window sapp_create_window(')
        for entry in (main, secondary_entry):
            after(entry, '_sapp_tc_win32_create_window(&native)')
        after(common, '_sapp_tc_win32_create_native_window(')
        transfer = 'native.window = ' + between(main, '    native.window = ', ';') + ';'
        secondary_transfer = 'native.window = ' + between(secondary_entry, '    native.window = ', ';') + ';'
        common_transfer = 'w->desc = ' + between(common, '    w->desc = ', ';') + ';'
        normalizers = '\n'.join(body(shared, 'static inline sapp_window_desc ' + name + '(')
                                for name in ('_sapp_tc_normalize_main_window', '_sapp_tc_normalize_window'))
        win_creator = body(native, 'static bool _sapp_tc_win32_create_native_window(')
    elif backend == 'mac':
        common = body(native, 'static _sapp_tc_window_t* _sapp_tc_mac_create_window_ex(')
        wrapper = body(native, 'static _sapp_tc_window_t* _sapp_tc_mac_create_window(')
        secondary_entry = body(before(after(native, main), '\nvoid sapp_destroy_window('),
                               'sapp_window sapp_create_window(')
        # Replay every statement from descriptor initialization to the actual
        # constructor call, including any overwrite after normalization. Only
        # native API types/services are stubbed, and the ObjC bridge is erased.
        after(main, 'native.window = _sapp_tc_normalize_main_window(d);')
        after(secondary_entry, 'native.window = _sapp_tc_normalize_window(desc);')

        def entry_transfer(entry, call):
            block = between(entry, '    _sapp_tc_native_desc native = {};',
                            '    _sapp_tc_window_t* w = ' + call + ';')
            return '_sapp_tc_native_desc native = {};' + block + 'return ' + call + ';'

        transfer = entry_transfer(main, '_sapp_tc_mac_create_window_ex(&native, &attrs)')
        bridge = '(__bridge const void*)'
        after(transfer, bridge)
        transfer = transfer.replace(bridge, '(const void*)')
        secondary_transfer = entry_transfer(secondary_entry, '_sapp_tc_mac_create_window(&native)')
        common_transfer = 'w->desc = ' + between(common, '    w->desc = ', '    w->swap_interval = ')
        # Preserve the real secondary wrapper's forwarding, too. A changed
        # argument or a descriptor overwrite cannot disappear from the probe.
        after(wrapper, '_sapp_tc_mac_create_window_ex(d, &none)')
        wrapper = wrapper.replace('static _sapp_tc_window_t*', 'static Descriptor', 1)
        mac_transfers = ('Descriptor _sapp_tc_mac_create_window_ex(const _sapp_tc_native_desc* d, '
                         '[[maybe_unused]] const _sapp_tc_mac_window_attrs* attrs) {\n'
                         'State state; State* w = &state;\n' + common_transfer +
                         '\nreturn w->desc;\n}\n' + wrapper)
        normalizers = '\n'.join(body(shared, 'static inline sapp_window_desc ' + name + '(')
                                for name in ('_sapp_tc_normalize_main_window', '_sapp_tc_normalize_window'))
    else:
        transfer = body(shared, 'static inline sapp_window_desc _sapp_tc_normalize_main_window(')
        transfer = between(transfer, '    window.width = ', '    window.title')
        transfer = ('    window.width = ' + transfer).replace('window.', 'w->desc.')
    def guard(creator):
        call = '_sapp_tc_window_pixels_representable('
        return call + between(creator, call, ')') + ')'

    validation = ''
    if backend == 'win':
        arithmetic = ''
    elif backend in ('x11', 'egl'):
        creator = body(native, 'static bool _sapp_tc_x11_create_native_window(')
        arithmetic = between(creator, '    const float scale = ', '    if (width_pt <= 0)')
        # Keep the actual early rejection, before either EGL or GLX visual lookup.
        validation = between(creator, '{', '\n#if')
        arithmetic = 'const float scale = ' + arithmetic + '\nreturn {px_w, px_h};'
    else:
        # One native constructor serves the first window and every other window.
        creator = body(native, 'static _sapp_tc_window_t* _sapp_tc_mac_create_window_ex(')
        call = between(creator, '[w->window setContentSize:NSMakeSize(', ')];')
        validation = 'if (!' + guard(creator) + ') return false;'
        arithmetic = 'return roundMac(' + call + ');'
    template = (ROOT / 'tools/tests/window_creation_size.cpp').read_text(encoding='utf-8')
    return (template.replace('NORMAL_LAUNCHER', normal).replace('HOT_LAUNCHER', hot)
            .replace('WIN32_NORMALIZERS', normalizers).replace('WIN32_NATIVE_CREATOR', win_creator)
            .replace('WIN32_SECONDARY_TRANSFER', secondary_transfer).replace('WIN32_COMMON_TRANSFER', common_transfer)
            .replace('MAC_TRANSFERS', mac_transfers)
            .replace('LAUNCHER', launcher).replace('SECONDARY', secondary)
            .replace('TRANSFER', transfer).replace('CREATION_GUARD', validation).replace('ARITHMETIC', arithmetic)
            .replace('X11_DIMENSIONS', body(native, 'static bool _sapp_tc_x11_update_dimensions(')))


def build_creation_probe(backend, scratch, *, native_path=None):
    cpp = Path(scratch) / 'probe.cpp'
    binary = Path(scratch) / 'probe'
    cpp.write_text(creation_source(backend, native_path=native_path), encoding='utf-8')
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall',
                    '-Wextra', '-Werror', '-fsanitize=undefined,float-cast-overflow',
                    '-fno-sanitize-recover=all', '-I', str(ROOT),
                    '-DTEST_' + backend.upper(), str(cpp), '-o', str(binary)], check=True)
    return binary


class WindowPixelSizeTests(unittest.TestCase):
    def test_creation_matches_resize(self):
        with tempfile.TemporaryDirectory(prefix='window-create-') as scratch:
            for backend in ('win', 'x11', 'egl', 'mac'):
                with self.subTest(backend=backend):
                    binary = build_creation_probe(backend, scratch)
                    subprocess.run([str(binary)], check=True)

    def test_mac_detects_dropped_pixel_transfers(self):
        native = (ROOT / 'core/include/sokol/sokol_app_tc.h').read_text(encoding='utf-8')
        cases = (
            ('main', 'static bool _sapp_tc_mac_create_main_window(',
             'native.window = _sapp_tc_normalize_main_window(d);',
             '\n    native.window._tc_pixel_width = native.window._tc_pixel_height = 0;', ('main',)),
            ('secondary', 'sapp_window sapp_create_window(',
             'native.window = _sapp_tc_normalize_window(desc);',
             '\n    native.window._tc_pixel_width = native.window._tc_pixel_height = 0;', ('secondary',)),
            ('common', 'static _sapp_tc_window_t* _sapp_tc_mac_create_window_ex(',
             'w->desc = d->window;',
             '\n    w->desc._tc_pixel_width = w->desc._tc_pixel_height = 0;', ('main', 'secondary')),
            ('wrapper', 'static _sapp_tc_window_t* _sapp_tc_mac_create_window(',
             'const _sapp_tc_mac_window_attrs none = {};',
             '\n    auto copy = *d; copy.window._tc_pixel_width = copy.window._tc_pixel_height = 0; d = &copy;',
             ('secondary',)),
        )
        baseline = creation_source('mac')
        for name, signature, marker, overwrite, roles in cases:
            with self.subTest(transfer=name), tempfile.TemporaryDirectory(prefix='window-mac-mutation-') as scratch:
                # Limit the public entry lookup to macOS, rather than the last
                # backend's same-named function selected by body().
                region = before(native, '\nvoid sapp_destroy_window(') if name == 'secondary' else native
                entry = body(region, signature)
                self.assertEqual(entry.count(marker), 1)
                private_header = Path(scratch) / 'sokol_app_tc.h'
                private_header.write_text(native.replace(entry, entry.replace(marker, marker + overwrite), 1),
                                          encoding='utf-8')
                self.assertNotEqual(baseline, creation_source('mac', native_path=private_header))
                binary = build_creation_probe('mac', scratch, native_path=private_header)
                for role in roles:
                    result = subprocess.run([str(binary), role, 'pixels', '1.25'],
                                            capture_output=True, text=True, encoding='utf-8')
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(f'{role} pixels=1 highDpi=1 dpi=1.25 request=640x400: '
                                  'creation=800x500 resize=640x400', result.stderr)
                    self.assertIn('actual.x == expectedNative.x && actual.y == expectedNative.y', result.stderr)
                    print(f'PASS: macOS {name}/{role} dropped-pixel mutation detected: '
                          'creation=800x500 resize=640x400')

    def test_win32_detects_missing_target_dpi_correction(self):
        native = (ROOT / 'core/include/sokol/sokol_app_tc.h').read_text(encoding='utf-8')
        marker = 'if (correct_size) {'
        self.assertEqual(native.count(marker), 1)
        with tempfile.TemporaryDirectory(prefix='window-create-mutation-') as scratch:
            private_header = Path(scratch) / 'sokol_app_tc.h'
            private_header.write_text(native.replace(marker, 'if (false && correct_size) {'), encoding='utf-8')
            binary = build_creation_probe('win', scratch, native_path=private_header)
            for role in ('main', 'secondary'):
                for pixels in (False, True):
                    with self.subTest(role=role, pixels=pixels):
                        mode = 'pixels' if pixels else 'logical'
                        result = subprocess.run([str(binary), role, mode], capture_output=True,
                                                text=True, encoding='utf-8')
                        self.assertNotEqual(result.returncode, 0)
                        # A compile failure, unrelated crash or sanitizer error is
                        # not evidence: require the exact size disagreement.
                        self.assertIn(f'{role} pixels={int(pixels)} highDpi={int(not pixels)} '
                                      'dpi=1.25 request=640x400: creation=640x400 resize=800x500', result.stderr)
                        self.assertIn('actual.x == expectedNative.x && actual.y == expectedNative.y', result.stderr)
                        print(f'PASS: Win32 {role} {mode} target-DPI mutation detected: '
                              'creation=640x400 resize=800x500')

    def test_missing_creation_markers_are_named(self):
        read_text = Path.read_text
        # Remove each function/statement marker from a private source view,
        # including shared normalization, adapters and desktop entry points.
        cases = (
            ('win', 'inline sapp_desc buildWindowDescriptor('),
            ('win', '    const auto& windowTitle'),
            ('win', '    sapp_desc desc = '),
            ('win', '    sapp_window_desc d = {};'),
            ('win', 'static bool _sapp_tc_win32_create_main_window('),
            ('win', 'static _sapp_tc_window_t* _sapp_tc_win32_create_window('),
            ('win', 'sapp_window sapp_create_window('),
            ('win', '    native.window = '),
            ('win', '_sapp_tc_win32_create_window(&native)'),
            ('win', '    w->desc = '),
            ('win', 'static inline sapp_window_desc _sapp_tc_normalize_main_window('),
            ('win', 'static inline sapp_window_desc _sapp_tc_normalize_window('),
            ('win', 'static bool _sapp_tc_win32_create_native_window('),
            ('x11', '    const float scale = '),
            ('mac', 'static _sapp_tc_window_t* _sapp_tc_mac_create_window_ex('),
            ('mac', 'static bool _sapp_tc_mac_create_main_window('),
            ('mac', 'static _sapp_tc_window_t* _sapp_tc_mac_create_window('),
            ('mac', 'sapp_window sapp_create_window('),
            ('mac', '    _sapp_tc_native_desc native = {};'),
            ('mac', 'native.window = _sapp_tc_normalize_main_window(d);'),
            ('mac', 'native.window = _sapp_tc_normalize_window(desc);'),
            ('mac', '    _sapp_tc_window_t* w = _sapp_tc_mac_create_window_ex(&native, &attrs);'),
            ('mac', '    _sapp_tc_window_t* w = _sapp_tc_mac_create_window(&native);'),
            ('mac', '_sapp_tc_mac_create_window_ex(d, &none)'),
            ('mac', '(__bridge const void*)'),
            ('mac', '    w->desc = '),
            ('mac', '    w->swap_interval = '),
            ('mac', 'static inline sapp_window_desc _sapp_tc_normalize_main_window('),
            ('mac', 'static inline sapp_window_desc _sapp_tc_normalize_window('),
            ('mac', '[w->window setContentSize:NSMakeSize('),
        )
        for backend, marker in cases:
            with self.subTest(backend=backend, marker=marker):
                def without_marker(path, *args, **kwargs):
                    return read_text(path, *args, **kwargs).replace(marker, 'REMOVED_MARKER')
                with patch.object(Path, 'read_text', without_marker):
                    with self.assertRaises(AssertionError) as caught:
                        creation_source(backend)
                self.assertIn(marker, str(caught.exception))
                self.assertIn('missing source marker:', str(caught.exception))

    def test_native_invalid_sizes_match_base(self):
        # Isolate expected assertion failures in child processes and compare real
        # native creation/default selection to the target branch, per dimension.
        header = (ROOT / 'core/include/TrussC.h').read_text(encoding='utf-8')
        launcher = between(body(header, 'inline sapp_desc buildWindowDescriptor('), '{', '    const auto& windowTitle')
        normal = 'return ' + before(split_marker(body(header, 'sapp_desc buildAppDescriptor('), '    return ', last=True)[1], ';') + ';'
        source = (ROOT / 'tools/tests/window_creation_native.cpp').read_text(encoding='utf-8')
        adapter = (ROOT / 'core/platform/linux/tcWindowLinux.cpp').read_text(encoding='utf-8')
        secondary = between(adapter, '    sapp_window_desc d = {};', '    d.title')
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
                for label in ('descriptor','native','secondary'):
                    values = [re.search(label+r'=(\d+|-\d+)x(\d+|-\d+)', r.stdout) for r in results]
                    assert bool(values[0]) == bool(values[1]), (label,results)
                    if values[0]:
                        assert values[0].group(dimension+1) == values[1].group(dimension+1), (label,results)
            if base.returncode:
                assert 'Assertion' in base.stderr and 'Assertion' in current.stderr, results
            else:
                assert 'native=' in current.stdout and 'secondary=' in current.stdout, results
            print('PASS native invalid input:',binary.rsplit('/',1)[-1],pixels,width,height,base.returncode)
egl, glx = sys.argv[-1].split(',')
subprocess.run([egl,'tiny-secondary','640','400','1'], check=True)
subprocess.run(['xrdb','-merge'], input='Xft.dpi: 9.6e-7\n', text=True, check=True)
subprocess.run([glx,'tiny','640','400','1'], check=True)
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
            # Both shared-path backends reject before native creation.
            tiny_binaries = [binaries[0], binaries[2]]
            subprocess.run(['xvfb-run','-a','-s','-screen 0 1280x720x24 -noreset',
                'bash','-c', 'ulimit -c 0; printf "Xft.dpi: 192\\n" | xrdb -merge; exec "$@"',
                'native-validation', 'python3','-c',runner,*binaries,','.join(tiny_binaries)], check=True,
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
            arguments = between(endpoint, 'NSMakeSize(', ')];')
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
        seed = 'geometry =' + between(seed, 'binding.geometry =', ';') + ';'
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
