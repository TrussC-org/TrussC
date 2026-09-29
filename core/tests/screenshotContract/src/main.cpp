// =============================================================================
// core/tests/screenshotContract — behavioral regression test for #230.
//
// Headless, console, exit code = pass/fail. build_all.py runs it natively
// (--core-tests-only). The web half runs only in a WebAssembly build of this
// project, started under node (Emscripten's default environment includes
// node): trusscli update -p <this dir> --ide cmake --web, then
// emcmake cmake -S . -B build-web, cmake --build build-web, and
// node bin/screenshotContract.js. No canvas or GPU is needed on either side:
// nothing here renders.
//
// Guards the return-value contract of the screenshot APIs:
//   - Web: capture is not implemented, so grabScreen() and saveScreenshot()
//     return false, saveScreenshot() queues nothing (and creates no folder),
//     and each API warns once, not once per call. Before #230 the web
//     saveScreenshot() queued the path and returned true, although no file
//     was ever written.
//   - Native: saveScreenshot() still prepares the destination, queues the
//     capture for the afterFrame drain and returns true, i.e. the web early
//     return does not leak into native builds. grabScreen() is not called
//     here: it reads the framebuffer back immediately, and a console test
//     has no framebuffer.
//
// Only the web half fails without the #230 fix; the native half passes on
// the pre-#230 code too, since #230 did not change native behaviour. CI runs
// only the native half, so CI does not notice if the web fix is undone. Run
// the web half by hand after touching the web screenshot path.
// =============================================================================

#include <TrussC.h>

#include <cstdio>
#include <string>

using namespace std;
using namespace tc;

static int g_fail = 0;
static void check(const char* name, bool ok) {
    std::printf("%-64s %s\n", name, ok ? "PASS" : "FAIL");
    std::fflush(stdout);   // flush per line so CI logs survive a later crash
    if (!ok) ++g_fail;
}

int main() {
    // Count the "[Screenshot]" warnings/errors the calls below log.
    int screenshotWarnings = 0;
    int screenshotErrors = 0;
    EventListener logListener = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.message.rfind("[Screenshot]", 0) != 0) return;
        if (e.level == LogLevel::Warning) ++screenshotWarnings;
        if (e.level >= LogLevel::Error) ++screenshotErrors;
    });

    auto& queue = internal::currentWindowContext().pendingScreenshotPaths;
    check("capture queue starts empty", queue.empty());

#ifdef __EMSCRIPTEN__
    std::printf("-- web build (running under node) --\n");

    // Each API warns once, not once per call, so an app calling these every
    // frame does not flood the browser console.
    Pixels px;
    check("grabScreen() returns false", !grabScreen(px));
    check("grabScreen() returns false again", !grabScreen(px));
    check("grabScreen() warned exactly once", screenshotWarnings == 1);

    // Count saveScreenshot()'s own warnings. A running total would pass by
    // accident on the pre-#230 code, where grabScreen() warned on every call
    // (2) and saveScreenshot() never warned (0).
    const int warningsBeforeSave = screenshotWarnings;
    check("saveScreenshot(relative) returns false", !saveScreenshot("shot.png"));
    check("saveScreenshot(absolute) returns false",
          !saveScreenshot("/tmp/tc_screenshotContract/shot.png"));
    check("saveScreenshot(nested relative) returns false",
          !saveScreenshot("sub/dir/shot.jpg"));
    check("saveScreenshot() warned exactly once",
          screenshotWarnings - warningsBeforeSave == 1);

    check("nothing was queued", queue.empty());
    check("no destination folder was created (absolute)",
          !fs::exists("/tmp/tc_screenshotContract"));
    check("no destination folder was created (relative)",
          !fs::exists(getDataPath("sub")));
    check("no screenshot errors logged", screenshotErrors == 0);
#else
    std::printf("-- native build --\n");

    // Sandbox under the OS temp dir; the nested folder does not exist yet, so
    // saveScreenshot() has to create it.
    const fs::path sandbox = fs::temp_directory_path() / "tc_screenshotContract_test";
    std::error_code ec;
    fs::remove_all(sandbox, ec);
    const fs::path target = sandbox / "sub" / "shot.png";

    check("saveScreenshot(absolute) returns true", saveScreenshot(target));
    check("exactly one capture was queued", queue.size() == 1);
    check("the queued path is the requested one",
          queue.size() == 1 && queue.front() == target);
    check("the destination folder was created", fs::is_directory(target.parent_path()));
    check("no screenshot warnings or errors logged",
          screenshotWarnings == 0 && screenshotErrors == 0);

    // No frame loop runs here, so nothing drains the queue: drop it by hand.
    queue.clear();
    fs::remove_all(sandbox, ec);
#endif

    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED",
                g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
