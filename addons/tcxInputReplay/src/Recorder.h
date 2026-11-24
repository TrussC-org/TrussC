// Records input from one window before the App receives it.
// Injected events are omitted; toolbar clicks remain ordinary input.
#pragma once
#include "Format.h"
#include <chrono>
namespace tcx::inputreplay {
enum class RecordingMode { Ring };
namespace detail {

template<class T> inline Json provenance(const T& e) {
    return {{"injected", e.injected}, {"tag", e.sourceTag}};
}
template<class T> inline void modifiers(Json& j, const T& e) {
    j["mods"] = int(e.shift) | (int(e.ctrl) << 1) | (int(e.alt) << 2) | (int(e.super) << 3);
}
template<class T> inline Json pointer(const T& e) {
    auto j = provenance(e);
    modifiers(j, e);
    j["x"] = e.pos.x;
    j["y"] = e.pos.y;
    return j;
}
inline Json touch(const tc::TouchEventArgs& e) {
    auto j = provenance(e);
    j["canceled"] = e.cancelled;
    j["touches"] = Json::array();
    for (int i = 0; i < e.numTouches; ++i) {
        const auto& p = e.touches[i];
        j["touches"].push_back({{"id", p.id}, {"x", p.x}, {"y", p.y},
            {"pressure", p.pressure}, {"changed", p.changed}});
    }
    return j;
}

} // namespace detail
class Recorder {
public:
    explicit Recorder(tc::Window& window) : window_(window) {
        auto& e = window.events();
        listeners_.push_back(e.rawEvent.listen([this](const sapp_event& v) {
            touchDispatch_ = v.type >= SAPP_EVENTTYPE_TOUCHES_BEGAN && v.type <= SAPP_EVENTTYPE_TOUCHES_CANCELLED;
            nativeDrop_ = v.type == SAPP_EVENTTYPE_FILES_DROPPED;
        }));
        auto moved = [this](const auto& v) {
            if (!v.injected && !touchDispatch_) write(detail::pointer(v), "mouseMoved");
        };
        listeners_.push_back(e.mouseMoved.listen(moved));
        listeners_.push_back(e.mouseDragged.listen(moved));
        auto button = [this](const char* type, const tc::MouseEventArgs& v) {
            if (v.injected || touchDispatch_) return;
            auto j = detail::pointer(v);
            j["button"] = v.button;
            write(std::move(j), type);
        };
        listeners_.push_back(e.mousePressed.listen([button](const auto& v) { button("mousePressed", v); }));
        listeners_.push_back(e.mouseReleased.listen([button](const auto& v) { button("mouseReleased", v); }));
        listeners_.push_back(e.mouseScrolled.listen([this](const tc::ScrollEventArgs& v) {
            if (v.injected) return;
            auto j = detail::pointer(v);
            j["scrollX"] = v.scroll.x;
            j["scrollY"] = v.scroll.y;
            write(std::move(j), "mouseScrolled");
        }));
        auto key = [this](const char* type, const tc::KeyEventArgs& v) {
            if (v.injected) return;
            auto j = detail::provenance(v);
            detail::modifiers(j, v);
            j["key"] = v.key;
            j["repeat"] = v.isRepeat;
            write(std::move(j), type);
        };
        listeners_.push_back(e.keyPressed.listen([key](const auto& v) { key("keyPressed", v); }));
        listeners_.push_back(e.keyReleased.listen([key](const auto& v) { key("keyReleased", v); }));
        listeners_.push_back(e.charInput.listen([this](const tc::CharEventArgs& v) {
            if (v.injected) return;
            auto j = detail::provenance(v);
            detail::modifiers(j, v);
            j["codepoint"] = v.codepoint;
            j["repeat"] = v.isRepeat;
            write(std::move(j), "charInput");
        }));
        auto touch = [this](const char* type, const tc::TouchEventArgs& v) {
            if (!v.injected) write(detail::touch(v), type);
        };
        listeners_.push_back(e.touchPressed.listen([touch](const auto& v) { touch("touchPressed", v); }));
        listeners_.push_back(e.touchMoved.listen([touch](const auto& v) { touch("touchMoved", v); }));
        listeners_.push_back(e.touchReleased.listen([touch](const auto& v) { touch("touchReleased", v); }));
        // Injected file drops bypass rawEvent and have no provenance in DragDropEventArgs.
        listeners_.push_back(e.filesDropped.listen([this](const tc::DragDropEventArgs& v) {
            if (!nativeDrop_) return;
            nativeDrop_ = false;
            write({{"x", v.x}, {"y", v.y}, {"files", v.files}, {"injected", false}, {"tag", ""}}, "filesDropped");
        }));
    }
    ~Recorder() { stop(); }
    bool start(const std::filesystem::path& path) {
        if (isRecording()) return false;
        begin();
        output_.clear();
        output_.open(tc::getDataPath(path), std::ios::binary | std::ios::trunc);
        if (!output_) return fail("cannot open recording file");
        output_ << header().dump() << '\n';
        output_.flush();
        if (!output_) return fail("recording write failed");
        return true;
    }
    bool start(std::chrono::milliseconds duration) {
        if (isRecording()) return false;
        begin();
        try {
            if (duration.count() <= 0) throw std::runtime_error("ring duration must be positive");
            ring_.start(header(), uint64_t(duration.count()));
            ringRecording_ = true;
            return true;
        } catch (const std::exception& e) { return fail(e.what()); }
    }
    bool stop() {
        if (ringRecording_) { ringRecording_ = false; ring_ = {}; return true; }
        if (!output_.is_open()) return false;
        if (!write(detail::Json::object(), "end")) return false;
        output_.close();
        if (output_.fail()) return fail("recording close failed");
        return true;
    }
    bool flush(const std::filesystem::path& path) {
        if (!ringRecording_) return false;
        ringRecording_ = false;
        try {
            const auto destination = tc::getDataPath(path);
            if (!destination.parent_path().empty()) std::filesystem::create_directories(destination.parent_path());
            std::ofstream file(destination, std::ios::binary | std::ios::trunc);
            if (!file) throw std::runtime_error("cannot open flush file");
            ring_.save(file, elapsed(), frame());
            file.close();
            if (!file) throw std::runtime_error("recording write failed");
            ring_ = {};
            return true;
        } catch (const std::exception& e) { return fail(e.what()); }
    }
    void mark(const std::string& name) { write({{"name", name}}, "mark"); }
    bool isRecording() const { return ringRecording_ || output_.is_open(); }
    bool isRing() const { return ringRecording_; }
    size_t count() {
        if (!isRecording()) return 0;
        if (!ringRecording_) return count_;
        ring_.trim(elapsed());
        return ring_.inputCount;
    }
private:
    tc::Window& window_;
    std::vector<tc::EventListener> listeners_;
    std::ofstream output_;
    detail::Ring ring_;
    bool ringRecording_ = false, touchDispatch_ = false, nativeDrop_ = false;
    uint64_t start_ = 0, frameStart_ = 0;
    size_t count_ = 0;
    // No public per-Window DPI or update-count getter exists.
    auto& context() { return tc::internal::WindowRequestAccess::context(window_); }
    uint64_t elapsed() const { return detail::clockMs() - start_; }
    uint64_t frame() { return context().updateCount - frameStart_; }
    void begin() { start_ = detail::clockMs(); frameStart_ = context().updateCount; count_ = 0; }
    detail::Json header() {
        const auto size = detail::windowSize(window_);
        // pixelPerfect has no public getter; the setting is shared by all windows.
        return {{"type", "header"}, {"version", 1}, {"app", window_.getTitle()}, {"trussc", tc::getVersion()},
            {"fps", (std::max)(0.0, tc::getFrameRate())}, {"started", std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count())},
            {"windows", detail::Json::array({{{"key", "main"}, {"width", size.width}, {"height", size.height},
                {"dpi", context().dpiScale}, {"pixelPerfect", tc::internal::pixelPerfectMode()}}})}};
    }
    bool fail(const std::string& message) {
        tc::logError("tcxInputReplay") << message;
        if (output_.is_open()) output_.close();
        ringRecording_ = false;
        ring_ = {};
        return false;
    }
    bool write(detail::Json j, const std::string& type) {
        if (!isRecording()) return false;
        try {
            j["type"] = type;
            j["ms"] = elapsed();
            j["frame"] = frame();
            if (type != "mark" && type != "end") j["window"] = "main";
            if (ringRecording_) { ring_.push(std::move(j)); return true; }
            detail::validateRecord(j);
            output_ << j.dump() << '\n';
            output_.flush();
            if (!output_) throw std::runtime_error("recording write failed");
            if (type != "mark" && type != "end") ++count_;
            return true;
        } catch (const std::exception& e) { return fail(e.what()); }
    }
};
} // namespace tcx::inputreplay
