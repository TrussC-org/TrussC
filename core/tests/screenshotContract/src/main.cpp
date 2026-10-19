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
#include <stdexcept>

using namespace std;
using namespace tc;

namespace {

static int g_fail = 0;
static void check(const char* name, bool ok) {
    std::printf("%-64s %s\n", name, ok ? "PASS" : "FAIL");
    std::fflush(stdout);   // flush per line so CI logs survive a later crash
    if (!ok) ++g_fail;
}

#ifndef __EMSCRIPTEN__
int drainCaptureCount = 0;
// Exercise the real drain and native encoder without a window/GPU.
bool captureDrainFile(const fs::path& path) {
    ++drainCaptureCount;
    if (path.filename() == "throw.png")
        throw std::runtime_error("screenshot drain test write failed");
    if (path.filename() == "append.png")
        logNotice("Screenshot") << "screenshot drain test append";
    Pixels pixels;
    pixels.allocate(2, 2, 4);
    return internal::writeScreenshotPixels(pixels, path).ok();
}

void drainWithoutEscape() {
    bool escaped = false;
    try { internal::drainPendingScreenshots(captureDrainFile); }
    catch (const std::exception&) { escaped = true; }
    check("queued write exception does not escape drain", !escaped);
}
#endif

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

    // No GPU is initialized: rejection must happen before any backend call,
    // including update nested in an otherwise open draw phase.
    Pixels outside;
    outside.allocate(1, 1, 4);
    outside.getData()[0] = 73;
    std::string phaseWarning;
    bool reentryRejected = false;
    auto phaseLog = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.level != LogLevel::Warning || e.message.rfind("[Screenshot]", 0) != 0) return;
        phaseWarning = e.message;
        reentryRejected = !grabScreen(outside);
    });
    {
        internal::EntryStackGuard draw(internal::AppEntry::Draw);
        internal::EntryStackGuard update(internal::AppEntry::Update);
        check("update inside a frame returns false", !grabScreen(outside));
        check("repeated update returns false", !grabScreen(outside));
    }
    check("update emits exactly one Warning and no Error", screenshotWarnings == 1 && screenshotErrors == 0);
    check("Warning points to afterFrame and saveScreenshot", phaseWarning.find("events().afterFrame") != std::string::npos &&
          phaseWarning.find("saveScreenshot()") != std::string::npos);
    check("Warning reentry returns false without another Warning", reentryRejected && screenshotWarnings == 1);
    for (auto phase : {internal::AppEntry::Setup, internal::AppEntry::Prelude,
                       internal::AppEntry::Event, internal::AppEntry::Exit}) {
        internal::EntryStackGuard guard(phase);
        check("other non-frame phase returns false", !grabScreen(outside));
    }
    check("unscoped call returns false after guard restoration", !grabScreen(outside));
    check("rejection preserves output Pixels", outside.getWidth() == 1 && outside.getData()[0] == 73);
    check("all outside calls share one Warning", screenshotWarnings == 1 && screenshotErrors == 0);
    phaseLog = EventListener{};
    // A different window gets its own warning, without inheriting A's Draw.
    {
        internal::EntryStackGuard draw(internal::AppEntry::Draw);
        internal::WindowContext otherWindow;
        internal::RenderContext otherRender;
        otherWindow.render = &otherRender;
        auto* previous = internal::currentWindowCtx();
        internal::restoreWindowContext(&otherWindow);
        check("another window does not inherit the draw phase", !grabScreen(outside));
        check("another window repeats without another Warning", !grabScreen(outside));
        internal::restoreWindowContext(previous);
    }
    check("warning is once per window", screenshotWarnings == 2 && screenshotErrors == 0);
    const int warningsBeforeSave = screenshotWarnings;

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
          screenshotWarnings == warningsBeforeSave && screenshotErrors == 0);

    queue.clear();

    std::string lastError;
    auto errorListener = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.level == LogLevel::Error && e.message.rfind("[Screenshot]", 0) == 0)
            lastError = e.message;
    });
    check("failing write still returns queued", saveScreenshot(sandbox / "throw.png"));
    check("following write is queued", saveScreenshot(target));
    const int errorsBeforeDrain = screenshotErrors;
    drainWithoutEscape();
    Pixels written;
    check("save after throwing write completes", fs::exists(target) && written.load(target).ok());
    check("throwing write logs exactly one Screenshot Error with what()",
          screenshotErrors == errorsBeforeDrain + 1 &&
          lastError == "[Screenshot] screenshot drain test write failed");
    check("original requests are removed after throwing write", queue.empty());
    queue.clear(); // Keep the append regression independent if the first fails.

    const auto appended = sandbox / "next.png";
    const int errorsBeforeAppend = screenshotErrors;
    bool enqueuedFromLog = false;
    auto appendListener = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.message == "[Screenshot] screenshot drain test append")
            enqueuedFromLog = saveScreenshot(appended);
    });
    // Spare capacity lets the unfixed drain fail by dropping the append,
    // without depending on undefined behavior from a vector reallocation.
    queue.reserve(2);
    check("log-producing write is queued", saveScreenshot(sandbox / "append.png"));
    drainWithoutEscape();
    check("log listener queues another screenshot", enqueuedFromLog);
    check("initial log-producing write completes", fs::exists(sandbox / "append.png"));
    check("appended request survives the first drain",
          queue.size() == 1 && queue.front() == appended && !fs::exists(appended));
    drainWithoutEscape();
    check("next drain writes the appended request", fs::exists(appended) && written.load(appended).ok());
    check("next drain empties the queue", queue.empty());
    check("append regression logs no additional errors", screenshotErrors == errorsBeforeAppend);

    // Async capture runs before the legacy save loop. Its log callback must
    // also leave a newly queued save for the next drain.
    const auto fromAsyncError = sandbox / "from-async-error.png";
    auto previousRun = internal::appRunToken();
    internal::appRunToken() = std::make_shared<std::atomic<bool>>(true);
    int asyncCaptureCount = 0;
    bool enqueuedFromAsyncError = false;
    auto asyncErrorListener = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.level == LogLevel::Error &&
            e.message == "[Screenshot] screenshot drain test async capture failed")
            enqueuedFromAsyncError = saveScreenshot(fromAsyncError);
    });
    internal::setScreenshotBackendForTests([&](Pixels&) {
        ++asyncCaptureCount;
        logError("Screenshot") << "screenshot drain test async capture failed";
        return false;
    }, {});
    auto& asyncQueue = internal::currentWindowContext().asyncScreenshots;
    check("legacy queue is empty before async capture regression", queue.empty());
    internal::submitScreenshot(&asyncQueue, sandbox / "async.png");
    check("async screenshot is admitted before drain",
          asyncQueue.pending.size() == 1 && *asyncQueue.admitted == 1);
    const int capturesBeforeAsync = drainCaptureCount;
    drainWithoutEscape();
    check("failing async backend runs and log listener queues save",
          asyncCaptureCount == 1 && enqueuedFromAsyncError);
    check("async log-enqueued save is not captured in the same drain",
          drainCaptureCount == capturesBeforeAsync && !fs::exists(fromAsyncError));
    check("async log-enqueued save remains queued for next drain",
          queue.size() == 1 && queue.front() == fromAsyncError);
    drainWithoutEscape();
    check("next drain captures async log-enqueued save exactly once",
          drainCaptureCount == capturesBeforeAsync + 1 &&
          fs::exists(fromAsyncError) && written.load(fromAsyncError).ok());
    check("next drain empties async log-enqueued save queue", queue.empty());
    internal::deliverResourceLoadBatch(internal::takeResourceLoadBatch());
    check("failed async capture releases admission", *asyncQueue.admitted == 0);
    internal::setScreenshotBackendForTests({}, {});
    internal::appRunToken() = previousRun;
    fs::remove_all(sandbox, ec);
#endif

    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED",
                g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
