// Real GPU regression for #239: no native window or presentation mock.
#include <TrussC.h>
#include "../common/tcCoreTest.h"
#include <EGL/egl.h>
#include <cstdio>
using namespace tc;
namespace {
int failures;
void check(const char* name, bool ok) {
    std::printf("render-boundary: %s: %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
struct WindowProbe : Window {
    int nativeStandIn = 0;
    WindowProbe() { native_ = &nativeStandIn; }
    ~WindowProbe() { native_ = nullptr; internal::WindowRequestAccess::endApp(*this); }
};
bool rejectSwapchain = false;
sg_swapchain swapchain(void*) {
    sg_swapchain s{}; s.invalid = rejectSwapchain; s.width = s.height = 64; s.sample_count = 1;
    s.color_format = SG_PIXELFORMAT_RGBA8; s.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    return s;
}
struct Resources {
    WindowProbe window;
    Light light;
    Fbo fbo, sink;
    EventListener draw;
    uint32_t firstImage = 0;
    int draws = 0;
    bool synchronized = false;
    bool shadowPass = true;
    int expectedShadowSlots = -1;
    double sourceDuration = 1.0 / 60;
    int offscreenRecordings = 0;
    void renderOffscreen() {
        auto& ctx = window.context();
        const auto recording = ctx.recordingCount;
        auto receiver = createPlane(8, 8);
        if (shadowPass) {
            beginShadowPass(light);
            check("offscreen shadow admitted on every recording", ctx.shadow.inPass && ctx.shadow.count == 1);
            shadowDraw(receiver);
            endShadowPass();
        }
        fbo.begin(0,0,0);
        Material material; setMaterial(material); receiver.draw(); clearMaterial();
        if (expectedShadowSlots >= 0) {
            const auto& pending = internal::currentTarget().deferredPbrDraws;
            check("offscreen shadow validity ages without real draws", !pending.empty() &&
                pending.back().cmd.fsp.shadowMapParams[1] == expectedShadowSlots);
        }
        fbo.end();
        sink.begin(0,0,0);
        fbo.draw(0,0); sink.end();
        const auto image = fbo.getTexture().getImage().id;
        if (!firstImage) firstImage = image;
        check("offscreen single revisions reuse FBO storage", image == firstImage);
        check("identity stays fixed during recording", ctx.recordingCount == recording);
        check("offscreen public count contract", getFrameCount() == getUpdateCount());
        ++offscreenRecordings;
    }
    void setup(bool sync) {
        synchronized = sync;
        auto& ctx = window.context(); internal::currentWindowCtx() = &ctx;
        ctx.fbWidth = ctx.fbHeight = 64; ctx.dpiScale = 1;
        ctx.swapchainTarget.width = ctx.swapchainTarget.height = 64;
        ctx.swapchainTarget.sampleCount = 1; ctx.swapchainTarget.colorFormat = SG_PIXELFORMAT_RGBA8;
        ctx.acquireSwapchain = swapchain;
        if (synchronized) setFps(VSYNC); else setIndependentFps(EVENT_DRIVEN, VSYNC);
        light.enableShadow(32); addLight(light); fbo.allocate(16,16); sink.allocate(16,16);
        draw = events().draw.listen([this] {
            auto& ctx = window.context();
            check("public count contract", getFrameCount() == getUpdateCount());
            check("draws without updates", synchronized || getUpdateCount() == 0);
            if (shadowPass) {
                beginShadowPass(light);
                check("every shadow admitted beyond four draws", ctx.shadow.inPass && ctx.shadow.count == 1);
                endShadowPass();
            }
            if (expectedShadowSlots >= 0) {
                internal::parkWindowPass();
                auto receiver = createPlane(8, 8);
                Material material; setMaterial(material); receiver.draw(); clearMaterial();
                const auto& pending = internal::currentTarget().deferredPbrDraws;
                check("shadow validity ages by owner draws, including no updates", !pending.empty() &&
                    pending.back().cmd.fsp.shadowMapParams[1] == expectedShadowSlots);
            }
            uint8_t glyph[13] = {static_cast<uint8_t>(++draws)};
            bitmapfont::updateGlyph(65, glyph);
            ensureFontAtlasForText("A");
            check("shared glyph uploads on next GPU frame", internal::bitmapFontAtlas().version == bitmapfont::internal::registryVersion());
            fbo.begin(); clear(0,0,0,1); fbo.end(); fbo.draw(0,0);
            auto image = fbo.getTexture().getImage().id;
            if (!firstImage) firstImage = image;
            check("single revision frames reuse FBO storage", image == firstImage);
        });
    }
    void tick(int n, bool force = false) {
        internal::restoreWindowContext(&window.context());
        internal::runWindowTick(std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(17*n), sourceDuration, force);
    }
};
}
TC_CORE_TEST_MAIN() {
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major, minor;
    if (!eglInitialize(display, &major, &minor)) return 2;
    eglBindAPI(EGL_OPENGL_API);
    const EGLint attrs[] = {EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_DEPTH_SIZE,24,EGL_STENCIL_SIZE,8,EGL_NONE};
    EGLConfig config; EGLint count;
    if (!eglChooseConfig(display, attrs, &config, 1, &count) || count != 1) return 2;
    const EGLint size[] = {EGL_WIDTH,64,EGL_HEIGHT,64,EGL_NONE};
    auto surface = eglCreatePbufferSurface(display, config, size);
    const EGLint version[] = {EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,EGL_NONE};
    auto context = eglCreateContext(display, config, EGL_NO_CONTEXT, version);
    if (!eglMakeCurrent(display, surface, surface, context)) return 2;
    getMainThreadId();
    sg_desc gd{}; gd.environment.defaults.color_format = SG_PIXELFORMAT_RGBA8;
    gd.environment.defaults.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    gd.environment.defaults.sample_count = 1; gd.logger.func = internal::sokolLog;
    // Generated by the runner from tc::setup(), so this fixture uses the
    // production resource pools rather than Sokol's smaller bare defaults.
#include "gpu-pools.inc"
    sg_setup(&gd);
    sgl_desc_t sd{}; sd.max_vertices = 65536; sd.logger.func = internal::sokolLog; sgl_setup(&sd);
    uint8_t glyph[13] = {};
    bitmapfont::registerGlyph({65, glyph, bitmapfont::Width::Halfwidth});
    internal::bitmapFontAtlas().initialized = true;
    {
        Resources fast, slow; fast.setup(false); slow.setup(false);
        slow.sourceDuration = 3.0 / 60; // distinct native refresh sources
        for (int n = 0; n < 24; ++n) {
            fast.tick(n);
            if (n % 3 == 0) slow.tick(n);
        }
        check("two windows draw at different rates without updates", fast.draws == 24 && slow.draws == 8 &&
            fast.window.context().updateCount == 0 && slow.window.context().updateCount == 0);
        // A device-shared atlas must defer a second in-place upload until commit,
        // even if the current window changes and its draw counter is different.
        internal::restoreWindowContext(&fast.window.context());
        bitmapfont::updateGlyph(65, glyph); ensureFontAtlasForText("A");
        auto uploaded = internal::bitmapFontAtlas().version;
        internal::restoreWindowContext(&slow.window.context());
        const auto gpuFrame = internal::shaderStreamFrame();
        internal::runWindowTick(std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(425),
            1.0/60, false, nullptr, true);
        check("native gate cannot commit foreign GPU work", internal::shaderStreamFrame() == gpuFrame);
        bitmapfont::updateGlyph(65, glyph); ensureFontAtlasForText("A");
        check("two windows share one upload guard before commit", internal::bitmapFontAtlas().version == uploaded);
        internal::endGpuFrame(); // upload-only boundary: neither window draws
        ensureFontAtlasForText("A");
        check("upload-only commit permits next atlas upload", internal::bitmapFontAtlas().version == bitmapfont::internal::registryVersion());
        internal::endGpuFrame();
        // Hidden forced capture is a real draw, with the same resource boundaries.
        slow.window.context().suspended = true;
        slow.tick(30, true); slow.tick(31, true);
        check("hidden capture counts real draws", slow.draws == 10);
        for (int n = 32; n < 36; ++n) fast.tick(n);
        slow.shadowPass = false; slow.expectedShadowSlots = 1;
        slow.tick(32, true);
        slow.expectedShadowSlots = 0; slow.tick(33, true);
        slow.shadowPass = true; slow.expectedShadowSlots = -1;
        internal::restoreWindowContext(&fast.window.context());
        fast.firstImage = 0; // establish a fresh storage reference for the control phase
        const auto synchronizedDraws = fast.draws;
        fast.synchronized = true; setFps(VSYNC);
        for (int n = 40; n < 52; ++n) fast.tick(n);
        check("synchronized control updates and draws", fast.draws == synchronizedDraws + 12 && fast.window.context().updateCount == 12);
        auto& owner = slow; auto& other = fast;
        owner.window.context().suspended = false;
        owner.tick(40);
        // Prelude FBO revisions belong to the next draw of their owner. A GPU
        // commit from another window cannot retire the first sampled revision.
        internal::restoreWindowContext(&owner.window.context());
        owner.fbo.begin(); clear(0,0,0,1); owner.fbo.end(); owner.fbo.draw(0,0);
        auto first = owner.fbo.getTexture().getImage().id;
        other.tick(52); other.tick(53);
        internal::restoreWindowContext(&owner.window.context());
        owner.fbo.begin(); clear(0,0,0,1); owner.fbo.end(); owner.fbo.draw(0,0);
        check("other window commits do not recycle owner FBO revisions", owner.fbo.getTexture().getImage().id != first);
        internal::endGpuFrame();
        // Run the resource-bearing listener on twelve update-only commits.
        internal::restoreWindowContext(&other.window.context());
        other.draw = {}; other.firstImage = 0;
        setIndependentFps(VSYNC, EVENT_DRIVEN);
        other.window.context().loop.redrawCount = 0;
        int afterFrames = 0;
        auto after = events().afterFrame.listen([&] { ++afterFrames; });
        auto update = events().update.listen([&] { other.renderOffscreen(); });
        const auto before = other.window.context().drawCount;
        for (int n = 54; n < 66; ++n) {
            const auto recording = other.window.context().recordingCount;
            other.tick(n);
            check("update-only work completes one owner recording", other.window.context().recordingCount == recording + 1);
        }
        check("twelve update-only recordings keep real draw count", other.offscreenRecordings == 12 &&
            other.window.context().drawCount == before && afterFrames == 0);
        other.shadowPass = false; other.expectedShadowSlots = 1; other.tick(66);
        other.expectedShadowSlots = 0; other.tick(67);
        other.shadowPass = true; other.expectedShadowSlots = -1;
        update = {};
        // An idle tick does not complete a recording just because it was ticked.
        setIndependentFps(EVENT_DRIVEN, EVENT_DRIVEN);
        const auto idleRecording = other.window.context().recordingCount;
        other.tick(68);
        check("idle tick does not advance recording identity", other.window.context().recordingCount == idleRecording);
        // Keep the draw listener through every invalid-swapchain attempt and
        // recovery: these attempts commit offscreen resources, but no real draw.
        setIndependentFps(EVENT_DRIVEN, VSYNC);
        other.draw = events().draw.listen([&] { other.renderOffscreen(); });
        rejectSwapchain = true;
        const auto attemptsBefore = other.offscreenRecordings;
        for (int n = 69; n < 81; ++n) {
            const auto recording = other.window.context().recordingCount;
            other.tick(n);
            check("failed admission completes one owner recording", other.window.context().recordingCount == recording + 1);
            check("failed admission retains real draw identity and no afterFrame",
                other.window.context().drawCount == before && afterFrames == 0);
        }
        check("resource listener runs on all rejected attempts", other.offscreenRecordings == attemptsBefore + 12);
        other.shadowPass = false; other.expectedShadowSlots = 1; other.tick(81);
        other.expectedShadowSlots = 0; other.tick(82);
        other.shadowPass = true; other.expectedShadowSlots = -1;
        rejectSwapchain = false; other.tick(83);
        check("first recovered draw completes normally", other.window.context().drawCount == before + 1 && afterFrames == 1);
        internal::currentWindowCtx() = nullptr;
    }
    // GPU globals intentionally live until process exit, matching offscreen probes.
    return failures ? 1 : 0;
}
