// Real GPU regression for #239: no native window or presentation mock.
#define GL_GLEXT_PROTOTYPES
#include <TrussC.h>
#include "../common/tcCoreTest.h"
#include <EGL/egl.h>
#if defined(SOKOL_GLES3)
#include <GLES3/gl3.h>
#else
#include <GL/gl.h>
#endif
#include <cstdio>
#include <array>
using namespace tc;
static int legacySwapchains;
extern "C" sapp_swapchain __wrap_sapp_get_swapchain() { ++legacySwapchains; return {}; }

namespace renewal {
using _sapp_tc_x11_surface_t = EGLSurface;
struct _sapp_tc_window_t { uint32_t win_id = 7; EGLSurface drawable{}; uintptr_t xwin = 1; int swap_interval = 1; };
static struct { EGLDisplay egl_display{}; EGLConfig fbconfig{}; int error_code = 0; size_t window_slots = 1;
    _sapp_tc_window_t* windows[1]{}; } _sapp_tc;
static _sapp_tc_window_t* _sapp_tc_lookup(sapp_window h) {
    auto* w = _sapp_tc.windows[0]; return w && w->win_id == h.id ? w : nullptr;
}
static void _sapp_tc_x11_grab_error_handler() {}
static void _sapp_tc_x11_release_error_handler() {}
static void _sapp_tc_x11_set_swapinterval(_sapp_tc_window_t*, int) {}
static EGLSurface eglCreateWindowSurface(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint*) { return EGL_NO_SURFACE; }
#if !defined(SOKOL_GLES3)
#define SOKOL_GLES3
#include "renewal.inc"
#undef SOKOL_GLES3
#else
#include "renewal.inc"
#endif
}
namespace {
int failures;
void check(const char* name, bool ok) {
    std::printf("render-boundary: %s: %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
void failedCurrentRenewal(EGLDisplay display, EGLConfig config, EGLSurface& surface) {
    renewal::_sapp_tc_window_t window;
    window.drawable = surface;
    renewal::_sapp_tc.egl_display = display; renewal::_sapp_tc.fbconfig = config;
    renewal::_sapp_tc.windows[0] = &window;
    const auto context = eglGetCurrentContext();
    check("failed renewal leaves a terminal window without a drawable",
        !renewal::_sapp_tc_x11_resize_surface(&window) && !window.drawable);
    check("failed current drawable is unbound before the native failure exit",
        eglGetCurrentContext() == EGL_NO_CONTEXT &&
        eglGetCurrentSurface(EGL_DRAW) == EGL_NO_SURFACE);
    // The native caller fails this window. Recreate only the test fixture so
    // the independent rendering checks below still have a real EGL context.
    const EGLint size[] = {EGL_WIDTH,64,EGL_HEIGHT,64,EGL_NONE};
    surface = ::eglCreatePbufferSurface(display, config, size);
    check("fixture restores its drawable", surface != EGL_NO_SURFACE &&
        eglMakeCurrent(display, surface, surface, context));
    renewal::_sapp_tc.windows[0] = nullptr;
}
void processPasses() {
    internal::restoreWindowContext(nullptr);
    auto& context = internal::currentWindowContext();
    int warnings = 0;
    auto logs = getLogger().onLog.listen([&](LogEventArgs& event) {
        if (event.level == LogLevel::Warning && event.message.find("No Window selected") != std::string::npos) {
            ++warnings;
            check("process draw warning names window callback and Fbo", event.message.find("window callback") != std::string::npos &&
                event.message.find("Fbo") != std::string::npos);
        }
    });
    // Loaded is the only seam: refusal must precede any shader/pipeline use.
    struct DrawProbe : FullscreenShader { DrawProbe() { loaded = true; } } shader;
    glClearColor(1, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
    const int before = legacySwapchains;
    std::thread worker([&] { runOnMainThread([&] {
        clear(0, 1, 0, 1); shader.draw(); shader.draw();
        check("process root pass cleanly refuses", !internal::ensureCurrentPass() && !context.swapchainTarget.inPass);
        Fbo fbo; fbo.allocate(8, 8);
        fbo.begin(0, 1, 0); clear(0, 1, 0, 1);
        unsigned char pixel[4]{}; glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        check("explicit process Fbo renders real green pixels", pixel[0] == 0 && pixel[1] == 255 && pixel[2] == 0 && pixel[3] == 255);
        fbo.end();
    }); });
    worker.join();
    { internal::HostTurnScope turn; }
    check("process root refusal warns once and never acquires legacy main swapchain", warnings == 1 && legacySwapchains == before);
    check("process pass never imports main dimensions", context.fbWidth == 0 && context.fbHeight == 0 &&
        context.swapchainTarget.width == 0 && context.swapchainTarget.height == 0);
    internal::parkWindowPass();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    unsigned char pixel[4]{}; glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    check("queued process clear and Shader draw leave window pixels untouched", pixel[0] == 255 && pixel[1] == 0 && pixel[2] == 0);
    sg_reset_state_cache();
}
struct WindowProbe : Window {
    int nativeStandIn = 0;
    WindowProbe() { native_ = &nativeStandIn; internal::WindowRequestAccess::bindForTests(*this, 202); }
    ~WindowProbe() { native_ = nullptr; internal::WindowRequestAccess::unbind(*this); internal::WindowRequestAccess::endApp(*this); }
};
bool rejectSwapchain = false;
sg_swapchain swapchain(void*) {
    sg_swapchain s{}; s.invalid = rejectSwapchain; s.width = s.height = 64; s.sample_count = 1;
    s.color_format = SG_PIXELFORMAT_RGBA8; s.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    return s;
}
void recordingIsolation() {
    WindowProbe owner;
    auto& ctx = owner.context();
    internal::currentWindowCtx() = &ctx;
    ctx.fbWidth = ctx.fbHeight = 64; ctx.dpiScale = 1;
    ctx.swapchainTarget.width = ctx.swapchainTarget.height = 64;
    ctx.swapchainTarget.sampleCount = 1; ctx.swapchainTarget.colorFormat = SG_PIXELFORMAT_RGBA8;
    ctx.acquireSwapchain = swapchain;
    beginFrame();
    clear(1, 0, 0, 1); setColor(1, 0, 0); drawRect(0, 0, 64, 64);
    const int before = sgl_num_vertices();
    check("caller has pending red geometry before process recording", before == 6);
    const auto caller = sgl_get_context();
    auto& process = internal::nonWindowContext();
    process.processSwapchainWarned = false;
    int warnings = 0;
    auto logs = getLogger().onLog.listen([&](LogEventArgs& event) {
        if (event.level == LogLevel::Warning && event.message.find("No Window selected") != std::string::npos) {
            ++warnings;
            check("recording refusal uses the shared guidance", event.message.find("window callback") != std::string::npos &&
                event.message.find("Fbo") != std::string::npos);
        }
    });
    std::thread worker([&] { runOnMainThread([&] {
        check("process recording selects no Window or SGL context", !internal::currentWindowContext().hostWindow && !sgl_get_context().id);
        setColor(0, 1, 0); drawRect(0, 0, 64, 64); drawRect(0, 0, 64, 64);
        pushMatrix(); translate(300, 400); scale(2, 3); popMatrix();
        setBlendMode(BlendMode::Add);
        check("refused immediate recording emits no vertices", sgl_num_vertices() == 0);
        Fbo fbo; fbo.allocate(8, 8);
        fbo.begin(1, 0, 0); resetBlendMode(); setColor(0, 1, 0); drawRect(0, 0, 8, 8);
        internal::currentTarget().flushLayers();
        unsigned char pixel[4]{}; glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        check("process immediate Fbo drawing produces green pixels", pixel[0] == 0 && pixel[1] == 255 && pixel[2] == 0 && pixel[3] == 255);
        fbo.end();
        check("process Fbo exit selects no SGL context", !sgl_get_context().id);
        drawRect(0, 0, 64, 64);
    }); });
    worker.join();
    { internal::HostTurnScope turn; }
    check("process root recording warns once per context", warnings == 1);
    check("caller SGL recording and vertex count are unchanged", sgl_get_context().id == caller.id && sgl_num_vertices() == before);
    // Flush the retained red prefix so any injected green geometry remains visible.
    internal::ensureCurrentPass(); ctx.swapchainTarget.flushLayers();
    unsigned char pixel[4]{}; glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    check("caller frame stays red after process immediate drawing", pixel[0] == 255 && pixel[1] == 0 && pixel[2] == 0 && pixel[3] == 255);
    ctx.swapchainTarget.endPass();
    internal::restoreWindowContext(nullptr);
    sg_reset_state_cache();
}
struct Resources {
    WindowProbe window;
    std::shared_ptr<App> queryOwner = internal::makeConstructedAppAt<App>(&window.context());
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
        internal::AppOwnerScope owner(internal::appOwner(*queryOwner));
        auto& ctx = window.context();
        const auto tick = internal::resourceFrameKey();
        auto receiver = createPlane(8, 8);
        if (shadowPass) {
            beginShadowPass(light);
            check("offscreen shadow admitted on every tick", ctx.shadow.inPass && ctx.shadow.count == 1);
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
        check("identity stays fixed during tick", internal::resourceFrameKey() == tick);
        check("offscreen public count contract", getFrameCount() == getUpdateCount());
        ++offscreenRecordings;
    }
    void setup(bool sync) {
        internal::AppOwnerScope owner(internal::appOwner(*queryOwner));
        synchronized = sync;
        auto& ctx = window.context(); internal::currentWindowCtx() = &ctx;
        ctx.fbWidth = ctx.fbHeight = 64; ctx.dpiScale = 1;
        ctx.swapchainTarget.width = ctx.swapchainTarget.height = 64;
        ctx.swapchainTarget.sampleCount = 1; ctx.swapchainTarget.colorFormat = SG_PIXELFORMAT_RGBA8;
        ctx.acquireSwapchain = swapchain;
        if (synchronized) window.setFps(VSYNC); else window.setIndependentFps(EVENT_DRIVEN, VSYNC);
        light.enableShadow(32); addLight(light); fbo.allocate(16,16); sink.allocate(16,16);
        draw = window.events().draw.listen([this] {
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
                check("shadow validity ages by admitted secondary ticks", !pending.empty() &&
                    pending.back().cmd.fsp.shadowMapParams[1] == expectedShadowSlots);
            }
            uint8_t glyph[13] = {static_cast<uint8_t>(++draws)};
            bitmapfont::updateGlyph(65, glyph);
            ensureFontAtlasForText("A");
            fbo.begin(); clear(0,0,0,1); fbo.end(); fbo.draw(0,0);
            auto image = fbo.getTexture().getImage().id;
            if (!firstImage) firstImage = image;
            check("single revision frames reuse FBO storage", image == firstImage);
        });
    }
    void tick(int n, bool force = false) {
        internal::AppOwnerScope owner(internal::appOwner(*queryOwner));
        internal::restoreWindowContext(&window.context());
        internal::runWindowTick(std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(17*n), sourceDuration, force);
    }
};

struct ShadowReplayProbe {
    sg_image image{};
    std::array<float, 32*32*internal::maxShadowLights> before{}, atReplay{};
    bool acquired = false;
    static void read(sg_image image, float* pixels) {
        const auto native = sg_gl_query_image_info(image);
#if defined(SOKOL_GLES3)
        // GLES has no glGetTexImage. Read every array layer through an FBO;
        // RGBA float readback also works for the single-channel shadow target.
        GLint previous = 0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous);
        GLuint fbo = 0;
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        std::array<float, 32*32*4> rgba{};
        for (int layer = 0; layer < internal::maxShadowLights; ++layer) {
            glFramebufferTextureLayer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                native.tex[native.active_slot], 0, layer);
            check("GLES shadow readback FBO complete",
                glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            glReadPixels(0, 0, 32, 32, GL_RGBA, GL_FLOAT, rgba.data());
            for (int pixel = 0; pixel < 32*32; ++pixel)
                pixels[layer*32*32+pixel] = rgba[pixel*4];
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER, previous);
        glDeleteFramebuffers(1, &fbo);
        check("GLES shadow readback succeeds", glGetError() == GL_NO_ERROR);
#else
        glBindTexture(GL_TEXTURE_2D_ARRAY, native.tex[native.active_slot]);
        glGetTexImage(GL_TEXTURE_2D_ARRAY, 0, GL_RED, GL_FLOAT, pixels);
#endif
        sg_reset_state_cache();
    }
    static sg_swapchain acquire(void* user) {
        auto& probe = *static_cast<ShadowReplayProbe*>(user);
        probe.acquired = true;
        read(probe.image, probe.atReplay.data());
        return swapchain(nullptr);
    }
};
void pendingShadowConsumer(Resources& owner) {
    auto& ctx = owner.window.context();
    internal::restoreWindowContext(&ctx);
    owner.draw = {};
    owner.window.setIndependentFps(EVENT_DRIVEN, EVENT_DRIVEN); ctx.loop.redrawCount = 0;
    owner.tick(105); // a newly admitted secondary tick, before any shadow producer
    owner.light.setShadowArea(Vec3(0,0,0),16);
    auto mesh = createBox(8);
    beginShadowPass(owner.light); shadowDraw(mesh); endShadowPass();
    ShadowReplayProbe probe;
    probe.image = ctx.shadow.storage->colorImage;
    ShadowReplayProbe::read(probe.image, probe.before.data());
    check("shadow producer writes original depth pixels", *std::min_element(probe.before.begin(), probe.before.begin()+32*32) < 10000);
    Material material; setMaterial(material); mesh.draw(); clearMaterial();
    auto& pending = ctx.swapchainTarget.deferredPbrDraws;
    check("shadow consumer captures original slot", pending.size() == 1 &&
        pending.back().cmd.shadowGeneration == ctx.shadow.storage && pending.back().cmd.fsp.shadowMapParams[1] == 1);
    const auto draws = ctx.drawCount, recording = internal::resourceFrameKey();
    owner.tick(106);
    check("idle commit preserves shadow consumer", pending.size() == 1 && internal::resourceFrameKey() == recording + 1);
    ctx.acquireSwapchain = ShadowReplayProbe::acquire; ctx.acquireSwapchainUser = &probe;
    // Existing consumeShadowSlot must replay the old draw BEFORE clearing the
    // reused layer. Acquisition reads its pixels at that consumption boundary.
    beginShadowPass(owner.light);
    check("shadow replay sees original pixels before overwrite", probe.acquired &&
        std::equal(probe.before.begin(), probe.before.begin()+32*32, probe.atReplay.begin()));
    check("shadow slot reused only after pending consumer replay", pending.empty() && ctx.shadow.inPass &&
        ctx.shadow.count == 1 && ctx.shadow.storage->colorImage.id == probe.image.id);
    endShadowPass();
    std::array<float, 32*32*internal::maxShadowLights> after{};
    ShadowReplayProbe::read(probe.image, after.data());
    check("next shadow pass writes new content to recycled slot", std::all_of(after.begin(), after.begin()+32*32,
        [](float value) { return value == 10000; }));
    check("internal shadow replay does not count a window draw", ctx.drawCount == draws);
    ctx.acquireSwapchain = swapchain; ctx.acquireSwapchainUser = nullptr;
    owner.tick(107);
    beginShadowPass(owner.light); endShadowPass();
    check("consumed shadow slot continues to recycle", ctx.shadow.storage->colorImage.id == probe.image.id && ctx.shadow.count == 1);
    owner.tick(108);
}
}
TC_CORE_TEST_MAIN() {
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major, minor;
    if (!eglInitialize(display, &major, &minor)) return 2;
#if defined(SOKOL_GLES3)
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint renderable = EGL_OPENGL_ES3_BIT;
    const EGLint version[] = {EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
#else
    eglBindAPI(EGL_OPENGL_API);
    const EGLint renderable = EGL_OPENGL_BIT;
    const EGLint version[] = {EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,EGL_NONE};
#endif
    const EGLint attrs[] = {EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,renderable,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_DEPTH_SIZE,24,EGL_STENCIL_SIZE,8,EGL_NONE};
    EGLConfig config; EGLint count;
    if (!eglChooseConfig(display, attrs, &config, 1, &count) || count != 1) return 2;
    const EGLint size[] = {EGL_WIDTH,64,EGL_HEIGHT,64,EGL_NONE};
    auto surface = eglCreatePbufferSurface(display, config, size);
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
    failedCurrentRenewal(display, config, surface);
    processPasses();
    recordingIsolation();
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
        // Base's atlas guard suppresses a repeat upload at the same resource key.
        internal::restoreWindowContext(&fast.window.context());
        bitmapfont::updateGlyph(65, glyph); ensureFontAtlasForText("A");
        const auto uploaded = internal::bitmapFontAtlas().version;
        bitmapfont::updateGlyph(65, glyph); ensureFontAtlasForText("A");
        check("atlas defers repeated upload at the same base-equivalent key", internal::bitmapFontAtlas().version == uploaded);
        const auto gpuFrame = internal::shaderStreamFrame();
        internal::runWindowTick(std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(425),
            1.0/60, false, nullptr, true);
        check("native gate cannot commit foreign GPU work", internal::shaderStreamFrame() == gpuFrame);
        internal::endGpuFrame();
        // Hidden forced capture is a real draw, with the same resource boundaries.
        auto hiddenMetadata = internal::WindowRequestAccess::binding(slow.window).metadata;
        hiddenMetadata.trust_hidden_signals = true; hiddenMetadata.occluded = true;
        internal::publishWindowMetadata(slow.window, hiddenMetadata);
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
        fast.synchronized = true; fast.window.setFps(VSYNC);
        for (int n = 40; n < 52; ++n) fast.tick(n);
        check("synchronized control updates and draws", fast.draws == synchronizedDraws + 12 && fast.window.context().updateCount == 12);
        auto& owner = slow; auto& other = fast;
        hiddenMetadata.occluded = false; internal::publishWindowMetadata(owner.window, hiddenMetadata);
        owner.tick(40);
        // Prelude FBO revisions belong to the next draw of their owner. A GPU
        // commit from another window cannot retire the first sampled revision.
        internal::restoreWindowContext(&owner.window.context());
        owner.fbo.begin(); clear(0,0,0,1); owner.fbo.end(); owner.fbo.draw(0,0);
        auto first = owner.fbo.getTexture().getImage().id;
        Fbo completedSource;
        completedSource.allocate(16,16);
        completedSource.begin(1,0,0); completedSource.end();
        owner.sink.begin(0,0,0); completedSource.draw(0,0); owner.sink.end();
        const auto completed = completedSource.getColorImage();
        other.tick(52); other.tick(53);
        internal::restoreWindowContext(&owner.window.context());
        completedSource.begin(0,1,0); completedSource.end();
        check("foreign commits do not reset FBO owner tick after offscreen consumption",
            completedSource.getColorImage().id != completed.id);
        owner.fbo.begin(); clear(0,0,0,1); owner.fbo.end(); owner.fbo.draw(0,0);
        check("other window commits do not recycle owner FBO revisions", owner.fbo.getTexture().getImage().id != first);
        internal::endGpuFrame();
        // Run the resource-bearing listener on twelve update-only commits.
        internal::restoreWindowContext(&other.window.context());
        other.draw = {}; other.firstImage = 0;
        other.window.setIndependentFps(VSYNC, EVENT_DRIVEN);
        other.window.context().loop.redrawCount = 0;
        int afterFrames = 0;
        auto after = other.window.events().afterFrame.listen([&] { ++afterFrames; });
        auto update = other.window.events().update.listen([&] { other.renderOffscreen(); });
        const auto before = other.window.context().drawCount;
        for (int n = 54; n < 66; ++n) {
            const auto tick = other.window.context().resourceTickCount;
            other.tick(n);
            check("update-only work completes one owner tick", other.window.context().resourceTickCount == tick + 1);
        }
        check("twelve update-only ticks keep real draw count", other.offscreenRecordings == 12 &&
            other.window.context().drawCount == before && afterFrames == 0);
        other.shadowPass = false; other.expectedShadowSlots = 1; other.tick(66);
        other.expectedShadowSlots = 0; other.tick(67);
        other.shadowPass = true; other.expectedShadowSlots = -1;
        update = {};
        // Idle callbacks retain the base secondary admission key.
        other.window.setIndependentFps(EVENT_DRIVEN, EVENT_DRIVEN);
        const auto idleRecording = other.window.context().resourceTickCount;
        other.tick(68);
        check("idle secondary callback advances the base admission key", other.window.context().resourceTickCount == idleRecording + 1);
        // Keep the draw listener through every invalid-swapchain attempt and
        // recovery: these attempts commit offscreen resources, but no real draw.
        other.window.setIndependentFps(EVENT_DRIVEN, VSYNC);
        other.draw = other.window.events().draw.listen([&] { other.renderOffscreen(); });
        rejectSwapchain = true;
        const auto attemptsBefore = other.offscreenRecordings;
        for (int n = 69; n < 81; ++n) {
            const auto tick = other.window.context().resourceTickCount;
            other.tick(n);
            check("failed admission completes one owner tick", other.window.context().resourceTickCount == tick + 1);
            check("failed admission retains real draw identity and no afterFrame",
                other.window.context().drawCount == before && afterFrames == 0);
        }
        check("resource listener runs on all rejected attempts", other.offscreenRecordings == attemptsBefore + 12);
        other.shadowPass = false; other.expectedShadowSlots = 1; other.tick(81);
        other.expectedShadowSlots = 0; other.tick(82);
        other.shadowPass = true; other.expectedShadowSlots = -1;
        rejectSwapchain = false; other.tick(83);
        check("first recovered draw completes normally", other.window.context().drawCount == before + 1 && afterFrames == 1);
        pendingShadowConsumer(other);
        internal::currentWindowCtx() = nullptr;
    }
    // All probe FBOs, meshes, listeners and window targets have now released
    // their resources. Drain the same retirement queue used at frame boundaries
    // while EGL is current, then use the framework shutdown (font, sgl, sg).
    // Device-owned shader/pipeline caches are reclaimed by sg_shutdown there.
    internal::drainPendingGpuDestroys();
    check("resource release leaves no GL error", glGetError() == GL_NO_ERROR);
    trussc::cleanup();
    check("framework shuts down sokol", !sg_isvalid());
    check("shutdown leaves no GL error", glGetError() == GL_NO_ERROR);
    check("EGL releases current context", eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT));
    check("EGL destroys context", eglDestroyContext(display, context));
    check("EGL destroys surface", eglDestroySurface(display, surface));
    check("EGL terminates", eglTerminate(display));
    return failures ? 1 : 0;
}
