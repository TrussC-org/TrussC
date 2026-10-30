#define SOKOL_TC_TEST_HOOKS // Test-only framework trace declarations.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <cstdio>
#include <cstring>
#if defined(_WIN32)
#include "../../../platform/win/tcCaptureTrace_win.h"
#endif
#if defined(SOKOL_GLCORE)
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#if defined(__linux__)
#include <GL/glx.h>
#endif
#elif defined(SOKOL_GLES3)
#include <GLES3/gl3.h>
#endif
using namespace tc;
namespace {
int failures = 0;
bool completed = false;
#if defined(_WIN32)
ScreenRecorder* diagnosticRecorder = nullptr;
void printCaptureTrace(const internal::D3D11CaptureTrace& t) {
    const int rw = diagnosticRecorder ? diagnosticRecorder->writer().getWidth() : 0;
    const int rh = diagnosticRecorder ? diagnosticRecorder->writer().getHeight() : 0;
    const auto layout = internal::recorderPackedLayout(rw, rh, internal::RecorderPixelFormat::RGBA8);
    std::printf("captureSubmission DIAG secondary windowId=%u fb=%dx%d "
        "lastSwapchain=%dx%d sgFormat=%d samples=%d renderView=%p resolveView=%p "
        "selectedRenderView=%p texture=%p desc=%ux%u dxgiFormat=%u samples=%u "
        "resolveFromSelected=%d reconciled=%ux%u Pixels=%dx%d success=%d "
        "recorder=%dx%d input=RGBA8 MF=BGRA8 rowBytes=%zu byteSize=%zu\n",
        t.windowId, t.fbWidth, t.fbHeight, t.lastWidth, t.lastHeight, t.lastFormat, t.lastSamples,
        t.lastRenderView, t.lastResolveView, t.selectedView, t.selectedTexture,
        t.width, t.height, t.format, t.samples, int(t.samples > 1),
        t.reconciledWidth, t.reconciledHeight, t.pixelWidth, t.pixelHeight, int(t.succeeded),
        rw, rh, layout.rowBytes, layout.byteSize);
}
#endif
void check(const char* label, bool ok) {
    std::printf("captureSubmission %s: %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
bool colorAt(const Pixels& pixels, int x, int y, int r, int g, int b) {
    if (!pixels.isAllocated() || x >= pixels.getWidth() || y >= pixels.getHeight()) return false;
    const auto* p = pixels.getData() + (y * pixels.getWidth() + x) * 4;
    return p[0] == r && p[1] == g && p[2] == b;
}
class CaptureApp : public App {
    std::shared_ptr<tc::Window> secondary_;
    uint64_t mainOwner_ = 0;
    int mainPresents_ = 0, mainAuthorizations_ = 0, prefixes_ = 0;
    int phaseWarnings_ = 0, phaseErrors_ = 0;
    int stackWarnings_ = 0;
    EventListener log_, after_, overlay_, draw_;
public:
    void setup() override {
        // This driver explicitly presents several frames. Run in the framework
        // Draw entry before Node::drawTree pushes its own matrix: present()
        // resets frame stacks and must not consume a live node's push.
        draw_ = events().draw.listen([this] { runCases(); });
        auto* mainWindow = &internal::currentWindowContext();
        log_ = getLogger().onLog.listen([this, mainWindow](LogEventArgs& e) {
            if (e.level == LogLevel::Warning &&
                (e.message.find("pushMatrix") != std::string::npos || e.message.find("pushStyle") != std::string::npos))
                ++stackWarnings_;
            if (e.message.rfind("[Screenshot]", 0) != 0) return;
            // The once-per-window contract below belongs to A. B's own
            // refusals must not affect it, whether B runs or is skipped.
            if (&internal::currentWindowContext() != mainWindow) return;
            if (e.level == LogLevel::Warning) ++phaseWarnings_;
            if (e.level >= LogLevel::Error) ++phaseErrors_;
        });
        Pixels outside;
        check("setup is outside a frame", !grabScreen(outside));
        check("setup rejection emits one Warning and no Error", phaseWarnings_ == 1 && phaseErrors_ == 0);
        after_ = events().afterFrame.listen([this] {
            if (!completed) return;
            Pixels full;
            check("afterFrame listener captures the finished frame", grabScreen(full) &&
                colorAt(full,16,16,255,255,255) && colorAt(full,48,16,255,255,0) &&
                colorAt(full,112,16,0,255,0));
            check("repeated outside calls emit only one Warning", phaseWarnings_ == 1 && phaseErrors_ == 0);
            check("coordinator presents leave stacks balanced", stackWarnings_ == 0);
            exitApp();
        });
#if defined(__linux__) && defined(SOKOL_GLCORE)
        if (internal::supportsSecondaryWindows()) {
            // #320: use Xft.dpi=192 under Xvfb to distinguish pre-tick DPI from 1.0.
            WindowSettings dpiSettings; dpiSettings.setSize(160,96); dpiSettings.setHighDpi(true);
            auto dpiWindow = createWindow(dpiSettings);
            check("early DPI window created", bool(dpiWindow));
            if (dpiWindow) {
                auto& ctx = dpiWindow->context();
                dpiWindow->setApp<App>();
                internal::applyPendingAppForTests(*dpiWindow);
                internal::AppOwnerScope queryScope(internal::appOwner(*dpiWindow->getApp()));
                const float nativeScale = ctx.fbWidth / 160.0f;
                check("DPI initialized before first tick", ctx.updateCount == 0 &&
                    nativeScale > 0 && std::abs(ctx.dpiScale - nativeScale) < 0.01f);
                auto* previous = internal::currentWindowCtx();
                internal::restoreWindowContext(&ctx);
                auto extentIs = [&](unsigned width, unsigned height) {
                    auto* display = glXGetCurrentDisplay();
                    XSync(display, False);
                    unsigned w = 0, h = 0;
                    glXQueryDrawable(display, glXGetCurrentDrawable(), GLX_WIDTH, &w);
                    glXQueryDrawable(display, glXGetCurrentDrawable(), GLX_HEIGHT, &h);
                    // X11 rounds physical sizes; the initial extent/160 ratio can
                    // differ slightly from the exact display DPI.
                    return std::abs(int(w) - int(width)) <= 1 &&
                        std::abs(int(h) - int(height)) <= 1;
                };
                dpiWindow->setSize(120,80);
                check("setSize immediately after create uses native DPI", extentIs(
                    unsigned(120 * nativeScale), unsigned(80 * nativeScale)));
                const bool pixelPerfect = internal::pixelPerfectMode();
                internal::pixelPerfectMode() = false;
                setWindowSize(100,60);
                check("logical setWindowSize before first tick", extentIs(
                    unsigned(100 * nativeScale), unsigned(60 * nativeScale)));
                internal::pixelPerfectMode() = true;
                setWindowSize(200,120);
                check("pixel-perfect setWindowSize before first tick", extentIs(
                    unsigned(int(200 / nativeScale) * nativeScale),
                    unsigned(int(120 / nativeScale) * nativeScale)));
                internal::pixelPerfectMode() = pixelPerfect;
                internal::restoreWindowContext(previous);
                dpiWindow->close();
            }
        }
#endif
        if (!internal::supportsSecondaryWindows()) {
            std::puts("SKIP: secondary windows unsupported on this platform");
        } else {
#if defined(SOKOL_METAL)
            std::puts("SKIP: secondary foreign-window cases: on Metal, a secondary window's drawable can't be acquired outside its native tick");
#else
            WindowSettings settings; settings.setSize(160,96); settings.setHighDpi(false);
            settings.sampleCount = internal::currentTarget().sampleCount;
            secondary_ = createWindow(settings);
            check("secondary created", bool(secondary_));
#if defined(_WIN32)
            if (secondary_) {
                auto& ctx = secondary_->context();
                const auto sc = ctx.acquireSwapchain(ctx.acquireSwapchainUser);
                check("D3D11 B views available before its first native tick", sc.d3d11.render_view &&
                    sc.d3d11.depth_stencil_view && sc.width == ctx.fbWidth && sc.height == ctx.fbHeight &&
                    sc.sample_count == settings.sampleCount && bool(sc.d3d11.resolve_view) == (settings.sampleCount > 1));
            }
#endif
#endif
        }
        mainOwner_ = internal::currentWindowContext().swapchainTarget.ownerId;
        sg_tc_mtl_set_drawable_trace([](int action, uint64_t owner, uint64_t, uint64_t, void* user) {
            auto& app = *static_cast<CaptureApp*>(user);
            if (action == 4) ++app.prefixes_;
            if (owner == app.mainOwner_ && action == 1) ++app.mainAuthorizations_;
            if (owner == app.mainOwner_ && action == 2) {
                ++app.mainPresents_;
                check("Metal presentation follows owner authorization", app.mainAuthorizations_ == app.mainPresents_);
            }
        }, this);
    }
    void exit() override {
        sg_tc_mtl_set_drawable_trace(nullptr,nullptr);
#if defined(_WIN32)
        internal::setD3D11CaptureTraceForTests(nullptr);
#endif
    }
    void update() override {
        Pixels pixels;
        check("update returns false after beginFrame", !grabScreen(pixels));
        check("repeated update returns false", !grabScreen(pixels));
        check("update rejection warns only once per window", phaseWarnings_ == 1 && phaseErrors_ == 0);
    }
    void runCases() {
        if (completed) { exitApp(); return; }
        auto* mainWindow = internal::currentWindowCtx();
        auto& a = internal::currentWindowContext().swapchainTarget;
        clear(1,0,0);
        setColor(1,1,1); drawRect(8,8,16,16);
        check("first main frame has only CPU recordings before capture", !a.inPass && !a.passStartedThisFrame);
        Pixels inFrame;
        check("first main draw capture submits clear and CPU prefix then resumes", grabScreen(inFrame) &&
            colorAt(inFrame,16,16,255,255,255) && colorAt(inFrame,40,40,255,0,0) && a.inPass);
        a.flushLayers();
        const auto token = a.activation;
        const auto frame = a.frameEpoch;
        check("A actually began its pass", a.inPass);
        VideoRecordSettings recordSettings; recordSettings.audio = false; recordSettings.fps = 25;
        if (secondary_) {
            internal::restoreWindowContext(&secondary_->context());
            {
                auto query = internal::makeConstructedAppAt<App>(&secondary_->context());
                internal::AppOwnerScope queryScope(internal::appOwner(*query));
                internal::EntryStackGuard secondaryDraw(internal::AppEntry::Draw);
                internal::prepareWindowTarget(); beginFrame(); clear(0,0,1);
                const auto estimate = internal::captureWindowSize(internal::currentWindowContext());
                check("secondary pre-pass capture estimate uses B dimensions", estimate.first == size_t(getFramebufferWidth()) &&
                    estimate.second == size_t(getFramebufferHeight()));
                setColor(1); drawRect(8,8,16,16);
                auto& secondaryTarget = internal::currentTarget();
                check("first secondary frame has only CPU recordings before capture",
                    !secondaryTarget.inPass && !secondaryTarget.passStartedThisFrame);
                Pixels secondaryInFrame;
                check("secondary synchronous capture with A parked", grabScreen(secondaryInFrame) &&
                    colorAt(secondaryInFrame,16,16,255,255,255) &&
                    colorAt(secondaryInFrame,40,40,0,0,255) && !a.inPass);
                const auto source = internal::windowCaptureSource();
                check("lazy secondary capture records B swapchain dimensions", source.swapchain.width == getFramebufferWidth() &&
                    source.swapchain.height == getFramebufferHeight() && source.swapchain.sample_count == a.sampleCount);
                internal::parkWindowPass();
                setColor(1,1,0); drawRect(72,8,16,16);
                check("secondary capture flushes new CPU drawings after parking", !secondaryTarget.inPass &&
                    grabScreen(secondaryInFrame) && colorAt(secondaryInFrame,80,16,255,255,0) &&
                    colorAt(secondaryInFrame,16,16,255,255,255) && colorAt(secondaryInFrame,40,40,0,0,255));
                present();
                Pixels b;
                check("B captures its own dimensions and pixels", grabScreen(b) &&
                    b.getWidth() == getFramebufferWidth() && b.getHeight() == getFramebufferHeight() &&
                    b.getWidth() != 256 && colorAt(b,40,40,0,0,255));
                // Exercise real recorder capture/encode with B's source extent.
                ScreenRecorder recorder;
#if defined(_WIN32)
                diagnosticRecorder = &recorder;
#endif
                const auto movie = fs::temp_directory_path() / "tc_capture_submission_secondary.mp4";
                const auto opened = recorder.start(movie, recordSettings);
                if (opened) {
                    check("secondary recorder opens at its source size", recorder.writer().getWidth() == b.getWidth() &&
                        recorder.writer().getHeight() == b.getHeight());
                    for (int marker = 0; marker < 5; ++marker) {
                        clear(marker == 0 || marker == 3 ? 1 : 0, marker == 1 || marker >= 3 ? 1 : 0,
                            marker == 2 || marker == 4 ? 1 : 0);
                        present(); events().afterFrame.notify();
                        std::this_thread::sleep_for(std::chrono::milliseconds(40));
                    }
                    const auto stopped = recorder.stop();
                    std::printf("captureSubmission recorder accepted=%d size=%dx%d file=%s\n",recorder.getFrameCount(),
                        recorder.writer().getWidth(),recorder.writer().getHeight(),movie.string().c_str());
                    check("native screen take finalizes with accepted frames", stopped.ok() && recorder.getFrameCount() > 0);
                } else std::printf("SKIP: captureSubmission recorder codec unavailable: %s\n",opened.message.c_str());
#if defined(_WIN32)
                diagnosticRecorder = nullptr;
                auto& ctx = internal::currentWindowContext();
                const auto saved = ctx.lastSwapchain;
                ctx.lastSwapchain = {};
                check("missing secondary view never falls back to A", !internal::windowCaptureSource().swapchain.d3d11.render_view);
                ctx.lastSwapchain = saved;
#endif
            }
            internal::restoreWindowContext(mainWindow);
        } else {
            internal::parkWindowPass();
        }
        check("parking keeps the outgoing activation", !a.inPass && a.activation == token);
        check("A stays lazy until its next draw", !a.inPass && a.activation == token && a.frameEpoch == frame);
#if defined(SOKOL_METAL)
        check("prefix capture and parking never present begun A", mainPresents_ == 0 && mainAuthorizations_ == 0 && prefixes_ > 0);
#endif
        setColor(1,1,0); drawRect(40,8,16,16);
        check("main capture flushes new CPU drawings after parking", !a.inPass &&
            grabScreen(inFrame) && colorAt(inFrame,48,16,255,255,0) &&
            colorAt(inFrame,16,16,255,255,255) && colorAt(inFrame,40,40,255,0,0));
        a.flushLayers();
        a.endPass();
#if defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)
        // Exercise Q2's destination seam without installing a completed-frame
        // allocation policy. All pixels are copied through the end-pass path.
        Fbo destination; destination.allocate(a.width,a.height);
        const auto texture = sg_gl_query_image_info(destination.getColorImage());
        GLint oldRead, oldDraw;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&oldRead);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&oldDraw);
        GLuint framebuffer = 0; glGenFramebuffers(1,&framebuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,framebuffer);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture.tex[texture.active_slot],0);
        check("explicit resolve destination complete", glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
        glScissor(0,0,1,1);
        sg_tc_gl_resolve_swapchain(&a.binding.swapchain,framebuffer);
        GLint restoredRead, restoredDraw;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&restoredRead);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&restoredDraw);
        check("resolve restores READ/DRAW and scissor enable", restoredRead == oldRead &&
            restoredDraw == (GLint)framebuffer && glIsEnabled(GL_SCISSOR_TEST));
        glBindFramebuffer(GL_READ_FRAMEBUFFER,oldRead); glBindFramebuffer(GL_DRAW_FRAMEBUFFER,oldDraw);
        glDeleteFramebuffers(1,&framebuffer);
        // Hostile native pack/read state must survive both screen and Fbo
        // capture, and must never redirect a CPU pointer into a bound PBO.
        GLuint stateFbos[2] = {}, packBuffer = 0;
        glGenFramebuffers(2,stateFbos);
        for (auto id : stateFbos) {
            glBindFramebuffer(GL_FRAMEBUFFER,id);
            glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture.tex[texture.active_slot],0);
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER,stateFbos[0]); glReadBuffer(GL_NONE);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,stateFbos[1]);
        glGenBuffers(1,&packBuffer); glBindBuffer(GL_PIXEL_PACK_BUFFER,packBuffer);
        glBufferData(GL_PIXEL_PACK_BUFFER,16,nullptr,GL_STREAM_READ);
        glPixelStorei(GL_PACK_ALIGNMENT,8); glPixelStorei(GL_PACK_ROW_LENGTH,a.width+13);
        glPixelStorei(GL_PACK_SKIP_ROWS,2); glPixelStorei(GL_PACK_SKIP_PIXELS,3);
#if defined(SOKOL_GLCORE)
        glPixelStorei(GL_PACK_SWAP_BYTES,GL_TRUE);
#endif
        GLint oldViewport[4]; glGetIntegerv(GL_VIEWPORT,oldViewport); glViewport(3,4,31,17);
        Pixels screenDuringPass;
        check("screen read ignores dirty pack state", grabScreen(screenDuringPass) && colorAt(screenDuringPass,16,16,255,255,255));
        Pixels copied; copied.allocate(a.width,a.height,4);
        check("explicit swapchain resolve reads back", destination.readPixels(copied.getData()));
        std::vector<float> floats((size_t)a.width*a.height*4);
        check("float read disables byte swapping", destination.readPixelsFloat(floats.data()) &&
            floats[((size_t)16*a.width+16)*4] == 1.0f);
        GLint gotRead, gotDraw, gotPack, gotAlignment, gotLength, gotRows, gotPixels, gotReadBuffer;
        GLint gotViewport[4], gotScissor[4];
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&gotRead); glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&gotDraw);
        glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&gotPack); glGetIntegerv(GL_PACK_ALIGNMENT,&gotAlignment);
        glGetIntegerv(GL_PACK_ROW_LENGTH,&gotLength); glGetIntegerv(GL_PACK_SKIP_ROWS,&gotRows);
        glGetIntegerv(GL_PACK_SKIP_PIXELS,&gotPixels); glGetIntegerv(GL_READ_BUFFER,&gotReadBuffer);
        glGetIntegerv(GL_VIEWPORT,gotViewport); glGetIntegerv(GL_SCISSOR_BOX,gotScissor);
        check("native read/draw/pack/viewport/scissor state restored", gotRead == (GLint)stateFbos[0] && gotDraw == (GLint)stateFbos[1] &&
            gotPack == (GLint)packBuffer && gotAlignment == 8 && gotLength == a.width+13 && gotRows == 2 && gotPixels == 3 &&
            gotReadBuffer == GL_NONE && gotViewport[0] == 3 && gotViewport[1] == 4 && gotViewport[2] == 31 && gotViewport[3] == 17 &&
            gotScissor[0] == 0 && gotScissor[1] == 0 && gotScissor[2] == 1 && gotScissor[3] == 1);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,oldRead); glBindFramebuffer(GL_DRAW_FRAMEBUFFER,oldDraw);
        glBindBuffer(GL_PIXEL_PACK_BUFFER,0); glDeleteBuffers(1,&packBuffer); glDeleteFramebuffers(2,stateFbos);
        glPixelStorei(GL_PACK_ALIGNMENT,4); glPixelStorei(GL_PACK_ROW_LENGTH,0);
        glPixelStorei(GL_PACK_SKIP_ROWS,0); glPixelStorei(GL_PACK_SKIP_PIXELS,0);
#if defined(SOKOL_GLCORE)
        glPixelStorei(GL_PACK_SWAP_BYTES,GL_FALSE);
#endif
        glViewport(oldViewport[0],oldViewport[1],oldViewport[2],oldViewport[3]);
        int wrong = 0;
        for (int y=0; y<a.height; ++y) for (int x=0; x<a.width; ++x) {
            const bool white = x>=8 && x<24 && y>=8 && y<24;
            const bool yellow = x>=40 && x<56 && y>=8 && y<24;
            wrong += !colorAt(copied,x,y,255,white || yellow ? 255 : 0,white ? 255 : 0);
        }
        std::printf("captureSubmission explicit swapchain resolve mismatched pixels=%d/%d\n",wrong,a.width*a.height);
        check("prefix and suffix survive full scissor-independent resolve", wrong == 0);
#endif
        {
            int captures = 0;
            auto listener = events().onRender.listen([&] {
                Pixels rendered;
                ++captures;
                check("synchronous capture inside present onRender resumes", grabScreen(rendered) &&
                    colorAt(rendered,16,16,255,255,255) && colorAt(rendered,48,16,255,255,0) && a.inPass);
            });
            present();
            check("onRender capture completes once", captures == 1);
        }
#if defined(SOKOL_METAL)
        check("A final owner commit presents exactly once", mainPresents_ == 1 && mainAuthorizations_ == 1);
#endif
        Pixels pixels;
        check("A capture keeps prefix and suffix", grabScreen(pixels) && colorAt(pixels,16,16,255,255,255) &&
            colorAt(pixels,48,16,255,255,0) && colorAt(pixels,80,40,255,0,0));
        // A public Fbo parked above its swapchain retains its lease/token too.
        Fbo fbo; fbo.allocate(32,24,4); fbo.begin(1,0,0);
        auto& target = internal::currentTarget(); const auto fboToken = target.activation;
        setColor(1,1,1); drawRect(0,0,8,8);
        if (secondary_) {
            internal::restoreWindowContext(&secondary_->context());
            present();
            internal::restoreWindowContext(mainWindow);
        } else {
            internal::parkWindowPass();
        }
        check("Fbo top parks with its activation intact", !target.inPass && target.activation == fboToken);
        internal::ensureCurrentPass();
        setColor(1,1,0); drawRect(16,0,8,8); fbo.end();
        Pixels fp; fp.allocate(32,24,4);
        check("Fbo LOAD resume preserves both sides of parking", fbo.readPixels(fp.getData()) &&
            colorAt(fp,4,4,255,255,255) && colorAt(fp,20,4,255,255,0) && colorAt(fp,28,16,255,0,0));
        // The fixed-size Fbo take detects either resize direction before copy.
        bool codecSkipPrinted = false;
        for (int resizedWidth : {48,16}) {
            ScreenRecorder fixed;
            const auto path = fs::temp_directory_path() / ("tc_capture_submission_resize_" + std::to_string(resizedWidth) + ".mp4");
            const auto started = fixed.start(fbo,path,recordSettings);
            if (started) {
                const int originalWidth = fbo.getWidth();
                check("Fbo recorder uses source extent", fixed.writer().getWidth() == originalWidth && fixed.writer().getHeight() == 24);
                fbo.allocate(resizedWidth,24);
                events().afterFrame.notify();
                check("resize skips and counts a fixed-size frame", fixed.isRecording() &&
                    fixed.getRejectedFrameCount() == 1 && fixed.getFrameCount() == 0 &&
                    fixed.writer().getWidth() == originalWidth);
                fbo.allocate(originalWidth,24);
                fbo.begin(); clear(0,0,0); fbo.end();
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                events().afterFrame.notify();
                check("matching Fbo extent resumes the same take", fixed.getFrameCount() == 1 && fixed.stop().ok());
            } else if (!codecSkipPrinted) {
                std::printf("SKIP: captureSubmission Fbo recorder codec unavailable: %s\n",started.message.c_str());
                codecSkipPrinted = true;
            }
        }
        sg_tc_mtl_set_drawable_trace(nullptr,nullptr);
        // A later onRender overlay must be absent from draw's prefix and
        // present in the real framework afterFrame notification above.
        beginFrame(); clear(1,0,0);
        setColor(1,1,1); drawRect(8,8,16,16);
        setColor(1,1,0); drawRect(40,8,16,16);
        Pixels beforeOverlay;
        check("draw prefix excludes a later overlay", grabScreen(beforeOverlay) &&
            colorAt(beforeOverlay,112,16,255,0,0));
        overlay_ = events().onRender.listen([] {
            setColor(0,1,0); drawRect(104,8,16,16);
        });
        completed = true;
    }
};
}
TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc < 2 || std::strcmp(argv[1],"--gpu-check")) {
        std::puts("SKIP: captureSubmission requires --gpu-check and a display"); return 0;
    }
    WindowSettings settings; settings.setSize(256,192); settings.setHighDpi(false);
    settings.sampleCount = 1;
    for (int i = 2; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--msaa4")) settings.sampleCount = 4;
#if defined(_WIN32)
        if (!std::strcmp(argv[i], "--diag")) internal::setD3D11CaptureTraceForTests(printCaptureTrace);
#endif
    }
    return tcCoreTest::runDisplayTest<CaptureApp>(settings, failures, [] { check("all cases completed", completed); });
}
