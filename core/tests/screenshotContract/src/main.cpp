#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
using namespace tc;
namespace {
int failures = 0;
}
TC_CORE_TEST_MAIN() {
    getMainThreadId();
    internal::appRunToken() = std::make_shared<std::atomic<bool>>(true);
    int completions = 0;
    auto listener = onScreenshotSave().listen([&](ScreenshotSaveEventArgs& args) {
        ++completions;
        if (args.result || !isMainThread()) ++failures;
    });
    {
        internal::ScreenshotQueue closing;
        internal::submitScreenshot(&closing, "closed.png");
    }
    internal::deliverResourceLoadBatch(internal::takeResourceLoadBatch());
    if (completions != 1) ++failures;
    std::printf("window close completes with failure: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
