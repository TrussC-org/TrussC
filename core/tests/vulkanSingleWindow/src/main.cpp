#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <cstdio>

using namespace tc;
namespace {
#if defined(SOKOL_VULKAN)
int failures = 0;
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
        exitApp(failures ? 1 : 0);
    }
};
#endif
}
TC_CORE_TEST_MAIN() {
#if defined(SOKOL_VULKAN)
    return tcCoreTest::runDisplayTest<GuardApp>(WindowSettings{}.setSize(160, 120), failures);
#else
    std::puts("SKIP: vulkanSingleWindow requires SOKOL_VULKAN");
    return 0;
#endif
}
