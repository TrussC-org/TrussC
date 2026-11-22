#include <tcxInputReplay.h>
#include <cstdio>
#include "../../../../core/tests/common/tcDisplayTest.h"

namespace {
using namespace tc;
using namespace tcx::inputreplay;
namespace replay = tcx::inputreplay;
using detail::Json;
int failures = 0, checks = 0;
void check(bool ok, const char* name) {
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL: %s\n", name); }
}
class Smoke : public App {
    std::vector<EventListener> received_, consumed_;
    EventListener ended_;
    std::vector<Json> expected_;
    size_t receivedCount_ = 0;
    bool completed_ = false;
    int updates_ = 0;
    std::filesystem::path path_, ownPath_;
#ifndef TCX_INPUTREPLAY_MOCK_WINDOW
    std::shared_ptr<Window> preview_;
    void checkRingWindows() {
        auto& state = detail::state();
        int mainCount = 0, previewCount = 0;
        auto mainListener = getWindow()->events().mouseMoved.listen([&](const MouseMoveEventArgs& e) {
            if (e.sourceTag == "replayed") ++mainCount;
        });
        auto previewListener = preview_->events().mouseMoved.listen([&](const MouseMoveEventArgs& e) {
            if (e.sourceTag == "replayed") ++previewCount;
        });
        check(replay::startRecording(RecordingMode::Ring, std::chrono::minutes(1), 2), "display ring starts");
        replay::mark("evicted");
        check(getWindow()->injectInput(InputEvent::mouseMoved({10, 20}), "script"), "display main ring input");
        check(preview_->injectInput(InputEvent::mouseMoved({30, 40}), "script"), "display second ring input");
        check(replay::flush(path_), "display ring flush");
        std::ifstream file(path_);
        auto session = detail::load(file);
        check(session.windows.size() == 2 && session.records.front().type == "mouseMoved", "display ring evicts initial mark");
        check(replay::play(path_), "display ring playback starts");
        state.timeline.start(std::move(session), 0);
        state.update();
        check(mainCount == 1 && previewCount == 1 && !state.timeline.active, "display two-window replay destinations");
    }
#endif
    void checkRecordingCount() {
        auto& state = detail::state();
        check(replay::getRecordedCount() == 0, "count before start");
        replay::startRecording(path_);
        check(replay::getRecordedCount() == 0, "header excluded from count");
        getWindow()->injectInput(InputEvent::mouseMoved({10, 20}), "script");
        check(replay::getRecordedCount() == 1, "file counts first input");
        replay::mark(std::string(65537, 'x'));
        check(replay::getRecordedCount() == 1, "mark excluded from count; long line records");
        state.recordStart -= 604800001;
        getWindow()->injectInput(InputEvent::mouseMoved({30, 40}), "script");
        check(replay::getRecordedCount() == 2 && state.recording(), "file continues beyond seven days");
        replay::stopRecording();
        check(replay::getRecordedCount() == 0, "count after file stop");
        std::ifstream file(path_);
        const auto session = detail::load(file);
        check(session.records.size() == 4 && session.records[2].ms > 604800000, "long recording loads");
        replay::startRecording(RecordingMode::Ring, std::chrono::milliseconds(1000), 2);
        replay::mark("scene");
        getWindow()->injectInput(InputEvent::mouseMoved({10, 20}), "script");
        check(replay::getRecordedCount() == 1, "ring counts inputs only");
        getWindow()->injectInput(InputEvent::mouseMoved({20, 30}), "script");
        getWindow()->injectInput(InputEvent::mouseMoved({30, 40}), "script");
        check(replay::getRecordedCount() == 2, "ring count reflects capacity eviction");
        // Advance the recording clock without waiting for the duration window.
        state.recordStart -= 2000;
        check(replay::getRecordedCount() == 0, "ring count trims idle expired inputs");
        getWindow()->injectInput(InputEvent::mouseMoved({40, 50}), "script");
        check(replay::getRecordedCount() == 1, "ring count after duration eviction");
        replay::stopRecording();
        check(replay::getRecordedCount() == 0, "count after ring stop");
        replay::startRecording(RecordingMode::Ring, std::chrono::minutes(1));
        check(state.ring.capacity == (std::numeric_limits<size_t>::max)(), "public ring default unbounded");
        replay::stopRecording();
    }
    void received(const char* type, Json j, bool own) {
        if (!own) return;
        j["type"] = type;
        if (receivedCount_ < expected_.size()) {
            auto expected = expected_[receivedCount_];
            for (const auto* key : {"ms", "frame", "window", "tag", "injected"}) {
                expected.erase(key);
                j.erase(key);
            }
            check(j == expected, "received event matches recording");
        } else check(false, "unexpected replay input");
        ++receivedCount_;
    }
public:
    bool finished = false;
    void finishSmoke() {
        finished = true;
#ifndef TCX_INPUTREPLAY_MOCK_WINDOW
        tc::exitApp();
#endif
    }
    void setup() override {
        path_ = std::filesystem::current_path() / "inputreplay-smoke.jsonl";
        ownPath_ = std::filesystem::current_path() / "inputreplay-own.jsonl";
#ifndef TCX_INPUTREPLAY_MOCK_WINDOW
        WindowSettings previewSettings;
        previewSettings.setSize(160, 100);
        preview_ = createWindow(previewSettings);
        check(bool(preview_), "second display window created");
        if (!preview_) { finishSmoke(); return; }
        replay::setup({{"main", getWindow()}, {"preview", preview_.get()}});
        checkRingWindows();
#else
        replay::setup();
#endif
        checkRecordingCount();
        auto& e = events();
        auto moved = [this](const auto& v) {
            if (!detail::state().touchDispatch) received("mouseMoved", detail::pointer(v), v.sourceTag == "replayed" && v.injected);
        };
        received_.push_back(e.mouseMoved.listen(moved, -25));
        received_.push_back(e.mouseDragged.listen(moved, -25));
        auto button = [this](const char* type, const MouseEventArgs& v) {
            if (detail::state().touchDispatch) return;
            auto j = detail::pointer(v);
            j["button"] = v.button;
            received(type, j, v.sourceTag == "replayed" && v.injected);
        };
        received_.push_back(e.mousePressed.listen([button](const MouseEventArgs& v) { button("mousePressed", v); }, -25));
        received_.push_back(e.mouseReleased.listen([button](const MouseEventArgs& v) { button("mouseReleased", v); }, -25));
        received_.push_back(e.mouseScrolled.listen([this](const ScrollEventArgs& v) {
            auto j = detail::pointer(v);
            j["scrollX"] = v.scroll.x;
            j["scrollY"] = v.scroll.y;
            received("mouseScrolled", j, v.sourceTag == "replayed" && v.injected);
        }, -25));
        auto key = [this](const char* type, const KeyEventArgs& v) {
            auto j = detail::provenance(v);
            detail::modifiers(j, v);
            j["key"] = v.key;
            j["repeat"] = v.isRepeat;
            received(type, j, v.sourceTag == "replayed" && v.injected);
        };
        received_.push_back(e.keyPressed.listen([key](const KeyEventArgs& v) { key("keyPressed", v); }, -25));
        received_.push_back(e.keyReleased.listen([key](const KeyEventArgs& v) { key("keyReleased", v); }, -25));
        received_.push_back(e.charInput.listen([this](const CharEventArgs& v) {
            auto j = detail::provenance(v);
            detail::modifiers(j, v);
            j["codepoint"] = v.codepoint;
            j["repeat"] = v.isRepeat;
            received("charInput", j, v.sourceTag == "replayed" && v.injected);
        }, -25));
        received_.push_back(e.touchPressed.listen([this](const TouchEventArgs& v) { received("touchPressed", detail::touch(v), v.sourceTag == "replayed" && v.injected); }, -25));
        received_.push_back(e.touchMoved.listen([this](const TouchEventArgs& v) { received("touchMoved", detail::touch(v), v.sourceTag == "replayed" && v.injected); }, -25));
        received_.push_back(e.touchReleased.listen([this](const TouchEventArgs& v) { received("touchReleased", detail::touch(v), v.sourceTag == "replayed" && v.injected); }, -25));
        received_.push_back(e.filesDropped.listen([this](const DragDropEventArgs& v) {
            received("filesDropped", {{"x", v.x}, {"y", v.y}, {"files", v.files}}, detail::state().injecting);
        }, -25));
        // Match ImGui's typed consumption priority. The recorder must still see every event.
        auto consume = [](auto& v) { v.consumed = true; };
        consumed_.push_back(e.mouseMoved.listen(consume, -20));
        consumed_.push_back(e.mouseDragged.listen(consume, -20));
        consumed_.push_back(e.mousePressed.listen(consume, -20));
        consumed_.push_back(e.mouseReleased.listen(consume, -20));
        consumed_.push_back(e.mouseScrolled.listen(consume, -20));
        consumed_.push_back(e.keyPressed.listen(consume, -20));
        consumed_.push_back(e.keyReleased.listen(consume, -20));
        consumed_.push_back(e.charInput.listen(consume, -20));
        ended_ = replay::onPlaybackEnd().listen([this] { completed_ = true; });
        replay::startRecording(path_);
        replay::startRecording(path_); // Repeated warnings must not reset the stream.
        replay::startRecording(path_);
        auto send = [this](const InputEvent& event) { check(getWindow()->injectInput(event, "script"), "script input accepted"); };
        send(InputEvent::mouseMoved({10, 20}, {true, false, true, false}));
        send(InputEvent::mousePressed({10, 20}, MouseButton::Left));
        send(InputEvent::mouseMoved({30, 40}));
        send(InputEvent::mouseReleased({30, 40}, MouseButton::Left));
        send(InputEvent::mouseScrolled({2, -3}));
        send(InputEvent::keyPressed('A', {true, true, false, false}, true));
        send(InputEvent::charInput(0x65e5, {}, true));
        send(InputEvent::keyReleased('A', {true, true, false, false}));
        replay::mark("scene");
        TouchEventArgs touches;
        touches.numTouches = 1;
        touches.touches[0] = {42, 50, 60, 0.5f, true};
        send(InputEvent::touchPressed(touches));
        touches.touches[0].x = 55;
        send(InputEvent::touchMoved(touches));
        touches.cancelled = true;
        send(InputEvent::touchReleased(touches));
        send(InputEvent::filesDropped({"one.txt", "日本語.txt"}, {70, 80}));
        replay::stopRecording();
        replay::stopRecording();
        replay::stopRecording();
        std::ifstream file(path_);
        const auto session = detail::load(file);
        for (const auto& r : session.records) {
            if (r.type != "mark" && r.type != "end") expected_.push_back(r.data);
        }
        check(expected_.size() == 12, "drag and touch synthesized mouse recorded once");
        check(expected_[0]["tag"] == "script" && expected_[0]["injected"] == true, "preserves injected provenance");
        replay::play(path_);
        replay::play(path_);
        replay::play(path_);
        check(getWindow()->injectInput(InputEvent::charInput('z'), "other"), "live input mixes with playback");
        check(detail::state().timeline.active, "live input does not stop playback");
        replay::startRecording(ownPath_);
    }
    void update() override {
        auto& state = detail::state();
        if (state.timeline.active && state.timeline.index < state.timeline.session.records.size() &&
            state.timeline.session.records[state.timeline.index].type == "mark") replay::mark("scene");
        if (completed_) {
            check(receivedCount_ == expected_.size(), "all recorded inputs replayed with replayed tag");
            replay::stopRecording();
            std::ifstream own(ownPath_);
            const auto session = detail::load(own);
            check(session.records.size() == 2 && session.records[0].type == "mark" && session.records[1].type == "end", "own replay omitted while recording");
            replay::stopPlayback();
            replay::stopPlayback();
            // Validation must finish before the first event is injected.
            std::ofstream invalid(path_, std::ios::app);
            invalid << "{}\n";
            invalid.close();
            replay::play(path_);
            check(!state.timeline.active && receivedCount_ == expected_.size(), "invalid file injects nothing");
            // App cleanup must finalize an active recording and release scoped listeners.
            replay::startRecording(path_);
            finishSmoke();
        } else if (++updates_ > 600) {
            check(false, "bounded frame watchdog");
            finishSmoke();
        }
    }
};
void finishChecks() {
    check(detail::state().window == nullptr && detail::state().listeners.empty(), "cleanup releases setup listeners");
    const auto path = std::filesystem::current_path() / "inputreplay-smoke.jsonl";
    std::ifstream file(path);
    try { check(detail::load(file).records.back().type == "end", "cleanup finalizes recording"); }
    catch (...) { check(false, "cleanup produces valid recording"); }
    file.close();
    std::filesystem::remove(path);
    std::filesystem::remove(std::filesystem::current_path() / "inputreplay-own.jsonl");
    std::printf("Window input replay: %d checks, %d failures\n", checks, failures);
}
} // namespace
#ifndef TCX_INPUTREPLAY_MOCK_WINDOW
namespace {
bool appImGuiFrame = false;
class ToolbarSmoke : public tc::App {
    tc::EventListener inspected_;
    int frames_ = 0;
    int previousFrame_ = 0;
public:
    void setup() override {
        replay::setup();
        previousFrame_ = ImGui::GetFrameCount();
        inspected_ = events().onRender.listen([this] {
            check(ImGui::GetFrameCount() == previousFrame_ + 1, "one ImGui frame per window draw");
            previousFrame_ = ImGui::GetFrameCount();
            auto* toolbar = ImGui::FindWindowByName("Input replay");
            check(toolbar && toolbar->Active, "toolbar drawn in shared or owned frame");
            if (++frames_ == 4) tc::exitApp();
        }, 1001);
    }
    void draw() override {
        if (appImGuiFrame) {
            if (tcx::imgui::imguiBegin()) {
                ImGui::Begin("App UI");
                ImGui::TextUnformatted("App frame");
                ImGui::End();
            }
            tcx::imgui::imguiEnd();
        }
    }
};
}
int main(int argc, char** argv) {
    if (argc == 2 && (std::string(argv[1]) == "--toolbar-check" || std::string(argv[1]) == "--toolbar-app-check")) {
        appImGuiFrame = std::string(argv[1]) == "--toolbar-app-check";
        tc::WindowSettings settings;
        settings.setSize(640, 240);
        return tcCoreTest::runDisplayTest<ToolbarSmoke>(settings, failures, [] {
            std::printf("Toolbar smoke: %d checks, %d failures\n", checks, failures);
        });
    }
    if (argc != 2 || std::string(argv[1]) != "--display-check") {
        std::printf("SKIP: use --display-check with a display server\n");
        return 0;
    }
    WindowSettings settings;
    settings.setSize(320, 200);
    return tcCoreTest::runDisplayTest<Smoke>(settings, failures, finishChecks);
}
#endif
