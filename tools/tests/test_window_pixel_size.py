#!/usr/bin/env python3
"""Host-check production Win32 forwarding and X11 main pixel sizing.

Run through tcbuild.sh. Native calls are recorded without a display or GPU;
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
enum class WindowControl { Title, Size, PixelSize, Position, Fullscreen, PositionQuery };
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
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'core/include'), str(cpp), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print('PASS: main/secondary Win32 fractional dispatch and forwarding, 125/150/175/200%')


class WindowPixelSizeTests(unittest.TestCase):
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
