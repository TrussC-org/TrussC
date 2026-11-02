#include <TrussC.h>
#include "../../common/tcDisplayTest.h"
#include <cstdio>
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
#include <X11/Xlib.h>
#if defined(SOKOL_GLES3)
#include <GLES3/gl3.h>
#else
#include <GL/gl.h>
#endif
namespace resizeFirstFrame {
inline int failures = 0;
inline bool completed = false;
inline std::shared_ptr<tc::Window> secondary;
inline void check(const char* name, bool ok) {
    std::printf("resize-first-frame: %s: %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
struct Secondary : tc::App {
    tc::EventListener after;
    void setup() override {
        after = tc::events().afterFrame.listen([] {
            const auto& ctx = trussc::internal::WindowRequestAccess::context(*secondary);
            const auto& target = ctx.swapchainTarget;
            check("first pass matches configured metadata", ctx.fbWidth == 100 && ctx.fbHeight == 60 &&
                target.binding.swapchain.width == 100 && target.binding.swapchain.height == 60);
            tc::Pixels pixels;
            const bool captured = tc::grabScreen(pixels);
            check("frame 1 capture has configured size", captured && pixels.getWidth() == 100 && pixels.getHeight() == 60);
            bool solid = captured;
            int red = 0;
            if (captured) for (int y = 0; y < pixels.getHeight(); ++y)
                for (int x = 0; x < pixels.getWidth(); ++x) {
                    const auto c = pixels.getColor(x, y);
                    const bool matches = c.r == 1 && c.g == 0 && c.b == 0 && c.a == 1;
                    solid &= matches;
                    red += matches;
                }
            std::printf("resize-first-frame: frame colour pixels %d/6000\n", red);
            check("frame 1 colour covers every configured pixel", solid);
            completed = true;
            tc::exitApp(failures ? 1 : 0);
        });
    }
    void draw() override {
        check("first secondary frame", tc::getDrawCount() == 0);
        const auto& ctx = trussc::internal::WindowRequestAccess::context(*getWindow());
        const auto& target = ctx.swapchainTarget;
        check("target and viewport absorb configure before begin-pass",
            ctx.fbWidth == 100 && ctx.fbHeight == 60 &&
            target.width == 100 && target.height == 60 && target.viewport.w == 100 && target.viewport.h == 60);
    }
};
struct Main : tc::App {
    void setup() override {
        tc::WindowSettings settings; settings.width = 40; settings.height = 30;
        settings.highDpi = false;
        secondary = tc::createWindow(settings);
        check("secondary created", bool(secondary));
        if (!secondary) { tc::exitApp(1); return; }
        secondary->setClearColor(tc::Color(1, 0, 0, 1));
        secondary->setApp<Secondary>();
        const auto handle = tc::internal::WindowRequestAccess::binding(*secondary).handle;
        auto* display = (Display*)sapp_x11_get_display();
        const auto native = (::Window)(uintptr_t)sapp_window_x11_get_window(handle);
        // Emulate a WM clamp between creation and the first native tick. Also
        // inject its ConfigureNotify; the real drawable and notification agree.
        XResizeWindow(display, native, 100, 60);
        XEvent event{}; event.xconfigure.type = ConfigureNotify;
        event.xconfigure.display = display; event.xconfigure.event = native;
        event.xconfigure.window = native; event.xconfigure.width = 100; event.xconfigure.height = 60;
        XSendEvent(display, native, False, StructureNotifyMask, &event);
        XSync(display, False);
    }
    void update() override { if (tc::getUpdateCount() > 120) { check("secondary first frame completed", completed); tc::exitApp(1); } }
    void cleanup() override { secondary.reset(); }
};
inline int run() {
    tc::WindowSettings settings; settings.width = settings.height = 128;
    return tcCoreTest::runDisplayTest<Main>(settings, failures, [] { check("completed", completed); });
}
}
int runResizeFirstFrameTest() { return resizeFirstFrame::run(); }
#else
int runResizeFirstFrameTest() { std::puts("SKIP: ConfigureNotify fixture requires Linux X11"); return 0; }
#endif
