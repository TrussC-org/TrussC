#pragma once

#include "tcxInputReplayFormat.h"
#include <chrono>
#include <tcxImGui.h>
#include <imgui/imgui_internal.h>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

// Opt-in debugging recorder. Call setup() in tcApp::setup(), then use
// these functions on the main thread. Paths use getDataPath(). See README.md
// for JSONL v1, scene marks, scaling and recording privacy.
namespace tcx::inputreplay {
enum class RecordingMode { Ring };
struct WindowMapping { std::string key; tc::Window* window; };
namespace detail {
inline void registerTools();
inline void installToolbar();
inline void removeToolbar();
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
    std::vector<WindowMapping> mappings;
    std::vector<std::shared_ptr<tc::Window>> retainedWindows;
    Ring ring;
    bool ringRecording = false;
    std::filesystem::path flushDirectory = "input-replay";
    std::string lastError;
    Timeline timeline;
    ScaleMode scaleMode = ScaleMode::None;
    tc::Event<void> playbackEnd;
    std::vector<tc::EventListener> listeners;
    std::shared_ptr<tc::internal::AppCancellation> cancellation;
    bool injecting = false, touchDispatch = false;
    std::ofstream output;
    uint64_t recordStart = 0, frameStart = 0;
    size_t writtenRecords = 0;

    bool toolbarVisible = true, visibilityBeforePlayback = true, playbackVisibilitySaved = false;
    bool hotKeyEnabled = true, toolbarFrameSeen = false;
    bool toolbarPlaybackInput = false;
    unsigned toolbarButtons = 0;
    int hotKey = tc::KEY_F9;
    ImGuiContext* toolbarContext = nullptr;
    ImGuiID toolbarHook = 0, shutdownHook = 0;
    std::filesystem::path selectedPath, recordingPath, temporaryRecording, pendingSave;

    bool toolbarOwns(ImGuiWindow* candidate) const {
        return candidate && candidate->RootWindow->ID == ImHashStr("Input replay");
    }
    bool toolbarPointerOwner(tc::Window* target) const {
        if (target != window) return false;
        return toolbarContext && toolbarVisible && !toolbarPlaybackInput &&
            (toolbarOwns(toolbarContext->HoveredWindow) ||
             (toolbarContext->ActiveId && toolbarOwns(toolbarContext->ActiveIdWindow)));
    }
    bool toolbarPointer(tc::Window* target) const {
        return (target == window && toolbarButtons) || toolbarPointerOwner(target);
    }
    bool toolbarKeyboard(tc::Window* target) const {
        return target == window && toolbarContext && toolbarVisible && !toolbarPlaybackInput &&
            toolbarOwns(toolbarContext->NavWindow) && toolbarContext->IO.WantCaptureKeyboard;
    }
    void restoreToolbar() {
        if (playbackVisibilitySaved) toolbarVisible = visibilityBeforePlayback;
        playbackVisibilitySaved = false;
    }
    void selectDrop(const tc::DragDropEventArgs& event) {
        for (const auto& file : event.files) {
            const auto path = tc::utf8ToPath(file);
            if (path.extension() == ".jsonl") {
                selectedPath = tc::getDataPath(path);
                break;
            }
        }
    }
    bool recording() const { return ringRecording || output.is_open(); }
    bool warning(const std::string& message) {
        lastError = message;
        tc::logWarning("tcxInputReplay") << message;
        return false;
    }
    bool mappedWindowsOpen() const {
        const auto open = tc::internal::openWindows();
        return std::find(open.begin(), open.end(), window) != open.end() && std::all_of(mappings.begin(), mappings.end(), [&](const auto& mapping) {
            return std::find(open.begin(), open.end(), mapping.window) != open.end();
        });
    }
    bool ready() {
        if (!tc::isMainThread()) {
            lastError = "Call input replay on the main thread";
            tc::logError("tcxInputReplay") << lastError;
            return false;
        }
        if (!window || !mappedWindowsOpen()) {
            error("Call setup() first with open mapped windows");
            return false;
        }
        return true;
    }
    auto& context() const { return tc::internal::WindowRequestAccess::context(*window); }
    double dpi(tc::Window* target = nullptr) const { return tc::internal::WindowRequestAccess::context(*(target ? target : window)).dpiScale; }
    Size currentSize(tc::Window* target = nullptr) const {
        const auto& ctx = tc::internal::WindowRequestAccess::context(*(target ? target : window));
        if (tc::internal::pixelPerfectMode()) return {double(ctx.fbWidth), double(ctx.fbHeight)};
        return {double(tc::internal::framebufferToLogical(ctx.fbWidth, ctx.dpiScale)),
                double(tc::internal::framebufferToLogical(ctx.fbHeight, ctx.dpiScale))};
    }
    void error(const std::string& message) {
        lastError = message;
        tc::logError("tcxInputReplay") << message;
        ringRecording = false;
        ring = {};
        if (output.is_open()) output.close();
        restoreToolbar();
        timeline = {};
    }
    void write(Json j, const std::string& type) {
        if (!recording()) return;
        try {
            j["type"] = type;
            if (type != "header") {
                const auto elapsed = clockMs() - recordStart;
                j["ms"] = elapsed;
                j["frame"] = context().updateCount - frameStart;
            }
            if (ringRecording) {
                ring.push(std::move(j));
                return;
            }
            if (type == "header") {
                Session metadata;
                validateHeader(j, metadata);
            } else validateRecord(j);
            const auto line = j.dump();
            output << line << '\n';
            output.flush();
            if (!output) throw std::runtime_error("recording write failed");
            if (type != "header" && type != "end" && type != "mark") ++writtenRecords;
        } catch (const std::exception& e) { error(e.what()); }
    }
    void record(const std::string& key, const std::string& type, Json j) {
        if (!recording() || injecting) return;
        j["window"] = key;
        write(std::move(j), type);
    }
    void finishRecording() {
        if (ringRecording) { ringRecording = false; ring = {}; return; }
        if (!output.is_open()) return;
        write(Json::object(), "end");
        if (output.is_open()) {
            output.close();
            if (output.fail()) error("recording close failed");
        }
    }
    void cleanup() {
        const bool closedDuringPlayback = timeline.active && !mappedWindowsOpen();
        finishRecording();
        if (closedDuringPlayback) error("mapped window is closed");
        restoreToolbar();
        timeline = {};
        removeToolbar();
        listeners.clear();
        if (!temporaryRecording.empty()) {
            warning("Unsaved recording retained at " + tc::pathToUtf8(temporaryRecording));
            temporaryRecording.clear();
        }
        pendingSave.clear();
        cancellation.reset();
        window = nullptr;
        mappings.clear();
        retainedWindows.clear();
    }
    void update() {
        if ((timeline.active || recording()) && !mappedWindowsOpen()) {
            error("mapped window is closed");
            return;
        }
        if (ringRecording) ring.trim(clockMs() - recordStart);
        while (auto record = timeline.next(clockMs())) {
            if (record->type == "end") {
                restoreToolbar();
                timeline = {};
                playbackEnd.notify();
                break;
            }
            try {
                const auto key = string(record->data, "window");
                auto found = std::find_if(mappings.begin(), mappings.end(), [&](const auto& mapping) { return mapping.key == key; });
                if (found == mappings.end()) throw std::runtime_error("missing window mapping");
                auto* target = found->window;
                auto event = input(*record, timeline.session.windows.at(key), currentSize(target), scaleMode);
                struct Scope {
                    bool& flag;
                    explicit Scope(bool& value) : flag(value) { flag = true; }
                    ~Scope() { flag = false; }
                } scope(injecting);
                if (!target->injectInput(event, "replayed")) {
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
inline void setup(std::vector<WindowMapping> mappings = {}) {
    auto& s = detail::state();
    if (!tc::isMainThread()) { tc::logError("tcxInputReplay") << "setup() requires the main thread"; return; }
    if (s.window) { s.warning("setup() already called"); return; }
    s.window = tc::internal::currentWindow();
    if (mappings.empty()) mappings.push_back({"main", s.window});
    const auto open = tc::internal::openWindows();
    for (const auto& mapping : mappings) {
        if (mapping.key.empty() || std::find(open.begin(), open.end(), mapping.window) == open.end() ||
            std::any_of(s.mappings.begin(), s.mappings.end(), [&](const auto& other) {
                return other.key == mapping.key || other.window == mapping.window;
            })) {
            s.error("missing, closed or duplicate window mapping");
            s.cleanup();
            return;
        }
        s.mappings.push_back(mapping);
        s.retainedWindows.push_back(tc::internal::WindowRequestAccess::retainHostOwner(*mapping.window));
    }
    if (!s.ready()) { s.cleanup(); return; }
    constexpr int priority = -30;
    for (const auto& mapping : s.mappings) {
        const auto keyName = mapping.key;
        auto* target = mapping.window;
        auto& e = mapping.window->events();

        // Record before ImGui consumes typed input at -20; exclude only our toolbar.
        s.listeners.push_back(e.rawEvent.listen([&s](const sapp_event& event) {
            s.touchDispatch = event.type == SAPP_EVENTTYPE_TOUCHES_BEGAN || event.type == SAPP_EVENTTYPE_TOUCHES_MOVED ||
                event.type == SAPP_EVENTTYPE_TOUCHES_ENDED || event.type == SAPP_EVENTTYPE_TOUCHES_CANCELLED;
        }, -30));
        auto moved = [&s, keyName, target](const auto& event) {
            if (!s.touchDispatch && !s.toolbarPointer(target)) s.record(keyName, "mouseMoved", detail::pointer(event));
        };
        s.listeners.push_back(e.mouseMoved.listen(moved, priority));
        s.listeners.push_back(e.mouseDragged.listen(moved, priority));
        auto button = [&s, keyName, target](const char* type, const tc::MouseEventArgs& event) {
            if (s.touchDispatch) return;
            const unsigned mask = 1u << event.button;
            const bool pressed = std::string_view(type) == "mousePressed";
            const bool skip = pressed ? s.toolbarPointerOwner(target) :
                (target == s.window && (s.toolbarButtons & mask));
            if (target == s.window) {
                if (pressed && skip) s.toolbarButtons |= mask;
                if (!pressed) s.toolbarButtons &= ~mask;
            }
            if (skip) return;
            auto j = detail::pointer(event);
            j["button"] = event.button;
            s.record(keyName, type, std::move(j));
        };
        s.listeners.push_back(e.mousePressed.listen([button](const tc::MouseEventArgs& event) { button("mousePressed", event); }, priority));
        s.listeners.push_back(e.mouseReleased.listen([button](const tc::MouseEventArgs& event) { button("mouseReleased", event); }, priority));
        s.listeners.push_back(e.mouseScrolled.listen([&s, keyName, target](const tc::ScrollEventArgs& event) {
            if (s.toolbarPointer(target)) return;
            auto j = detail::pointer(event);
            j["scrollX"] = event.scroll.x;
            j["scrollY"] = event.scroll.y;
            s.record(keyName, "mouseScrolled", std::move(j));
        }, priority));
        auto key = [&s, keyName, target](const char* type, const tc::KeyEventArgs& event) {
            if (s.toolbarKeyboard(target)) return;
            auto j = detail::provenance(event);
            detail::modifiers(j, event);
            j["key"] = event.key;
            j["repeat"] = event.isRepeat;
            s.record(keyName, type, std::move(j));
        };
        s.listeners.push_back(e.keyPressed.listen([key](const tc::KeyEventArgs& event) { key("keyPressed", event); }, priority));
        s.listeners.push_back(e.keyReleased.listen([key](const tc::KeyEventArgs& event) { key("keyReleased", event); }, priority));
        s.listeners.push_back(e.charInput.listen([&s, keyName, target](const tc::CharEventArgs& event) {
            if (s.toolbarKeyboard(target)) return;
            auto j = detail::provenance(event);
            detail::modifiers(j, event);
            j["codepoint"] = event.codepoint;
            j["repeat"] = event.isRepeat;
            s.record(keyName, "charInput", std::move(j));
        }, priority));
        s.listeners.push_back(e.touchPressed.listen([&s, keyName](const tc::TouchEventArgs& event) { s.record(keyName, "touchPressed", detail::touch(event)); }, priority));
        s.listeners.push_back(e.touchMoved.listen([&s, keyName](const tc::TouchEventArgs& event) { s.record(keyName, "touchMoved", detail::touch(event)); }, priority));
        s.listeners.push_back(e.touchReleased.listen([&s, keyName](const tc::TouchEventArgs& event) { s.record(keyName, "touchReleased", detail::touch(event)); }, priority));
        s.listeners.push_back(e.filesDropped.listen([&s, keyName](const tc::DragDropEventArgs& event) {
            s.record(keyName, "filesDropped", {{"x", event.x}, {"y", event.y}, {"files", event.files}, {"injected", false}, {"tag", ""}});
        }, priority));
    }
    auto& e = s.window->events();
    s.listeners.push_back(e.keyPressed.listen([&s](const tc::KeyEventArgs& event) {
        if (s.hotKeyEnabled && event.key == s.hotKey && !event.isRepeat) {
            s.toolbarVisible = !s.toolbarVisible;
            tc::redraw();
        }
    }, -30));
    s.listeners.push_back(e.filesDropped.listen([&s](const tc::DragDropEventArgs& event) { s.selectDrop(event); }, priority));
    s.listeners.push_back(e.update.listen([&s] { s.update(); }, priority));
    detail::installToolbar();
    s.listeners.push_back(tc::internal::FrameworkEventAccess::listen(e.exit, [&s] { s.cleanup(); }, tc::EventPriority::AfterApp));
    s.cancellation = tc::internal::ownAppCancellation([&s] { s.cleanup(); });
    detail::registerTools();
}

// Paths resolve against the app's data directory. File failures log Error and stop.
namespace detail {
inline Json recordingHeader(State& s) {
    auto windows = Json::array();
    for (const auto& mapping : s.mappings) {
        const auto size = s.currentSize(mapping.window);
        windows.push_back({{"key", mapping.key}, {"width", size.width}, {"height", size.height},
            {"dpi", s.dpi(mapping.window)}, {"pixelPerfect", tc::internal::pixelPerfectMode()}});
    }
    return {{"type", "header"}, {"version", 1}, {"app", s.window->getTitle()}, {"trussc", tc::getVersion()},
        {"fps", (std::max)(0.0, tc::getFrameRate())},
        {"started", std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count())}, {"windows", std::move(windows)}};
}
inline bool beginRecording(State& s) {
    if (!s.ready()) return false;
    if (s.recording()) return s.warning("Already recording");
    if (!s.temporaryRecording.empty()) {
        s.warning("Discarding unsaved recording at " + tc::pathToUtf8(s.temporaryRecording));
        std::error_code ignored;
        std::filesystem::remove(s.temporaryRecording, ignored);
        s.temporaryRecording.clear();
    }
    s.pendingSave.clear();
    s.lastError.clear();
    s.recordStart = clockMs();
    s.frameStart = s.context().updateCount;
    return true;
}
}
inline bool startRecording(const std::filesystem::path& path) {
    auto& s = detail::state();
    if (!detail::beginRecording(s)) return false;
    s.output.clear();
    s.output.open(tc::getDataPath(path), std::ios::binary | std::ios::trunc);
    if (!s.output) { s.error("cannot open recording file"); return false; }
    s.recordingPath = tc::getDataPath(path);
    s.writtenRecords = 0;
    s.write(detail::recordingHeader(s), "header");
    return s.output.is_open();
}
// Capacity defaults to unbounded; an explicit capacity includes input/marks, excludes header/end.
inline bool startRecording(RecordingMode, std::chrono::milliseconds duration, size_t capacity = (std::numeric_limits<size_t>::max)()) {
    auto& s = detail::state();
    if (!detail::beginRecording(s)) return false;
    try {
        if (duration.count() <= 0) throw std::runtime_error("ring duration must be positive");
        s.ring.start(detail::recordingHeader(s), uint64_t(duration.count()), capacity);
        s.ringRecording = true;
        return true;
    } catch (const std::exception& e) { s.error(e.what()); return false; }
}
// Input records written in the current recording, excluding marks/header/end.
// Ring mode reports retained input records; returns zero when not recording.
inline size_t getRecordedCount() {
    auto& s = detail::state();
    if (!s.recording()) return 0;
    if (s.ringRecording) {
        s.ring.trim(detail::clockMs() - s.recordStart);
        return s.ring.inputCount;
    }
    return s.writtenRecords;
}
inline bool stopRecording() {
    auto& s = detail::state();
    if (!s.ready()) return false;
    if (!s.recording()) return s.warning("Recording is already stopped");
    s.lastError.clear();
    s.finishRecording();
    return s.lastError.empty();
}
inline void setFlushDirectory(const std::filesystem::path& folder) {
    auto& s = detail::state();
    if (s.ready()) s.flushDirectory = folder;
}
inline bool flush(const std::filesystem::path& path) {
    auto& s = detail::state();
    if (!s.ready()) return false;
    if (!s.recording()) return s.warning("Recording is already stopped; cannot flush");
    if (!s.ringRecording) return s.warning("flush requires ring recording; stopRecording closes a file recording");
    s.lastError.clear();
    s.ringRecording = false; // Every flush attempt stops recording, including file failures.
    try {
        const auto destination = tc::getDataPath(path);
        if (!destination.parent_path().empty()) std::filesystem::create_directories(destination.parent_path());
        std::ofstream file(destination, std::ios::binary | std::ios::trunc);
        if (!file) throw std::runtime_error("cannot open flush file");
        s.ring.save(file, detail::clockMs() - s.recordStart, s.context().updateCount - s.frameStart);
        file.close();
        if (!file) throw std::runtime_error("flush close failed");
        s.ring = {};
        return true;
    } catch (const std::exception& e) { s.error(e.what()); return false; }
}
inline bool flush() {
    auto& s = detail::state();
    const auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return flush(s.flushDirectory / ("input-" + std::to_string(timestamp) + ".jsonl"));
}
inline bool play(const std::filesystem::path& path, ScaleMode mode = ScaleMode::None) {
    auto& s = detail::state();
    if (!s.ready()) return false;
    if (s.timeline.active) return s.warning("Already playing");
    s.lastError.clear();
    try {
        std::ifstream file(tc::getDataPath(path), std::ios::binary);
        if (!file) throw std::runtime_error("cannot open playback file");
        auto session = detail::load(file);
        for (const auto& recorded : session.header.at("windows")) {
            const auto key = detail::string(recorded, "key");
            auto found = std::find_if(s.mappings.begin(), s.mappings.end(), [&](const auto& mapping) { return mapping.key == key; });
            if (found == s.mappings.end()) throw std::runtime_error("missing window mapping");
            const auto size = s.currentSize(found->window);
            const auto recordedSize = session.windows.at(key);
            if ((mode == ScaleMode::None && (size.width != recordedSize.width || size.height != recordedSize.height)) ||
                recorded.at("dpi") != s.dpi(found->window) || recorded.at("pixelPerfect") != tc::internal::pixelPerfectMode())
                tc::logWarning("tcxInputReplay") << "Recording window metadata differs from playback";
        }
        s.scaleMode = mode;
        s.timeline.start(std::move(session), detail::clockMs());
        s.playbackVisibilitySaved = true;
        s.visibilityBeforePlayback = s.toolbarVisible;
        s.toolbarVisible = false;
        s.toolbarPlaybackInput = true;
        return true;
    } catch (const std::exception& e) { s.error(e.what()); return false; }
}
inline bool stopPlayback() {
    auto& s = detail::state();
    if (!s.ready()) return false;
    if (!s.timeline.active) return s.warning("Playback is already stopped");
    s.lastError.clear();
    s.restoreToolbar();
    s.timeline = {};
    return true;
}
// Hotkeys only toggle the toolbar on the setup window.
inline void setHotKeyEnabled(bool enabled) { detail::state().hotKeyEnabled = enabled; }
inline void setHotKey(int key) { detail::state().hotKey = key; }
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
namespace detail {
using FileChoice = std::function<void(const tc::FileDialogResult&)>;
// Internal seams let virtual-window tests observe dialogs without native UI.
inline std::function<void(FileChoice)> savePicker = [](FileChoice callback) {
    tc::saveDialogAsync("Save input recording", "", {}, "session.jsonl", std::move(callback));
};
inline std::function<void(FileChoice)> openPicker = [](FileChoice callback) {
    tc::loadDialogAsync("Open input recording", "", {}, false, std::move(callback));
};
inline std::function<bool()> savePickerSupported = [] {
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
    return false;
#else
    return true;
#endif
};
inline std::filesystem::path stagingPath() {
    static uint64_t sequence = 0;
    const auto directory = tc::getDataPath(state().flushDirectory);
    std::filesystem::create_directories(directory);
    const auto path = directory / (".trussc-input-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
        "-" + std::to_string(++sequence) + ".jsonl");
    // Create exclusively before opening a stream; POSIX permissions apply at creation.
#ifdef _WIN32
    const int fd = _wopen(path.c_str(), _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY, _S_IREAD | _S_IWRITE);
    if (fd >= 0) _close(fd);
#else
    const int fd = ::open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY, S_IRUSR | S_IWUSR);
    if (fd >= 0) ::close(fd);
#endif
    if (fd < 0) throw std::runtime_error("cannot create staging file at " + tc::pathToUtf8(path));
    return path;
}
inline bool hasPendingSave() {
    const auto& s = state();
    return !s.recording() && (!s.pendingSave.empty() || !s.temporaryRecording.empty());
}
inline void toolbarSave() {
    auto& s = state();
    if (!hasPendingSave()) return;
    if (s.pendingSave.empty()) s.pendingSave = s.temporaryRecording;
    const auto path = s.pendingSave;
    if (!savePickerSupported()) {
        s.warning("Toolbar saving requires a desktop file dialog; recording retained at " + tc::pathToUtf8(path));
        return;
    }
    savePicker([path](const tc::FileDialogResult& choice) {
        auto& s = state();
        if (choice.success) {
            std::error_code failure;
            if (choice.filePath != path)
                std::filesystem::copy_file(path, choice.filePath, std::filesystem::copy_options::overwrite_existing, failure);
            if (failure) {
                s.lastError = "cannot save recording: " + failure.message() + "; recording retained at " + tc::pathToUtf8(path);
                tc::logError("tcxInputReplay") << s.lastError;
                return;
            }
            s.selectedPath = choice.filePath;
        }
        if (s.temporaryRecording == path) {
            if (!choice.success || choice.filePath != path) {
                std::error_code ignored;
                std::filesystem::remove(path, ignored);
            }
            s.temporaryRecording.clear();
        }
        if (s.pendingSave == path) s.pendingSave.clear();
    });
}
inline void toolbarRecord() {
    auto& s = state();
    try {
        if (!s.recording()) {
            // beginRecording discards the previous unsaved session, with a warning.
            const auto path = stagingPath();
            if (startRecording(path)) s.temporaryRecording = path;
            else {
                std::error_code ignored;
                std::filesystem::remove(path, ignored);
            }
            return;
        }
        if (s.ringRecording) {
            const auto path = stagingPath();
            s.temporaryRecording = path;
            if (!flush(path)) return;
            s.pendingSave = path;
        } else {
            if (!stopRecording()) return;
            s.pendingSave = s.recordingPath;
        }
        toolbarSave();
    } catch (const std::exception& e) { s.error(e.what()); }
}
inline void toolbarOpen() {
    openPicker([](const tc::FileDialogResult& choice) {
        if (choice.success) state().selectedPath = choice.filePath;
    });
}
inline void drawToolbar() {
    auto& s = state();
    if (!s.toolbarVisible) return;
    const auto& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, 4}, ImGuiCond_Always, {0.5f, 0});
    const bool ignoreInput = s.timeline.active || s.toolbarPlaybackInput;
    const auto flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        (ignoreInput ? ImGuiWindowFlags_NoInputs : 0);
    ImGui::BeginDisabled(ignoreInput);
    if (ImGui::Begin("Input replay", nullptr, flags)) {
        if (ImGui::Button(s.recording() ? "Stop recording" : "Record")) toolbarRecord();
        if (hasPendingSave()) {
            ImGui::SameLine();
            if (ImGui::Button("Save")) toolbarSave();
        }
        ImGui::SameLine();
        if (ImGui::Button(s.timeline.active ? "Stop playback" : "Play")) {
            if (s.timeline.active) stopPlayback();
            else if (!s.selectedPath.empty()) play(s.selectedPath);
        }
        ImGui::SameLine();
        if (ImGui::Button("Open")) toolbarOpen();
        ImGui::SameLine();
        const auto name = tc::pathToUtf8(s.selectedPath.filename());
        ImGui::TextUnformatted(name.empty() ? "No file" : name.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tc::pathToUtf8(s.selectedPath).c_str());
        ImGui::SameLine();
        if (s.ringRecording) ImGui::Text("Ring: %zu", s.ring.records.size());
        else ImGui::TextUnformatted(s.recording() ? "Recording" : "Idle");
    }
    ImGui::End();
    ImGui::EndDisabled();
}
inline void installToolbar() {
    auto& s = state();
    // ImGui's NewFrame hook shares the app's frame; fallback runs only if the
    // app did not begin one, before tcxImGui submits it at priority 1000.
    tcx::imgui::imguiSetup();
    // imguiSetup may return with another window's context still current.
    for (const auto& [context, widgets] : tcx::imgui::detail::contexts()) {
        if (widgets.owner == &s.context()) {
            s.toolbarContext = context;
            break;
        }
    }
    ImGui::SetCurrentContext(s.toolbarContext);
    ImGuiContextHook hook;
    hook.Type = ImGuiContextHookType_NewFramePost;
    hook.Callback = [](ImGuiContext*, ImGuiContextHook*) {
        state().toolbarFrameSeen = true;
        drawToolbar();
        // update() can restore visibility before NewFrame processes the last
        // replayed click. Keep ignoring input through its trickled queue tail.
        auto& s = state();
        if (!s.timeline.active && ImGui::GetCurrentContext()->InputEventsQueue.empty())
            s.toolbarPlaybackInput = false;
    };
    s.toolbarHook = ImGui::AddContextHook(s.toolbarContext, &hook);
    hook.Type = ImGuiContextHookType_Shutdown;
    hook.Callback = [](ImGuiContext*, ImGuiContextHook*) { state().toolbarContext = nullptr; };
    s.shutdownHook = ImGui::AddContextHook(s.toolbarContext, &hook);
    s.listeners.push_back(s.window->events().onRender.listen([] {
        auto& s = state();
        if (!s.toolbarFrameSeen && s.toolbarVisible) {
            tcx::imgui::imguiBegin();
            tcx::imgui::imguiEnd();
        }
        s.toolbarFrameSeen = false;
    }, 900));
}
inline void removeToolbar() {
    auto& s = state();
    if (s.toolbarContext) {
        ImGui::RemoveContextHook(s.toolbarContext, s.toolbarHook);
        ImGui::RemoveContextHook(s.toolbarContext, s.shutdownHook);
        s.toolbarContext = nullptr;
    }
    s.toolbarFrameSeen = false;
    s.toolbarPlaybackInput = false;
    s.toolbarButtons = 0;
}
}

namespace detail {
inline Json toolResult(bool ok) {
    return ok ? Json{{"status", "ok"}} : Json{{"status", "error"}, {"message", state().lastError}};
}
inline void registerTools() {
    using tc::mcp::ToolLevel;
    tc::mcp::tool("tcx_inputreplay_start_recording", "Start file or ring input recording (debugging only)")
        .level(ToolLevel::Control)
        .arg<std::string>("path", "File path; omit for ring recording", false)
        .arg<int>("durationMs", "Positive ring retention in milliseconds", false)
        .arg<int>("capacity", "Positive ring record capacity; omit for unbounded", false)
        .bind(std::function<Json(const Json&)>([](const Json& args) {
            if (args.contains("path")) return toolResult(startRecording(tc::utf8ToPath(string(args, "path"))));
            const auto duration = args.contains("durationMs") ? unsignedValue(args, "durationMs", uint64_t((std::numeric_limits<std::chrono::milliseconds::rep>::max)())) : 300000;
            const auto capacity = args.contains("capacity") ? unsignedValue(args, "capacity", (std::numeric_limits<size_t>::max)()) : (std::numeric_limits<size_t>::max)();
            return toolResult(startRecording(RecordingMode::Ring, std::chrono::milliseconds(duration), size_t(capacity)));
        }));
    tc::mcp::tool("tcx_inputreplay_stop_recording", "Stop recording; ring content is discarded")
        .level(ToolLevel::Control).bind(std::function<Json()>([] { return toolResult(stopRecording()); }));
    tc::mcp::tool("tcx_inputreplay_play", "Play an input recording using the setup window mappings")
        .level(ToolLevel::Control).arg<std::string>("path", "Recording path")
        .arg<std::string>("scale", "none (default), stretch or fit", false)
        .bind(std::function<Json(const Json&)>([](const Json& args) {
            const auto scale = args.value("scale", std::string("none"));
            if (scale != "none" && scale != "stretch" && scale != "fit")
                return Json{{"status", "error"}, {"message", "unknown scale mode"}};
            return toolResult(play(tc::utf8ToPath(string(args, "path")), scale == "fit" ? ScaleMode::Fit :
                scale == "stretch" ? ScaleMode::Stretch : ScaleMode::None));
        }));
    tc::mcp::tool("tcx_inputreplay_stop_playback", "Stop input playback")
        .level(ToolLevel::Control).bind(std::function<Json()>([] { return toolResult(stopPlayback()); }));
    tc::mcp::tool("tcx_inputreplay_flush", "Save ring recording and stop, without a dialog")
        .level(ToolLevel::Control).arg<std::string>("path", "Destination; omit for timestamped file", false)
        .bind(std::function<Json(const Json&)>([](const Json& args) {
            return toolResult(args.contains("path") ? flush(tc::utf8ToPath(string(args, "path"))) : flush());
        }));
}
}
} // namespace tcx::inputreplay
