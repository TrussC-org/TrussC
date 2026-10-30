#!/usr/bin/env python3
"""Run the production Web update body with real Texture and playback-error handling.

Linux prerequisite: build core/tests/allCoreTests, then run this script through
 tcbuild.sh. Browser dimension getters are stubbed; GPU storage is Mesa EGL.
"""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
BUILD = ROOT / 'core/tests/allCoreTests/build-linux'
source = (ROOT / 'core/platform/web/tcVideoPlayer_web.cpp').read_text(encoding='utf-8')
body = source.split('void VideoPlayer::updatePlatform() {', 1)[1].split(
    '\nbool VideoPlayer::hasNewFramePlatform()', 1)[0]
cpp = r'''
#include "TrussC.h"
#include <EGL/egl.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
using namespace trussc;
static int browserWidth = 64, browserHeight = 4, copies = 0;
static int emscripten_run_script_int(const char* script) {
    if (std::strstr(script, "!!window._trussc_player_error")) return 0;
    if (std::strstr(script, "? window._trussc_player_video.videoWidth")) return browserWidth;
    if (std::strstr(script, "? window._trussc_player_video.videoHeight")) return browserHeight;
    ++copies;
    return 1;
}
static const char* emscripten_run_script_string(const char*) { return ""; }
static void emscripten_run_script(const char*) {}
static void reportWebVideoStorageError(internal::VideoDecodeStatus&) { std::abort(); }
static void check(bool ok, const char* label) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) std::exit(1);
}
// Use the actual base error queue, dispatch, log, event and stopped state.
class WebProbe : public VideoPlayer {
public:
    Texture texture_;
    unsigned char* pixels_ = nullptr;
    std::shared_ptr<internal::VideoDecodeStatus> decodeStatus_ =
        std::make_shared<internal::VideoDecodeStatus>();
    WebProbe() {
        initialized_ = playing_ = true;
        width_ = 64; height_ = 4;
        texture_.allocate(width_, height_, 4, TextureUsage::Stream);
        pixels_ = new unsigned char[64 * 4 * 4];
        check(texture_.isAllocated(), "small initial texture allocated");
    }
    ~WebProbe() { delete[] pixels_; initialized_ = false; }
    void setPausedImpl(bool) override {} // Browser pause, with no browser in this probe.
    void tick() {
        if (dispatchPlaybackError()) return;
        updatePlatform();
        dispatchPlaybackError();
    }
    void updatePlatform();
};
void WebProbe::updatePlatform() {
''' + body + r'''
int main() {
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    check(display != EGL_NO_DISPLAY && eglInitialize(display, nullptr, nullptr), "surfaceless EGL initialized");
    check(eglBindAPI(EGL_OPENGL_API), "OpenGL API bound");
    const EGLint configAttrs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE};
    EGLConfig config; EGLint count;
    check(eglChooseConfig(display, configAttrs, &config, 1, &count) && count, "EGL config found");
    const EGLint contextAttrs[] = {EGL_CONTEXT_MAJOR_VERSION, 4, EGL_CONTEXT_MINOR_VERSION, 1, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttrs);
    check(context != EGL_NO_CONTEXT && eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context), "EGL context current");
    sg_desc desc{}; desc.image_pool_size = 32; desc.view_pool_size = 64; desc.sampler_pool_size = 32;
    sg_setup(&desc);
    check(sg_isvalid(), "production sokol GPU initialized");
    const int limit = sg_query_limits().max_image_size_2d;
    std::printf("Device texture limit: %d\n", limit);
    for (int scenario = 0; scenario < 3; ++scenario) {
        browserWidth = 64; browserHeight = 4;
        WebProbe player;
        int events = 0, errors = 0, textureErrors = 0;
        auto listener = player.onError.listen([&](VideoErrorEventArgs&) { ++events; });
        auto logger = getLogger().onLog.listen([&](LogEventArgs& event) {
            if (event.level == LogLevel::Error && event.message.find("[VideoPlayer]") == 0) ++errors;
            if (event.level == LogLevel::Error && event.message.find("[Texture]") == 0) ++textureErrors;
        });
        auto* mirror = player.pixels_;
        auto image = player.texture_.getImage();
        const int previousCopies = copies;
        std::vector<sg_image> reserved;
        if (scenario == 2) {
            for (int i = 0; i < desc.image_pool_size; ++i) {
                auto handle = sg_alloc_image();
                if (handle.id) reserved.push_back(handle);
            }
        }
        browserWidth = scenario == 1 ? limit + 1 : 128;
        player.tick();
        if (scenario == 0) {
            check(!player.hasError() && events == 0 && player.isLoaded() && player.isPlaying(), "valid resize keeps playing");
            check(player.getWidth() == 128 && player.texture_.isAllocated() &&
                player.texture_.getWidth() == 128 && player.pixels_ && copies == previousCopies + 1,
                "valid resize replaces texture and CPU mirror, reaches copy");
        } else {
            check(player.hasError() && events == 1 && errors == 1 && !player.isPlaying(), "resize failure delivers one playback Error and stops");
            // The existing playback-error contract retains loaded state.
            check(player.isLoaded() && player.pixels_ == (scenario == 1 ? mirror : nullptr) && copies == previousCopies,
                "failed upload does not resize CPU mirror or copy a frame");
            if (scenario == 1) check(player.texture_.getImage().id == image.id && player.getWidth() == 64,
                "device rejection precedes storage replacement");
            else check(!player.texture_.isAllocated() && player.getWidth() == 128, "real image-pool exhaustion leaves no replacement mirror");
            player.tick(); player.tick();
            check(events == 1 && errors == 1 && player.pixels_ == (scenario == 1 ? mirror : nullptr) &&
                copies == previousCopies && textureErrors == (scenario == 2 ? 1 : 0),
                "later ticks neither retry failed GPU allocation nor copy an impossible frame or repeat the playback Error");
        }
        for (auto handle : reserved) sg_dealloc_image(handle);
    }
    sg_shutdown();
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context); eglTerminate(display);
}
'''
flags = (BUILD / 'CMakeFiles/allCoreTests.dir/flags.make').read_text(encoding='utf-8')
includes = shlex.split(next(line.split(' = ', 1)[1] for line in flags.splitlines() if line.startswith('CXX_INCLUDES = ')))
link = shlex.split((BUILD / 'CMakeFiles/allCoreTests.dir/link.txt').read_text(encoding='utf-8'))
# Reuse the built production core archive and its dependencies, without harness objects.
libs = link[link.index('-o') + 2:]
with tempfile.TemporaryDirectory(prefix='trussc-web-resize-') as scratch:
    path = Path(scratch)
    (path / 'probe.cpp').write_text(cpp, encoding='utf-8')
    subprocess.run(['/usr/bin/c++', '-std=c++20', '-O2', '-DNDEBUG', '-DSOKOL_GLCORE',
                    *includes, str(path / 'probe.cpp'), '-o', str(path / 'probe'),
                    *libs, '-lEGL'], cwd=BUILD, check=True)
    env = dict(os.environ, EGL_PLATFORM='surfaceless', LIBGL_ALWAYS_SOFTWARE='1', GALLIUM_DRIVER='llvmpipe')
    subprocess.run([str(path / 'probe')], env=env, check=True)
