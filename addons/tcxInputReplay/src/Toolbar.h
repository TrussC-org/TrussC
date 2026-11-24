// A small Node-based recorder and playback toolbar for its own window.
// Owns the recorder/player; optional keys and MCP controls are instance scoped.
#pragma once
#include "Button.h"
#include "Label.h"
#include "Player.h"
#include "Recorder.h"
#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
namespace tcx::inputreplay {
class Toolbar : public tc::RectNode {
public:
    explicit Toolbar(int toggleKey = 0) : toggleKey_(toggleKey) { enableEvents(); setSize(550, 32); }
    void setToggleKey(int key) { toggleKey_ = key; }
    void setRecordKey(int key) { recordKey_ = key; }
    bool startRecording(const std::filesystem::path& path) {
        if (!recorder_->start(path)) return false;
        recordingPath_ = tc::getDataPath(path);
        return true;
    }
    bool startRecording(RecordingMode, std::chrono::milliseconds duration) { return recorder_->start(duration); }
    bool stopRecording() { return recorder_->stop(); }
    bool flush(const std::filesystem::path& path) { return recorder_->flush(path); }
    void mark(const std::string& name) { recorder_->mark(name); player_->mark(name); }
    bool isRecording() const { return recorder_ && recorder_->isRecording(); }
    size_t getRecordedCount() { return recorder_ ? recorder_->count() : 0; }
    bool play(const std::filesystem::path& path, ScaleMode mode = ScaleMode::None) {
        if (!player_->play(path, mode)) return false;
        selectedPath_ = tc::getDataPath(path);
        return true;
    }
    void stopPlayback() { player_->stop(); }
    bool isPlaying() const { return player_ && player_->isPlaying(); }
    bool isPointerOver() {
        if (!isVisible()) return false;
        auto hit = tc::getRootNode()->findHitNodeFromScreen(tc::getMouseX(), tc::getMouseY()).node;
        for (; hit; hit = hit->getParent()) if (hit.get() == this) return true;
        return false;
    }
    void setup() override {
        auto& window = *static_cast<tc::App*>(tc::getRootNode())->getWindow();
        recorder_ = std::make_unique<Recorder>(window);
        player_ = std::make_unique<Player>(window);
        const auto parent = std::dynamic_pointer_cast<tc::RectNode>(getParent());
        const float width = parent ? parent->getWidth() : float(window.getWidth());
        setPos((width - getWidth()) / 2, 4);
        record_ = std::make_shared<Button>("Record", [this] { toggleRecording(); });
        save_ = std::make_shared<Button>("Save", [this] { save(); });
        play_ = std::make_shared<Button>("Play", [this] {
            if (isPlaying()) stopPlayback();
            else if (!selectedPath_.empty()) play(selectedPath_);
        });
        open_ = std::make_shared<Button>("Open", [this] { open(); });
        label_ = std::make_shared<Label>();
        addChild(record_);
        addChild(save_);
        addChild(play_);
        addChild(open_);
        addChild(label_);
        listeners_.push_back(window.events().keyPressed.listen([this](const tc::KeyEventArgs& e) { keyPressed(e); }));
        listeners_.push_back(window.events().filesDropped.listen([this](const tc::DragDropEventArgs& e) {
            for (const auto& file : e.files) {
                const auto path = tc::utf8ToPath(file);
                if (path.extension() == ".jsonl") { selectedPath_ = tc::getDataPath(path); break; }
            }
        }));
        update();
    }
    void cleanup() override {
        recorder_->stop();
        player_->stop();
        listeners_.clear();
    }
    void update() override {
        player_->update();
        record_->setLabel(isRecording() ? "Stop" : "Record");
        if (isRecording()) record_->setActiveColor({0.84f, 0.14f, 0.14f});
        else record_->clearActiveColor();
        play_->setLabel(isPlaying() ? "Stop" : "Play");
        if (isPlaying()) play_->setActiveColor({0.05f, 0.48f, 0.80f});
        else play_->clearActiveColor();
        save_->setVisible(!isRecording() && retrySave_);
        float x = 8;
        for (const auto& button : {record_, save_, play_, open_}) {
            if (!button->isVisible()) continue;
            button->setPos(x, 6);
            x += 76;
        }
        label_->setPos(x, 6);
        label_->setPath(selectedPath_);
    }
    void draw() override {
        tc::fill();
        tc::setColor(0.16f, 0.17f, 0.20f);
        tc::drawRectRounded(0, 0, getWidth(), getHeight(), 6);
        if (recorder_->isRing()) {
            tc::setColor(1.0f);
            tc::drawBitmapString("Ring: " + std::to_string(getRecordedCount()), 464, getHeight() / 2, tc::Direction::Left, tc::Direction::Center);
        }
    }
    void keyPressed(const tc::KeyEventArgs& e) {
        if (e.injected || e.isRepeat) return;
        if (toggleKey_ && e.key == toggleKey_) setVisible(!isVisible());
        if (recordKey_ && e.key == recordKey_) toggleRecording();
    }
    void registerMcpTools() {
        using tc::mcp::ToolLevel;
        auto weak = std::weak_ptr<Toolbar>(std::static_pointer_cast<Toolbar>(shared_from_this()));
        auto bind = [weak](auto action) -> std::function<tc::Json(const tc::Json&)> {
            return [weak, action](const tc::Json& args) -> tc::Json {
                auto self = weak.lock();
                if (!self) return {{"status", "error"}, {"message", "toolbar no longer exists"}};
                return {{"status", action(*self, args) ? "ok" : "error"}};
            };
        };
        tc::mcp::tool("tcx_inputreplay_start_recording", "Start file or ring input recording")
            .level(ToolLevel::Control).arg<std::string>("path", "File path; omit for ring", false)
            .arg<int>("durationMs", "Ring duration in milliseconds", false)
            .bind(bind([](Toolbar& s, const tc::Json& j) {
                if (j.contains("path")) return s.startRecording(tc::utf8ToPath(detail::string(j, "path")));
                return s.startRecording(RecordingMode::Ring, std::chrono::milliseconds(j.value("durationMs", 300000)));
            }));
        tc::mcp::tool("tcx_inputreplay_stop_recording", "Stop recording and discard ring contents")
            .level(ToolLevel::Control).bind(bind([](Toolbar& s, const auto&) { return s.stopRecording(); }));
        tc::mcp::tool("tcx_inputreplay_play", "Play input in this toolbar's window")
            .level(ToolLevel::Control).arg<std::string>("path", "Recording path")
            .arg<std::string>("scale", "none, stretch, or fit", false)
            .bind(bind([](Toolbar& s, const tc::Json& j) {
                const auto scale = j.value("scale", std::string("none"));
                if (scale != "none" && scale != "stretch" && scale != "fit") return false;
                return s.play(tc::utf8ToPath(detail::string(j, "path")), scale == "fit" ? ScaleMode::Fit :
                    scale == "stretch" ? ScaleMode::Stretch : ScaleMode::None);
            }));
        tc::mcp::tool("tcx_inputreplay_stop_playback", "Stop input playback")
            .level(ToolLevel::Control).bind(bind([](Toolbar& s, const auto&) { s.stopPlayback(); return true; }));
        tc::mcp::tool("tcx_inputreplay_flush", "Save ring recording and stop")
            .level(ToolLevel::Control).arg<std::string>("path", "Destination path")
            .bind(bind([](Toolbar& s, const tc::Json& j) { return s.flush(tc::utf8ToPath(detail::string(j, "path"))); }));
    }
private:
    int toggleKey_ = 0, recordKey_ = 0;
    std::unique_ptr<Recorder> recorder_;
    std::unique_ptr<Player> player_;
    std::shared_ptr<Button> record_, save_, play_, open_;
    std::shared_ptr<Label> label_;
    std::vector<tc::EventListener> listeners_;
    std::filesystem::path selectedPath_, pendingSave_, recordingPath_, stagingFile_;
    bool retrySave_ = false;
    std::filesystem::path stagingPath() {
        const auto directory = tc::getDataPath("input-replay");
        std::filesystem::create_directories(directory);
        const auto path = directory / ("input-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".jsonl");
        // Input may contain private text. Create staging files exclusively with owner-only access.
#ifdef _WIN32
        const int fd = _wopen(path.c_str(), _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY, _S_IREAD | _S_IWRITE);
        if (fd >= 0) _close(fd);
#else
        const int fd = ::open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY, S_IRUSR | S_IWUSR);
        if (fd >= 0) ::close(fd);
#endif
        if (fd < 0) throw std::runtime_error("cannot create recording file");
        return path;
    }
    void toggleRecording() {
        try {
            if (!isRecording()) {
                if (!pendingSave_.empty() && pendingSave_ == stagingFile_) std::filesystem::remove(pendingSave_);
                pendingSave_.clear();
                retrySave_ = false;
                const auto path = stagingPath();
                if (startRecording(path)) stagingFile_ = path;
                else std::filesystem::remove(path);
                return;
            }
            if (recorder_->isRing()) {
                const auto path = stagingPath();
                if (!flush(path)) { std::filesystem::remove(path); return; }
                stagingFile_ = pendingSave_ = path;
            } else {
                if (!stopRecording()) return;
                pendingSave_ = recordingPath_;
            }
            save();
        } catch (const std::exception& e) { tc::logError("tcxInputReplay") << e.what(); }
    }
    void save() {
        if (pendingSave_.empty() || isRecording()) return;
        retrySave_ = false;
        auto weak = std::weak_ptr<Toolbar>(std::static_pointer_cast<Toolbar>(shared_from_this()));
        const auto source = pendingSave_;
        const bool staged = source == stagingFile_;
        tc::saveDialogAsync("Save input recording", "", {}, "session.jsonl", [weak, source, staged](const tc::FileDialogResult& choice) {
            auto self = weak.lock();
            if (!self) return;
            std::error_code error;
            if (!choice.success) {
                if (staged) std::filesystem::remove(source, error);
            } else {
                auto target = choice.filePath;
                if (!target.has_extension()) {
                    target += ".jsonl";
                    // The OS confirmed only the typed name, not this appended path.
                    if (std::filesystem::exists(target, error)) {
                        tc::logWarning("tcxInputReplay") << "File already exists: " << target;
                        self->retrySave_ = true;
                        return;
                    }
                }
                if (!error && source != target) {
                    // Explicit recording paths belong to the caller; only move staging files.
                    if (staged) std::filesystem::rename(source, target, error);
                    if (!staged || error) {
                        std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing, error);
                        if (staged && !error) std::filesystem::remove(source, error);
                    }
                }
                if (!error) self->selectedPath_ = target;
            }
            if (error) {
                tc::logError("tcxInputReplay") << error.message();
                self->retrySave_ = true;
                return;
            }
            self->pendingSave_.clear();
            if (staged) self->stagingFile_.clear();
        });
    }
    void open() {
        auto weak = std::weak_ptr<Toolbar>(std::static_pointer_cast<Toolbar>(shared_from_this()));
        tc::loadDialogAsync("Open input recording", "", {}, false, [weak](const tc::FileDialogResult& choice) {
            if (auto self = weak.lock(); self && choice.success) self->selectedPath_ = choice.filePath;
        });
    }
};
} // namespace tcx::inputreplay
