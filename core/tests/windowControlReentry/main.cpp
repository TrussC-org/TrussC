// Linux dummy SG/SGL fixture modeling the synchronous RESIZED events
// produced by AppKit setFrame / Win32 SetWindowPos.
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
double nativeWidth = 0, nativeHeight = 0;
int nativeSizes = 0;
float nativeDpi = 1;
int framebufferWidth = 320, framebufferHeight = 240;
void check(const char* label, bool ok) {
    std::printf("windowControlReentry: %s: %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
struct DrawApp : App {
    Window* target = nullptr;
    int resized = 0, resizedWidth = 0, resizedHeight = 0;
    Vec2 pointer{}, delta{}, scroll{};
    int moves = 0, drags = 0, scrolls = 0;
    void mouseMoved(const MouseMoveEventArgs& e) override { pointer = e.pos; delta = e.delta; ++moves; }
    void mouseDragged(const MouseDragEventArgs& e) override { pointer = e.pos; delta = e.delta; ++drags; }
    void mouseScrolled(const ScrollEventArgs& e) override { pointer = e.pos; scroll = e.scroll; ++scrolls; }
    void windowResized(int w, int h) override { ++resized; resizedWidth = w; resizedHeight = h; }
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
namespace internal { void dispatchSecondaryWindowEventForTests(const sapp_event*, Window&); }
void Window::setSecondarySize(double width, double height, bool) {
    nativeWidth = width; nativeHeight = height; ++nativeSizes;
}
}
extern "C" bool sapp_mouse_locked() { return false; }
extern "C" int sapp_width() { return framebufferWidth; }
extern "C" int sapp_height() { return framebufferHeight; }
extern "C" float sapp_dpi_scale() { return nativeDpi; }
extern "C" float sapp_window_dpi_scale(sapp_window) { return nativeDpi; }
extern "C" bool sapp_isvalid() { return true; }
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
        // Production secondary size delivery, compared with main's real RESIZED
        // dispatcher and App-swap seed, at deterministic non-1 DPI.
        framebufferWidth = 1001; framebufferHeight = 667;
        for (bool enabled : {false, true}) {
            internal::pixelPerfectMode() = enabled;
            for (float dpi : {1.5f, 2.0f}) {
                nativeDpi = secondary.context().dpiScale = dpi;
                const int expectedWidth = enabled ? 1001 : int(1001 / dpi);
                const int expectedHeight = enabled ? 667 : int(667 / dpi);
                internal::restoreWindowContext(&secondary.context());
                internal::syncSecondaryAppSize(secondary, framebufferWidth, framebufferHeight);
                internal::restoreWindowContext(&main->context());
                sapp_event resize{}; resize.type = SAPP_EVENTTYPE_RESIZED;
                internal::_event_cb(&resize);
                check("secondary App size and resize arguments match main at non-1 DPI",
                    secondaryApp->getWidth() == expectedWidth && secondaryApp->getHeight() == expectedHeight &&
                    secondaryApp->resizedWidth == expectedWidth && secondaryApp->resizedHeight == expectedHeight &&
                    mainApp->getWidth() == expectedWidth && mainApp->getHeight() == expectedHeight &&
                    mainApp->resizedWidth == expectedWidth && mainApp->resizedHeight == expectedHeight);
                check("main Window getters stay logical", main->getWidth() == int(1001 / dpi) && main->getHeight() == int(667 / dpi));
                auto replacement = std::make_shared<DrawApp>();
                secondary.setApp(replacement);
                internal::WindowRequestAccess::applyPendingApp(secondary);
                internal::restoreWindowContext(&secondary.context());
                internal::syncSecondaryAppSize(secondary, framebufferWidth, framebufferHeight);
                check("secondary replacement seeded before first setup",
                    replacement->getWidth() == expectedWidth && replacement->getHeight() == expectedHeight &&
                    replacement->resizedWidth == expectedWidth && replacement->resizedHeight == expectedHeight);
                secondaryApp = replacement;
                auto mainReplacement = std::make_shared<DrawApp>();
                main->setApp(mainReplacement);
                internal::WindowRequestAccess::applyPendingApp(*main);
                internal::WindowRequestAccess::seedMainAppSize(*main);
                check("main replacement seed matches secondary",
                    mainReplacement->getWidth() == replacement->getWidth() && mainReplacement->getHeight() == replacement->getHeight() &&
                    mainReplacement->resizedWidth == replacement->resizedWidth && mainReplacement->resizedHeight == replacement->resizedHeight);
                mainApp = mainReplacement;
            }
        }
        // Real Linux secondary decoder with a recording native DPI endpoint;
        // compare against main's unchanged production dispatcher at 2x.
        nativeDpi = 2.0f;
        for (bool enabled : {false, true}) {
            internal::pixelPerfectMode() = enabled;
            auto deliver = [&](Window& window, const sapp_event& event) {
                internal::restoreWindowContext(&window.context());
                if (window.context().isMain) internal::_event_cb(&event);
                else internal::dispatchSecondaryWindowEventForTests(&event, window);
            };
            sapp_event ev{};
            ev.type = SAPP_EVENTTYPE_MOUSE_MOVE;
            ev.mouse_x = 200; ev.mouse_y = 100;
            ev.mouse_dx = 200; ev.mouse_dy = 100;
            main->context().mouseX = main->context().mouseY = 0;
            secondary.context().mouseX = secondary.context().mouseY = 0;
            deliver(*main, ev); deliver(secondary, ev);
            const Vec2 expected = enabled ? Vec2(200, 100) : Vec2(100, 50);
            auto same = [](Vec2 a, Vec2 b) { return a.x == b.x && a.y == b.y; };
            check("2x pointer position and delta match main in both modes",
                mainApp->moves > 0 && secondaryApp->moves > 0 &&
                same(mainApp->pointer, expected) && same(secondaryApp->pointer, expected) &&
                same(mainApp->delta, expected) && same(secondaryApp->delta, expected));
            check("secondary cursor getters use App pointer units", same(getMousePos(), expected) &&
                same(Vec2(secondaryApp->getMouseX(), secondaryApp->getMouseY()), expected));
            ev.type = SAPP_EVENTTYPE_MOUSE_DOWN; ev.mouse_button = SAPP_MOUSEBUTTON_LEFT;
            deliver(*main, ev); deliver(secondary, ev);
            ev.type = SAPP_EVENTTYPE_MOUSE_MOVE; ev.modifiers = SAPP_MODIFIER_LMB;
            ev.mouse_x = 220; ev.mouse_y = 110; ev.mouse_dx = 20; ev.mouse_dy = 10;
            deliver(*main, ev); deliver(secondary, ev);
            check("2x drag position and delta match main in both modes",
                mainApp->drags > 0 && secondaryApp->drags > 0 &&
                same(mainApp->pointer, expected * 1.1f) && same(secondaryApp->pointer, mainApp->pointer) &&
                same(mainApp->delta, enabled ? Vec2(20, 10) : Vec2(10, 5)) && same(secondaryApp->delta, mainApp->delta));
            ev.type = SAPP_EVENTTYPE_MOUSE_SCROLL; ev.scroll_x = 3; ev.scroll_y = -4;
            // Main uses the last pointer position, not the scroll event's x/y.
            ev.mouse_x = 999; ev.mouse_y = 999;
            deliver(*main, ev); deliver(secondary, ev);
            check("2x scroll position matches main and wheel amounts stay unscaled",
                mainApp->scrolls > 0 && secondaryApp->scrolls > 0 &&
                same(mainApp->pointer, expected * 1.1f) && same(secondaryApp->pointer, mainApp->pointer) &&
                same(mainApp->scroll, Vec2(3, -4)) && same(secondaryApp->scroll, mainApp->scroll));
            ev.type = SAPP_EVENTTYPE_MOUSE_UP; ev.mouse_x = 220; ev.mouse_y = 110;
            deliver(*main, ev); deliver(secondary, ev);
            ev.type = SAPP_EVENTTYPE_MOUSE_ENTER; ev.mouse_x = 200; ev.mouse_y = 100;
            deliver(secondary, ev);
            check("2x secondary entry updates hover coordinates in App units", same(getMousePos(), expected));
            ev.type = SAPP_EVENTTYPE_MOUSE_LEAVE; deliver(secondary, ev);
            check("secondary leave retains offscreen hover sentinel", same(getMousePos(), Vec2(-1, -1)));
        }
        internal::pixelPerfectMode() = false;
        for (float invalid : {0.0f, -1.0f, std::numeric_limits<float>::infinity(),
                              std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::denorm_min()})
            check("invalid native pointer DPI has finite unit fallback", internal::appPointerScale(invalid) == 1.0f);
        nativeDpi = 1;
        internal::restoreWindowContext(&secondary.context());
        internal::pixelPerfectMode() = true;
        secondary.context().dpiScale = 1.1f;
        int calls = nativeSizes;
        setWindowSize(550, 330);
        check("public fractional DPI size reaches native fractional logical size",
              nativeSizes == ++calls && nativeWidth == double(550) / 1.1f && nativeHeight == double(330) / 1.1f);
        // An attached App targets its own secondary even from the main caller.
        internal::restoreWindowContext(&main->context());
        secondaryApp->setSize(550, 330);
        check("attached App fractional DPI size reaches native fractional logical size",
              nativeSizes == ++calls && nativeWidth == double(550) / 1.1f && nativeHeight == double(330) / 1.1f);
        bool publicSweep = true, appSweep = true;
        for (float scale : {1.0f, 1.1f, 1.25f, 1.5f, 1.75f, 2.0f, 2.25f, 3.0f}) {
            secondary.context().dpiScale = scale;
            for (int pixels = 1; pixels <= 4096; ++pixels) {
                // Pixel requests keep the exact double quotient, including
                // sub-point extents, until native pixel conversion.
                const double expected = double(pixels) / scale;
                const int expectedCalls = 1;
                internal::restoreWindowContext(&secondary.context());
                calls = nativeSizes;
                setWindowSize(pixels, pixels);
                publicSweep &= nativeSizes == calls + expectedCalls &&
                    (!expectedCalls || (nativeWidth == expected && nativeHeight == expected));
                internal::restoreWindowContext(&main->context());
                calls = nativeSizes;
                // The base App first casts its float extents to int, then
                // keeps the fractional pixel-to-logical double division.
                secondaryApp->setSize(float(pixels) + 0.5f, float(pixels) + 0.5f);
                appSweep &= nativeSizes == calls + expectedCalls &&
                    (!expectedCalls || (nativeWidth == expected && nativeHeight == expected));
            }
        }
        check("public size sweep keeps fractional logical size", publicSweep);
        check("attached App size sweep keeps integer pixel casts and fractional logical size", appSweep);
        internal::restoreWindowContext(&secondary.context());
        calls = nativeSizes;
        secondary.context().dpiScale = 0.5f;
        setWindowSize((std::numeric_limits<int>::max)(), 330); // logical quotient exceeds int range
        secondary.context().dpiScale = std::numeric_limits<float>::min();
        setWindowSize(550, 330); // logical quotient exceeds int range
        check("out-of-range logical quotients never reach native size", nativeSizes == calls);
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
int main() { std::puts("SKIP: Linux dummy SG/SGL reentry fixture; macOS/Windows are covered by the device probe"); }
#endif
