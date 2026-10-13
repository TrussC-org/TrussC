#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <type_traits>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif
#if defined(__linux__)
#include <sys/resource.h>
#include <sys/wait.h>
#include <csignal>
#endif

using namespace tc;
namespace {
int failures = 0;
void check(bool ok, const char* name) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}
static_assert(!std::is_convertible_v<SaveResult, bool>);
static_assert(std::is_same_v<decltype(AudioRecorder().start("x")), SaveResult>);
static_assert(std::is_same_v<decltype(AudioRecorder().stop()), SaveResult>);
static_assert(std::is_same_v<decltype(VideoWriter().close()), SaveResult>);
static_assert(std::is_same_v<decltype(ScreenRecorder().stop()), SaveResult>);
static_assert(std::is_same_v<decltype(saveScreenshot("x")), bool>);
static_assert(std::is_same_v<decltype(Pixels().save("x")), SaveResult>);
static_assert(std::is_same_v<decltype(saveTextFile("x", "")), SaveResult>);
}

TC_CORE_TEST_MAIN() {
    auto dir = fs::temp_directory_path() / "tc_saveContract";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    setDataPathRoot(dir);
    const auto target = dir / "old.txt";
    check(saveTextFile("old.txt", "previous").ok(), "relative path resolves and saves");
    check(loadTextFile(target) == "previous", "relative path uses data root");
    check(saveTextFile(fs::path(u8"日本語/設定.txt"), "hello\n").ok() &&
          loadTextFile(dir / fs::path(u8"日本語/設定.txt")) == "hello\n", "creates Unicode parent");
    check(!saveTextFile("empty/", "x") && !fs::exists(dir / "empty"), "empty filename creates no directory");
    fs::path resolved;
    check(internal::prepareOutputPath("image.WEIRD", resolved, "test", ".png",
          {".png", ".jpg", ".jpeg", ".bmp"}).ok() && resolved.filename() == "image.WEIRD.png",
          "optional extension policy appends default");
    check(internal::prepareOutputPath("image.JpG", resolved, "test", ".png",
          {".png", ".jpg"}).ok() && resolved.filename() == "image.JpG", "extension preserves case");
    internal::setAppBundlePathForTests(dir / "bundle");
    check(saveTextFile(dir / "bundle" / "new" / "file", "x").error == SaveError::AccessDenied,
          "bundle refusal is AccessDenied");
    check(!fs::exists(dir / "bundle"), "bundle refusal creates nothing");
    internal::setAppBundlePathForTests({});
#ifndef _WIN32
    fs::create_directory(dir / "readonly");
    chmod((dir / "readonly").c_str(), 0555);
    if (geteuid() != 0)
        check(saveTextFile(dir / "readonly" / "file", "x").error == SaveError::AccessDenied,
              "unwritable folder is AccessDenied");
    else std::printf("SKIP permission test as root\n");
    chmod((dir / "readonly").c_str(), 0755);
#endif
    // A reusable callback writer permits encoders to stream into the checked
    // temporary. Here a partial encoder/write failure must preserve the target.
    for (int attempt = 0; attempt < 3; ++attempt) {
        auto result = internal::atomicWrite(target, [&](std::FILE* file) {
            auto first = internal::checkedWrite(file, "partial", 7, target);
            if (!first) return first;
            return SaveResult::fail(SaveError::NoSpace, "simulated full disk after partial write");
        }, "test");
        check(result.error == SaveError::NoSpace && loadTextFile(target) == "previous",
              "partial failure preserves previous output");
        check(!fs::exists(dir / "old.txt.tmp"), "failed retry leaves no temporary");
    }
    auto thrown = internal::atomicWrite(target, [&](std::FILE*) -> SaveResult {
        throw std::runtime_error("encoder failed");
    }, "test");
    check(thrown.error == SaveError::Unknown && loadTextFile(target) == "previous" &&
          !fs::exists(dir / "old.txt.tmp"), "writer exception closes and cleans temporary");
    Pixels px;
    px.allocate(2, 2, 4);
    Xml xml;
    xml.addRoot("saved");
    check(px.save("nested/image.PNG").ok(), "Pixels uses shared save entry");
    check(saveJson(Json{{"value", 3}}, "nested/state.json").ok(), "JSON uses shared save entry");
    check(xml.save("nested/state.xml").ok(), "XML uses shared save entry");
    check(!fs::exists(dir / "nested/image.PNG.tmp") &&
          !fs::exists(dir / "nested/state.json.tmp") &&
          !fs::exists(dir / "nested/state.xml.tmp"), "successful savers leave no temporary");
    // Force rename failure after writing, without a production fault hook.
    auto renameTarget = dir / "rename.txt";
    auto renamed = internal::atomicWrite(renameTarget, [&](std::FILE* file) {
        auto written = internal::checkedWrite(file, "new", 3, renameTarget);
        fs::create_directory(renameTarget);
        return written;
    }, "test");
    check(renamed.error == SaveError::WriteFailed && fs::is_directory(renameTarget) &&
          !fs::exists(dir / "rename.txt.tmp"), "rename failure is WriteFailed and cleans temporary");
#if defined(__linux__)
    // /dev/full is used only through a private temporary symlink, never renamed
    // or replaced. This exercises actual stdio ENOSPC, including flush failure.
    fs::create_symlink("/dev/full", dir / "old.txt.tmp");
    auto full = saveTextFile(target, "x");
    check(full.error == SaveError::NoSpace && loadTextFile(target) == "previous" &&
          !fs::exists(dir / "old.txt.tmp"), "real ENOSPC at flush preserves previous file");
    for (const auto& name : {"image.PNG", "state.json", "state.xml"}) {
        const auto file = dir / "nested" / name;
        const auto previous = loadTextFile(file);
        fs::create_symlink("/dev/full", fs::path(file).concat(".tmp"));
        SaveResult result;
        if (std::string(name) == "image.PNG") result = px.save(file);
        else if (std::string(name) == "state.json") result = saveJson(Json{{"value", 4}}, file);
        else result = xml.save(file);
        check(result.error == SaveError::NoSpace && loadTextFile(file) == previous &&
              !fs::exists(fs::path(file).concat(".tmp")), "each small-file saver preserves output on ENOSPC");
    }
    // Kernel-generated failure in the middle of a real regular-file write.
    std::fflush(stdout);
    const auto pid = fork();
    if (pid == 0) {
        signal(SIGXFSZ, SIG_IGN);
        rlimit limit{4096, 4096};
        if (setrlimit(RLIMIT_FSIZE, &limit) != 0) _exit(2);
        auto result = saveTextFile(target, std::string(65536, 'x'));
        _exit(result.error == SaveError::WriteFailed && loadTextFile(target) == "previous" &&
              !fs::exists(dir / "old.txt.tmp") ? 0 : 1);
    }
    int status = 0;
    check(pid > 0 && waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "kernel mid-write failure preserves old file and cleans temporary");
    check(appendToFile("/dev/full", "x").error == SaveError::NoSpace, "append reports NoSpace");
    int errors = 0;
    auto listener = getLogger().onLog.listen([&](LogEventArgs& event) {
        if (event.level == LogLevel::Error) ++errors;
    });
    FileWriter writer;
    check(writer.open("/dev/full").ok(), "FileWriter opens in place");
    const int before = errors;
    writer.write("x").write("y");
    check(!writer.isOpen() && errors == before + 1, "FileWriter reports first write error and stops");
    check(setLogFile("/dev/full").ok(), "logger opens in append mode");
    const int logBefore = errors;
    logNotice() << "cannot fit";
    logNotice() << "after failure";
    check(!getLogger().isFileOpen() && errors == logBefore + 1,
          "logger reports first write error through onLog and disables file");
#endif
#if !defined(__EMSCRIPTEN__)
    {
        int screenshotErrors = 0;
        auto sub = getLogger().onLog.listen([&](LogEventArgs& e) {
            if (e.level == LogLevel::Error) ++screenshotErrors;
        });
        const auto queued = internal::currentWindowContext().pendingScreenshotPaths.size();
        check(!saveScreenshot(target / "bad.png") && screenshotErrors == 1 &&
              internal::currentWindowContext().pendingScreenshotPaths.size() == queued,
              "screenshot bad parent refused synchronously with one Error");
        internal::setAppBundlePathForTests(dir / "bundle");
        check(!saveScreenshot(dir / "bundle/shot.png"), "screenshot bundle refused synchronously");
        internal::setAppBundlePathForTests({});
        check(saveScreenshot(dir / "deferred.png"), "screenshot returns queued bool");
        // Exercise the same writer used by the deferred capture, with known
        // pixels so this headless test needs no swapchain/readback.
        auto& queue = internal::currentWindowContext().pendingScreenshotPaths;
        const auto destination = queue.back(); queue.pop_back();
#if defined(__linux__)
        for (const auto* extension : {".png", ".jpg", ".bmp"}) {
            auto output = fs::path(destination).replace_extension(extension);
            check(internal::saveScreenshotPixels(px, output), "prepare previous screenshot output");
            const auto previous = loadTextFile(output);
            fs::create_symlink("/dev/full", fs::path(output).concat(".tmp"));
            const int before = screenshotErrors;
            check(!internal::saveScreenshotPixels(px, output) && screenshotErrors == before + 1 &&
                  loadTextFile(output) == previous &&
                  !fs::exists(fs::path(output).concat(".tmp")),
                  "deferred screenshot ENOSPC logs exactly one Error and preserves output");
        }
#else
        fs::create_directory(destination);
        const int before = screenshotErrors;
        check(!internal::saveScreenshotPixels(px, destination) && screenshotErrors == before + 1,
              "deferred screenshot write failure logs exactly one Error");
#endif
    }
#endif
#if defined(__linux__)
    // Isolate the process-wide file limit, and start threads only after fork.
    std::fflush(stdout);
    const auto audioPid = fork();
    if (audioPid == 0) {
        signal(SIGXFSZ, SIG_IGN);
        rlimit limit{4096, 4096};
        if (setrlimit(RLIMIT_FSIZE, &limit) != 0) _exit(2);
        std::atomic<int> audioErrors{0};
        auto sub = getLogger().onLog.listen([&](LogEventArgs& e) {
            if (e.level == LogLevel::Error) ++audioErrors;
        });
        AudioSettings settings;
        settings.backend = AudioBackend::Null;
        settings.sampleRate = 48000; settings.channels = 2; settings.bufferSize = 256;
        auto& engine = AudioEngine::getInstance();
        if (!engine.init(settings)) _exit(3);
        AudioRecorder rec;
        const auto path = dir / "limited.wav";
        if (!rec.start(path)) _exit(4);
        for (int i = 0; i < 500 && rec.isRecording(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        const bool ended = !rec.isRecording();
        const auto result = rec.stop();
        const auto counted = (uint64_t)std::llround(rec.getRecordedSeconds() * settings.sampleRate);
        const auto actual = (fs::file_size(path) - 80) / 4;
        const bool ok = ended && !result && !rec.stop() && audioErrors == 1 &&
                        counted == actual && actual > 0;
        engine.shutdown();
        _exit(ok ? 0 : 5);
    }
    int audioStatus = 0;
    check(audioPid > 0 && waitpid(audioPid, &audioStatus, 0) == audioPid &&
          WIFEXITED(audioStatus) && WEXITSTATUS(audioStatus) == 0,
          "audio mid-take short write ends take, logs once, counts frames actually written");
    {
        // A symlink gives filesink a deterministic ENOSPC destination while
        // retaining an mp4 path for container selection.
        VideoWriter healthy;
        Pixels goodFrame; goodFrame.allocate(16, 16, 4);
        auto notOpen = healthy.addFrame(goodFrame);
        check(!notOpen && notOpen.message.find("not open") != std::string::npos && healthy.close().ok(),
              "not-open submission reports its cause without retaining a take failure");
        const auto healthyOpen = healthy.open(dir / "healthy.mp4", 16, 16);
        if (healthyOpen) {
            check(healthy.addFrame(goodFrame).ok() && healthy.getFrameCount() == 1,
                  "Linux video backend accepts a frame");
            int callErrors = 0;
            auto sub = getLogger().onLog.listen([&](LogEventArgs& e) {
                if (e.level == LogLevel::Error) ++callErrors;
            });
            Pixels wrongFrame; wrongFrame.allocate(8, 8, 4);
            float samples[2] = {};
            auto wrongSize = healthy.addFrame(wrongFrame);
            auto noAudio = healthy.writeAudio(samples, 1, 0);
            check(!wrongSize && wrongSize.message.find("pixels size 8x8 != writer 16x16") != std::string::npos &&
                  !noAudio && noAudio.message.find("writer has no audio track") != std::string::npos &&
                  callErrors == 2 && healthy.getFrameCount() == 1,
                  "size mismatch and missing audio each report one specific Error without counting frames");
            auto nullFrame = healthy.addFrameAt(nullptr, 16, 16, 0);
            auto nullAudio = healthy.writeAudio(nullptr, 1, 0);
            auto noFrames = healthy.writeAudio(samples, 0, 0);
            check(!nullFrame && nullFrame.message.find("null frame data") != std::string::npos &&
                  !nullAudio && nullAudio.message.find("null audio data") != std::string::npos &&
                  !noFrames && noFrames.message.find("audio frames must be > 0") != std::string::npos &&
                  callErrors == 5, "invalid buffers and audio frame count are recoverable call errors");
            check(healthy.addFrame(goodFrame).ok() && healthy.addFrame(goodFrame).ok() &&
                  healthy.getFrameCount() == 3 && healthy.close().ok() && healthy.close().ok() && callErrors == 5,
                  "valid frames after caller mistakes are accepted and close succeeds with accepted count");
        } else {
            check(false, "Linux video encoder required for deterministic failure test");
        }
        fs::create_symlink("/dev/full", dir / "full.mp4");
        VideoWriter video;
        std::atomic<int> videoErrors{0};
        auto sub = getLogger().onLog.listen([&](LogEventArgs& e) {
            if (e.level == LogLevel::Error) ++videoErrors;
        });
        auto opened = video.open(dir / "full.mp4", 16, 16);
        Pixels frame; frame.allocate(16, 16, 4);
        if (opened) {
            bool failed = false;
            int accepted = 0;
            for (int i = 0; i < 30; ++i) {
                if (!video.addFrame(frame)) { failed = true; break; }
                ++accepted;
            }
            if (failed) check(videoErrors == 1, "backend rejection logs at the first failing append");
            auto closed = video.close();
            auto closedAgain = video.close();
            check(!closed && !closedAgain && closed.message == closedAgain.message &&
                  video.getFrameCount() == accepted && videoErrors == 1,
                  "Linux backend failure logs one Error per take, retains close failure and accepted count");
        } else {
            check(!opened && video.getFrameCount() == 0,
                  "Linux filesink rejects full destination at open");
        }
    }
    {
        // Enough changing intra frames to reach filesink during the take,
        // rather than only when close flushes the encoder's buffered tail.
        fs::create_symlink("/dev/full", dir / "full-during-take.mp4");
        VideoWriter video;
        VideoRecordSettings settings;
        settings.keyframeInterval = 1;
        settings.bitrate = 8000000; // exceed filesink buffering before finalization
        std::atomic<int> videoErrors{0};
        auto sub = getLogger().onLog.listen([&](LogEventArgs& e) {
            if (e.level == LogLevel::Error) ++videoErrors;
        });
        const auto opened = video.open(dir / "full-during-take.mp4", 128, 128, settings);
        check(opened.ok(), "Linux video opens for mid-take backend failure");
        if (opened) {
            Pixels frame; frame.allocate(128, 128, 4);
            uint32_t noise = 1;
            SaveResult appended;
            int accepted = 0;
            for (int i = 0; i < 240; ++i) {
                for (size_t j = 0; j < 128 * 128 * 4; ++j) {
                    noise = noise * 1664525u + 1013904223u;
                    frame.getData()[j] = static_cast<unsigned char>(noise >> 24);
                }
                appended = video.addFrame(frame);
                if (!appended) break;
                ++accepted;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            check(!appended && videoErrors == 1, "mid-take backend rejection emits exactly one Error");
            const auto retried = video.addFrame(frame);
            const auto closed = video.close();
            const auto closedAgain = video.close();
            check(!retried && !closed && !closedAgain && retried.message == appended.message &&
                  closed.message == appended.message && closedAgain.message == appended.message &&
                  videoErrors == 1 && video.getFrameCount() == accepted,
                  "retry and repeated close retain first backend error without relogging or counting rejected frames");
        }
    }
#endif
    check(saveTextFile(target, "replacement").ok() && loadTextFile(target) == "replacement",
          "retry replaces existing target");
    check(appendToFile(target, " plus").ok() && loadTextFile(target) == "replacement plus",
          "append preserves existing bytes");
    fs::remove_all(dir, ec);
    return failures ? 1 : 0;
}
