// Native counterpart of shaderScopeDummy's synchronous-resize probes.
// macOS changes public dimensions inside setWindowSize; X11 reports the
// resize on a later native tick. Check the first callback exposing that size.
class ResizeStateApp : public App {
    EasyCam camera;
    Fbo fbo;
    internal::TargetCameraState selected;
    int scenario = 0, attempts = 0, oldWidth = 0, oldHeight = 0;
    bool ready = false;
    EventListener draw_;
public:
    void setup() override {
        draw_ = events().draw.listen([this] {
            if (!ready || (scenario != 3 && scenario != 7)) return;
            check("resize leaves active Fbo state intact", tc::getWidth() == 16 && tc::getHeight() == 12);
            // Restore before App::handleDraw/Node::drawTree pushes its own
            // matrix on the selected target. Closing the Fbo inside draw()
            // would discard that live framework push on the child target.
            fbo.end();
            if (scenario == 7) {
                const auto& target = internal::currentTarget();
                check("Fbo restore preserves the open window camera", target.cameraStack.size() == 1 &&
                      sameMatrix(target.camera.currentProjectionMatrix, selected.currentProjectionMatrix) &&
                      sameMatrix(target.camera.currentViewMatrix, selected.currentViewMatrix) &&
                      target.camera.currentCameraContext == selected.currentCameraContext);
            }
        });
    }
    void update() override {
        if (attempts++ == 0) {
            oldWidth = getFramebufferWidth(); oldHeight = getFramebufferHeight();
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
            auto* display = (Display*)sapp_x11_get_display();
            XResizeWindow(display, (::Window)(uintptr_t)sapp_x11_get_window(), 280 + scenario * 40, 220 + scenario * 30);
            XFlush(display);
#else
            setWindowSize(280 + scenario * 40, 220 + scenario * 30);
#endif
        }
        ready = getFramebufferWidth() != oldWidth || getFramebufferHeight() != oldHeight;
        if (!ready) {
            if (attempts > 120) { check("native resize completed", false); exitApp(); }
            return;
        }
        if (scenario == 1) setupScreenFovWithSize(0, getWindowWidth(), getWindowHeight());
        if (scenario == 2) camera.begin();
        if (scenario == 4) { pushMatrix(); translate(7, 8, 9); popMatrix(); }
        if (scenario == 5) { camera.begin(); camera.end(); }
        if (scenario == 6 || scenario == 7) camera.begin();
        selected = internal::currentTarget().camera;
        if (scenario == 3 || scenario == 7) { fbo.allocate(16, 12); fbo.begin(); }
    }
    void draw() override {
        if (!ready) return;
        if (scenario == 6 || scenario == 7) camera.end();
        auto& target = internal::currentTarget();
        check("first resized draw uses the full viewport", target.viewport.w == getFramebufferWidth() &&
              target.viewport.h == getFramebufferHeight());
        if (scenario == 1 || scenario == 2) {
            check("resize preserves explicit projection and camera", sameMatrix(target.camera.currentProjectionMatrix,
                  selected.currentProjectionMatrix) && sameMatrix(target.camera.currentViewMatrix, selected.currentViewMatrix) &&
                  target.camera.currentCameraContext == selected.currentCameraContext);
        } else {
            check("first resized draw uses the automatic screen extent", target.camera.currentViewW == getWindowWidth() &&
                  target.camera.currentViewH == getWindowHeight());
        }
        clear(0); drawRect(0, 0, getWindowWidth(), getWindowHeight());
        if (scenario == 2) camera.end();
        if (scenario != 1) {
            check("screen camera restored after scope closes", target.automaticScreenProjection &&
                  target.camera.currentViewW == getWindowWidth() && target.camera.currentViewH == getWindowHeight());
            const Vec4 clip = target.camera.currentProjectionMatrix * target.camera.currentViewMatrix * Vec4(48, 40, 0, 1);
            check("restored screen geometry maps to framebuffer pixels", std::abs((clip.x / clip.w + 1) * 0.5f *
                  target.viewport.w - 48 * getFramebufferWidth() / float(getWindowWidth())) < 0.01f &&
                  std::abs((1 - clip.y / clip.w) * 0.5f * target.viewport.h -
                  40 * getFramebufferHeight() / float(getWindowHeight())) < 0.01f);
            drawRect(0, 40, getWindowWidth(), getWindowHeight() - 40);
            // Read geometry recorded after the camera scope closed. A stale
            // projection moves the y=40 boundary, even with a full viewport.
            disableDepthTest(); clear(0);
            setColor(1, 0, 0); drawRect(0, 0, getWindowWidth(), 40);
            setColor(0, 1, 0); drawRect(0, 40, getWindowWidth(), getWindowHeight() - 40);
            Pixels pixels;
            const bool captured = grabScreen(pixels) && pixels.isAllocated() &&
                pixels.getWidth() == getFramebufferWidth() && pixels.getHeight() == getFramebufferHeight();
            check("resized screen geometry capture", captured);
            if (captured) {
                const float scale = getFramebufferHeight() / float(getWindowHeight());
                expectPixel(pixels, 8, int(38 * scale), Color(1, 0, 0), "screen geometry above restored boundary");
                expectPixel(pixels, 8, int(42 * scale), Color(0, 1, 0), "screen geometry below restored boundary");
                expectPixel(pixels, pixels.getWidth() - 4, pixels.getHeight() - 4, Color(0, 1, 0),
                            "screen geometry fills resized far corner");
            }
        }
        if (++scenario == 8) { resizeStateCompleted = true; exitApp(); }
        attempts = 0; ready = false;
    }
    inline static bool resizeStateCompleted = false;
    inline static int stackWarnings = 0;
};
int runResizeStateChecks(int samples, bool pixelPerfect) {
    ResizeStateApp::resizeStateCompleted = false;
    ResizeStateApp::stackWarnings = 0;
    resizeWarningLog = getLogger().onLog.listen([](LogEventArgs& e) {
        if (isStackTargetWarning(e)) {
            ++ResizeStateApp::stackWarnings;
            internal::appExitCode() = 1;
        }
    });
    WindowSettings settings;
    settings.setSize(200, 160);
    settings.setHighDpi(true);
    settings.setSampleCount(samples);
    settings.setPixelPerfect(pixelPerfect);
    return tcCoreTest::runDisplayTest<ResizeStateApp>(settings, failures, [] {
        check("native resize scenarios completed", ResizeStateApp::resizeStateCompleted);
        check("native resize scenarios emit no stack or target warnings", ResizeStateApp::stackWarnings == 0);
    });
}
