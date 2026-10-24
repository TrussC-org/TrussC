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
#include <TrussC.h>
#include <cstdio>
using namespace tc;
namespace {
int failures = 0;
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
