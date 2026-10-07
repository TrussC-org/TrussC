// =============================================================================
// core/tests/screenshotContract — screenshot contracts for #230 and #298.
// Native: deferred filesystem capture. Web (under node): deferred canvas
// download with a mock DOM/toBlob, including asynchronous error paths.
// Neither run needs a GPU; actual browser rendering is a separate check.
// =============================================================================

#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <cstdio>
#include <string>

using namespace std;
using namespace tc;

namespace {

static int g_fail = 0;
static void check(const char* name, bool ok) {
    std::printf("%-64s %s\n", name, ok ? "PASS" : "FAIL");
    std::fflush(stdout);   // flush per line so CI logs survive a later crash
    if (!ok) ++g_fail;
}

} // namespace

TC_CORE_TEST_MAIN() {
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

    // Synchronous pixel capture remains unsupported and warns only once.
    Pixels px;
    check("grabScreen() returns false", !grabScreen(px));
    check("grabScreen() returns false again", !grabScreen(px));
    check("grabScreen() warned exactly once", screenshotWarnings == 1);

    // Mock only the browser boundary; exercise the real queue and EM_JS glue.
    EM_ASM({
        Module.screenshotTest = ({ calls: [], downloads: [], callbacks: [],
                                   revoked: [], mode: 'ok', attached: 0 });
        const t = Module.screenshotTest;
        Module.canvas = { toBlob: (callback, mime) => {
            if (t.mode === 'tainted') throw new Error('SecurityError');
            t.calls.push(mime);
            t.callbacks.push(callback);
        } };
        globalThis.document = ({
            body: { appendChild: () => { ++t.attached; } },
            createElement: () => ({
                click: function() {
                    if (t.mode === 'click-error') throw new Error('download failed');
                    t.downloads.push(this.download);
                },
                remove: () => { --t.attached; }
            })
        });
        URL.createObjectURL = () => 'blob:screenshot-test';
        URL.revokeObjectURL = (url) => t.revoked.push(url);
        t.finish = (blob) => {
            const originalTimeout = globalThis.setTimeout;
            const cleanup = [];
            globalThis.setTimeout = (callback) => { cleanup.push(callback); };
            try { t.callbacks.shift()(blob); }
            finally { globalThis.setTimeout = originalTimeout; }
            cleanup.forEach((callback) => callback());
        };
    });
    const int warningsBeforeSave = screenshotWarnings;
    check("saveScreenshot(relative) queues download", saveScreenshot("shot.png"));
    check("saveScreenshot(absolute) queues download",
          saveScreenshot("/tmp/tc_screenshotContract/画像.JpEg"));
    check("saveScreenshot(nested relative) queues download",
          saveScreenshot("sub/dir/shot.jpg"));
    check("saveScreenshot(empty) queues download", saveScreenshot(""));
    check("saveScreenshot(empty filename) queues download", saveScreenshot("sub/"));
    check("only filenames are queued", queue.size() == 5 && queue[0] == "shot.png" &&
          queue[1] == "画像.JpEg" && queue[2] == "shot.jpg");
    check("empty names get timestamped PNG defaults", queue.size() == 5 &&
          queue[3].string().find("screenshot-") == 0 && queue[3].extension() == ".png" &&
          queue[3].string().size() > 20 && queue[4].string().find("screenshot-") == 0);
    check("no destination folder was created (absolute)",
          !fs::exists("/tmp/tc_screenshotContract"));
    check("no destination folder was created (relative)",
          !fs::exists(getDataPath("sub")));
    check("capture is deferred", EM_ASM_INT({ return Module.screenshotTest.calls.length === 0; }));
    internal::drainPendingScreenshots();
    check("capture queue drained", queue.empty());
    check("toBlob runs at drain, downloads wait for callbacks", EM_ASM_INT({
        const t = Module.screenshotTest;
        return t.calls.join(',') === 'image/png,image/jpeg,image/jpeg,image/png,image/png' &&
               t.downloads.length === 0;
    }));
    EM_ASM({
        const t = Module.screenshotTest;
        while (t.callbacks.length) t.finish({});
    });
    check("callbacks retain UTF-8 filenames and release resources", EM_ASM_INT({
        const t = Module.screenshotTest;
        return t.downloads.length === 5 && t.downloads[0] === 'shot.png' &&
               t.downloads[1] === '画像.JpEg' && t.downloads[2] === 'shot.jpg' &&
               t.attached === 0 && t.revoked.length === 5;
    }));
    check("successful saves log no warnings or errors",
          screenshotWarnings == warningsBeforeSave && screenshotErrors == 0);

    check("unsupported extension queues PNG", saveScreenshot("sub/shot.bmp"));
    check("unsupported extension appends .png", queue.size() == 1 && queue[0] == "shot.bmp.png");
    check("extension fallback warns once", screenshotWarnings == warningsBeforeSave + 1);
    internal::drainPendingScreenshots();
    EM_ASM({ Module.screenshotTest.finish({}); });
    check("fallback is PNG without duplicate suffix", EM_ASM_INT({
        const t = Module.screenshotTest;
        return t.calls[5] === 'image/png' && t.downloads[5] === 'shot.bmp.png';
    }));
    check("missing extension queues PNG", saveScreenshot("shot"));
    check("missing extension appends .png", queue.size() == 1 && queue[0] == "shot.png");
    queue.clear();

    EM_ASM({ Module.screenshotTest.mode = 'tainted'; });
    check("tainted canvas request still returns true", saveScreenshot("tainted.png"));
    internal::drainPendingScreenshots();
    check("tainted canvas logs an error and drains queue", screenshotErrors == 1 && queue.empty());

    EM_ASM({ Module.screenshotTest.mode = 'ok'; });
    check("null blob request returns true", saveScreenshot("null.png"));
    internal::drainPendingScreenshots();
    check("encoding has not completed yet", screenshotErrors == 1);
    EM_ASM({ Module.screenshotTest.finish(null); });
    check("null blob logs an asynchronous error", screenshotErrors == 2);

    check("failed download request returns true", saveScreenshot("click.png"));
    internal::drainPendingScreenshots();
    EM_ASM({
        Module.screenshotTest.mode = 'click-error';
        Module.screenshotTest.finish({});
    });
    check("failed download logs an asynchronous error", screenshotErrors == 3);
    check("failed download releases resources", EM_ASM_INT({
        const t = Module.screenshotTest;
        return t.attached === 0 && t.revoked.length === 7 && t.downloads.length === 6;
    }));
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
