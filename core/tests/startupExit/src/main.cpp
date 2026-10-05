// #394: exercise the real runApp return path with a failed platform startup.
// --window additionally runs a real window through setup and normal shutdown.
#include "sokol/sokol_app_tc.h"
namespace {
void startupTestRun(const sapp_desc* desc);
void (*const platformRun)(const sapp_desc*) = sapp_run;
}
// Only this translation unit's launcher uses the test platform entry point.
#define sapp_run startupTestRun
#include <TrussC.h>
#undef sapp_run
#include "../../common/tcCoreTest.h"

#include <cstdio>
#include <cstring>

namespace {
bool openWindow = false;
bool setupRan = false;
bool exitRan = false;
int failures = 0;

void startupTestRun(const sapp_desc* desc) {
    if (openWindow) {
        platformRun(desc);
        return;
    }
    // Simulate a platform startup diagnostic before init has ever run.
    desc->logger.func("sapp", 1, 0, "forced startup failure", 0, nullptr,
                      desc->logger.user_data);
    // A failed platform startup returns without calling init or cleanup.
}

struct StartupApp : tc::App {
    void setup() override { setupRan = true; tc::exitApp(); }
    void exit() override { exitRan = true; }
};

void check(bool ok, const char* label) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
    bool sawStartupError = false;
    auto listener = tc::getLogger().onLog.listen([&](tc::LogEventArgs& e) {
        if (e.level == tc::LogLevel::Error &&
            e.message.find("forced startup failure") != std::string::npos) {
            sawStartupError = true;
        }
    });
    tc::WindowSettings settings;
    settings.width = 64;
    settings.height = 64;
    check(tc::runApp<StartupApp>(settings) == 1, "failed startup returns 1");
    check(!setupRan && !exitRan, "failed startup never enters App lifecycle");
    check(sawStartupError, "startup error reaches Logger before setup");
    if (argc > 1 && std::strcmp(argv[1], "--window") == 0) {
        openWindow = true;
        check(tc::runApp<StartupApp>(settings) == 0, "normal shutdown returns 0");
        check(setupRan && exitRan, "normal launch calls setup and exit");
        openWindow = false;
        setupRan = exitRan = false;
        check(tc::runApp<StartupApp>(settings) == 1, "failed start after success returns 1");
        check(!setupRan && !exitRan, "second failed start does not enter App lifecycle");
    }
    return failures ? 1 : 0;
}
