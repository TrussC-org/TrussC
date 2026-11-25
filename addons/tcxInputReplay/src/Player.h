// Plays a validated JSONL session into one window, waiting at scene marks.
// This is the addon's only injection site: scheduling stays separate from UI.
#pragma once
#include "Format.h"
namespace tcx::inputreplay {
class Player {
public:
    explicit Player(tc::Window& window) : window_(window) {}
    bool play(const std::filesystem::path& path, ScaleMode mode = ScaleMode::None) {
        try {
            std::ifstream file(tc::getDataPath(path), std::ios::binary);
            if (!file) throw std::runtime_error("cannot open playback file");
            auto session = detail::load(file);
            if (session.windows.size() != 1) throw std::runtime_error("playback requires a single-window recording");
            mode_ = mode;
            timeline_.start(std::move(session), detail::clockMs());
            return true;
        } catch (const std::exception& e) { return fail(e.what()); }
    }
    void stop() { timeline_ = {}; }
    bool isPlaying() const { return timeline_.active; }
    void mark(const std::string& name) { timeline_.mark(name, detail::clockMs()); }
    void update() {
        while (auto record = timeline_.next(detail::clockMs())) {
            if (record->type == "end") break;
            try {
                const auto event = detail::input(*record, timeline_.session.size, detail::windowSize(window_), mode_);
                if (!window_.injectInput(event, "replayed")) { fail("input injection failed"); break; }
            } catch (const std::exception& e) { fail(e.what()); break; }
        }
    }
private:
    tc::Window& window_;
    detail::Timeline timeline_;
    ScaleMode mode_ = ScaleMode::None;
    bool fail(const std::string& message) {
        tc::logError("tcxInputReplay") << message;
        stop();
        return false;
    }
};
} // namespace tcx::inputreplay
