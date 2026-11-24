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
enum class Mode { Smoke, HiddenDraws, HideInDraw, Resize };
Mode mode = Mode::Smoke;
bool wmClose = false;
bool stencil = false;
int resizes = 0;
int resizeFrame = 0;
sg_total_stats resourcesBefore{};
bool resourcesSampled = false;
bool resizeChecked = false;
constexpr int resizeCount = 24;
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
        getWindow()->setTitle("TrussC Vulkan validation");
        sg_enable_stats();
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
        if (mode == Mode::Resize && shownDraws >= phaseFrames) {
            if (!resourcesSampled) {
                resourcesBefore = sg_query_stats().total;
                resourcesSampled = true;
            }
            if (resizes < resizeCount && draws >= resizeFrame) {
                getWindow()->setSize(160 + ((resizes + 1) % 5) * 19, 120 + ((resizes + 1) % 7) * 13);
                ++resizes;
                resizeFrame = draws + 4;
            }
            if (resizes == resizeCount && draws >= resizeFrame && !resizeChecked) {
                const auto after = sg_query_stats().total;
                auto same = [](const char* name, const sg_total_resource_stats& before,
                               const sg_total_resource_stats& current) {
                    std::printf("vulkanSingleWindow: live %s before=%u after=%u\n", name, before.alive, current.alive);
                    check(name, before.alive == current.alive);
                };
                same("buffers", resourcesBefore.buffers, after.buffers);
                same("images", resourcesBefore.images, after.images);
                same("views", resourcesBefore.views, after.views);
                same("samplers", resourcesBefore.samplers, after.samplers);
                same("shaders", resourcesBefore.shaders, after.shaders);
                same("pipelines", resourcesBefore.pipelines, after.pipelines);
                std::printf("vulkanSingleWindow: %d resizes completed; ready for close\n", resizes);
                resizeChecked = true;
                if (!wmClose) exitApp(failures ? 1 : 0);
#if defined(_WIN32)
                else {
                    // The Windows runner has no X11/probe; use the real WM_CLOSE path.
                    const auto hwnd = (HWND)sapp_win32_get_hwnd();
                    check("WM_CLOSE posted", PostMessageW(hwnd, WM_CLOSE, 0, 0) != 0);
                }
#endif
            }
        } else if (mode != Mode::Resize && shownDraws >= phaseFrames) {
            exitApp(failures ? 1 : 0);
        }
        if (mode == Mode::HiddenDraws && hiddenDraws >= phaseFrames && !getWindow()->isVisible()) {
            getWindow()->setVisible(true);
        }
        if (mode == Mode::HideInDraw && draws > 0 && !getWindow()->isVisible()) {
            if (++hiddenUpdates >= phaseFrames) getWindow()->setVisible(true);
        }
    }
    void draw() override {
        const auto sc = sapp_acquire_swapchain();
        if (sc.invalid) {
            // OUT_OF_DATE recreates the chain and skips this acquisition.
            std::puts("vulkanSingleWindow: acquisition skipped during recreation");
            return;
        }
        if (stencil) {
            // Command-level check on the window attachment; Vulkan pixel readback
            // is deferred. Linux's probe records native reference values;
            // Windows logs the draws and relies on validation diagnostics.
            for (uint8_t ref : {uint8_t(1), uint8_t(73), uint8_t(255)}) {
                setStencilDesc(StencilDesc::shape(ref));
                drawRect(0, 0, 32, 32);
                setStencilDesc(StencilDesc::inside(ref));
                drawRect(0, 0, 32, 32);
#if defined(_WIN32)
                if (draws == 0) std::printf("vulkanSingleWindow: stencil draw ref=%u\n", unsigned(ref));
#endif
            }
            resetStencilDesc();
        }
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
    // Include host lifecycle evidence from startup through teardown.
    setConsoleLogLevel(LogLevel::Verbose);
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--msaa") expectedSamples = 4;
        if (arg == "--hidden-draws") mode = Mode::HiddenDraws;
        if (arg == "--hide-in-draw") mode = Mode::HideInDraw;
        if (arg == "--resize") mode = Mode::Resize;
        if (arg == "--wm-close") wmClose = true;
        if (arg == "--stencil") stencil = true;
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
            if (mode == Mode::Resize) check("24 resize requests completed", resizes == resizeCount);
            errors.disconnect();
            std::puts("vulkanSingleWindow: app checks complete");
        });
#else
    (void)argc;
    (void)argv;
    std::puts("SKIP: vulkanSingleWindow requires SOKOL_VULKAN");
    return 0;
#endif
}
