// Cross-version native regression: inject the earlier part of a long setup
// into the measured delta, while timer creation is later on the same timeline.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <cstdio>
using namespace tc;
namespace {
int failures = 0;
bool completed = false;
std::shared_ptr<Window> secondary;
template<class Context> void seedEarlierBaseline(Context& ctx) {
    if constexpr (requires { ctx.lastUpdateCallTime; })
        ctx.lastUpdateCallTime -= std::chrono::seconds(3);
    else ctx.mainUpdateCallTime -= std::chrono::seconds(3);
}
struct TimerApp : App {
    int updates = 0, fired = 0;
    void update() override {
        if (++updates == 1) {
            callAfter(2, [this] { ++fired; });
            // Model a creation time after the injected earlier delta. A future
            // timestamp makes this deterministic even if the runner is preempted.
            // The legacy tick has no update-time sample and ignores this seam.
            timers_.back().created = (std::chrono::steady_clock::time_point::max)();
            seedEarlierBaseline(internal::currentWindowContext());
        } else {
            const bool ok = fired == 0;
            std::printf("native secondary excludes pre-creation delta: %s\n", ok ? "PASS" : "FAIL");
            std::fflush(stdout);
            if (!ok) ++failures;
            completed = true;
            exitApp();
        }
    }
};
struct MainApp : App {
    void setup() override {
        WindowSettings settings; settings.setSize(64, 64);
        secondary = createWindow(settings);
        if (!secondary) { ++failures; exitApp(1); return; }
        secondary->setApp(std::make_shared<TimerApp>());
    }
    void cleanup() override { secondary.reset(); }
};
}
TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc <= 1 || std::string(argv[1]) != "--display") {
        std::puts("SKIP: secondaryTimerCreation requires --display");
        return 0;
    }
    if (!internal::supportsSecondaryWindows()) { std::puts("SKIP: secondaryTimerCreation needs secondary windows"); return 0; }
    WindowSettings settings; settings.setSize(128, 96);
    return tcCoreTest::runDisplayTest<MainApp>(settings, failures, [] {
        if (!completed) ++failures;
    });
}
