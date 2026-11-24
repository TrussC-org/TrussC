#!/usr/bin/env python3
"""Check the real Win32 Vulkan destruction sequence without Windows or a GPU.

Run through tcbuild.sh. The temporary C++ unit has a plain main, native-resource
stubs, and the production destroy-window body and sapp_run cleanup sequence.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
source = (ROOT / 'core/include/sokol/sokol_app_tc.h').read_text(encoding='utf-8')
source = source[source.index('#elif defined(_WIN32)\n/*== Windows'):]
start = source.index('void sapp_tc_destroy_window(sapp_tc_window win) {')
opening = source.index('{', start)
end, depth = opening + 1, 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
destroy = source[start:end]
start = source.index('    _sapp_tc_win32_cleanup();', end)
end = source.index('    _sapp_tc.app.desc.window_title = 0;', start)
cleanup = source[start:end]
code = r'''
#include <cstdio>
#include <cstdint>
#define _SAPP_TC_MAX_WINDOWS 32
struct sapp_tc_window { uint32_t id; };
int callbacks=0;
void callback() { ++callbacks; }
struct Window {
    bool ready=true, constructing=false, close_requested=false;
    struct { void (*event_cb)()=nullptr; } desc;
} window;
using _sapp_tc_window_t = Window;
struct {
    struct { int device=1; } vk;
    struct { struct { const char* window_title=nullptr; } desc; } app;
    Window* windows[_SAPP_TC_MAX_WINDOWS]{&window};
} _sapp_tc;
bool nativeAlive=true;
int stage=0, failures=0;
void check(bool value) { if (!value) ++failures; }
Window* _sapp_tc_lookup(sapp_tc_window win) { return win.id == 1 ? &window : nullptr; }
void _sapp_tc_win32_unwind(Window* value) {
    if (!value) return;
    check(stage == 2 && nativeAlive);
    nativeAlive=false;
    stage=3;
}
void _sapp_tc_win32_cleanup() {
    check(stage == 0 && nativeAlive && _sapp_tc.vk.device);
    stage=1;
}
void _sapp_tc_vk_discard() {
    check(stage == 1 && nativeAlive && _sapp_tc.vk.device);
    _sapp_tc.vk.device=0;
    stage=2;
}
''' + destroy + r'''
int main() {
    window.desc.event_cb=callback;
    sapp_tc_destroy_window({1});
    if (window.desc.event_cb) window.desc.event_cb();
    check(nativeAlive && callbacks == 0 && stage == 0);
''' + cleanup + r'''
    check(!nativeAlive && stage == 3);
    std::printf("Win32 Vulkan lifetime: %s (Runtime release -> gfx cleanup -> Vulkan teardown -> HWND destruction)\n",
        failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
'''
with tempfile.TemporaryDirectory(prefix='tc-vk-win-lifetime-') as directory:
    path = Path(directory)
    cpp = path / 'test.cpp'
    cpp.write_text(code, encoding='utf-8')
    binary = path / 'test'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-DSOKOL_VULKAN',
                    str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
