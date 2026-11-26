#include "nodeChecks.h"
#include "../../../../core/tests/common/tcWindowTest.h"
#include "../../../../core/tests/common/tcInjectedWindowTest.h"
int runInputReplayNodeChecks() {
    using namespace tc;
    internal::beginRuntimeRun();
    internal::beginFrameworkLifetime();
    {
        // A plain Node root (no App, no window): the Toolbar logs an Error and stays inactive.
        auto root = std::make_shared<Node>();
        auto bar = std::make_shared<tcx::inputreplay::Toolbar>(KEY_F9);
        root->addChild(bar);
        internal::setupNodeOnce(*bar);
        bar->update();
        bar->draw();
        const auto path = std::filesystem::temp_directory_path() / "tc-inputreplay-no-app.jsonl";
        inputReplayTest::check(bar->getChildren().empty() && !bar->startRecording(path) && !bar->isRecording() &&
            !bar->play(path) && !bar->isPlaying() && !bar->isPointerOver() && !std::filesystem::exists(path),
            "Toolbar under a plain Node root stays inactive");
    }
    tcCoreTest::WindowFixture fixture(9501);
    internal::retainRuntimeWindow(fixture.window);
    auto& window = *fixture.window;
    tcCoreTest::InjectedWindow native(window, 9501);
    auto& context = internal::WindowRequestAccess::context(window);
    context.fbWidth = 1280;
    context.fbHeight = 960;
    context.dpiScale = 2;
    const bool pixelPerfect = internal::pixelPerfectMode();
    internal::pixelPerfectMode() = true;
    auto size = tcx::inputreplay::detail::windowSize(window);
    inputReplayTest::check(size.width == 1280 && size.height == 960, "DPI-2 pixelPerfect size uses framebuffer pixels");
    internal::pixelPerfectMode() = false;
    size = tcx::inputreplay::detail::windowSize(window);
    inputReplayTest::check(size.width == 640 && size.height == 480, "DPI-2 logical size uses points");
    internal::pixelPerfectMode() = pixelPerfect;
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
