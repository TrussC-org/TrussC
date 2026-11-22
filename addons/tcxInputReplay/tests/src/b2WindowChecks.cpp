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
