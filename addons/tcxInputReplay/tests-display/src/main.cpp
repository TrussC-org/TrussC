#include "../../tests/src/nodeChecks.h"
#include "../../../../core/tests/common/tcDisplayTest.h"
namespace {
using namespace inputReplayTest;
std::filesystem::path shots;
class DisplaySmoke : public Smoke {
    int frame_ = 0;
    std::shared_ptr<Window> secondary_;
    std::shared_ptr<Toolbar> retained_;
    std::filesystem::path cleanupRecording_ = std::filesystem::temp_directory_path() / "tc-inputreplay-cleanup.jsonl";
    std::filesystem::path recording_ = std::filesystem::temp_directory_path() / "tc-inputreplay-shots.jsonl";
public:
    void update() override {
        ++frame_;
        if (frame_ == 2) {
            runChecks();
            WindowSettings secondarySettings;
            secondarySettings.setSize(720, 260);
            secondary_ = createWindow(secondarySettings);
            check(bool(secondary_), "create secondary window for cleanup recording");
            secondary_->setApp<Smoke>();
            if (!shots.empty()) {
                removeChild(toolbar);
                toolbar = std::make_shared<Toolbar>();
                addChild(toolbar);
            }
        }
        if (frame_ == 3) {
            retained_ = std::static_pointer_cast<Smoke>(secondary_->getApp())->toolbar;
            retained_->startRecording(cleanupRecording_);
            retained_->mark("wait");
            retained_->stopRecording();
            check(retained_->play(cleanupRecording_), "start playback before window close");
            check(retained_->startRecording(cleanupRecording_), "start recording before window close");
            secondary_->close();
        }
        if (frame_ == 4) {
            check(!secondary_->isOpen(), "secondary window closed");
            secondary_.reset();
            check(!retained_->isRecording() && !retained_->isPlaying(), "window cleanup stops recording and playback");
            retained_.reset();
            std::ifstream file(cleanupRecording_);
            const auto session = detail::load(file);
            check(session.records.back().type == "end", "recording ends before retained Toolbar is released");
            file.close();
            std::filesystem::remove(cleanupRecording_);
        }
        if (shots.empty()) {
            if (frame_ == 5) exitApp();
            return;
        }
        if (frame_ == 5) saveScreenshot(shots / "idle.png");
        if (frame_ == 10) toolbar->startRecording(recording_);
        if (frame_ == 15) saveScreenshot(shots / "recording.png");
        if (frame_ == 20) {
            toolbar->mark("screenshot");
            toolbar->stopRecording();
            toolbar->play(recording_);
        }
        if (frame_ == 25) saveScreenshot(shots / "playing.png");
        if (frame_ == 30) {
            toolbar->stopPlayback();
            toolbar->startRecording(RecordingMode::Ring, std::chrono::seconds(30));
            native(*getWindow(), SAPP_EVENTTYPE_MOUSE_DOWN, 200, 140);
            native(*getWindow(), SAPP_EVENTTYPE_MOUSE_UP, 200, 140);
        }
        if (frame_ == 35) saveScreenshot(shots / "ring-recording.png");
        if (frame_ == 40) {
            toolbar->stopRecording();
            std::filesystem::remove(recording_);
            exitApp();
        }
    }
};
}
int main(int argc, char** argv) {
    if (argc < 2 || std::string(argv[1]) != "--display-check") {
        std::puts("SKIP: use --display-check [--shots <absolute-directory>] with a display server");
        return 0;
    }
    if (argc == 4 && std::string(argv[2]) == "--shots") shots = argv[3];
    tc::WindowSettings settings;
    settings.setSize(720, 260);
    return tcCoreTest::runDisplayTest<DisplaySmoke>(settings, inputReplayTest::failures, [] {
        std::printf("Display Nodes: %d checks, %d failures\n", inputReplayTest::checks, inputReplayTest::failures);
    });
}
