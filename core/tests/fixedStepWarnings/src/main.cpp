// Deterministic episode checks through both production loop entry points.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>

namespace {
using namespace tc;
int failures = 0;
void check(const char* label, bool ok) {
    std::printf("%s: %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
}

TC_CORE_TEST_MAIN() {
    using namespace tc;
    int mainWarnings = 0;
    int headlessWarnings = 0;
    auto listener = getLogger().onLog.listen([&](LogEventArgs& event) {
        if (event.level != LogLevel::Warning ||
                event.message.find("fell behind") == std::string::npos) return;
        if (event.message.find("Headless loop") != std::string::npos) ++headlessWarnings;
        else ++mainWarnings;
    });
    setMaxUpdateSteps(10);
    setIndependentFps(100, VSYNC);
    auto now = std::chrono::steady_clock::now();
    internal::runIndependentUpdates(now);
    auto mainPass = [&](int milliseconds) {
        now += std::chrono::milliseconds(milliseconds);
        internal::runIndependentUpdates(now);
    };
    App app;
    double accumulator = 0.0;
    auto headlessPass = [&](double elapsed) {
        internal::runHeadlessUpdatePass(app, internal::mainWindowContext(),
                                        accumulator, elapsed, 0.01);
    };

    mainPass(1000);
    check("main: first drop warns", mainWarnings == 1 && headlessWarnings == 0);
    headlessPass(1.0);
    check("headless: first drop warns independently", headlessWarnings == 1);
    mainPass(1000);
    headlessPass(1.0);
    check("both: continued drops are silent", mainWarnings == 1 && headlessWarnings == 1);
    // A clean pass need not run any steps. It must re-arm just its own loop.
    mainPass(0);
    headlessPass(1.0);
    check("main clean pass does not re-arm headless", headlessWarnings == 1);
    mainPass(1000);
    check("main: drop after clean pass warns again", mainWarnings == 2);
    headlessPass(0.0);
    mainPass(1000);
    check("headless clean pass does not re-arm main", mainWarnings == 2);
    headlessPass(1.0);
    check("headless: drop after clean pass warns again", headlessWarnings == 2);
    mainPass(1000);
    headlessPass(1.0);
    check("both: second sustained episode stays silent", mainWarnings == 2 && headlessWarnings == 2);
    return failures ? 1 : 0;
}
