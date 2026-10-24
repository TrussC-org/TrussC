// Deterministic AppKit setFrame / Win32 SetWindowPos synchronous reentry.
#if defined(__linux__) && !defined(__ANDROID__)
#undef SOKOL_GLCORE
#undef SOKOL_GLES3
#define SOKOL_DUMMY_BACKEND
#define SOKOL_GFX_IMPL
#include "sokol/sokol_gfx.h"
#undef SOKOL_GFX_IMPL
#define SOKOL_GL_IMPL
#include "sokol/util/sokol_gl_tc.h"
#undef SOKOL_GL_IMPL
// Keep real desktop Window dispatch while SG/SGL above use the dummy backend.
#define SOKOL_GLCORE
#include <TrussC.h>
#include <cstdio>
using namespace tc;
namespace {
int failures = 0;
int nativeWidth = 0, nativeHeight = 0, nativeSizes = 0;
void check(const char* label, bool ok) {
    std::printf("windowControlReentry: %s: %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
struct DrawApp : App {
    Window* target = nullptr;
    int resized = 0;
    void windowResized(int, int) override { ++resized; }
    void draw() override {
        auto* caller = internal::currentWindowCtx();
        const auto context = sgl_get_context();
        target->setSize(320, 240);
        check("draw caller pointer survives synchronous control", internal::currentWindowCtx() == caller);
        check("draw SGL context survives synchronous control", sgl_get_context().id == context.id);
        sgl_begin_triangles();
        sgl_v2f(0, 0); sgl_v2f(1, 0); sgl_v2f(0, 1);
        sgl_end();
    }
};
bool reenter(Window& target, internal::WindowControl op, int, int, const std::string*, IVec2&) {
    check("size reached native control hook", op == internal::WindowControl::Size);
    if (!target.context().isMain) {
        // The production secondary callback boundary and resize delivery. Its
        // platform-specific event decoder only supplies the dimensions here.
        internal::windowCallback([&] {
            auto* previous = internal::currentWindowCtx();
            internal::restoreWindowContext(&target.context());
            target.syncRootSize(320, 240);
            internal::restoreWindowContext(previous);
        });
    }
    // Always exercise the real main dispatcher too: native controls can pump
    // events for other windows. In the main-target case this is precisely the
    // windowDidResize -> RESIZED path from the reviewer's nested-dummy probe.
    sapp_event ev{};
    ev.type = SAPP_EVENTTYPE_RESIZED;
    internal::_event_cb(&ev);
    return true;
}
int vertices(Window& window) {
    sgl_set_context(window.context().swapchainTarget.resources().context);
    return sgl_num_vertices();
}
}
// Replace only the native secondary endpoint: public dispatch, App ownership
// and PixelSize conversion above it remain production code (no control hook).
namespace trussc {
void Window::setSecondarySize(int width, int height) {
    nativeWidth = width; nativeHeight = height; ++nativeSizes;
}
}
extern "C" bool sapp_mouse_locked() { return false; }
extern "C" int sapp_width() { return 320; }
extern "C" int sapp_height() { return 240; }
extern "C" float sapp_dpi_scale() { return 1; }
int main() {
    getMainThreadId();
    sg_desc gd{}; sg_setup(&gd);
    sgl_desc_t sd{}; sgl_setup(&sd);
    {
        internal::createMainWindow(WindowSettings{});
        Window* main = internal::mainWindow();
        internal::WindowRequestAccess::openMain(*main);
        Window secondary;
        secondary.native_ = &secondary; // open handle; hook replaces only native control
        auto mainApp = std::make_shared<DrawApp>();
        auto secondaryApp = std::make_shared<DrawApp>();
        check("main App attached", internal::attachMainApp(mainApp));
        secondary.setApp(secondaryApp);
        internal::WindowRequestAccess::applyPendingApp(secondary);
        check("secondary App attached", secondary.getApp() == secondaryApp);
        sgl_context_desc_t cd{};
        main->context().swapchainTarget.resources().context = sgl_make_context(&cd);
        secondary.context().swapchainTarget.resources().context = sgl_make_context(&cd);
        internal::windowControlHookForTests() = reenter;

        internal::restoreWindowContext(&secondary.context());
        const int mainResizes = mainApp->resized;
        secondaryApp->target = main;
        secondaryApp->draw();
        check("main App received nested windowResized", mainApp->resized > mainResizes);
        check("secondary subsequent vertices stay secondary", vertices(secondary) == 3);
        check("main received no secondary vertices", vertices(*main) == 0);
        // Reset CPU recordings between the two independent directions.
        sgl_tc_context_reset(main->context().swapchainTarget.resources().context);
        sgl_tc_context_reset(secondary.context().swapchainTarget.resources().context);

        internal::restoreWindowContext(&main->context());
        const int secondaryResizes = secondaryApp->resized;
        const int reverseMainResizes = mainApp->resized;
        mainApp->target = &secondary;
        mainApp->draw();
        check("secondary App received nested windowResized", secondaryApp->resized > secondaryResizes);
        check("main dispatcher ran inside secondary control", mainApp->resized > reverseMainResizes);
        check("main subsequent vertices stay main", vertices(*main) == 3);
        check("secondary received no main vertices", vertices(secondary) == 0);

        internal::windowControlHookForTests() = nullptr;
        internal::restoreWindowContext(&secondary.context());
        internal::pixelPerfectMode() = true;
        secondary.context().dpiScale = 1.1f;
        int calls = nativeSizes;
        setWindowSize(550, 330);
        check("public fractional DPI size reaches native logical 500,300",
              nativeSizes == ++calls && nativeWidth == 500 && nativeHeight == 300);
        // An attached App targets its own secondary even from the main caller.
        internal::restoreWindowContext(&main->context());
        secondaryApp->setSize(550, 330);
        check("attached App fractional DPI size reaches native logical 500,300",
              nativeSizes == ++calls && nativeWidth == 500 && nativeHeight == 300);
        bool publicSweep = true, appSweep = true;
        for (float scale : {1.0f, 1.1f, 1.25f, 1.5f, 1.75f, 2.0f, 2.25f, 3.0f}) {
            secondary.context().dpiScale = scale;
            for (int pixels = 1; pixels <= 4096; ++pixels) {
                // Exact next/window public formula; zero logical sizes are
                // rejected before the native endpoint, as in the base.
                const int expected = static_cast<int>(pixels / scale);
                const int expectedCalls = expected > 0 ? 1 : 0;
                internal::restoreWindowContext(&secondary.context());
                calls = nativeSizes;
                setWindowSize(pixels, pixels);
                publicSweep &= nativeSizes == calls + expectedCalls &&
                    (!expectedCalls || (nativeWidth == expected && nativeHeight == expected));
                internal::restoreWindowContext(&main->context());
                calls = nativeSizes;
                // The base App first casts its float extents to int, then
                // performs the same pixel-to-logical float division.
                secondaryApp->setSize(float(pixels) + 0.5f, float(pixels) + 0.5f);
                appSweep &= nativeSizes == calls + expectedCalls &&
                    (!expectedCalls || (nativeWidth == expected && nativeHeight == expected));
            }
        }
        check("public size sweep matches base float division", publicSweep);
        check("attached App size sweep matches base casts and float division", appSweep);
        internal::restoreWindowContext(&secondary.context());
        calls = nativeSizes;
        secondary.context().dpiScale = 1.0f;
        setWindowSize((std::numeric_limits<int>::max)(), 330); // rounds to 2^31
        secondary.context().dpiScale = std::numeric_limits<float>::min();
        setWindowSize(550, 330); // non-finite quotient
        check("out-of-range float quotients never reach native size", nativeSizes == calls);
        internal::pixelPerfectMode() = false;
        internal::restoreWindowContext(nullptr);
        secondary.native_ = nullptr;
        internal::WindowRequestAccess::endApp(secondary);
        internal::destroyMainWindow();
    }
    sgl_shutdown(); sg_shutdown();
    return failures ? 1 : 0;
}
#else
#include <cstdio>
int main() { std::puts("SKIP: Linux dummy SG/SGL reentry fixture"); }
#endif
