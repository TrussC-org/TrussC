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
#include "../../common/tcDisplayTest.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#ifdef _WIN32
#include <process.h>
#elif !defined(__EMSCRIPTEN__)
#include <sys/wait.h>
#include <unistd.h>
#endif

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

#ifndef __EMSCRIPTEN__
// A successful AppKit launch terminates the process. Keep the returning
// startup-failure checks in the parent and exercise success in a child.
int runWindowChild() {
    const std::string path = tc::getExecutablePath().string();
    const char* args[] = {path.c_str(),
#ifdef TC_CORE_TEST_NAME
        TC_CORE_TEST_NAME,
#endif
        "--window-child", nullptr};
#ifdef _WIN32
    return static_cast<int>(_spawnv(_P_WAIT, path.c_str(), args));
#else
    const pid_t child = fork();
    if (child == 0) {
        execv(path.c_str(), const_cast<char* const*>(args));
        std::_Exit(127);
    }
    if (child < 0) return -1;
    int status = 0;
    pid_t waited;
    do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
    return waited == child && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}
#endif
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
    if (argc > 1 && std::strcmp(argv[1], "--window-child") == 0) {
        openWindow = true;
        return tcCoreTest::runDisplayTest<StartupApp>(settings, failures, [] {
            check(tc::internal::appExitCode() == 0, "normal shutdown selects 0");
            check(setupRan && exitRan, "normal launch calls setup and exit");
        });
    }
    // runapp-verdict: returning-startup-failure -- startupTestRun never enters AppKit.
    check(tc::runApp<StartupApp>(settings) == 1, "failed startup returns 1");
    check(!setupRan && !exitRan, "failed startup never enters App lifecycle");
    check(sawStartupError, "startup error reaches Logger before setup");
    if (argc > 1 && std::strcmp(argv[1], "--glx-failure") == 0) {
#if defined(__linux__) && defined(SOKOL_GLES3)
        std::puts("SKIP: GLX extension failure mode does not apply to EGL");
#else
        openWindow = true;
        // runapp-verdict: returning-startup-failure -- GLX fails before App setup.
        check(tc::runApp<StartupApp>(settings) == 1, "real GLX startup failure returns 1");
        check(!setupRan && !exitRan, "real GLX failure skips App setup and exit");
        check(sawGlxError, "real GLX startup diagnostic reaches Logger");
#endif
    }
    if (argc > 1 && std::strcmp(argv[1], "--window") == 0) {
#ifndef __EMSCRIPTEN__
        check(runWindowChild() == 0, "normal window child exits 0");
#endif
        openWindow = false;
        setupRan = exitRan = false;
        tc::internal::appSetupCalled() = true; // Simulate the preceding successful setup.
        // runapp-verdict: returning-startup-failure -- the mock platform returns without init.
        check(tc::runApp<StartupApp>(settings) == 1, "failed start after success returns 1");
        check(!setupRan && !exitRan, "second failed start does not enter App lifecycle");
    }
#ifdef TC_HOT_RELOAD_BUILD
    openWindow = false;
    setupRan = exitRan = false;
    tc::internal::appSetupCalled() = true; // A previous setup must not mask failure.
    // runapp-verdict: returning-startup-failure -- the hot reload launcher uses the same mock.
    check(TC_RUN_APP(StartupApp, settings) == 1, "hot reload failed startup returns 1");
    check(!tc::internal::appSetupCalled() && !setupRan && !exitRan,
          "hot reload resets setup status and skips App lifecycle");
#endif
    return failures ? 1 : 0;
}
