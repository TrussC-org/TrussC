#pragma once

#include "tcxInputReplayFormat.h"
#include <chrono>

// Single-window debugging recorder. Call setup() in tcApp::setup(), then use
// these functions on the main thread. Paths use getDataPath(). See README.md
// for JSONL v1, scene marks, scaling and recording privacy.
namespace tcx::inputreplay {
namespace detail {
inline uint64_t clockMs() {
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

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

class State {
public:
    tc::Window* window = nullptr;
    Timeline timeline;
    ScaleMode scaleMode = ScaleMode::None;
    tc::Event<void> playbackEnd;
    std::vector<tc::EventListener> listeners;
    std::shared_ptr<tc::internal::AppCancellation> cancellation;
    bool injecting = false, touchDispatch = false;
    std::ofstream output;
    uint64_t recordStart = 0, frameStart = 0;
    size_t writtenBytes = 0, writtenRecords = 0;

    bool ready() const {
        if (!tc::isMainThread()) {
            tc::logError("tcxInputReplay") << "Call input replay on the main thread";
            return false;
        }
        if (!window || !window->isOpen()) {
            tc::logError("tcxInputReplay") << "Call setup() in tcApp::setup() first";
            return false;
        }
        return true;
    }
    auto& context() const { return tc::internal::WindowRequestAccess::context(*window); }
    double dpi() const { return context().dpiScale; }
    Size currentSize() const {
        const auto& ctx = context();
        if (tc::internal::pixelPerfectMode()) return {double(ctx.fbWidth), double(ctx.fbHeight)};
        return {double(tc::internal::framebufferToLogical(ctx.fbWidth, ctx.dpiScale)),
                double(tc::internal::framebufferToLogical(ctx.fbHeight, ctx.dpiScale))};
    }
    void error(const std::string& message) {
        tc::logError("tcxInputReplay") << message;
        if (output.is_open()) output.close();
        timeline = {};
    }
    void write(Json j, const std::string& type) {
        if (!output.is_open()) return;
        try {
            j["type"] = type;
            if (type != "header") {
                const auto elapsed = clockMs() - recordStart;
                j["ms"] = type == "end" ? (std::min)(elapsed, timeLimit) : elapsed;
                j["frame"] = context().updateCount - frameStart;
                if (elapsed > timeLimit && type != "end") {
                    tc::logWarning("tcxInputReplay") << "Recording stopped because the seven-day limit was reached";
                    finishRecording();
                    return;
                }
            }
            if (type == "header") {
                Session metadata;
                validateHeader(j, metadata);
            } else validateRecord(j);
            const auto line = j.dump();
            // Reserve space for a final end record so successful recordings always load.
            const size_t reserve = type == "end" ? 0 : 128;
            if (line.size() > lineLimit || writtenRecords >= recordLimit - (type == "end" ? 0 : 1) ||
                line.size() + 1 + reserve > fileLimit - writtenBytes) {
                if (type == "end") throw std::runtime_error("recording exceeds JSONL limits");
                tc::logWarning("tcxInputReplay") << "Recording stopped because a JSONL limit was reached";
                finishRecording();
                return;
            }
            output << line << '\n';
            output.flush();
            if (!output) throw std::runtime_error("recording write failed");
            writtenBytes += line.size() + 1;
            ++writtenRecords;
        } catch (const std::exception& e) { error(e.what()); }
    }
    void record(const std::string& type, Json j) {
        if (!output.is_open() || injecting) return;
        j["window"] = "main";
        write(std::move(j), type);
    }
    void finishRecording() {
        if (!output.is_open()) return;
        write(Json::object(), "end");
        if (output.is_open()) {
            output.close();
            if (output.fail()) error("recording close failed");
        }
    }
    void cleanup() {
        finishRecording();
        timeline = {};
        listeners.clear();
        cancellation.reset();
        window = nullptr;
    }
    void update() {
        while (auto record = timeline.next(clockMs())) {
            if (record->type == "end") {
                timeline = {};
                playbackEnd.notify();
                break;
            }
            try {
                auto event = input(*record, timeline.session.size, currentSize(), scaleMode);
                struct Scope {
                    bool& flag;
                    explicit Scope(bool& value) : flag(value) { flag = true; }
                    ~Scope() { flag = false; }
                } scope(injecting);
                if (!window || !window->injectInput(event, "replayed")) {
                    error("input injection failed");
                    break;
                }
            } catch (const std::exception& e) { error(e.what()); break; }
        }
    }
};
inline State& state() { static State value; return value; }
} // namespace detail

// Registers scoped listeners on this setup window. Cleanup closes recording
// with an end record and releases the listeners; repeated setup warns.
inline void setup() {
    auto& s = detail::state();
    if (!tc::isMainThread()) { tc::logError("tcxInputReplay") << "setup() requires the main thread"; return; }
    if (s.window) { tc::logWarning("tcxInputReplay") << "setup() already called"; return; }
    s.window = tc::internal::currentWindow();
    if (!s.ready()) return;
    auto& e = s.window->events();
    // ImGui consumes typed input at -20. Capture before that, independent of setup order.
    constexpr int priority = -30;
    s.listeners.push_back(e.rawEvent.listen([&s](const sapp_event& event) {
        s.touchDispatch = event.type == SAPP_EVENTTYPE_TOUCHES_BEGAN || event.type == SAPP_EVENTTYPE_TOUCHES_MOVED ||
            event.type == SAPP_EVENTTYPE_TOUCHES_ENDED || event.type == SAPP_EVENTTYPE_TOUCHES_CANCELLED;
    }, priority));
    auto moved = [&s](const auto& event) {
        if (!s.touchDispatch) s.record("mouseMoved", detail::pointer(event));
    };
    s.listeners.push_back(e.mouseMoved.listen(moved, priority));
    s.listeners.push_back(e.mouseDragged.listen(moved, priority));
    auto button = [&s](const char* type, const tc::MouseEventArgs& event) {
        if (s.touchDispatch) return;
        auto j = detail::pointer(event);
        j["button"] = event.button;
        s.record(type, std::move(j));
    };
    s.listeners.push_back(e.mousePressed.listen([button](const tc::MouseEventArgs& event) { button("mousePressed", event); }, priority));
    s.listeners.push_back(e.mouseReleased.listen([button](const tc::MouseEventArgs& event) { button("mouseReleased", event); }, priority));
    s.listeners.push_back(e.mouseScrolled.listen([&s](const tc::ScrollEventArgs& event) {
        auto j = detail::pointer(event);
        j["scrollX"] = event.scroll.x;
        j["scrollY"] = event.scroll.y;
        s.record("mouseScrolled", std::move(j));
    }, priority));
    auto key = [&s](const char* type, const tc::KeyEventArgs& event) {
        auto j = detail::provenance(event);
        detail::modifiers(j, event);
        j["key"] = event.key;
        j["repeat"] = event.isRepeat;
        s.record(type, std::move(j));
    };
    s.listeners.push_back(e.keyPressed.listen([key](const tc::KeyEventArgs& event) { key("keyPressed", event); }, priority));
    s.listeners.push_back(e.keyReleased.listen([key](const tc::KeyEventArgs& event) { key("keyReleased", event); }, priority));
    s.listeners.push_back(e.charInput.listen([&s](const tc::CharEventArgs& event) {
        auto j = detail::provenance(event);
        detail::modifiers(j, event);
        j["codepoint"] = event.codepoint;
        j["repeat"] = event.isRepeat;
        s.record("charInput", std::move(j));
    }, priority));
    s.listeners.push_back(e.touchPressed.listen([&s](const tc::TouchEventArgs& event) { s.record("touchPressed", detail::touch(event)); }, priority));
    s.listeners.push_back(e.touchMoved.listen([&s](const tc::TouchEventArgs& event) { s.record("touchMoved", detail::touch(event)); }, priority));
    s.listeners.push_back(e.touchReleased.listen([&s](const tc::TouchEventArgs& event) { s.record("touchReleased", detail::touch(event)); }, priority));
    s.listeners.push_back(e.filesDropped.listen([&s](const tc::DragDropEventArgs& event) {
        s.record("filesDropped", {{"x", event.x}, {"y", event.y}, {"files", event.files}, {"injected", false}, {"tag", ""}});
    }, priority));
    s.listeners.push_back(e.update.listen([&s] { s.update(); }, priority));
    s.listeners.push_back(tc::internal::FrameworkEventAccess::listen(e.exit, [&s] { s.cleanup(); }, tc::EventPriority::AfterApp));
    s.cancellation = tc::internal::ownAppCancellation([&s] { s.cleanup(); });
}

// Paths resolve against the app's data directory. File failures log Error and stop.
inline void startRecording(const std::filesystem::path& path) {
    auto& s = detail::state();
    if (!s.ready()) return;
    if (s.output.is_open()) { tc::logWarning("tcxInputReplay") << "Already recording"; return; }
    s.output.clear();
    s.output.open(tc::getDataPath(path), std::ios::binary | std::ios::trunc);
    if (!s.output) { s.error("cannot open recording file"); return; }
    s.recordStart = detail::clockMs();
    s.frameStart = s.context().updateCount;
    s.writtenBytes = s.writtenRecords = 0;
    const auto size = s.currentSize();
    s.write({{"version", 1}, {"app", s.window->getTitle()}, {"trussc", tc::getVersion()},
        {"fps", (std::max)(0.0, tc::getFrameRate())},
        {"started", std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count())},
        {"windows", detail::Json::array({{{"key", "main"}, {"width", size.width}, {"height", size.height},
            {"dpi", s.dpi()}, {"pixelPerfect", tc::internal::pixelPerfectMode()}}})}}, "header");
}
inline void stopRecording() {
    auto& s = detail::state();
    if (!s.ready()) return;
    if (!s.output.is_open()) { tc::logWarning("tcxInputReplay") << "Recording is already stopped"; return; }
    s.finishRecording();
}
inline void play(const std::filesystem::path& path, ScaleMode mode) {
    auto& s = detail::state();
    if (!s.ready()) return;
    if (s.timeline.active) { tc::logWarning("tcxInputReplay") << "Already playing"; return; }
    try {
        std::ifstream file(tc::getDataPath(path), std::ios::binary);
        if (!file) throw std::runtime_error("cannot open playback file");
        auto session = detail::load(file);
        const auto size = s.currentSize();
        const auto& recorded = session.header.at("windows").at(0);
        if ((mode == ScaleMode::None && (size.width != session.size.width || size.height != session.size.height)) ||
            recorded.at("dpi") != s.dpi() || recorded.at("pixelPerfect") != tc::internal::pixelPerfectMode())
            tc::logWarning("tcxInputReplay") << "Recording window metadata differs from playback";
        s.scaleMode = mode;
        s.timeline.start(std::move(session), detail::clockMs());
    } catch (const std::exception& e) { s.error(e.what()); }
}
inline void play(const std::filesystem::path& path) { play(path, ScaleMode::None); }
inline void stopPlayback() {
    auto& s = detail::state();
    if (!s.ready()) return;
    if (!s.timeline.active) { tc::logWarning("tcxInputReplay") << "Playback is already stopped"; return; }
    s.timeline = {};
}
// Only the next matching mark advances playback. Early arrival skips remaining
// input in the previous scene; late arrival waits without a timeout.
inline void mark(const std::string& name) {
    auto& s = detail::state();
    if (!s.ready()) return;
    s.write({{"name", name}}, "mark");
    s.timeline.mark(name, detail::clockMs());
}
// Natural completion only (explicit stop/errors do not notify). Retain listen()'s RAII handle.
inline tc::Event<void>& onPlaybackEnd() { return detail::state().playbackEnd; }
} // namespace tcx::inputreplay
