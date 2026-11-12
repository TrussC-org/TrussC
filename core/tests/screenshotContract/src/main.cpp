#include "../../common/tcWindowTest.h"
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
using namespace tc;
namespace {
int failures = 0;
struct ScreenshotOwner : App {
    EventListener completion;
    explicit ScreenshotOwner(int& calls) {
        completion = onScreenshotSave().listen([&calls](ScreenshotSaveEventArgs&) { ++calls; });
    }
};
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
        tcCoreTest::WindowFixture closing;
        internal::submitScreenshot(&trussc::internal::WindowRequestAccess::context(*closing.window).asyncScreenshots, "closed.png");
    }
    internal::deliverResourceLoadBatch(internal::takeResourceLoadBatch());
    if (completions != 1) ++failures;
    int retiredCalls = 0;
    std::weak_ptr<App> lifetime;
    {
        tcCoreTest::WindowFixture closing;
        closing.window->setApp<ScreenshotOwner>(retiredCalls);
        internal::applyPendingAppForTests(*closing.window);
        auto app = closing.window->getApp();
        lifetime = app;
        internal::AppOwnerScope owner(internal::appOwner(*app));
        internal::submitScreenshot(&trussc::internal::WindowRequestAccess::context(*closing.window).asyncScreenshots, "owned-closed.png");
    }
    internal::deliverResourceLoadBatch(internal::takeResourceLoadBatch());
    if (!lifetime.expired() || retiredCalls != 0 || completions != 1) ++failures;
    std::printf("retired App receives no completion and is not retained: %s\n", failures ? "FAIL" : "PASS");
    std::printf("window close completes with failure: %s\n", failures ? "FAIL" : "PASS");
    int endedRunErrors = 0;
    auto logs = getLogger().onLog.listen([&](LogEventArgs& args) {
        if (args.level == LogLevel::Error &&
            args.message.find("ended-run.png") != std::string::npos &&
            args.message.find("Window closed before screenshot capture; app run already ended") != std::string::npos)
            ++endedRunErrors;
    });
    for (int pass = 0; pass < 2; ++pass) {
        internal::appRunToken() = std::make_shared<std::atomic<bool>>(true);
        {
            tcCoreTest::WindowFixture closing;
            internal::submitScreenshot(&internal::WindowRequestAccess::context(*closing.window).asyncScreenshots, "ended-run.png");
            *internal::appRunToken() = false;
        }
        if (endedRunErrors != pass + 1) ++failures; // synchronous, before mailbox drain
        internal::deliverResourceLoadBatch(internal::takeResourceLoadBatch());
    }
    const bool endedRunReported = endedRunErrors == 2 && completions == 1;
    if (!endedRunReported) ++failures;
    std::printf("ended run reports every canceled screenshot without completion: %s\n", endedRunReported ? "PASS" : "FAIL");
    return failures ? 1 : 0;
}
