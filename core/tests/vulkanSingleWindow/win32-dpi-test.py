#!/usr/bin/env python3
"""Run production Win32 DPI polling and Vulkan metadata statements with OS stubs.

Use tcbuild.sh; no Windows runtime or GPU is needed for the plain-main fixture.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
source = (ROOT / 'core/include/sokol/sokol_app_tc.h').read_text(encoding='utf-8')
source = source[source.index('#elif defined(_WIN32)\n/*== Windows'):]


def function(name):
    start = source.index('static ', source.index(name) - 20)
    opening = source.index('{', start)
    end, depth = opening + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


creation = source[source.index('static _sapp_tc_window_t* _sapp_tc_win32_create_window('):]
start = creation.index('    w->desc = d->window;')
normalization = creation[start:creation.index('    w->events_enabled', start)]
helper = (ROOT / 'core/include/sokol/util/sokol_app_vulkan.h').read_text(encoding='utf-8')
start = helper.index('    _sapp_tc.app.main->fb_width = (int)fb_width;')
publication = helper[start:helper.index('    auto* w =', start)]
code = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
using UINT = unsigned;
using HMONITOR = int;
struct RECT { int left, top, right, bottom; };
struct Desc { bool no_high_dpi; };
struct _sapp_tc_native_desc { Desc window; };
struct _sapp_tc_window_t {
    Desc desc;
    int hwnd=1, hmonitor=1, win_width=0, win_height=0, fb_width=0, fb_height=0;
    float window_scale=1, dpi_scale=1, mouse_scale=1;
};
UINT dpi=192;
int width=320, height=240, warnings=0;
struct {
    struct { bool dpi_aware=true; int logger=0; _sapp_tc_window_t* main=nullptr;
        struct { int logger=0; } desc;
    } app;
    UINT (*GetDpiForWindow)(int) = [](int) { return dpi; };
} _sapp_tc;
#define MONITOR_DEFAULTTONEAREST 2
#define SAPP_LOGITEM_OK 0
bool GetClientRect(int, RECT* r) { *r={0,0,width,height}; return true; }
HMONITOR MonitorFromWindow(int, int) { return 1; }
void _sapp_tc_win32_update_refresh(_sapp_tc_window_t*) {}
void _sapp_tc_log(int*, int level, uint32_t, int, const char*, const char*) {
    assert(level == 2);
    ++warnings;
}
''' + function('_sapp_tc_win32_update_scale(') + '\n' + function('_sapp_tc_win32_update_dimensions(') + r'''
void create(_sapp_tc_window_t* w, const _sapp_tc_native_desc* d) {
''' + normalization + r'''
}
void publish(int fb_width, int fb_height) {
''' + publication + r'''
}
int main() {
    for (bool lowDpi : {false, true}) {
        _sapp_tc_window_t w;
        _sapp_tc_native_desc d{{lowDpi}};
        const int before = warnings;
        create(&w, &d);
        _sapp_tc.app.main = &w;
        assert(warnings == before + (lowDpi ? 1 : 0));
        assert(!w.desc.no_high_dpi);
        assert(_sapp_tc_win32_update_dimensions(&w));
        publish(width, height);
        for (int tick=0; tick<4; ++tick) {
            assert(!_sapp_tc_win32_update_dimensions(&w));
            publish(width, height);
            assert(w.fb_width == width && w.fb_height == height);
            assert(w.win_width == width / 2 && w.win_height == height / 2);
            assert(w.dpi_scale == 2 && w.mouse_scale == 1);
        }
        width += 20;
        assert(_sapp_tc_win32_update_dimensions(&w));
        publish(width, height);
        assert(!_sapp_tc_win32_update_dimensions(&w));
        assert(warnings == before + (lowDpi ? 1 : 0));
    }
    std::puts("Win32 Vulkan DPI: PASS (192 DPI, highDpi(false), no recreation without OS resize, one creation Warning)");
}
'''
code = '#include <initializer_list>\n' + code
with tempfile.TemporaryDirectory(prefix='tc-vk-win-dpi-') as directory:
    path = Path(directory)
    cpp = path / 'test.cpp'
    cpp.write_text(code, encoding='utf-8')
    binary = path / 'test'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-DSOKOL_VULKAN',
                    str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
