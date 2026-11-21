// Exercise the same smoke sequence with the core's virtual native producer.
#define TCX_INPUTREPLAY_MOCK_WINDOW
#include "../../tests-display/src/main.cpp"
#include "../../../../core/tests/common/tcWindowTest.h"
#include "../../../../core/tests/common/tcInjectedWindowTest.h"

int runInputReplayWindowChecks() {
    for (bool pixels : {false, true}) for (float dpi : {1.0f, 2.0f}) {
        tcCoreTest::WindowFixture fixture(9501);
        auto& window = *fixture.window;
        tcCoreTest::InjectedWindow native(window, 9501);
        native.metadata.dpi_scale = dpi;
        tc::internal::pixelPerfectMode() = pixels;
        { tc::internal::WindowEntryScope seed(window, false, true, false); }
        auto app = tc::internal::makeConstructedAppAt<Smoke>(&tc::internal::WindowRequestAccess::context(window));
        tc::internal::attachCandidate(window, app);
        tc::internal::applyPendingAppForTests(window);
        tc::internal::AppOwnerScope owner(tc::internal::appOwner(*app));
        app->setup();
        while (!app->finished) {
            detail::state().update();
            app->update();
            if (!app->finished) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        tc::internal::WindowRequestAccess::teardown(window);
        finishChecks();
    }
    tc::internal::pixelPerfectMode() = false;
    return failures;
}
