#pragma once
#include <tcxInputReplay.h>
#include <cstdio>
#include "tc/utils/internal/tcAsyncDialog.h"
#include "tc/utils/tcResourceLoad.h"
#include <set>
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
inline int saveDialogs = 0;
inline FileDialogResult saveChoice;
inline internal::DialogDismiss dialogPresenter(const internal::DialogInputs& inputs, internal::DialogCompletion done) {
    check(inputs.kind == internal::DialogKind::Save, "toolbar requests save dialog");
    ++saveDialogs;
    done(saveChoice);
    return {};
}
inline void deliverDialogs() {
    // Present, then deliver the injected result through the real async mailbox.
    internal::deliverResourceLoadBatch(internal::takeResourceLoadBatch());
    internal::deliverResourceLoadBatch(internal::takeResourceLoadBatch());
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
        internal::setDialogPresenterForTests(dialogPresenter);
        saveDialogs = 0;
        saveChoice = {};
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
        deliverDialogs();
        check(saveDialogs == 0, "code stopRecording opens no dialog");
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
        saveChoice.success = true;
        saveChoice.filePath = path;
        deliverDialogs();
        saveChoice = {};
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
        const auto dialogsBeforeFlush = saveDialogs;
        check(toolbar->flush(path) && !toolbar->isRecording(), "flush ring and stop");
        deliverDialogs();
        check(saveDialogs == dialogsBeforeFlush, "code flush opens no dialog");
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
        deliverDialogs();
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
        saveChecks();
        internal::setDialogPresenterForTests(nullptr);
    }
    void saveChecks() {
        auto& w = *getWindow();
        auto click = [&](size_t index) {
            toolbar->update();
            auto button = toolbar->getChildren().at(index);
            const auto pos = toolbar->getPos() + button->getPos();
            native(w, SAPP_EVENTTYPE_MOUSE_DOWN, pos.x + 10, pos.y + 10);
            native(w, SAPP_EVENTTYPE_MOUSE_UP, pos.x + 10, pos.y + 10);
        };
        const auto directory = getDataPath("input-replay");
        auto files = [&] {
            std::set<std::filesystem::path> paths;
            if (std::filesystem::exists(directory))
                for (const auto& entry : std::filesystem::directory_iterator(directory)) paths.insert(entry.path());
            return paths;
        };
        auto start = [&] {
            const auto before = files();
            click(0);
            for (const auto& file : files()) if (!before.contains(file)) return file;
            check(false, "toolbar creates staging file");
            return std::filesystem::path{};
        };
        const auto target = path;
        auto source = start();
        check(toolbar->isRecording() && std::filesystem::exists(source), "Record creates staging recording");
        const auto beforeStop = saveDialogs;
        saveChoice.success = true;
        saveChoice.filePath = target;
        click(0);
        deliverDialogs();
        toolbar->update();
        check(saveDialogs == beforeStop + 1, "toolbar Stop opens save dialog immediately");
        check(!std::filesystem::exists(source) && std::filesystem::exists(target), "save moves staging file to target");
        check(!toolbar->getChildren().at(1)->isVisible(), "successful save hides Save button");
        std::filesystem::remove(target);

        const auto typed = path.parent_path() / "test5";
        const auto appended = path.parent_path() / "test5.jsonl";
        std::filesystem::remove(appended);
        source = start();
        saveChoice.filePath = typed;
        click(0);
        deliverDialogs();
        check(!std::filesystem::exists(typed) && std::filesystem::exists(appended) &&
            !std::filesystem::exists(source), "extensionless test5 saves as test5.jsonl and moves staging file");
        { std::ofstream existing(appended); existing << "keep existing recording"; }
        bool warned = false;
        auto listener = getLogger().onLog.listen([&](const LogEventArgs& e) {
            if (e.level == LogLevel::Warning && e.message.find(pathToUtf8(appended)) != std::string::npos) warned = true;
        });
        source = start();
        click(0);
        deliverDialogs();
        toolbar->update();
        std::ifstream existing(appended);
        std::string contents((std::istreambuf_iterator<char>(existing)), {});
        existing.close();
        check(contents == "keep existing recording" && warned, "appended-path collision preserves target and warns with path");
        check(std::filesystem::exists(source) && toolbar->getChildren().at(1)->isVisible(),
            "appended-path collision retains staging file and shows Save");
        saveChoice.filePath = target;
        click(1);
        deliverDialogs();
        toolbar->update();
        check(!std::filesystem::exists(source) && std::filesystem::exists(target) &&
            !toolbar->getChildren().at(1)->isVisible(), "collision retry succeeds with another name");
        std::filesystem::remove(appended);
        std::filesystem::remove(target);

        const auto withExtension = path.parent_path() / "tc-inputreplay-save.custom";
        source = start();
        saveChoice.filePath = withExtension;
        click(0);
        deliverDialogs();
        check(std::filesystem::exists(withExtension) && !std::filesystem::exists(source),
            "chosen name with an extension is used as is");
        std::filesystem::remove(withExtension);

        source = start();
        saveChoice = {};
        click(0);
        deliverDialogs();
        toolbar->update();
        check(!std::filesystem::exists(source) && !std::filesystem::exists(target), "Cancel deletes staging file without target");
        check(!toolbar->getChildren().at(1)->isVisible(), "Cancel hides Save button");

        source = start();
        // A file cannot be used as a destination directory.
        saveChoice.success = true;
        saveChoice.filePath = source / "invalid.jsonl";
        click(0);
        deliverDialogs();
        toolbar->update();
        check(std::filesystem::exists(source) && toolbar->getChildren().at(1)->isVisible(), "failed save retains staging and shows Save");
        saveChoice.filePath = target;
        click(1);
        deliverDialogs();
        toolbar->update();
        check(!std::filesystem::exists(source) && std::filesystem::exists(target) &&
            !toolbar->getChildren().at(1)->isVisible(), "Save retry moves file and hides Save");
        std::filesystem::remove(target);

        check(toolbar->startRecording(RecordingMode::Ring, std::chrono::seconds(5)), "start ring for toolbar flush");
        const auto beforeRing = files();
        const auto dialogsBeforeRing = saveDialogs;
        saveChoice = {};
        click(0);
        deliverDialogs();
        check(!toolbar->isRecording() && saveDialogs == dialogsBeforeRing + 1, "toolbar ring flush opens dialog");
        check(files() == beforeRing, "ring Cancel removes staging file");

        check(toolbar->startRecording(target), "start caller-owned file");
        click(0);
        deliverDialogs();
        check(std::filesystem::exists(target), "toolbar Cancel preserves caller-owned recording");
        std::filesystem::remove(target);
    }
    void draw() override { clear(0.12f); drawCircle(110, 120, 6); }
};
} // namespace inputReplayTest
