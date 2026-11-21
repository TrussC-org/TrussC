"""Compile Android EGL selection/getter bodies against deterministic EGL seams.

Run through tcbuild.sh; no Android SDK, display, or EGL driver is required.
"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
header = (root / 'core/include/sokol/sokol_app_tc.h').read_text(encoding='utf-8')
android = header[header.index('#elif defined(__ANDROID__)\n\n//'):]


def body(signature):
    start = android.index(signature)
    end = android.index('{', start) + 1
    depth = 1
    while depth:
        depth += (android[end] == '{') - (android[end] == '}')
        end += 1
    return android[start:end]


code = r'''
#include <cassert>
#include <climits>
#include <cstdio>
#include <vector>
#define _SOKOL_PRIVATE
#define SOKOL_API_IMPL
#define SOKOL_ASSERT assert
#define _SAPP_WARN_MSG(code, message) (++warnings)
using EGLint = int;
using EGLConfig = int;
using EGLDisplay = int;
using EGLContext = int;
enum { EGL_FALSE, EGL_TRUE, EGL_NONE, EGL_SURFACE_TYPE, EGL_RENDERABLE_TYPE,
    EGL_RED_SIZE, EGL_GREEN_SIZE, EGL_BLUE_SIZE, EGL_ALPHA_SIZE, EGL_DEPTH_SIZE,
    EGL_STENCIL_SIZE, EGL_CONTEXT_MAJOR_VERSION, EGL_CONTEXT_MINOR_VERSION };
constexpr int EGL_NO_DISPLAY=0, EGL_NO_CONTEXT=0, EGL_DEFAULT_DISPLAY=1;
constexpr int EGL_WINDOW_BIT=1, EGL_PBUFFER_BIT=2, EGL_OPENGL_ES3_BIT=4;
enum sapp_pixel_format { SAPP_PIXELFORMAT_DEPTH, SAPP_PIXELFORMAT_DEPTH_STENCIL };
struct Config { int depth, stencil, red=8, green=8, blue=8, alpha=8; };
std::vector<Config> configs;
struct { struct { int display=0, context=0, config=0; } android;
    struct { bool alpha=false; struct { int major_version=3, minor_version=0; } gl; } desc;
} _sapp_tc;
int warnings=0, requests=0, context_creations=0;
bool choose_fails=false, attrib_fails=false;
EGLDisplay eglGetDisplay(int) { return 1; }
int eglInitialize(int, void*, void*) { return EGL_TRUE; }
int eglGetConfigAttrib(int, EGLConfig c, int key, int* value) {
    if (attrib_fails) return EGL_FALSE;
    const auto& cfg=configs.at(c-1);
    switch(key) {
        case EGL_RED_SIZE: *value=cfg.red; break;
        case EGL_GREEN_SIZE: *value=cfg.green; break;
        case EGL_BLUE_SIZE: *value=cfg.blue; break;
        case EGL_ALPHA_SIZE: *value=cfg.alpha; break;
        case EGL_DEPTH_SIZE: *value=cfg.depth; break;
        case EGL_STENCIL_SIZE: *value=cfg.stencil; break;
        default: assert(false);
    }
    return EGL_TRUE;
}
int eglChooseConfig(int display, const int* attrs, int* found, int capacity, int* count) {
    ++requests;
    if (choose_fails) return EGL_FALSE;
    const int expected_depth[] = {24, 0, 16};
    const int expected_stencil[] = {8, 8, 0};
    assert(requests<=3);
    for (const int* a=attrs; *a!=EGL_NONE; a+=2) {
        if (*a==EGL_DEPTH_SIZE) assert(a[1]==expected_depth[requests-1]);
        if (*a==EGL_STENCIL_SIZE) assert(a[1]==expected_stencil[requests-1]);
    }
    *count=0;
    for (int c=1; c<=int(configs.size()) && *count<capacity; ++c) {
        bool matches=true;
        for (const int* a=attrs; *a!=EGL_NONE; a+=2) {
            if (*a==EGL_SURFACE_TYPE) { assert(a[1]==(EGL_WINDOW_BIT|EGL_PBUFFER_BIT)); continue; }
            if (*a==EGL_RENDERABLE_TYPE) { assert(a[1]==EGL_OPENGL_ES3_BIT); continue; }
            int actual=0;
            eglGetConfigAttrib(display, c, *a, &actual);
            matches &= actual>=a[1];
        }
        if(matches) found[(*count)++]=c;
    }
    return EGL_TRUE;
}
EGLContext eglCreateContext(int, int, int, const int*) { ++context_creations; return 1; }
'''
code += body('_SOKOL_PRIVATE bool _sapp_tc_android_init_egl(void)')
code += '\n' + body('SOKOL_API_IMPL sapp_pixel_format sapp_depth_format(void)')
code += r'''
void reset(std::vector<Config> input) {
    configs=input; _sapp_tc={}; warnings=requests=context_creations=0;
    choose_fails=attrib_fails=false;
    assert(sapp_depth_format()==SAPP_PIXELFORMAT_DEPTH);
}
void selected(int expected, bool stencil) {
    assert(_sapp_tc_android_init_egl());
    assert(_sapp_tc.android.config==expected && context_creations==1);
    assert(warnings==(stencil ? 0 : 1));
    assert(sapp_depth_format()==(stencil ? SAPP_PIXELFORMAT_DEPTH_STENCIL : SAPP_PIXELFORMAT_DEPTH));
}
int main() {
    reset({{16,0}, {32,8}, {24,8}}); selected(3,true); assert(requests==1);
    reset({{32,16}, {16,8}}); selected(2,true); assert(requests==2);
    reset({{32,8}, {24,8,10,10,10}, {24,8}}); selected(3,true);
    reset({{16,0}, {16,8}}); selected(2,true);
    reset({{0,8}, {16,0}}); selected(1,true);
    reset({{32,0}, {16,0}}); selected(2,false); assert(requests==3);
    reset({{32,0}}); selected(1,false);
    reset({{16,4}}); selected(1,true); // DEPTH_STENCIL also describes a smaller fallback stencil.
    reset({{24,8,8,8,8,0}, {24,8}}); _sapp_tc.desc.alpha=true; selected(2,true);
    reset({}); assert(!_sapp_tc_android_init_egl()); assert(context_creations==0);
    reset({{24,8}}); choose_fails=true; assert(!_sapp_tc_android_init_egl());
    reset({{24,8}}); attrib_fails=true; assert(!_sapp_tc_android_init_egl());
    assert(context_creations==0);
    std::puts("androidStencil: EGL preference, stencil fallback, warning, depth format, errors: PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='android-stencil-', dir=root / '.build') as scratch:
    source = Path(scratch) / 'main.cpp'
    binary = Path(scratch) / 'test'
    source.write_text(code, encoding='utf-8')
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
