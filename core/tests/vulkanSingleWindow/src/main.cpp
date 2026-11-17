#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <cstdio>
#if defined(SOKOL_VULKAN)
#include <chrono>
#include <cstdlib>
#include <thread>
#endif

using namespace tc;
namespace {
#if defined(SOKOL_VULKAN)
int failures = 0;
int draws = 0;
int expectedSamples = 1;
enum class Mode { Smoke, HiddenDraws, HideInDraw };
Mode mode = Mode::Smoke;
constexpr int phaseFrames = 16;
int hiddenDraws = 0;
int shownDraws = 0;
int hiddenUpdates = 0;
EventListener errors;
void check(const char* label, bool okay) {
    std::printf("vulkanSingleWindow: %s: %s\n", label, okay ? "PASS" : "FAIL");
    if (!okay) ++failures;
}
struct GuardApp : App {
    void setup() override {
        const auto before = internal::openWindows();
        check("launch window exists", before.size() == 1 && getWindow());
        int warnings = 0;
        auto listener = getLogger().onLog.listen([&](LogEventArgs& e) {
            if (e.level == LogLevel::Warning && e.message ==
                    "[Window] Vulkan backend supports one window for now; this window was not created")
                ++warnings;
        });
        check("second window refused", !createWindow());
        check("hidden second window refused", !createWindow(WindowSettings{}.setVisible(false)));
        check("every attempt warns", warnings == 2);
        check("native window set unchanged", internal::openWindows() == before);
        check("launch window still open", getWindow() && getWindow()->isOpen());
        check("Vulkan backend active", sg_query_backend() == SG_BACKEND_VULKAN);
        const auto env = sapp_get_environment();
        check("native Vulkan device and queue", env.vulkan.instance && env.vulkan.physical_device &&
              env.vulkan.device && env.vulkan.queue);
        getWindow()->setClearColor(Color(0.25f, 0.5f, 0.75f, 1.0f));
        errors = getLogger().onLog.listen([](LogEventArgs& event) {
            if (event.level == LogLevel::Error || event.level == LogLevel::Fatal) ++failures;
        });
        if (mode == Mode::HiddenDraws) {
            getWindow()->setDrawWhileHidden(true);
            getWindow()->setVisible(false);
            check("window hidden before drawing", !getWindow()->isVisible());
        }
    }
    void update() override {
        // Exit on the following tick, after every counted draw was submitted/presented.
        if (shownDraws >= phaseFrames) exitApp(failures ? 1 : 0);
        if (mode == Mode::HiddenDraws && hiddenDraws >= phaseFrames && !getWindow()->isVisible()) {
            getWindow()->setVisible(true);
        }
        if (mode == Mode::HideInDraw && draws > 0 && !getWindow()->isVisible()) {
            if (++hiddenUpdates >= phaseFrames) getWindow()->setVisible(true);
        }
    }
    void draw() override {
        const auto sc = sapp_acquire_swapchain();
        check("valid acquired Vulkan image", !sc.invalid && sc.vulkan.render_image && sc.vulkan.render_view);
        check("depth and presentation semaphores", sc.vulkan.depth_stencil_image &&
              sc.vulkan.depth_stencil_view && sc.vulkan.render_finished_semaphore &&
              sc.vulkan.present_complete_semaphore);
        check("sample count and resolve image", sc.sample_count == expectedSamples &&
              (expectedSamples == 1 || (sc.vulkan.resolve_image && sc.vulkan.resolve_view)));
        const auto again = sapp_acquire_swapchain();
        check("same image on repeated acquisition", again.vulkan.render_image == sc.vulkan.render_image &&
              again.vulkan.present_complete_semaphore == sc.vulkan.present_complete_semaphore);
        if (mode == Mode::HideInDraw && draws == 0) {
            // Keep the default hidden-drawing policy; update will show the window later.
            getWindow()->setVisible(false);
            check("window hidden inside draw", !getWindow()->isVisible());
        } else if (!getWindow()->isVisible()) {
            ++hiddenDraws;
        } else {
            ++shownDraws;
        }
        ++draws;
    }
};
#endif
}
TC_CORE_TEST_MAIN(int argc, char** argv) {
#if defined(SOKOL_VULKAN)
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--msaa") expectedSamples = 4;
        if (arg == "--hidden-draws") mode = Mode::HiddenDraws;
        if (arg == "--hide-in-draw") mode = Mode::HideInDraw;
    }
    // A GPU wait can block the main thread, so the timeout runs independently.
    std::thread([] {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        std::fputs("vulkanSingleWindow: watchdog timeout\n", stderr);
        std::_Exit(124);
    }).detach();
    return tcCoreTest::runDisplayTest<GuardApp>(WindowSettings{}.setSize(160, 120)
        .setSampleCount(expectedSamples), failures, [] {
            check("16 visible frames completed without errors", shownDraws >= phaseFrames && failures == 0);
            if (mode == Mode::HiddenDraws) check("16 hidden frames completed", hiddenDraws >= phaseFrames);
            if (mode == Mode::HideInDraw) {
                check("16 hidden updates completed", hiddenUpdates >= phaseFrames);
                check("default policy suppressed hidden draws", hiddenDraws == 0);
            }
            errors.disconnect();
        });
#else
    (void)argc;
    (void)argv;
    std::puts("SKIP: vulkanSingleWindow requires SOKOL_VULKAN");
    return 0;
#endif
}
