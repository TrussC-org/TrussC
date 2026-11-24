#pragma once
#include <tcxInputReplay.h>
#include <cstdio>
namespace inputReplayTest {
using namespace tc;
using namespace tcx::inputreplay;
inline int failures = 0, checks = 0;
inline void check(bool ok, const char* name) {
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL: %s\n", name); }
}
inline void native(Window& window, sapp_event_type type, float x = 20, float y = 100, int key = 0) {
    sapp_event event{};
    event.type = type;
    event.mouse_x = x;
    event.mouse_y = y;
    event.mouse_button = SAPP_MOUSEBUTTON_LEFT;
    event.key_code = sapp_keycode(key);
    internal::dispatchWindowEventForTests(&event, window);
}
class Smoke : public App {
public:
    std::shared_ptr<Toolbar> toolbar;
    std::filesystem::path path = std::filesystem::temp_directory_path() / "tc-inputreplay-node-test.jsonl";
    int appClicks = 0, replayClicks = 0;
    void setup() override {
        toolbar = std::make_shared<Toolbar>(KEY_F9);
        addChild(toolbar);
    }
    void mousePressed(const MouseEventArgs& e) override {
        if (e.injected) ++replayClicks;
        if (!toolbar->isPointerOver()) ++appClicks;
    }
    void runChecks() {
        auto& w = *getWindow();
        check(toolbar->getChildren().size() == 5, "four Button Nodes and one Label Node");
        check(!mcp::hasTool("tcx_inputreplay_start_recording"), "MCP is opt-in");
        const auto pos = toolbar->getPos();
        toolbar->setPos(25, 4);
        toolbar->update();
        check(toolbar->getPos().x == 25, "update preserves user position");
        toolbar->setPos(pos);
        native(w, SAPP_EVENTTYPE_KEY_DOWN, 0, 0, KEY_F9);
        check(!toolbar->isVisible(), "toggle key hides toolbar");
        w.injectInput(InputEvent::keyPressed(KEY_F9), "test");
        check(!toolbar->isVisible(), "injected toggle key ignored");
        native(w, SAPP_EVENTTYPE_KEY_UP, 0, 0, KEY_F9);
        native(w, SAPP_EVENTTYPE_KEY_DOWN, 0, 0, KEY_F9);
        check(toolbar->isVisible(), "toggle key shows toolbar");
        toolbar->setToggleKey(0);
        native(w, SAPP_EVENTTYPE_KEY_DOWN, 0, 0, KEY_F9);
        check(toolbar->isVisible(), "zero disables toggle key");
        check(toolbar->startRecording(path), "start file recording");
        native(w, SAPP_EVENTTYPE_MOUSE_DOWN);
        native(w, SAPP_EVENTTYPE_MOUSE_UP);
        w.injectInput(InputEvent::mousePressed({30, 100}, MouseButton::Left), "test");
        w.injectInput(InputEvent::filesDropped({"injected.jsonl"}, {20, 100}), "test");
        check(toolbar->getRecordedCount() == 2, "native input recorded; injected mouse and drop omitted");
        toolbar->mark("scene");
        native(w, SAPP_EVENTTYPE_KEY_DOWN, 0, 0, 'A');
        check(toolbar->stopRecording(), "stop file recording");
        std::ifstream file(path);
        auto session = detail::load(file);
        check(session.windows.size() == 1 && session.records.size() == 5, "single-window JSONL v1 with mark and end");
        file.close();
        const int before = replayClicks;
        check(toolbar->play(path), "play file");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (replayClicks == before && std::chrono::steady_clock::now() < deadline) {
            toolbar->update();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(toolbar->isPlaying(), "Player waits at mark");
        toolbar->mark("scene");
        check(replayClicks == before + 1, "Player injects mouse with replayed provenance");
        toolbar->stopPlayback();
        // The Stop click is recorded before the Button acts and still reaches App.
        check(toolbar->startRecording(path), "start toolbar-click recording");
        const float x = toolbar->getPos().x + 20;
        const auto appBefore = appClicks;
        native(w, SAPP_EVENTTYPE_MOUSE_DOWN, x, 20);
        check(toolbar->isPointerOver() && appClicks == appBefore, "isPointerOver excludes toolbar press from App dots");
        native(w, SAPP_EVENTTYPE_MOUSE_UP, x, 20);
        check(!toolbar->isRecording(), "Record Button stops recording");
        std::ifstream clicked(path);
        session = detail::load(clicked);
        check(session.records.size() == 2 && session.records[0].type == "mousePressed", "toolbar press faithfully recorded before stop");
        clicked.close();
        check(toolbar->play(path), "play recorded toolbar click");
        const auto clickDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (toolbar->isPlaying() && std::chrono::steady_clock::now() < clickDeadline) {
            toolbar->update();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(!toolbar->isPlaying() && !toolbar->isRecording(), "injected Record click does not invoke Button; natural playback end");
        toolbar->stopPlayback();
        check(toolbar->startRecording(RecordingMode::Ring, std::chrono::seconds(5)), "start ring");
        native(w, SAPP_EVENTTYPE_MOUSE_DOWN);
        native(w, SAPP_EVENTTYPE_MOUSE_UP);
        check(toolbar->getRecordedCount() == 2, "ring count");
        check(toolbar->flush(path) && !toolbar->isRecording(), "flush ring and stop");
        check(toolbar->startRecording(path), "start touch recording");
        sapp_event touch{};
        touch.num_touches = 1;
        touch.touches[0].identifier = 7;
        touch.touches[0].pos_x = 200;
        touch.touches[0].pos_y = 140;
        touch.touches[0].changed = true;
        for (auto type : {SAPP_EVENTTYPE_TOUCHES_BEGAN, SAPP_EVENTTYPE_TOUCHES_MOVED, SAPP_EVENTTYPE_TOUCHES_ENDED}) {
            touch.type = type;
            internal::dispatchWindowEventForTests(&touch, w);
        }
        check(toolbar->getRecordedCount() == 3, "touch recorded once without synthesized mouse");
        toolbar->stopRecording();
        toolbar->setRecordKey(KEY_F8);
        native(w, SAPP_EVENTTYPE_KEY_DOWN, 0, 0, KEY_F8);
        check(toolbar->isRecording(), "record key starts");
        native(w, SAPP_EVENTTYPE_KEY_UP, 0, 0, KEY_F8);
        native(w, SAPP_EVENTTYPE_KEY_DOWN, 0, 0, KEY_F8);
        check(!toolbar->isRecording(), "record key stops");
        toolbar->registerMcpTools();
        check(mcp::hasTool("tcx_inputreplay_start_recording"), "explicit MCP registration");
        auto other = std::make_shared<Toolbar>();
        addChild(other);
        mcp::internal::grantToolLevelWithoutOpening(mcp::ToolLevel::Control);
        const auto response = mcp::internal::Server::instance().processMessage(Json{
            {"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"},
            {"params", {{"name", "tcx_inputreplay_start_recording"}, {"arguments", {{"durationMs", 1000}}}}}}.dump());
        check(response.find("ok") != std::string::npos && toolbar->isRecording() && !other->isRecording(), "MCP controls only registered instance");
        toolbar->stopRecording();
        removeChild(other);
        std::filesystem::remove(path);
    }
    void draw() override { clear(0.12f); drawCircle(110, 120, 6); }
};
} // namespace inputReplayTest
