// Interactive #239 device pass. No mode means usage only, including in CI.
#include <TrussC.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <thread>
#ifdef _WIN32
#include <share.h>
#endif

using namespace tc;

namespace {
struct ProbeState {
    std::string mode;
    fs::path logPath;
    std::FILE* file = nullptr;
    bool vetoNext = true;
    bool visible = true;
    bool customBound = false;
    std::string shape = "Default";
    std::string focused = "none (press a key in a window)";
    int timeoutIndex = 0;
    int attached = 0;
    int exited = 0;
    int cleaned = 0;
    std::shared_ptr<Window> second;
};
// Kept alive for the atexit handler, including after runApp returns on Linux.
ProbeState& state() { static auto* value = new ProbeState; return *value; }

std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto stamp = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &stamp);
#else
    localtime_r(&stamp, &local);
#endif
    std::ostringstream out;
    out << std::put_time(&local, "%Y-%m-%dT%H:%M:%S") << '.'
        << std::setfill('0') << std::setw(3)
        << std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    return out.str();
}
void writeLine(const std::string& line) {
    std::puts(line.c_str());
    std::fflush(stdout);
    if (state().file) {
        std::fprintf(state().file, "%s\n", line.c_str());
        std::fflush(state().file);
    }
}
void record(const std::string& text) {
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) writeLine(timestamp() + " " + line);
}
void processExit() {
    auto& s = state();
    record("process exit attached=" + std::to_string(s.attached) + " exit="
        + std::to_string(s.exited) + " cleanup=" + std::to_string(s.cleaned));
    // The framework's own "[System] exit: clean" line is the evidence; this
    // line only says whether every attached App got exit and cleanup once.
    if (s.attached > 0 && s.exited == s.attached && s.cleaned == s.attached)
        record("probe: callbacks balanced");
    else
        record("exit: incomplete (inspect framework log)");
    if (s.file) { std::fclose(s.file); s.file = nullptr; }
}
fs::path probeLogPath() {
#ifdef _WIN32
    return fs::path("C:/Users/Public/device-239-probe.log");
#else
    const char* userHome = std::getenv("HOME");
    return fs::path(userHome ? userHome : ".") / "device-239-probe.log";
#endif
}
std::string sessionMapping() {
    return "Public events only: session-end corresponds to mac applicationShouldTerminate /\n"
           "Win WM_QUERYENDSESSION; exit/cleanup + framework exit: clean correspond to\n"
           "accepted termination / WM_ENDSESSION. Native query/cancel is not directly visible.\n"
           "args.reason is a veto explanation (may be empty); framework exit line has cause.\n"
           "Linux logout delivery depends on the session manager; no native hook is added.";
}

class ProbeApp : public App {
    std::string label_;
    uint32_t windowId_ = 0;
    int exitCount_ = 0;
    int cleanupCount_ = 0;
    EventListener rawListener_, dropListener_, closeListener_, exitRequestListener_;
    std::string identity() const {
        return "window=" + (windowId_ ? std::to_string(windowId_) : "pending-raw-event") + " label=" + label_;
    }
    void announceState() {
        auto& s = state();
        record(identity() + " cursor shape=" + s.shape + " customBound="
            + std::to_string(s.customBound) + " visible=" + std::to_string(s.visible));
    }
public:
    explicit ProbeApp(std::string label = "A") : label_(std::move(label)) {}
    void setup() override {
        auto& s = state();
        ++s.attached;
        getWindow()->setQuitOnClose(s.mode == "close" && label_ == "A");
        // rawEvent is public. Its window_id is the creation ID used by
        // tc_list_windows; Window itself has no public ID getter on this base.
        // Cache it because getWindow() is unavailable during session teardown.
        rawListener_ = events().rawEvent.listen([this](const sapp_event& e) {
            if (e.window_id && windowId_ != e.window_id) {
                windowId_ = e.window_id;
                record(identity() + " identity from rawEvent.window_id");
            }
        });
        dropListener_ = events().filesDropped.listen([this](DragDropEventArgs& e) {
            record(identity() + " filesDropped count=" + std::to_string(e.files.size()));
            for (size_t index = 0; index < e.files.size(); ++index)
                record(identity() + " path[" + std::to_string(index) + "]=" + e.files[index]);
        });
        closeListener_ = events().closeRequested.listen([this](CloseRequestEventArgs&) {
            record(identity() + " closeRequested (OS close only; allowed)");
        });
        exitRequestListener_ = events().exitRequested.listen([this](ExitRequestEventArgs& e) {
            record(identity() + " exitRequested args.reason=" + e.reason + " (empty allowed)");
        });
        record(identity() + " setup quitOnClose=" + std::to_string(s.mode == "close" && label_ == "A"));
        if (label_ == "A") {
            if (s.mode == "session") {
                runtime().setLogoutTimeout(2);
                record("setLogoutTimeout seconds=2");
                record(sessionMapping());
            }
            if (s.mode == "drop" || s.mode == "cursor" || s.mode == "close") {
                WindowSettings settings;
                settings.setSize(780, 480).setTitle("device-239-probe " + s.mode + " B");
                s.second = createWindow(settings);
                if (s.second) {
                    s.second->setPosition(80, 100);
                    s.second->setApp<ProbeApp>("B");
                } else {
                    record("FAIL: second window could not be created");
                    exitApp(1);
                }
            }
        }
    }
    void draw() override {
        auto& s = state();
        clear(0.08f, 0.11f, 0.16f);
        setColor(1.0f);
        std::string text = "#239 device probe: " + s.mode + "  " + identity()
            + "\nLog (append + flush): " + pathToUtf8(s.logPath) + "\n\n";
        if (s.mode == "drop")
            text += "Drop files onto A, then B. Check receiver ID, count and every path.\n"
                    "Try 16 and >16 files, and long paths. Close both windows to finish.";
        if (s.mode == "cursor")
            text += "Move the pointer between both windows after every change.\n"
                    "1 Arrow   2 IBeam   3 Crosshair   4 Hand   5 ResizeEW\n"
                    "B bind/select generated 32x32 Custom0   U unbind Custom0\n"
                    "H hide   S show. Close both windows to finish.\n\n"
                    "App-wide: shape=" + s.shape + " customBound=" + std::to_string(s.customBound)
                    + " visible=" + std::to_string(s.visible);
        if (s.mode == "close")
            text += "A quitOnClose=true; B ordinary. V toggles veto next exitRequested.\n"
                    "Default ON; one veto auto-disarms it. C calls close() on THIS window.\n"
                    "Try title-bar X and C in separate launches. First close A: both stay.\n"
                    "Repeat: process exits. Also close B first, then test last-window A.\n"
                    "Code close bypasses closeRequested; OS X emits closeRequested.\n\n"
                    "vetoNext=" + std::to_string(s.vetoNext) + " last key window: " + s.focused;
        if (s.mode == "session")
            text += "SAVE OTHER WORK before real OS logout/restart/shutdown.\n"
                    "T cycles logout timeout 2 / 5 / 10 seconds (current="
                    + std::to_string(std::array<int, 3>{2, 5, 10}[s.timeoutIndex]) + ").\n"
                    "Idle: log out now. Busy: press D, then log out during the 30 s block.\n"
                    "D blocks main in this key handler, sleeping in a loop.\n"
                    "After login inspect log: idle exit/cleanup + exit: clean; busy may lack them.\n"
                    "For a normal local finish, close the window.\n\n" + sessionMapping();
        if (s.mode == "keepawake")
            text += "K toggles setKeepScreenOn. Observe Windows display power behavior.\n"
                    "Close window to finish and release the request.\n\nkeepScreenOn="
                    + std::to_string(getKeepScreenOn());
        drawBitmapString(text, 16, 28);
    }
    void keyPressed(int key) override {
        auto& s = state();
        s.focused = identity();
        record(identity() + " key=" + std::to_string(key) + " focused (last key receiver)");
        if (s.mode == "cursor") {
            constexpr std::array<Cursor, 5> shapes{Cursor::Arrow, Cursor::IBeam, Cursor::Crosshair, Cursor::Hand, Cursor::ResizeEW};
            constexpr std::array<const char*, 5> names{"Arrow", "IBeam", "Crosshair", "Hand", "ResizeEW"};
            if (key >= '1' && key <= '5') {
                setCursor(shapes[key - '1']); s.shape = names[key - '1'];
            } else if (key == 'B') {
                std::array<unsigned char, 32 * 32 * 4> pixels{};
                for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) {
                    const auto offset = static_cast<size_t>((y * 32 + x) * 4);
                    pixels[offset] = 255; pixels[offset + 1] = static_cast<unsigned char>(x * 8);
                    pixels[offset + 2] = static_cast<unsigned char>(y * 8);
                    pixels[offset + 3] = (x == y || x + y == 31 || x < 3 || y < 3) ? 255 : 0;
                }
                bindCursorImage(Cursor::Custom0, 32, 32, pixels.data());
                setCursor(Cursor::Custom0); s.shape = "Custom0"; s.customBound = true;
            } else if (key == 'U') { unbindCursorImage(Cursor::Custom0); s.customBound = false;
            } else if (key == 'H') { hideCursor(); s.visible = false;
            } else if (key == 'S') { showCursor(); s.visible = true;
            } else return;
            announceState();
        } else if (s.mode == "close") {
            if (key == 'V') { s.vetoNext = !s.vetoNext; record("vetoNext=" + std::to_string(s.vetoNext)); }
            if (key == 'C') { record(identity() + " close() requested by focused window"); getWindow()->close(); }
        } else if (s.mode == "session") {
            if (key == 'T') {
                s.timeoutIndex = (s.timeoutIndex + 1) % 3;
                const int seconds = std::array<int, 3>{2, 5, 10}[s.timeoutIndex];
                runtime().setLogoutTimeout(static_cast<float>(seconds));
                record("setLogoutTimeout seconds=" + std::to_string(seconds));
            }
            if (key == 'D') {
                record(identity() + " busy main START seconds=30");
                const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
                while (std::chrono::steady_clock::now() < until)
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                record(identity() + " busy main END");
            }
        } else if (s.mode == "keepawake" && key == 'K') {
            setKeepScreenOn(!getKeepScreenOn());
            record(identity() + " keepScreenOn=" + std::to_string(getKeepScreenOn()));
        }
    }
    void exit() override {
        ++state().exited;
        record(identity() + " exit count=" + std::to_string(++exitCount_));
    }
    void cleanup() override {
        ++state().cleaned;
        record(identity() + " cleanup count=" + std::to_string(++cleanupCount_));
    }
};
} // namespace

int main(int argc, char** argv) {
    auto& s = state();
    s.logPath = probeLogPath();
    s.mode = argc == 2 ? argv[1] : "";
    if (s.mode != "drop" && s.mode != "cursor" && s.mode != "close"
            && s.mode != "session" && s.mode != "keepawake") {
        std::printf("Usage: an2DeviceProbe {drop|cursor|close|session|keepawake}\nLog: %s\n", pathToUtf8(s.logPath).c_str());
        return 0;
    }
#ifdef _WIN32
    // Shared append: setLogFile opens the same path again.
    s.file = _wfsopen(s.logPath.c_str(), L"a", _SH_DENYNO);
#else
    s.file = std::fopen(s.logPath.c_str(), "a");
#endif
    if (!s.file) { std::perror("Cannot append probe log"); return 1; }
    if (!setLogFile(s.logPath)) { std::fclose(s.file); s.file = nullptr; return 1; }
    std::atexit(processExit);
    record("BEGIN mode=" + s.mode + " log=" + pathToUtf8(s.logPath));
    // Runtime runs first, so one veto covers the process-wide request while
    // every App still logs its own delivery. The RAII listener survives runApp.
    auto runtimeListener = runtime().events().exitRequested.listen([](ExitRequestEventArgs& e) {
        auto& current = state();
        const bool veto = current.mode == "close" && current.vetoNext;
        if (veto) { e.cancel = true; current.vetoNext = false; }
        record("Runtime exitRequested args.reason=" + e.reason + " veto=" + std::to_string(veto)
            + " vetoNext=" + std::to_string(current.vetoNext));
    });
    WindowSettings settings;
    settings.setSize(780, 480).setTitle("device-239-probe " + s.mode + " A");
    return runApp<ProbeApp>(settings);
}
