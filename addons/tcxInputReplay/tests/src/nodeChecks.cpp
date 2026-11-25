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
    // Load v0.8.2 JSONL v1 payloads through the v0.9 input types.
    using namespace tcx::inputreplay;
    using Json = detail::Json;
    std::vector<MouseButton> buttons;
    auto pressed = window.events().mousePressed.listen([&](const MouseEventArgs& e) { buttons.push_back(e.button); });
    Json record = {{"type", "mousePressed"}, {"ms", 0}, {"frame", 0}, {"window", "main"},
        {"x", 20}, {"y", 100}, {"mods", 0}, {"injected", false}, {"tag", "legacy"}};
    for (int button = 0; button < 3; ++button) {
        record["button"] = button;
        const auto event = detail::input(detail::validateRecord(record), {100, 50}, {100, 50}, ScaleMode::None);
        inputReplayTest::check(window.injectInput(event), "inject legacy numeric button");
        window.injectInput(InputEvent::mouseReleased({20, 100}, static_cast<MouseButton>(button)));
    }
    inputReplayTest::check(buttons == std::vector<MouseButton>{MouseButton::Left, MouseButton::Right, MouseButton::Middle},
        "JSONL v1 numeric buttons preserve their v0.9 enum values");
    std::vector<fs::path> paths;
    auto dropped = window.events().filesDropped.listen([&](const DragDropEventArgs& e) { paths = e.files; });
    record["type"] = "filesDropped";
    record["files"] = Json::array({"日本語/one.txt", "a\\b.txt"});
    window.injectInput(detail::input(detail::validateRecord(record), {100, 50}, {100, 50}, ScaleMode::None));
    inputReplayTest::check(paths.size() == 2 && pathToUtf8(paths[0]) == "日本語/one.txt" && pathToUtf8(paths[1]) == "a\\b.txt",
        "JSONL UTF-8 paths reach drop events unchanged");
    Json touch = {{"type", "touchReleased"}, {"ms", 0}, {"frame", 0}, {"window", "main"},
        {"injected", false}, {"tag", "legacy"}, {"canceled", true},
        {"touches", Json::array({
            {{"id", -7}, {"x", 10}, {"y", 20}, {"pressure", 0.25}, {"changed", true}},
            {{"id", 9}, {"x", 30}, {"y", 40}, {"pressure", 0.75}, {"changed", false}}})}};
    Json delivered;
    auto released = window.events().touchReleased.listen([&](const TouchEventArgs& e) { delivered = detail::touch(e); });
    window.injectInput(detail::input(detail::validateRecord(touch), {100, 50}, {200, 100}, ScaleMode::Stretch));
    touch["touches"][0]["x"] = 20;
    touch["touches"][0]["y"] = 40;
    touch["touches"][1]["x"] = 60;
    touch["touches"][1]["y"] = 80;
    inputReplayTest::check(delivered["touches"] == touch["touches"] && delivered["canceled"] == true,
        "touch vector replay preserves IDs, pressure, changes, cancellation, and scaled positions");
    std::printf("Nodes: %d checks, %d failures\n", inputReplayTest::checks, inputReplayTest::failures);
    return inputReplayTest::failures;
}
