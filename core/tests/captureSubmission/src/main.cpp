#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
using namespace std;
using namespace tc;
namespace {
int failures = 0;
fs::path sandbox;
atomic<bool> exitWriteFailed{false};
EventListener exitLog;
bool exitRequested = false;
void check(const char* label, bool ok) {
    printf("%s: %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
struct ScreenshotApp : App {
    EventListener listener;
    int saved = 0;
    bool requested = false;
    void setup() override {
        listener = onScreenshotSave().listen([this](ScreenshotSaveEventArgs& args) {
            check("shot completes on main and makes a decodable file", isMainThread() && args.result && fs::exists(args.path));
            Pixels pixels;
            check("shot contains the presented frame", pixels.load(args.path) && pixels.getWidth() == getFramebufferWidth() && pixels.getHeight() == getFramebufferHeight() && pixels.getData()[0] == 255);
            if (++saved == 5) {
                exitRequested = true;
                saveScreenshotAsync(sandbox / "exit.png");
                exitApp();
            }
        });
    }
    void draw() override {
        clear(1, 0, 0);
        if (!requested) {
            requested = true;
            // A small batch also exercises the writer queue without a four-shot ceiling.
            for (int i = 0; i < 5; ++i) saveScreenshotAsync(sandbox / (to_string(i) + ".png"));
        }
    }
    void cleanup() override { check("five screenshot completions before exit", saved == 5); }
};
}
TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc < 2) { printf("SKIP: use --gpu-check with a display (Xvfb supported)\n"); return 0; }
    sandbox = fs::absolute("screenshot-smoke");
    fs::remove_all(sandbox);
    fs::create_directories(sandbox);
    exitLog = getLogger().onLog.listen([](LogEventArgs& args) {
        if (args.level == LogLevel::Error &&
            args.message.find((sandbox / "exit.png").string()) != string::npos)
            exitWriteFailed = true;
    });
    // Register before runApp creates the resource host: atexit runs in reverse
    // order, so this checks after both app shutdown and process worker drain,
    // including on platforms where runApp never returns.
    atexit([] {
        const bool reportedFailure = exitWriteFailed.load();
        Pixels pixels;
        const bool written = fs::exists(sandbox / "exit.png") && pixels.load(sandbox / "exit.png") &&
            pixels.getWidth() == 32 && pixels.getHeight() == 32;
        printf("exit.png disposition: %s\n", written ? "written" : reportedFailure ? "reported failure" : "missing");
        check("exit drains screenshot work", exitRequested && (written || reportedFailure));
        fs::remove_all(sandbox);
        fflush(nullptr);
        if (failures) std::_Exit(1);
    });
    WindowSettings settings; settings.setSize(32, 32); settings.setHighDpi(false);
    if (argc > 2 && string(argv[2]) == "--msaa4") settings.sampleCount = 4;
    return tcCoreTest::runDisplayTest<ScreenshotApp>(settings, failures);
}
