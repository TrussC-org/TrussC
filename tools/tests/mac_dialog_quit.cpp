// Manual macOS sheet-quit probe; see .verification/sheetquit/AIR.md.
#include <TrussC.h>
#include <csignal>
#include <cstdlib>
#include <string>
using namespace tc;
namespace {
std::string dialog = "alert", action = "request";
int callbacks = 0, requests = 0, exits = 0;
bool didCleanup = false;
void check(bool ok, const char* label) {
    logNotice("sheetquit") << label << ": " << (ok ? "PASS" : "FAIL");
    if (!ok) std::_Exit(1);
}
void completed(bool cancelled = true) {
    ++callbacks;
    check(cancelled && callbacks == 1 && exits == 0 && !didCleanup,
          "cancel callback once before App exit/cleanup");
}
struct Probe : App {
    int frames = 0;
    EventListener quit;
    void setup() override {
        quit = events().exitRequested.listen([](ExitRequestEventArgs& args) {
            ++requests;
            check(callbacks == 0, "sheet pending during exitRequested");
            if (action == "veto" && requests == 1) {
                args.cancel = true;
                logNotice("sheetquit") << "veto: sheet must remain visible";
            }
        });
    }
    void update() override {
        ++frames;
        if (frames == 1) {
            if (dialog == "alert") alertDialogAsync("sheetquit", "Leave this sheet open", [] { completed(); });
            else if (dialog == "confirm") confirmDialogAsync("sheetquit", "Leave this sheet open", [](bool yes) { completed(!yes); });
            else if (dialog == "load") loadDialogAsync("sheetquit", "Leave this sheet open", {}, false,
                [](const FileDialogResult& result) { completed(!result.success && result.filePath.empty() && result.fileName.empty()); });
            else if (dialog == "save") saveDialogAsync("sheetquit", "Leave this sheet open", {}, "sheetquit.txt",
                [](const FileDialogResult& result) { completed(!result.success && result.filePath.empty() && result.fileName.empty()); });
            else std::_Exit(2);
            logNotice("sheetquit") << "sheet opened";
        }
        if (frames == 60) {
            if (action == "exit") exitApp();
            else if (action == "sigterm") std::raise(SIGTERM);
            else if (action == "request" || action == "veto") requestExitApp();
            // manual: send SIGTERM externally, Cmd+Q, or perform supervised logout.
        }
        if (action == "veto" && frames == 120) {
            check(callbacks == 0 && requests == 1, "veto kept callback pending");
            requestExitApp();
        }
    }
    void exit() override {
        ++exits;
        check(callbacks == 1 && exits == 1, "one cancel callback before one App exit");
    }
    void cleanup() override {
        didCleanup = true;
        check(callbacks == 1 && exits == 1, "cleanup after cancellation and App exit");
    }
};
} // namespace
int main(int argc, char** argv) {
    if (argc > 1) dialog = argv[1];
    if (argc > 2) action = argv[2];
    WindowSettings settings;
    settings.setSize(480, 320);
    return runApp<Probe>(settings);
}
