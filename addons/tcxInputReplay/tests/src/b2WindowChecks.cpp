#include <tcxInputReplay.h>
#include "../../../../core/tests/common/tcWindowTest.h"
#include "../../../../core/tests/common/tcInjectedWindowTest.h"
#include <cstdio>

namespace {
namespace replay = tcx::inputreplay;
using namespace replay::detail;
int failures = 0, checks = 0;
void check(bool ok, const char* label) {
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL: %s\n", label); }
}
void windowChecks() {
    tcCoreTest::WindowFixture fixture(9701);
    auto& window = *fixture.window;
    tcCoreTest::InjectedWindow native(window, 9701);
    { tc::internal::WindowEntryScope seed(window, false, true, false); }
    auto app = tc::internal::makeConstructedAppAt<tc::App>(&tc::internal::WindowRequestAccess::context(window));
    tc::internal::attachCandidate(window, app);
    tc::internal::applyPendingAppForTests(window);
    tc::internal::AppOwnerScope owner(tc::internal::appOwner(*app));
    tcx::imgui::imguiSetup();
    auto* expectedContext = ImGui::GetCurrentContext();
    tcCoreTest::WindowFixture other(9702);
    tcCoreTest::InjectedWindow otherNative(*other.window, 9702);
    {
        tc::internal::WindowEntryScope entry(*other.window, false, true, false);
        tcx::imgui::imguiSetup();
    }
    auto* otherContext = ImGui::GetCurrentContext();
    tc::internal::currentWindowCtx() = &tc::internal::WindowRequestAccess::context(window);
    replay::setup();
    auto& s = state();
    check(expectedContext != otherContext && s.toolbarContext == expectedContext, "toolbar attaches to setup window after another ImGui setup");
    s.toolbarVisible = true;
    sg_commit();
    tcx::imgui::imguiBegin();
    check(s.toolbarFrameSeen && ImGui::FindWindowByName("Input replay"), "setup window frame runs toolbar hook");
    tcx::imgui::imguiEnd();
    {
        tc::internal::WindowEntryScope entry(*other.window, false, true, false);
        s.toolbarFrameSeen = false;
        sg_commit();
        tcx::imgui::imguiBegin();
        check(!s.toolbarFrameSeen && !ImGui::FindWindowByName("Input replay"), "other window has no toolbar hook");
        tcx::imgui::imguiEnd();
        tcx::imgui::imguiShutdown();
    }
    s.toolbarVisible = true;
    int dialogs = 0;
    const auto originalSave = savePicker, originalOpen = openPicker;
    const auto path = std::filesystem::current_path() / "inputreplay-b2.jsonl";
    savePicker = [&](FileChoice callback) {
        ++dialogs;
        tc::FileDialogResult choice;
        choice.success = true;
        choice.filePath = path;
        callback(choice);
    };
    openPicker = [&](FileChoice callback) {
        ++dialogs;
        tc::FileDialogResult choice;
        choice.success = true;
        choice.filePath = path;
        callback(choice);
    };
    auto key = [&](int code, bool repeat = false) { window.injectInput(tc::InputEvent::keyPressed(code, {}, repeat)); };
    key(tc::KEY_F9);
    check(!s.toolbarVisible && !s.recording() && !s.timeline.active, "default hotkey only hides toolbar");
    key(tc::KEY_F9, true);
    check(!s.toolbarVisible, "repeat does not toggle toolbar");
    replay::setHotKeyEnabled(false);
    key(tc::KEY_F9);
    check(!s.toolbarVisible, "disabled hotkey ignored");
    replay::setHotKey(tc::KEY_F7);
    replay::setHotKeyEnabled(true);
    key(tc::KEY_F9);
    check(!s.toolbarVisible, "old hotkey ignored");
    key(tc::KEY_F7);
    check(s.toolbarVisible, "custom hotkey toggles");
    check(replay::startRecording(path) && replay::stopRecording(), "code file recording stops");
    check(replay::startRecording(replay::RecordingMode::Ring, std::chrono::minutes(1)) && replay::flush(path), "code ring flush stops");
    check(dialogs == 0, "code stop and flush open no dialogs");
    for (bool visible : {false, true}) for (bool natural : {false, true}) {
        s.toolbarVisible = visible;
        check(replay::play(path) && !s.toolbarVisible, "play hides toolbar");
        key(tc::KEY_F7);
        check(s.toolbarVisible, "hotkey toggles during playback");
        if (natural) {
            s.timeline.start(s.timeline.session, 0);
            s.update();
        } else replay::stopPlayback();
        check(!s.timeline.active && s.toolbarVisible == visible, "end or stop restores prior visibility");
    }
    // Reproduce an old file's final Stop click, including ImGui's queued tail
    // after update() has already consumed end and restored the toolbar.
    // Settle the existing manager frame, then advance only ImGui: virtual
    // windows have no native display on which to update the OS cursor.
    window.events().onRender.notify();
    bool appUi = false;
    int appActivations = 0;
    bool focusAppText = false;
    char appText[16] = {};
    auto frame = [&] {
        ImGui::NewFrame();
        if (appUi) {
            ImGui::SetNextWindowPos({20, 180});
            ImGui::SetNextWindowSize({200, 100});
            ImGui::Begin("App controls");
            if (ImGui::Button("App button")) ++appActivations;
            if (focusAppText) ImGui::SetKeyboardFocusHere();
            ImGui::InputText("Text", appText, sizeof(appText));
            ImGui::End();
        }
        ImGui::EndFrame();
    };
    s.toolbarVisible = true;
    frame();
    auto* toolbar = ImGui::FindWindowByName("Input replay");
    const tc::Vec2 buttonPos{toolbar->Pos.x + 20, toolbar->Pos.y + 12};
    window.injectInput(tc::InputEvent::mouseMoved(buttonPos));
    frame();
    auto press = Json{{"type", "mousePressed"}, {"ms", 0}, {"frame", 0}, {"window", "main"},
        {"x", buttonPos.x}, {"y", buttonPos.y}, {"button", 0}, {"mods", 0}, {"injected", false}, {"tag", ""}};
    auto release = press;
    release["type"] = "mouseReleased";
    {
        std::ofstream file(path);
        file << recordingHeader(s).dump() << '\n' << press.dump() << '\n' << release.dump() << '\n'
             << Json{{"type", "end"}, {"ms", 0}, {"frame", 0}}.dump() << '\n';
    }
    check(replay::play(path), "legacy toolbar Stop click file plays");
    s.timeline.start(s.timeline.session, 0);
    s.update();
    check(!s.timeline.active && s.toolbarVisible, "final click and end consumed before toolbar draw");
    for (int i = 0; i < 5; ++i) frame();
    check(!s.recording() && !s.timeline.active, "queued Stop click does not start recording after playback");
    check(!s.toolbarPlaybackInput, "toolbar input restored after replay queue drains");

    // The actual ImGui consumer owns press through release, even outside UI.
    window.injectInput(tc::InputEvent::mouseMoved(buttonPos));
    frame();
    check(replay::startRecording(replay::RecordingMode::Ring, std::chrono::minutes(1)), "consumed input recording starts");
    int appButtons = 0;
    auto appPress = window.events().mousePressed.listen([&](const tc::MouseEventArgs&) { ++appButtons; });
    auto appRelease = window.events().mouseReleased.listen([&](const tc::MouseEventArgs&) { ++appButtons; });
    window.injectInput(tc::InputEvent::mousePressed(buttonPos, tc::MouseButton::Left));
    s.toolbarContext->HoveredWindow = nullptr; // Pointer has left the toolbar before release.
    ImGui::GetIO().WantCaptureMouse = false;
    window.injectInput(tc::InputEvent::mouseReleased({0, 100}, tc::MouseButton::Left));
    check(appButtons == 0 && s.ring.records.empty(), "toolbar-consumed press and outside release omitted from recording and app");
    replay::stopRecording();
    frame();
    frame();
    frame();
    int appKeys = 0;
    auto appKey = window.events().keyPressed.listen([&](const tc::KeyEventArgs&) { ++appKeys; });
    int appChars = 0;
    auto appChar = window.events().charInput.listen([&](const tc::CharEventArgs&) { ++appChars; });
    window.injectInput(tc::InputEvent::mouseMoved(buttonPos));
    frame();
    window.injectInput(tc::InputEvent::mousePressed(buttonPos, tc::MouseButton::Left));
    frame();
    window.injectInput(tc::InputEvent::mouseReleased(buttonPos, tc::MouseButton::Left));
    for (int i = 0; i < 5; ++i) frame();
    check(s.recording() && s.toolbarContext->NavWindow == toolbar && !ImGui::GetIO().WantCaptureKeyboard,
        "real Record click leaves toolbar navigation without keyboard capture");
    const auto beforeTyping = replay::getRecordedCount();
    window.injectInput(tc::InputEvent::keyPressed('A'));
    window.injectInput(tc::InputEvent::charInput('a'));
    window.injectInput(tc::InputEvent::keyReleased('A'));
    check(appKeys == 1 && appChars == 1 && replay::getRecordedCount() == beforeTyping + 3,
        "typing after Record reaches app and records keys and characters");
    replay::stopRecording();
    std::filesystem::remove(s.temporaryRecording);
    s.temporaryRecording.clear();
    appUi = true;
    for (int i = 0; i < 5; ++i) frame();
    const tc::Vec2 appPos{40, 210};
    window.injectInput(tc::InputEvent::mouseMoved(appPos));
    frame();
    check(s.toolbarContext->HoveredWindow == ImGui::FindWindowByName("App controls"), "app ImGui window is hovered in toolbar context");
    replay::startRecording(replay::RecordingMode::Ring, std::chrono::minutes(1));
    window.injectInput(tc::InputEvent::mousePressed(appPos, tc::MouseButton::Left));
    frame();
    window.injectInput(tc::InputEvent::mouseReleased(appPos, tc::MouseButton::Left));
    check(appButtons == 0 && s.ring.records.size() == 2, "non-toolbar ImGui click is consumed but still recorded");
    focusAppText = true;
    frame();
    focusAppText = false;
    frame();
    frame();
    window.injectInput(tc::InputEvent::keyPressed('A'));
    window.injectInput(tc::InputEvent::charInput('a'));
    window.injectInput(tc::InputEvent::keyReleased('A'));
    check(appKeys == 1 && s.ring.records.size() == 5, "non-toolbar ImGui keys and characters are still recorded");
    replay::stopRecording();
    frame();
    for (int i = 0; i < 5; ++i) frame();
    window.injectInput(tc::InputEvent::mouseMoved(buttonPos));
    frame();
    replay::startRecording(replay::RecordingMode::Ring, std::chrono::minutes(1));
    tc::TouchEventArgs touch;
    touch.numTouches = 1;
    touch.touches[0].id = 1;
    touch.touches[0].x = toolbar->Pos.x + toolbar->Size.x - 4;
    touch.touches[0].y = toolbar->Pos.y + toolbar->Size.y - 4;
    touch.touches[0].changed = true;
    window.injectInput(tc::InputEvent::touchPressed(touch));
    window.injectInput(tc::InputEvent::touchMoved(touch));
    window.injectInput(tc::InputEvent::touchReleased(touch));
    check(s.ring.records.size() == 3 && s.ring.records.front().data["type"] == "touchPressed" &&
        s.ring.records.back().data["type"] == "touchReleased", "touch over toolbar is recorded");
    replay::stopRecording();
    for (int i = 0; i < 5; ++i) frame();
    window.injectInput(tc::InputEvent::mouseMoved(buttonPos));
    frame();
    window.injectInput(tc::InputEvent::mousePressed(buttonPos, tc::MouseButton::Right));
    frame();
    window.injectInput(tc::InputEvent::mouseMoved(appPos));
    frame();
    replay::startRecording(replay::RecordingMode::Ring, std::chrono::minutes(1));
    const auto beforeClick = appActivations;
    window.injectInput(tc::InputEvent::mousePressed(appPos, tc::MouseButton::Left));
    frame();
    window.injectInput(tc::InputEvent::mouseReleased(appPos, tc::MouseButton::Left));
    frame();
    check(appActivations == beforeClick + 1 && s.ring.records.size() == 2 &&
        s.ring.records.front().data["type"] == "mousePressed" && s.ring.records.back().data["type"] == "mouseReleased",
        "left app ImGui click records while right is held on toolbar");
    window.injectInput(tc::InputEvent::mouseReleased(appPos, tc::MouseButton::Right));
    replay::stopRecording();
    frame();
    // F9 may reveal the toolbar while the file is still playing.
    window.injectInput(tc::InputEvent::mouseMoved(buttonPos));
    frame();
    check(replay::play(path), "toolbar input guard starts another playback");
    s.timeline.start(s.timeline.session, 0);
    s.timeline.session.records.back().ms = clockMs() + 10000;
    key(tc::KEY_F7);
    s.update();
    for (int i = 0; i < 5; ++i) frame();
    check(s.toolbarVisible && s.timeline.active && !s.recording(), "visible toolbar ignores clicks during file playback");
    replay::stopPlayback();
    frame();
    int drops = 0;
    auto listener = window.events().filesDropped.listen([&](const tc::DragDropEventArgs&) { ++drops; });
    s.selectedPath.clear();
    replay::startRecording(replay::RecordingMode::Ring, std::chrono::minutes(1));
    window.injectInput(tc::InputEvent::filesDropped({"picture.png"}, {1, 2}));
    check(s.selectedPath.empty(), "non-jsonl drop ignored by addon");
    window.injectInput(tc::InputEvent::filesDropped({tc::pathToUtf8(path)}, {1, 2}));
    check(s.selectedPath == path && drops == 2 && s.recording(), "jsonl selects, reaches app and keeps recording");
    check(s.ring.records.size() == 2, "selection drops are recorded normally");
    toolbarRecord();
    check(dialogs == 1 && !s.recording(), "toolbar ring stop opens save dialog");
    toolbarRecord();
    check(s.recording(), "toolbar starts file recording");
    const auto temporary = s.temporaryRecording;
    toolbarRecord();
    check(dialogs == 2 && !s.recording() && !std::filesystem::exists(temporary), "toolbar file stop saves and removes staging file");
    toolbarOpen();
    check(dialogs == 3 && s.selectedPath == path, "toolbar open selects file");
    savePicker = [&](FileChoice callback) { ++dialogs; callback({}); };
    toolbarRecord();
    const auto canceled = s.temporaryRecording;
    toolbarRecord();
    check(!s.recording() && !std::filesystem::exists(canceled), "save cancel stops and removes toolbar recording");
    check(replay::startRecording(path), "code starts file for toolbar stop");
    toolbarRecord();
    check(std::filesystem::exists(path) && !s.recording(), "cancel retains code recording at original path");
    replay::setFlushDirectory(std::filesystem::current_path() / "inputreplay-b2-staging");
#ifndef _WIN32
    const auto previousMask = umask(0022);
#endif
    toolbarRecord();
    const auto retained = s.temporaryRecording;
#ifndef _WIN32
    umask(previousMask);
    struct stat info{};
    check(stat(retained.c_str(), &info) == 0 && (info.st_mode & 0777) == 0600, "staging created owner-only despite ordinary umask");
#endif
    check(retained.parent_path() == tc::getDataPath(s.flushDirectory), "staging lives in configured flush directory");
    savePicker = [&](FileChoice callback) {
        ++dialogs;
        tc::FileDialogResult choice;
        choice.success = true;
        choice.filePath = retained / "missing" / "save.jsonl";
        callback(choice);
    };
    toolbarRecord();
    check(!s.recording() && hasPendingSave() && std::filesystem::exists(retained), "failed save retains recording and Save action");
    check(s.lastError.find(tc::pathToUtf8(retained)) != std::string::npos, "save error identifies retained path");
    const auto supported = savePickerSupported;
    savePickerSupported = [] { return false; };
    const auto beforeUnsupported = dialogs;
    toolbarSave();
    check(hasPendingSave() && std::filesystem::exists(retained) && dialogs == beforeUnsupported, "unsupported picker retains staging without cancel");
    s.lastError.clear();
    toolbarSave();
    check(s.lastError.find(tc::pathToUtf8(retained)) != std::string::npos, "unsupported picker warns with path on every attempt");
    savePickerSupported = supported;
    savePicker = [&](FileChoice callback) {
        ++dialogs;
        tc::FileDialogResult choice;
        choice.success = true;
        choice.filePath = path;
        callback(choice);
    };
    toolbarSave();
    check(!hasPendingSave() && !std::filesystem::exists(retained) && std::filesystem::exists(path), "Save retry succeeds and releases staging");
    // A ring copy failure also retains its frozen segment for retry.
    replay::startRecording(replay::RecordingMode::Ring, std::chrono::minutes(1));
    savePicker = [&](FileChoice callback) {
        ++dialogs;
        tc::FileDialogResult choice;
        choice.success = true;
        choice.filePath = path / "missing";
        callback(choice);
    };
    toolbarRecord();
    check(hasPendingSave() && std::filesystem::exists(s.temporaryRecording), "ring failed save retains frozen segment");
    savePicker = [&](FileChoice callback) { ++dialogs; callback({}); };
    toolbarSave();
    check(!hasPendingSave() && s.temporaryRecording.empty(), "explicit cancel discards pending ring segment");
    std::filesystem::remove_all(tc::getDataPath(s.flushDirectory));
    replay::setFlushDirectory("input-replay");
    savePicker = originalSave;
    openPicker = originalOpen;
    replay::setHotKey(tc::KEY_F9);
    std::filesystem::remove(path);
}
}
int runInputReplayB2WindowChecks() {
    windowChecks();
    std::printf("B2 window replay: %d checks, %d failures\n", checks, failures);
    return failures;
}
