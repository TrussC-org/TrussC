#include "nodeChecks.h"
#include "../../../../core/tests/common/tcWindowTest.h"
#include "../../../../core/tests/common/tcInjectedWindowTest.h"
int runInputReplayNodeChecks() {
    using namespace tc;
    tcCoreTest::WindowFixture fixture(9501);
    auto& window = *fixture.window;
    tcCoreTest::InjectedWindow native(window, 9501);
    { internal::WindowEntryScope seed(window, false, true, false); }
    auto app = internal::makeConstructedAppAt<inputReplayTest::Smoke>(&internal::WindowRequestAccess::context(window));
    internal::attachCandidate(window, app);
    internal::applyPendingAppForTests(window);
    internal::AppOwnerScope owner(internal::appOwner(*app));
    internal::setupNodeOnce(*app);
    app->runChecks();
    std::printf("Nodes: %d checks, %d failures\n", inputReplayTest::checks, inputReplayTest::failures);
    return inputReplayTest::failures;
}
