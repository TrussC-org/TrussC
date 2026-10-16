#include <TrussC.h>
#include "tc/sound/tcMicInputInternal.h"
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <thread>
#if defined(__linux__) && defined(SOKOL_GLCORE) && !defined(__ANDROID__)
#include <X11/Xlib.h>
#endif
#ifndef _WIN32
#include <sys/wait.h>
#endif

using namespace std;
using namespace tc;
namespace {
int failures = 0;
void check(const char* label, bool ok) {
    printf("%-78s %s\n", label, ok ? "PASS" : "FAIL"); fflush(stdout);
    if (!ok) ++failures;
}
struct Gate {
    mutex guard;
    condition_variable changed;
    int arrivals = 0;
    bool released = false;
    void arrive() { lock_guard<mutex> lock(guard); ++arrivals; changed.notify_all(); }
    void wait(int count = 1) {
        unique_lock<mutex> lock(guard);
        if (!changed.wait_for(lock, chrono::seconds(60), [&] { return arrivals >= count; })) {
            fprintf(stderr, "Gate watchdog: %d/%d arrivals\n", arrivals, count); std::_Exit(5);
        }
    }
    void block() {
        unique_lock<mutex> lock(guard); ++arrivals; changed.notify_all();
        changed.wait(lock, [&] { return released; });
    }
    void open() { lock_guard<mutex> lock(guard); released = true; changed.notify_all(); }
};
void drain() { internal::deliverResourceLoadBatch(internal::takeResourceLoadBatch()); }
void finish() { internal::waitForResourceLoadsForTests(); drain(); }
void newRun() {
    if (auto token = internal::appRunToken()) token->store(false);
    internal::cancelResourceBackends();
    internal::appRunToken() = make_shared<atomic<bool>>(true);
}
fs::path sandbox;
#ifndef __EMSCRIPTEN__
void screenshotAliasTests() {
    newRun();
    const auto real = sandbox / "real";
    const auto alias = sandbox / "alias";
    fs::create_directories(real);
    std::error_code ec;
    fs::create_directory_symlink(real, alias, ec);
    if (ec) { printf("SKIP directory symlink: %s\n", ec.message().c_str()); return; }
    internal::ScreenshotQueue queue;
    vector<ScreenshotSaveEventArgs> events;
    auto listener = onScreenshotSave().listen([&](ScreenshotSaveEventArgs& args) { events.push_back(args); });
    int captures = 0, launches = 0;
    Gate firstWriter;
    internal::setResourceLoadProbeForTests([&](const char* stage, const fs::path&) {
        if (string(stage) == "screenshot-writer-launch") ++launches;
    });
    internal::setScreenshotBackendForTests([&](Pixels& pixels) {
        pixels.allocate(1, 1, 4); pixels.getData()[0] = ++captures; return true;
    }, [&](const Pixels& pixels, const fs::path& path) {
        if (pixels.getData()[0] == 1) firstWriter.block();
        return internal::writeScreenshotPixels(pixels, path);
    });
    const auto a = real / "shot.png", b = alias / "shot.png";
    internal::submitScreenshot(&queue, a); internal::captureScreenshots(queue); firstWriter.wait();
    internal::submitScreenshot(&queue, b); internal::captureScreenshots(queue);
    check("directory aliases launch only one destination writer", launches == 1);
    firstWriter.open(); finish();
    Pixels image; const bool loaded = image.load(a).ok();
    check("symlink alias A then B leaves B on disk", loaded && image.getData()[0] == 2 &&
        events.size() == 2 && events[0].result && events[1].result);
    check("alias completion retains each submitted output path", events.size() == 2 && events[0].path == a && events[1].path == b);
    // Exercise native filesystem case comparison when the volume supports it.
    const auto upper = real / "SHOT.png";
    if (fs::equivalent(a, upper, ec)) {
        fs::remove(a); // case aliases must share a key before the file exists
        launches = 0; captures = 0; firstWriter.released = false;
        internal::submitScreenshot(&queue, a); internal::captureScreenshots(queue); firstWriter.wait(2);
        internal::submitScreenshot(&queue, upper); internal::captureScreenshots(queue);
        check("case aliases launch only one destination writer", launches == 1);
        firstWriter.open(); finish();
        check("case alias A then B leaves B on disk", image.load(a) && image.getData()[0] == 2);
    } else printf("SKIP case aliases: case-sensitive filesystem\n");
    internal::setResourceLoadProbeForTests({});
    internal::setScreenshotBackendForTests({}, {});

    // Hold two live encoder files for one aliased destination. They must be
    // separate files, and each file must retain its own bytes until retirement.
    fs::path tempA, tempB;
    auto* fileA = internal::openScreenshotTemporary(a, tempA);
    auto* fileB = internal::openScreenshotTemporary(b, tempB);
    check("overlapping screenshot jobs use distinct .tmp files", fileA && fileB &&
        tempA.extension() == ".tmp" && tempB.extension() == ".tmp" && !fs::equivalent(tempA, tempB));
    if (fileA) { std::fwrite("A", 1, 1, fileA); std::fclose(fileA); }
    if (fileB) { std::fwrite("B", 1, 1, fileB); std::fclose(fileB); }
    check("unique temporary files never truncate another job", loadTextFile(tempA) == "A" && loadTextFile(tempB) == "B");
    fs::remove(tempA); fs::remove(tempB);
    const auto previous = loadTextFile(a);
    auto failed = internal::atomicWrite(a, [&](std::FILE* file) {
        auto partial = internal::checkedWrite(file, "partial", 7, a);
        return partial ? SaveResult::fail(SaveError::NoSpace, "full disk during encoding") : partial;
    }, "Screenshot", true);
    check("failed unique encoder preserves existing destination", failed.error == SaveError::NoSpace && loadTextFile(a) == previous);
    bool clean = true;
    for (const auto& entry : fs::directory_iterator(real)) if (entry.path().extension() == ".tmp") clean = false;
    check("successful and failed writers retire unique temporaries", clean);
}

void screenshotFinalSymlinkTests() {
    newRun();
    const auto original = sandbox / "original.png", path = sandbox / "linked-shot.png";
    { ofstream file(original); file << "original"; }
    std::error_code ec;
    fs::create_symlink(original, path, ec);
    if (ec) { printf("SKIP final symlink: %s\n", ec.message().c_str()); return; }
    internal::ScreenshotQueue queue;
    vector<ScreenshotSaveEventArgs> events;
    auto listener = onScreenshotSave().listen([&](ScreenshotSaveEventArgs& args) { events.push_back(args); });
    int captures = 0, launches = 0;
    Gate first, second, thirdWritten;
    mutex records;
    vector<int> order;
    internal::setResourceLoadProbeForTests([&](const char* stage, const fs::path&) {
        if (string(stage) == "screenshot-writer-launch") ++launches;
    });
    internal::setScreenshotBackendForTests([&](Pixels& pixels) {
        pixels.allocate(1, 1, 4); pixels.getData()[0] = ++captures; return true;
    }, [&](const Pixels& pixels, const fs::path& output) {
        const int value = pixels.getData()[0];
        if (value == 1) first.block();
        if (value == 2) second.block();
        auto result = internal::writeScreenshotPixels(pixels, output);
        { lock_guard<mutex> lock(records); order.push_back(value); }
        if (value == 3) thirdWritten.arrive();
        return result;
    });
    internal::submitScreenshot(&queue, path); internal::submitScreenshot(&queue, path);
    internal::captureScreenshots(queue); first.wait(); first.open(); second.wait();
    check("first screenshot replaces final symlink with regular file", !fs::is_symlink(path) && fs::is_regular_file(path));
    internal::submitScreenshot(&queue, path); internal::captureScreenshots(queue);
    check("final symlink replacement keeps a single destination writer", launches == 1);
    // Make the old split-key failure deterministic: C completes before B if
    // it incorrectly launched a second writer. The correct queue waits for B.
    if (launches > 1) thirdWritten.wait();
    second.open(); finish();
    check("final symlink replacement preserves A B C write order", order == vector<int>({1, 2, 3}));
    Pixels image;
    check("A B queued then C after link replacement leaves C on disk", image.load(path) && image.getData()[0] == 3);
    check("final symlink target remains unchanged", loadTextFile(original) == "original");
    check("final symlink captures report three successful original paths", events.size() == 3 &&
        events[0].result && events[1].result && events[2].result &&
        events[0].path == path && events[1].path == path && events[2].path == path);
    internal::setResourceLoadProbeForTests({});
    internal::setScreenshotBackendForTests({}, {});
}

void screenshotTests() {
    newRun();
    vector<ScreenshotSaveEventArgs> events;
    auto listener = onScreenshotSave().listen([&](ScreenshotSaveEventArgs& args) {
        check("screenshot delivery on main", isMainThread());
        events.push_back(args);
    });
    internal::ScreenshotQueue mainQueue, hiddenQueue;
    int captures = 0;
    Gate encoder, independent;
    mutex recordsMutex;
    vector<int> samePath;
    internal::setScreenshotBackendForTests([&](Pixels& pixels) {
        pixels.allocate(1, 1, 4); pixels.getData()[0] = ++captures; return true;
    }, [&](const Pixels& pixels, const fs::path& path) {
        if (path.filename() == "same.png") {
            { lock_guard<mutex> lock(recordsMutex); samePath.push_back(pixels.getData()[0]); }
            if (pixels.getData()[0] == 1) encoder.block();
        } else independent.arrive();
        return SaveResult::success();
    });
    for (int i = 0; i < 4; ++i) internal::submitScreenshot(&hiddenQueue, sandbox / "hidden.png");
    internal::submitScreenshot(&hiddenQueue, sandbox / "overflow.png");
    check("four hidden slots reserved without any pixels", *hiddenQueue.admitted == 4 && captures == 0 && events.empty());
    drain();
    check("fifth request fails through deferred event", events.size() == 1 && events.back().result.error == SaveError::WriteFailed && events.back().result.message == "Screenshot queue is full");
    internal::submitScreenshot(&mainQueue, sandbox / "same.png");
    internal::submitScreenshot(&mainQueue, sandbox / "same.png");
    internal::submitScreenshot(&mainQueue, sandbox / "other.png");
    internal::captureScreenshots(mainQueue);
    encoder.wait(); independent.wait();
    check("independent path progresses while same path is blocked", samePath.size() == 1 && samePath[0] == 1 && captures == 3);
    check("captured jobs keep slots before delivery", *mainQueue.admitted == 3);
    encoder.open(); finish();
    check("same destination writes follow capture order", samePath == vector<int>({1, 2}));
    check("completion releases slots, hidden window stays pending", *mainQueue.admitted == 0 && *hiddenQueue.admitted == 4 && hiddenQueue.pending.size() == 4);
    for (int i = 0; i < 6; ++i) {
        internal::submitScreenshot(&mainQueue, sandbox / ("visible" + to_string(i) + ".png"));
        internal::captureScreenshots(mainQueue); finish();
    }
    check("visible window keeps saving beyond four hidden shots", captures == 9 && *hiddenQueue.admitted == 4);
    internal::closeScreenshots(hiddenQueue);
    check("window close failures are deferred and retain admission", *hiddenQueue.admitted == 4);
    drain();
    check("window close fails its four pending shots", *hiddenQueue.admitted == 0 && !events.back().result);
    // Successful worker completion also retains its admission until delivery.
    for (int i = 0; i < 4; ++i) internal::submitScreenshot(&mainQueue, sandbox / "undelivered.png");
    internal::captureScreenshots(mainQueue); internal::waitForResourceLoadsForTests();
    check("four undelivered jobs still occupy all slots", *mainQueue.admitted == 4);
    internal::submitScreenshot(&mainQueue, sandbox / "fifth.png"); drain();
    check("undelivered cap rejects fifth without capture", captures == 13 && events.back().result.message == "Screenshot queue is full");
    internal::submitScreenshot(&mainQueue, sandbox / "discarded.png");
    internal::captureScreenshots(mainQueue); internal::waitForResourceLoadsForTests();
    auto discarded = internal::takeResourceLoadBatch();
    check("discarded completion retains slot until payload retirement", *mainQueue.admitted == 1);
    discarded.clear();
    check("app-exit discard releases admission without notification", *mainQueue.admitted == 0);
    // Snapshot the destination before changes to cwd/data root.
    internal::submitScreenshot(&mainQueue, "snapshot.xyz");
    setDataPathRoot(sandbox / "different-root");
    internal::captureScreenshots(mainQueue); finish();
    check("submission freezes data root and normalized extension", events.back().path == sandbox / "snapshot.xyz.png");
    setDataPathRoot(sandbox);
    const auto before = events.size();
    saveScreenshotAsync("headless.png");
    check("no-window failure never fires inline", events.size() == before);
    drain(); check("no-window failure is deferred", events.size() == before + 1 && !events.back().result);
    bool submitted = false;
    auto reentrant = onScreenshotSave().listen([&](ScreenshotSaveEventArgs&) {
        if (!submitted) { submitted = true; internal::submitScreenshot(nullptr, "during-drain.png"); }
    });
    internal::submitScreenshot(nullptr, "first-drain.png");
    drain(); const auto afterFirst = events.size(); drain();
    check("submission during delivery waits another drain", events.size() == afterFirst + 1);
    reentrant.disconnect();
    internal::setScreenshotBackendForTests([](Pixels&) { return false; }, {});
    internal::submitScreenshot(&mainQueue, sandbox / "capture-error.png");
    internal::captureScreenshots(mainQueue); drain();
    check("capture failure reaches deferred result", !events.back().result && events.back().result.message == "Screenshot capture failed");
    for (auto error : {SaveError::AccessDenied, SaveError::NoSpace, SaveError::WriteFailed}) {
        internal::setScreenshotBackendForTests([](Pixels& pixels) { pixels.allocate(1, 1); return true; },
            [error](const Pixels&, const fs::path&) { return SaveResult::fail(error, "injected writer failure"); });
        internal::submitScreenshot(&mainQueue, sandbox / "write-error.png");
        internal::captureScreenshots(mainQueue); finish();
        check("writer preserves detailed SaveError", events.back().result.error == error);
    }
    // Drain two real WindowContexts independently, after changing current.
    internal::WindowContext contextA, contextB;
    contextA.isMain = true; contextB.isMain = false;
    map<fs::path, int> contextPixels;
    mutex contextRecords;
    internal::setScreenshotBackendForTests([](Pixels& pixels) {
        pixels.allocate(1, 1); pixels.getData()[0] = internal::currentWindowContext().isMain ? 31 : 63;
        return true;
    }, [&](const Pixels& pixels, const fs::path& path) {
        lock_guard<mutex> lock(contextRecords); contextPixels[path.filename()] = pixels.getData()[0];
        return SaveResult::success();
    });
    auto* previousContext = internal::currentWindowCtx();
    internal::currentWindowCtx() = &contextA;
    internal::submitScreenshot(&contextA.asyncScreenshots, sandbox / "contextA.png");
    internal::currentWindowCtx() = &contextB;
    internal::submitScreenshot(&contextB.asyncScreenshots, sandbox / "contextB.png");
    internal::drainPendingScreenshots();
    check("secondary tick cannot capture main window's pending request", contextA.asyncScreenshots.pending.size() == 1);
    internal::currentWindowCtx() = &contextA; internal::drainPendingScreenshots();
    internal::currentWindowCtx() = previousContext; finish();
    check("per-window captures retain the requesting drawable pixels", contextPixels["contextA.png"] == 31 && contextPixels["contextB.png"] == 63);
    internal::setScreenshotBackendForTests({}, {});
    Pixels pixel; pixel.allocate(1, 1); pixel.setColor(0, 0, Color(1, 0, 0, 1));
    auto path = sandbox / utf8ToPath("画像.png");
    check("real PNG writer succeeds", internal::writeScreenshotPixels(pixel, path).ok());
    ifstream stream(path, ios::binary); string magic(8, '\0'); stream.read(magic.data(), 8);
    check("real PNG writer retains UTF-8 path and format", magic == string("\x89PNG\r\n\x1a\n", 8));
#if defined(__linux__)
    const auto oldImage = loadTextFile(path);
    // Unique temporaries deliberately ignore a planted fixed .tmp symlink.
    // Feed the real encoder a full device to retain the write-error check.
    internal::setScreenshotBackendForTests([](Pixels& pixels) { pixels.allocate(1, 1); return true; },
        [](const Pixels& pixels, const fs::path& output) {
            auto* full = internal::openFile("/dev/full", "wb");
            auto result = internal::writePixelsEncoded(full, pixels, "png", output);
            auto closed = internal::checkedClose(full, output);
            return result ? closed : result;
        });
    internal::submitScreenshot(&mainQueue, path); internal::captureScreenshots(mainQueue); finish();
    check("real screenshot encoder ENOSPC preserves old destination", events.back().result.error == SaveError::NoSpace && loadTextFile(path) == oldImage);
    internal::setScreenshotBackendForTests({}, {});
#endif
    check("rename failure preserves NoSpace", internal::saveFailure(path, "Cannot replace file", make_error_code(errc::no_space_on_device)).error == SaveError::NoSpace);
    Gate cancelledWriter;
    internal::setScreenshotBackendForTests([](Pixels& pixels) { pixels.allocate(1, 1); return true; },
        [&](const Pixels&, const fs::path&) { cancelledWriter.block(); return SaveResult::success(); });
    internal::submitScreenshot(&mainQueue, sandbox / "in-flight-old-run.png");
    internal::captureScreenshots(mainQueue); cancelledWriter.wait();
    const auto beforeCancellation = events.size(); newRun();
    check("cancelled in-flight screenshot retains its expensive slot", *mainQueue.admitted == 1);
    cancelledWriter.open(); finish();
    check("retired old-run screenshot releases slot without event", *mainQueue.admitted == 0 && events.size() == beforeCancellation);
    internal::setScreenshotBackendForTests({}, {});
    auto saved = events.size(); internal::submitScreenshot(&mainQueue, sandbox / "old-run.png");
    newRun(); drain(); check("expired run suppresses pending screenshot events", events.size() == saved && mainQueue.pending.empty() && *mainQueue.admitted == 0);
}

void micTests() {
    newRun();
    MicInput mic;
    int events = 0;
    bool expectSuccess = true;
    auto listener = mic.onStart.listen([&](MicStartResult& result) {
        ++events;
        check("mic clears isStarting before main-thread event", !mic.isStarting() && isMainThread());
        check("mic publishes running/name before event", result.success == expectSuccess && mic.isRunning() == expectSuccess && (expectSuccess ? mic.getDeviceName() == "Fake capture" : mic.getDeviceName().empty()));
    });
    Gate opening, opened, closing, queued;
    internal::setResourceLoadProbeForTests([&](const char* stage, const fs::path&) {
        if (string(stage) == "mic-ready") queued.arrive();
    });
    thread::id lifecycleId, cleanupId;
    int fakeComBalance = 0;
    internal::setMicBackendForTests([&](internal::MicState& state) {
        lifecycleId = this_thread::get_id(); ++fakeComBalance; opening.block();
        state.name = "Fake capture"; state.sampleRate = 44100;
        float samples[] = {0.25f, 0.5f}; state.audio(samples, 2);
        opened.arrive(); return MicStartResult{true, {}};
    }, [&](internal::MicState&) { cleanupId = this_thread::get_id(); closing.block(); --fakeComBalance; });
    mic.startAsync();
    check("mic is starting immediately after submission", mic.isStarting() && !mic.isRunning() && events == 0);
    opening.wait(); check("mic stays starting while backend is gated", mic.isStarting());
    opening.open(); opened.wait();
    queued.wait();
    check("mic stays starting after readiness is queued", mic.isStarting() && events == 0);
    drain();
    check("async success delivered once", events == 1 && mic.getSampleRate() == 44100);
    float samples[2]{};
    check("callback writes retained State ring", mic.getBuffer(samples, 2) == 2 && samples[0] == 0.25f && samples[1] == 0.5f);
    mic.stop(); closing.wait();
    check("stop returns while cleanup is still blocked", !mic.isStarting() && !mic.isRunning() && mic.getDeviceName().empty());
    closing.open(); finish();
    internal::setResourceLoadProbeForTests({});
    check("init and cleanup use one non-main thread", lifecycleId == cleanupId && lifecycleId != this_thread::get_id() && fakeComBalance == 0);
    // Failure is also queued, with actionable backend detail.
    expectSuccess = false;
    internal::setMicBackendForTests([](internal::MicState&) { return MicStartResult{false, "permission denied (backend=-7)"}; }, [](internal::MicState&) {});
    mic.startAsync(); internal::waitForResourceLoadsForTests();
    check("failed startup remains starting until delivery", mic.isStarting() && events == 1);
    auto errorListener = mic.onStart.listen([&](MicStartResult& result) { check("backend detail retained", result.message == "permission denied (backend=-7)"); });
    drain(); check("failed attempt delivers once", events == 2 && !mic.isRunning());
    errorListener.disconnect(); mic.stop();
    Gate stoppedOpen, stoppedCleanup;
    internal::setMicBackendForTests([&](internal::MicState&) {
        stoppedOpen.block(); return MicStartResult{true, {}};
    }, [&](internal::MicState&) { stoppedCleanup.arrive(); });
    mic.startAsync(); stoppedOpen.wait(); mic.stop();
    check("stop returns before blocked startup is released", !mic.isStarting() && !mic.isRunning());
    stoppedOpen.open(); stoppedCleanup.wait(); finish();
    check("stop suppresses late successful startup", events == 2);
    // Cancellation at a blocked open keeps State alive without blocking main.
    Gate blockedOpen, lateCleanup;
    internal::setMicBackendForTests([&](internal::MicState& state) {
        blockedOpen.block(); state.name = "late"; return MicStartResult{true, {}};
    }, [&](internal::MicState&) { lateCleanup.arrive(); });
    auto dying = make_unique<MicInput>();
    auto staleListener = dying->onStart.listen([&](MicStartResult&) { check("no stale event after destruction", false); });
    dying->startAsync(); blockedOpen.wait(); dying.reset();
    check("destruction returns before blocked startup is released", events == 2);
    blockedOpen.open(); lateCleanup.wait(); finish();
    // Replacement must wait for predecessor teardown, independently of main.
    Gate firstCleanup, replacementOpen;
    atomic<int> openCount{0}, closeCount{0};
    vector<thread::id> starts, stops; mutex identities;
    map<thread::id, int> threadBalances;
    internal::setMicBackendForTests([&](internal::MicState& state) {
        { lock_guard<mutex> lock(identities); starts.push_back(this_thread::get_id()); ++threadBalances[this_thread::get_id()]; }
        if (++openCount == 2) replacementOpen.arrive();
        state.name = "Fake capture"; return MicStartResult{true, {}};
    }, [&](internal::MicState&) {
        { lock_guard<mutex> lock(identities); stops.push_back(this_thread::get_id()); --threadBalances[this_thread::get_id()]; }
        if (++closeCount == 1) firstCleanup.block();
    });
    expectSuccess = true;
    check("synchronous start shares backend and emits no onStart", mic.start(48000) && mic.isRunning() && events == 2);
    mic.startAsync(48000); firstCleanup.wait();
    check("replacement returns before predecessor teardown", mic.isStarting() && openCount == 1);
    firstCleanup.open(); replacementOpen.wait();
    mic.stop(); finish();
    check("same-rate replacement suppresses stale events", events == 2);
    check("sync and async own matching lifecycle threads", starts.size() == 2 && stops.size() == 2 && starts[0] == stops[0] && starts[1] == stops[1]);
    check("fake COM ownership balances per sync/replacement thread", all_of(threadBalances.begin(), threadBalances.end(), [](const auto& entry) { return entry.second == 0; }));
    // A listener may restart and then delete the emitter safely.
    internal::setMicBackendForTests([](internal::MicState& state) { state.name = "Fake capture"; return MicStartResult{true, {}}; }, [](internal::MicState&) {});
    auto reentrant = make_unique<MicInput>(); int restarted = 0;
    auto reentrantListener = reentrant->onStart.listen([&](MicStartResult&) {
        if (++restarted == 1) reentrant->startAsync(); else reentrant.reset();
    });
    Gate reentrantQueued;
    internal::setResourceLoadProbeForTests([&](const char* stage, const fs::path&) {
        if (string(stage) == "mic-ready") reentrantQueued.arrive();
    });
    reentrant->startAsync();
    reentrantQueued.wait(); drain(); reentrantQueued.wait(2); drain();
    check("onStart listener can restart and delete mic", restarted == 2 && !reentrant);
    finish();
    internal::setResourceLoadProbeForTests({});
    mic.startAsync(); newRun(); finish();
    check("run invalidation retires mic and suppresses completion", events == 2 && !mic.isStarting() && !mic.isRunning() && mic.getDeviceName().empty());
    mic.stop();
    // Model partial context/device initialization and every cleanup stage.
    for (int failedStage = 0; failedStage < 3; ++failedStage) {
        thread::id initThread, uninitThread;
        int contexts = 0, devices = 0;
        internal::setMicBackendForTests([&](internal::MicState& state) {
            initThread = this_thread::get_id(); ++contexts;
            if (failedStage == 0) return MicStartResult{false, "context init failure"};
            ++devices;
            if (failedStage == 1) return MicStartResult{false, "device init failure"};
            state.name = "Fake capture"; return MicStartResult{false, "device start failure"};
        }, [&](internal::MicState&) {
            uninitThread = this_thread::get_id(); devices = 0; --contexts;
        });
        mic.startAsync(); internal::waitForResourceLoadsForTests(); mic.stop(); drain();
        check("partial initialization cleanup stays on owning thread", contexts == 0 && devices == 0 && initThread == uninitThread && initThread != this_thread::get_id());
    }
    internal::setMicBackendForTests({}, {});
}

void nativeAutomaticMicTests() {
    newRun();
    auto& engine = AudioEngine::getInstance();
    engine.shutdown();
    MicInput mic; int delivered = 0; MicStartResult outcome;
    auto listener = mic.onStart.listen([&](MicStartResult& result) {
        ++delivered; outcome = result;
        check("automatic mic settles status before its event", !mic.isStarting() && mic.isRunning() == result.success);
    });
    Gate queued;
    internal::setResourceLoadProbeForTests([&](const char* stage, const fs::path&) {
        if (string(stage) == "mic-ready") queued.arrive();
    });
    mic.startAsync(); queued.wait();
    check("automatic mic result is deferred", delivered == 0 && mic.isStarting());
    drain();
    check("automatic microphone never reports Null capture success", delivered == 1 &&
        (!outcome.success || mic.getDeviceName().find("NULL") == string::npos));
    const bool noDevice = !outcome.success;
    if (noDevice) check("no capture device fails async startup with actionable guidance",
        !mic.isRunning() && !mic.isStarting() && outcome.message.find("microphone") != string::npos &&
        outcome.message.find("permissions") != string::npos);
    else printf("SKIP no-device precondition: real capture device available\n");
    mic.stop(); finish();
    const bool sync = mic.start();
    check("synchronous automatic microphone never reports Null capture", !sync || mic.getDeviceName().find("NULL") == string::npos);
    if (noDevice) check("no capture device also fails synchronous start without an event",
        !sync && !mic.isRunning() && !mic.isStarting() && delivered == 1);
    mic.stop(); finish(); internal::setResourceLoadProbeForTests({});
}

void nativeNullMicTests() {
    newRun();
    AudioSettings settings; settings.backend = AudioBackend::Null;
    auto& engine = AudioEngine::getInstance();
    check("Null playback backend initializes for real capture lifetime", engine.init(settings));
    Gate queued;
    internal::setResourceLoadProbeForTests([&](const char* stage, const fs::path&) {
        if (string(stage) == "mic-ready") queued.arrive();
    });
    MicInput mic; int delivered = 0;
    auto listener = mic.onStart.listen([&](MicStartResult& result) {
        ++delivered;
        check("real Null microphone applies metadata before event", result.success && mic.isRunning() && !mic.isStarting() && mic.getSampleRate() == 44100 && !mic.getDeviceName().empty());
    });
    mic.startAsync(44100); queued.wait();
    check("real Null async startup is pending until delivery", mic.isStarting() && delivered == 0);
    drain();
    engine.shutdown();
    check("private real capture context survives engine shutdown", mic.isRunning());
    mic.stop(); finish();
    internal::setResourceLoadProbeForTests({});
    check("Null backend reselected before synchronous parity", engine.init(settings));
    check("real Null sync startup shares rate/backend without event", mic.start(48000) && mic.getSampleRate() == 48000 && delivered == 1);
    mic.stop(); finish();
    check("real miniaudio callbacks retire before State release", !mic.isRunning() && internal::resourceLoadWorkersForTests() == 0 && delivered == 1);
    engine.shutdown();
}

Gate headlessOpen;
int headlessMicEvents = 0;
class HeadlessMicApp : public App {
    MicInput mic;
    EventListener listener;
public:
    void setup() override {
        listener = mic.onStart.listen([](MicStartResult&) { ++headlessMicEvents; });
        mic.startAsync(); headlessOpen.wait();
        saveScreenshotAsync("headless-runtime.png");
    }
    void update() override { exitApp(); }
    void exit() override {
        check("headless exit invalidates mic readiness before app exit", !mic.isStarting() && !mic.isRunning());
        headlessOpen.open();
    }
};
void headlessTests() {
    thread::id initThread, cleanupThread;
    bool retired = false;
    internal::setMicBackendForTests([&](internal::MicState&) {
        initThread = this_thread::get_id(); headlessOpen.block(); return MicStartResult{true, {}};
    }, [&](internal::MicState&) { cleanupThread = this_thread::get_id(); retired = true; });
    int screenshotEvents = 0;
    auto listener = onScreenshotSave().listen([&](ScreenshotSaveEventArgs& args) {
        ++screenshotEvents; check("headless screenshot returns deferred failure", !args.result);
    });
    const int result = runHeadlessApp<HeadlessMicApp>();
    check("headless runtime waits for owning-thread mic cleanup", result == 0 && retired && initThread == cleanupThread && headlessMicEvents == 0 && screenshotEvents == 1);
    internal::setMicBackendForTests({}, {});
}

string shellQuote(const string& value) {
#ifdef _WIN32
    return "\"" + value + "\"";
#else
    string result = "'";
    for (char c : value) result += c == '\'' ? "'\\''" : string(1, c);
    return result + "'";
#endif
}
int exitChild(bool workerExit) {
    // Register proof before the resource host, so it runs after retirement.
    static atomic<int>* retired = new atomic<int>(0);
    static atomic<int>* comBalance = new atomic<int>(0);
    atexit([] { if (retired->load() != 2 || comBalance->load() != 0) std::_Exit(8); });
    newRun();
    static Gate* micGate = new Gate;
    static Gate* shotGate = new Gate;
    static thread::id ownerThread;
    internal::setMicBackendForTests([](internal::MicState&) {
        ownerThread = this_thread::get_id(); ++*comBalance; micGate->block(); return MicStartResult{true, {}};
    }, [](internal::MicState&) {
        if (ownerThread != this_thread::get_id()) std::_Exit(9);
        --*comBalance; ++*retired;
    });
    internal::setScreenshotBackendForTests([](Pixels& pixels) { pixels.allocate(1, 1); return true; },
        [](const Pixels&, const fs::path&) { shotGate->block(); ++*retired; return SaveResult::success(); });
    auto* mic = new MicInput; mic->startAsync();
    auto* queue = new internal::ScreenshotQueue;
    internal::submitScreenshot(queue, sandbox / "exit.png"); internal::captureScreenshots(*queue);
    micGate->wait(); shotGate->wait();
    atexit([] { micGate->open(); shotGate->open(); });
    if (workerExit) {
        internal::startResourceLoad([] { std::exit(0); });
        Gate parked; parked.block(); return 7;
    }
    std::exit(0);
}
void exitTests(const char* executable) {
    string command = shellQuote(fs::absolute(executable).string());
#ifdef TC_CORE_TEST_NAME
    command += " " TC_CORE_TEST_NAME;
#endif
    for (auto mode : {"main", "worker"}) {
        fflush(nullptr);
        int code = system((command + " --exit-child " + mode).c_str());
#ifndef _WIN32
        code = WIFEXITED(code) ? WEXITSTATUS(code) : 128 + WTERMSIG(code);
#endif
        check("process exit waits for gated mic/screenshot retirement", code == 0);
    }
}

class ScreenshotApp : public App {
    shared_ptr<tc::Window> secondary;
#if defined(__linux__) && defined(SOKOL_GLCORE) && !defined(__ANDROID__)
    Display* xdisplay = nullptr;
    ::Window cover = 0;
#endif
    bool hiddenRequested = false;
    bool closingSecondary = false;
    EventListener completion;
    int delivered = 0;
    int mainSaved = 0;
    int hiddenFailed = 0;
public:
    void setup() override {
        completion = onScreenshotSave().listen([this](ScreenshotSaveEventArgs& args) {
            ++delivered;
            if (args.path.filename() == "hidden-real.png") { ++hiddenFailed; check("closing hidden window reports failure", !args.result); }
            else { check("real visible screenshot succeeds", args.result.ok()); ++mainSaved; }
        });
        if (internal::supportsSecondaryWindows()) {
            WindowSettings settings; settings.setSize(32, 32); settings.setTitle("G2 hidden screenshot");
            secondary = createWindow(settings);
#if defined(__linux__) && defined(SOKOL_GLCORE) && !defined(__ANDROID__)
            if (secondary) {
                xdisplay = (Display*)sapp_x11_get_display();
                ::Window root, parent, *children = nullptr; unsigned count = 0;
                XQueryTree(xdisplay, DefaultRootWindow(xdisplay), &root, &parent, &children, &count);
                for (unsigned i = 0; i < count; ++i) {
                    char* title = nullptr; XFetchName(xdisplay, children[i], &title);
                    if (title && string(title) == "G2 hidden screenshot") {
                        XMoveWindow(xdisplay, children[i], 300, 300);
                        XSetWindowAttributes attrs{}; attrs.override_redirect = True;
                        cover = XCreateWindow(xdisplay, root, 298, 298, 64, 64, 0,
                            CopyFromParent, InputOutput, CopyFromParent, CWOverrideRedirect, &attrs);
                        XMapRaised(xdisplay, cover); XSync(xdisplay, False);
                    }
                    if (title) XFree(title);
                }
                if (children) XFree(children);
                check("real secondary covered by native X11 window", cover != 0);
            }
#else
            printf("SKIP native secondary occlusion control: Linux X11 test helper only\n");
            if (secondary) { secondary->close(); secondary.reset(); }
#endif

        } else printf("SKIP hidden secondary: backend does not support secondary windows\n");
        setIndependentFps(VSYNC, EVENT_DRIVEN);
    }
    void update() override {
        if (secondary && !hiddenRequested) {
            if (!secondary->isOccluded()) return;
            auto* previous = internal::currentWindowCtx(); internal::currentWindowCtx() = &secondary->context();
            for (int i = 0; i < 4; ++i) saveScreenshotAsync(sandbox / "hidden-real.png");
            internal::currentWindowCtx() = previous;
            hiddenRequested = true;
        }
        if (mainSaved == 6) {
            if (secondary && !closingSecondary) {
                check("hidden secondary kept all four shots pending", secondary->context().asyncScreenshots.pending.size() == 4);
                secondary->close(); closingSecondary = true;
            }
            if (!secondary || hiddenFailed == 4) exitApp();
        } else if (*internal::mainWindowContext().asyncScreenshots.admitted == 0) {
            saveScreenshotAsync(sandbox / ("real" + to_string(mainSaved) + ".png"));
        }
    }
    void draw() override { clear(Color(0.2f, 0.4f, 0.7f)); }
    void cleanup() override {
        secondary.reset();
#if defined(__linux__) && defined(SOKOL_GLCORE) && !defined(__ANDROID__)
        if (cover) XDestroyWindow(xdisplay, cover);
#endif
        check("real main progresses beyond hidden window cap", mainSaved == 6);
    }
};
#endif
}
TC_CORE_TEST_MAIN(int argc, char** argv) {
#ifdef __EMSCRIPTEN__
    (void)argc; (void)argv; printf("SKIP native asyncScreenshotMic; Web mocks are in asyncScreenshotMicWeb\n"); return 0;
#else
    getMainThreadId(); sandbox = fs::absolute("asyncScreenshotMic-fixtures"); fs::create_directories(sandbox); setDataPathRoot(sandbox);
    if (argc > 1 && string(argv[1]) == "--screen") {
        WindowSettings settings; settings.setSize(64, 48); settings.setHighDpi(false);
        return tcCoreTest::runDisplayTest<ScreenshotApp>(settings, failures, [] { fs::remove_all(sandbox); });
    }
    if (argc == 3 && string(argv[1]) == "--exit-child") return exitChild(string(argv[2]) == "worker");
    screenshotTests(); screenshotAliasTests(); screenshotFinalSymlinkTests(); micTests(); nativeAutomaticMicTests(); nativeNullMicTests(); headlessTests(); exitTests(argv[0]);
    fs::remove_all(sandbox);
    return failures ? 1 : 0;
#endif
}
