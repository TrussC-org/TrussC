// #394: exercise the real runApp return path with a failed platform startup.
// --window additionally runs a real window through setup and normal shutdown.
// --glx-failure exercises real Linux startup with GLX disabled on the X server.
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
    bool sawGlxError = false;
    auto listener = tc::getLogger().onLog.listen([&](tc::LogEventArgs& e) {
        if (e.level == tc::LogLevel::Error &&
            e.message.find("forced startup failure") != std::string::npos) {
            sawStartupError = true;
        }
        if (e.level == tc::LogLevel::Error &&
            e.message.find("GLX extension not present") != std::string::npos) {
            sawGlxError = true;
        }
    });
    tc::WindowSettings settings;
    settings.width = 64;
    settings.height = 64;
    check(tc::runApp<StartupApp>(settings) == 1, "failed startup returns 1");
    check(!setupRan && !exitRan, "failed startup never enters App lifecycle");
    check(sawStartupError, "startup error reaches Logger before setup");
    if (argc > 1 && std::strcmp(argv[1], "--glx-failure") == 0) {
        openWindow = true;
        check(tc::runApp<StartupApp>(settings) == 1, "real GLX startup failure returns 1");
        check(!setupRan && !exitRan, "real GLX failure skips App setup and exit");
        check(sawGlxError, "real GLX startup diagnostic reaches Logger");
    }
    if (argc > 1 && std::strcmp(argv[1], "--window") == 0) {
        openWindow = true;
        check(tc::runApp<StartupApp>(settings) == 0, "normal shutdown returns 0");
        check(setupRan && exitRan, "normal launch calls setup and exit");
        openWindow = false;
        setupRan = exitRan = false;
        check(tc::runApp<StartupApp>(settings) == 1, "failed start after success returns 1");
        check(!setupRan && !exitRan, "second failed start does not enter App lifecycle");
    }
#ifdef TC_HOT_RELOAD_BUILD
    openWindow = false;
    setupRan = exitRan = false;
    tc::internal::appSetupCalled = true; // A previous setup must not mask failure.
    check(TC_RUN_APP(StartupApp, settings) == 1, "hot reload failed startup returns 1");
    check(!tc::internal::appSetupCalled && !setupRan && !exitRan,
          "hot reload resets setup status and skips App lifecycle");
#endif
    return failures ? 1 : 0;
}
