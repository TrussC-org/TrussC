#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#if defined(__linux__) && !defined(__EMSCRIPTEN__) && !defined(__ANDROID__) && !defined(SOKOL_GLES3)
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include <X11/Xlib.h>
#endif
#include <atomic>
#include <chrono>
#include <cstdio>
#include <limits>

#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__) && \
    (defined(_WIN32) || defined(__linux__) || (defined(__APPLE__) && !TARGET_OS_IPHONE))
#define TC_RECORDER_DEVICE_CHECK 1
#else
#define TC_RECORDER_DEVICE_CHECK 0
#endif

using namespace tc;
using trussc::internal::RecorderFrameLayout;
using trussc::internal::RecorderFrameBuffer;
using F = trussc::internal::RecorderPixelFormat;

namespace trussc::internal {
struct RecorderFrameTestAccess {
    template<class Acquire, class Copy, class Discard, class Submit>
    static bool complete(VideoWriter& writer, const RecorderFrameLayout& source,
                         std::mutex& mutex, Acquire acquire, Copy copy, Discard discard, Submit submit) {
        VideoWriter::Diagnostics diagnostics;
        bool result;
        {
            std::lock_guard<std::mutex> lock(mutex);
            result = writer.completeCapture(source, acquire, copy, discard, submit, diagnostics);
        }
        diagnostics.log();
        return result;
    }
};
#if TC_RECORDER_DEVICE_CHECK
// Same definition as audioListenerTeardown: both TUs can share allCoreTests.
struct ScreenRecorderTestAccess {
    static void startTap(ScreenRecorder& recorder, int rate, int channels) {
        recorder.state_->startAudioTap(rate, channels);
    }
    static std::weak_ptr<void> lifetime(ScreenRecorder& recorder) {
        return recorder.state_;
    }
    static std::unique_lock<std::mutex> lockWriter(ScreenRecorder& recorder) {
        return std::unique_lock<std::mutex>(recorder.state_->writerMutex_);
    }
    static auto pendingCapture(ScreenRecorder& recorder) {
        auto state = recorder.state_;
        state->inFlight_.fetch_add(1);
        return [state]() {
            state->finishCaptureAndLog();
        };
    }
};
#endif
}

namespace {
int failures = 0;
void check(bool ok, const char* name) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}
}

#if defined(__linux__) && !defined(__EMSCRIPTEN__) && !defined(__ANDROID__) && !defined(SOKOL_GLES3)
namespace {
bool gpuCompleted = false;
class RecorderApp : public App {
    ScreenRecorder recorder;
    EventListener afterFrame, logs;
    Fbo fbo;
    VideoWriter fboWriter;
    fs::path directory;
    int step = 0, warnings = 0, acceptedBefore = 0;
public:
    void setup() override {
        setFps(120);
        directory = fs::temp_directory_path() / ("tc-recorder-frame-size-display-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(directory);
        fbo.allocate(16, 16);
        check(fboWriter.open(directory / "fbo.mp4", 16, 16).ok(), "Fbo recorder opens");
        VideoRecordSettings settings;
        settings.fps = 120;
        logs = getLogger().onLog.listen([this](LogEventArgs& event) {
            if (event.level == LogLevel::Warning &&
                event.message.find("frame layout does not match") != std::string::npos) ++warnings;
        });
        check(recorder.start(directory / "screen.mp4", settings).ok(), "screen recorder opens");
        afterFrame = events().afterFrame.listen([this] {
            if (step == 3) acceptedBefore = recorder.getFrameCount();
            if (step == 7) check(sapp_width() == 64 && sapp_height() == 96,
                                  "recording window becomes taller");
            if (step == 11) check(sapp_width() == 96 && sapp_height() == 64,
                                   "recording window becomes wider");
            if (step < 20) return;
            check(recorder.getRejectedFrameCount() > 0 && warnings == 1,
                  "resized recording window counts frames and warns once");
            check(recorder.getFrameCount() > acceptedBefore && recorder.writer().getWidth() == 64 &&
                  recorder.writer().getHeight() == 64 && recorder.stop().ok(),
                  "screen recorder resumes matching frames at the take size");
            check(fboWriter.getFrameCount() == 1 && fboWriter.close().ok(),
                  "Fbo recording encodes with caller pixel pack state");
            gpuCompleted = true;
            exitApp();
        });
    }
    void update() override {
        ++step;
        // Use the native window in this Linux display test.
        auto resize = [](unsigned width, unsigned height) {
            auto* display = (Display*)sapp_x11_get_display();
            XResizeWindow(display, (::Window)(uintptr_t)sapp_x11_get_window(), width, height);
            XFlush(display);
        };
        if (step == 4) resize(64, 96);
        if (step == 8) resize(96, 64);
        if (step == 12) resize(64, 64);
    }
    void draw() override {
        clear(0.2f, 0.3f, 0.4f);
        if (step != 2) return;
        fbo.begin();
        clear(0.4f, 0.3f, 0.2f);
        fbo.end();
        GLuint buffer = 0;
        glGenBuffers(1, &buffer);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, buffer);
        glBufferData(GL_PIXEL_PACK_BUFFER, 4, nullptr, GL_STREAM_READ);
        glPixelStorei(GL_PACK_ALIGNMENT, 8);
        glPixelStorei(GL_PACK_ROW_LENGTH, 37);
        glPixelStorei(GL_PACK_SKIP_ROWS, 2);
        glPixelStorei(GL_PACK_SKIP_PIXELS, 3);
        check(fboWriter.addFrame(fbo).ok(), "Fbo readback uses the destination row layout");
        Pixels fboPixels;
        fboPixels.allocate(16, 16, 4);
        check(fbo.readPixels(fboPixels.getData()) && fboPixels.getData()[0] >= 100 &&
              fboPixels.getData()[0] <= 104, "Fbo readback copies the rendered pixel bytes");
        Pixels pixels;
        glGetError();
        const bool captured = grabScreen(pixels);
        check(captured && pixels.isAllocated() && glGetError() == GL_NO_ERROR,
              "screen readback uses the destination row layout");
        GLint alignment, row, rows, columns, bound;
        glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
        glGetIntegerv(GL_PACK_ROW_LENGTH, &row);
        glGetIntegerv(GL_PACK_SKIP_ROWS, &rows);
        glGetIntegerv(GL_PACK_SKIP_PIXELS, &columns);
        glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &bound);
        check(alignment == 8 && row == 37 && rows == 2 && columns == 3 && bound == (GLint)buffer,
              "recorder readbacks restore caller pixel pack state");
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glPixelStorei(GL_PACK_ROW_LENGTH, 0);
        glPixelStorei(GL_PACK_SKIP_ROWS, 0);
        glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
        glDeleteBuffers(1, &buffer);
    }
    void cleanup() override {
        recorder.stop();
        fboWriter.close();
        fs::remove_all(directory);
    }
};
}
#endif

#if TC_RECORDER_DEVICE_CHECK
namespace {
fs::path keepDirectory;
bool deviceCompleted = false;

class DeviceRecorderApp : public App {
    using Clock = std::chrono::steady_clock;
    static constexpr int requestWidth = 320, requestHeight = 240;
    ScreenRecorder recorder;
    EventListener afterFrame, logs;
    fs::path directory;
    // Shared with the log listener: a completion thread may still be inside it
    // after a failed stop, so the counters must outlive this app.
    struct Diagnostics { std::atomic<int> warnings{0}, errors{0}; };
    std::shared_ptr<Diagnostics> diagnostics = std::make_shared<Diagnostics>();
    int takeWidth = 0, takeHeight = 0, phase = 0, frames = 0;
    int acceptedBeforeReturn = 0;
    int observedWidth = 0, observedHeight = 0, stableFrames = 0;
    int beforeWidth = 0, beforeHeight = 0;
    uint64_t rejectedBefore = 0;
    bool started = false, finished = false, stepChanged = false;
    bool anyResize = false, anyMismatch = false, resumed = false, returnSeen = false;
    bool screenStopAttempted = false, preserveOutput = false;
    std::vector<std::string> skippedSteps;
    Clock::time_point phaseStart = Clock::now();
    Clock::time_point stableSince = phaseStart;
    Clock::time_point began = phaseStart;

    int accepted() {
        // Metal appends on a completion thread; the public getter is not atomic.
        auto lock = internal::ScreenRecorderTestAccess::lockWriter(recorder);
        return recorder.getFrameCount();
    }
    double seconds() const {
        return std::chrono::duration<double>(Clock::now() - phaseStart).count();
    }
    const char* stepName() const {
        return phase == 1 ? "taller" : phase == 2 ? "wider" : "return";
    }
    void requestResize(int next) {
        phase = next;
        frames = 0;
        stableFrames = 0;
        stepChanged = false;
        phaseStart = Clock::now();
        stableSince = phaseStart;
        beforeWidth = observedWidth = getFramebufferWidth();
        beforeHeight = observedHeight = getFramebufferHeight();
        rejectedBefore = recorder.getRejectedFrameCount();
        if (phase == 3) acceptedBeforeReturn = accepted();
        // Repeat the same API/units used before the take; never predict DPI pixels.
        const int w = phase == 2 ? requestWidth * 3 / 2 : requestWidth;
        const int h = phase == 1 ? requestHeight * 3 / 2 : requestHeight;
        setWindowSize(w, h);
        std::printf("resize step=%s requested=%dx%d take=%dx%d\n", stepName(), w, h,
                    takeWidth, takeHeight);
    }
    void failedResult(const char* kind, const char* status, const fs::path& path) {
        preserveOutput = true; // A late completion may still write here.
        const auto file = json(internal::pathToUtf8(path)).dump();
        std::printf("RESULT %s status=%s file=%s\n", kind, status, file.c_str());
    }
    bool stopScreen() {
        screenStopAttempted = true;
        const bool stopped = recorder.stop().ok();
        check(stopped, "device screen take finalizes (drains Metal captures)");
        if (!stopped) failedResult("screen", "stop-failed", directory / "screen.mp4");
        return stopped;
    }
    void summary(const char* kind, const fs::path& path, int w, int h,
                 int count, uint64_t rejected, int warningCount) {
        std::error_code ec;
        const auto bytes = fs::file_size(path, ec);
        check(!ec && bytes > 0, "recording file is nonempty");
        // Player support is optional (e.g. a device without a decoder). Its
        // load diagnostics are outside the recording's zero-Error assertion.
        VideoPlayer player;
        player.setAutoPoster(false);
        player.setUseHwAccel(false); // metadata check needs no Linux hardware probing
        const bool loaded = player.load(path).ok();
        std::string fileSize = "unavailable";
        if (loaded) {
            const int fw = player.getWidth(), fh = player.getHeight();
            fileSize = std::to_string(fw) + "x" + std::to_string(fh);
            check(fw == w && fh == h, "file dimensions equal the take framebuffer size");
        } else {
            std::printf("SKIP: %s file dimensions unavailable from VideoPlayer\n", kind);
        }
        player.close();
        const auto file = json(internal::pathToUtf8(path)).dump();
        std::printf("RESULT %s take=%dx%d accepted=%d rejected=%llu warnings=%d file=%s fileBytes=%llu fileSize=%s\n",
            kind, w, h, count, static_cast<unsigned long long>(rejected), warningCount,
            file.c_str(), ec ? 0ULL : static_cast<unsigned long long>(bytes),
            fileSize.c_str());
    }
    void synchronousTake() {
        diagnostics->warnings = 0;
        VideoWriter writer;
        Fbo match, mismatch;
        match.allocate(takeWidth, takeHeight);
        mismatch.allocate(takeWidth, takeHeight + 120);
        for (Fbo* fbo : {&match, &mismatch}) {
            fbo->begin();
            clear(0.6f, 0.2f, 0.1f);
            fbo->end();
        }
        check(writer.open(directory / "fbo.mp4", takeWidth, takeHeight).ok(), "device Fbo take opens");
        if (!writer.isOpen()) {
            failedResult("fbo", "open-failed", directory / "fbo.mp4");
            logs = EventListener{};
            return;
        }
        check(!writer.addFrame(mismatch).ok() && writer.getRejectedFrameCount() == 1 &&
              writer.getFrameCount() == 0, "synchronous Fbo mismatch is refused and counted");
        check(writer.addFrame(match).ok() && writer.getFrameCount() == 1 &&
              writer.getRejectedFrameCount() == 1, "synchronous matching Fbo is accepted after refusal");
        const bool closed = writer.close().ok();
        check(closed, "device Fbo take finalizes");
        if (!closed) {
            failedResult("fbo", "stop-failed", directory / "fbo.mp4");
            logs = EventListener{};
            return; // No writer samples or finalized-file claims after failed close.
        }
        const int count = writer.getFrameCount();
        const auto rejected = writer.getRejectedFrameCount();
        const int warningCount = diagnostics->warnings.load();
        check(warningCount == 1, "Fbo take emits exactly one layout Warning");
        check(diagnostics->errors == 0, "device recordings emit zero Errors");
        logs = EventListener{};
        summary("fbo", directory / "fbo.mp4", takeWidth, takeHeight, count, rejected, warningCount);
    }
    void finish() {
        if (finished) return;
        finished = true;
        if (stopScreen()) {
            const int count = recorder.getFrameCount(); // successful stop drained completions
            const auto rejected = recorder.getRejectedFrameCount();
            const int warningCount = diagnostics->warnings.load();
            check(count > 0, "screen take accepts matching frames");
            if (anyResize || rejected > 0) {
                check(anyMismatch && rejected > 0, "effective window resizes reject mismatching frames");
                check(resumed, "effective window resizes are followed by verified matching-frame resume");
                check(warningCount == 1, "screen take emits exactly one layout Warning");
                for (const auto& step : skippedSteps) std::printf("SKIP: %s\n", step.c_str());
            } else {
                std::printf("SKIP: resize part: platform made no framebuffer changes within limits (taller, wider, return)\n");
                check(warningCount == 0, "unchanged screen take emits no layout Warning");
            }
            check(diagnostics->errors == 0, "screen recording emits zero Errors");
            logs = EventListener{};
            summary("screen", directory / "screen.mp4", takeWidth, takeHeight, count, rejected, warningCount);
        }
        logs = EventListener{};
        listenForDiagnostics();
        synchronousTake();
        deviceCompleted = true;
        exitApp();
    }
    void listenForDiagnostics() {
        logs = getLogger().onLog.listen([diagnostics = diagnostics](LogEventArgs& event) {
            if (event.level == LogLevel::Warning &&
                event.message.find("frame layout does not match") != std::string::npos) ++diagnostics->warnings;
            if (event.level == LogLevel::Error) ++diagnostics->errors;
        });
    }
    void tick() {
        if (finished) return;
        ++frames;
        const int w = getFramebufferWidth(), h = getFramebufferHeight();
        if (w != observedWidth || h != observedHeight) {
            observedWidth = w;
            observedHeight = h;
            stableFrames = 0;
            stableSince = Clock::now();
            if (started) {
                anyResize = true;
                anyMismatch |= w != takeWidth || h != takeHeight;
                stepChanged |= w != beforeWidth || h != beforeHeight;
                std::printf("resize step=%s observed=%dx%d take=%dx%d\n", stepName(), w, h,
                            takeWidth, takeHeight);
            }
        } else {
            ++stableFrames;
        }
        // Bound (platform): native extents need a 12-frame/0.25-second dwell. Overflow of the enclosing wait is counted below.
        const bool settled = stableFrames >= 12 &&
            Clock::now() - stableSince >= std::chrono::duration<double>(0.25);
        if (!started) {
            // Bound (platform): initial native resize/DPI must settle. Overflow records FAIL and aborts before the take.
            const bool timeout = frames >= 240 || seconds() >= 4;
            if (!settled && !timeout) return;
            if (!settled || w <= 0 || h <= 0) {
                check(false, "initial framebuffer settles to a positive size within limit");
                finished = true;
                exitApp();
                return;
            }
            takeWidth = w;
            takeHeight = h;
            std::printf("take requested=%dx%d settledFramebuffer=%dx%d\n",
                        requestWidth, requestHeight, takeWidth, takeHeight);
            VideoRecordSettings settings;
            settings.fps = 60;
            check(recorder.start(directory / "screen.mp4", settings).ok(), "device window take opens");
            started = true;
            check(recorder.writer().getWidth() == takeWidth && recorder.writer().getHeight() == takeHeight,
                  "screen take uses the actual framebuffer dimensions");
            if (!recorder.isRecording()) { finish(); return; }
            frames = 0;
            phaseStart = Clock::now();
            return;
        }
        if (phase == 0) {
            // Bound (platform): initial captures need a 12-frame/0.25-second dwell and four accepted frames. Overflow of the enclosing wait is counted below.
            if (frames >= 12 && seconds() >= 0.25 && accepted() >= 4) requestResize(1);
            // Bound (platform): initial matching captures may lag. Overflow fails the run with one check.
            else if (frames >= 240 || seconds() >= 4) {
                check(false, "initial matching screen frames arrive within limit");
                requestResize(1);
            }
            return;
        }
        const bool matches = w == takeWidth && h == takeHeight;
        const bool direction = phase == 1 ? w == takeWidth && h > takeHeight :
                               phase == 2 ? w > takeWidth && h == takeHeight : matches;
        if (phase == 3 && matches && !returnSeen) {
            acceptedBeforeReturn = accepted();
            returnSeen = true;
        }
        // Metal completion can lag afterFrame; require the appropriate counter to move.
        const bool counted = phase == 3 ? accepted() > acceptedBeforeReturn :
                             recorder.getRejectedFrameCount() > rejectedBefore;
        const bool ready = settled && direction && counted && (phase == 3 || stepChanged);
        // Bound (platform): OS resize/completion may stall. Overflow prints SKIP only for unchanged steps; effective mismatch or return failures are counted FAILs.
        const bool timeout = frames >= 240 || seconds() >= 4;
        if (!ready && !timeout) return;
        if (phase < 3) {
            if (!stepChanged) {
                skippedSteps.push_back(std::string("resize step=") + stepName() +
                    " made no framebuffer change within limit framebuffer=" +
                    std::to_string(w) + "x" + std::to_string(h));
            } else {
                if (!direction) std::printf("resize step=%s unexpected framebuffer=%dx%d take=%dx%d\n",
                                           stepName(), w, h, takeWidth, takeHeight);
                check(direction && settled, "effective resize settles in the requested direction relative to take");
                check(counted, "effective mismatching framebuffer is refused within limit");
            }
        } else if (matches) {
            // Mandatory whenever the return reached the take, even if a prior step was skipped.
            resumed = settled && counted;
            check(resumed, "matching captures resume within the return deadline");
        } else {
            std::printf("resize step=return unexpected framebuffer=%dx%d take=%dx%d\n",
                        w, h, takeWidth, takeHeight);
            check(false, "framebuffer returns to the recorded take size within limit");
        }
        if (phase < 3) requestResize(phase + 1);
        else finish();
    }

public:
    void setup() override {
        directory = (keepDirectory.empty() ? fs::temp_directory_path() : keepDirectory) /
            ("tc-recorder-device-" + std::to_string(Clock::now().time_since_epoch().count()));
        fs::create_directories(directory);
        listenForDiagnostics();
        phaseStart = stableSince = Clock::now();
        observedWidth = getFramebufferWidth();
        observedHeight = getFramebufferHeight();
        setWindowSize(requestWidth, requestHeight);
        afterFrame = events().afterFrame.listen([this] { tick(); });
    }
    void update() override {
        // Bound (platform): device recording cannot wait indefinitely. Overflow records FAIL and attempts safe finalization.
        if (!finished && Clock::now() - began > std::chrono::seconds(25)) {
            check(false, "device recording completes within global deadline");
            if (started) finish();
            else exitApp();
        }
    }
    void draw() override { clear(0.2f, 0.4f, 0.6f); }
    void cleanup() override {
        if (started && !screenStopAttempted) stopScreen();
        afterFrame = EventListener{};
        logs = EventListener{};
        if (preserveOutput) {
            const auto path = json(internal::pathToUtf8(directory)).dump();
            std::printf("Output retained after finalization failure (pending work may still write): %s\n", path.c_str());
        } else if (keepDirectory.empty() && !directory.empty()) {
            std::error_code ec;
            fs::remove_all(directory, ec);
            check(!ec, "temporary device recordings removed");
        }
    }
};

}
#endif

TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--device-check") {
#if TC_RECORDER_DEVICE_CHECK
        if (argc == 4 && std::string(argv[2]) == "--keep") keepDirectory = fs::absolute(argv[3]);
        else if (argc != 2) {
            std::printf("Usage: recorderFrameSize --device-check [--keep dir]\n");
            return 2;
        }
        WindowSettings settings;
        settings.setSize(320, 240).setHighDpi(true).setSampleCount(1);
        return tcCoreTest::runDisplayTest<DeviceRecorderApp>(settings, failures, [] {
            check(deviceCompleted, "portable device checks complete");
        });
#else
        std::printf("SKIP: --device-check requires macOS, Windows or Linux desktop/Pi\n");
        return 0;
#endif
    }
#if defined(__linux__) && !defined(__EMSCRIPTEN__) && !defined(__ANDROID__) && !defined(SOKOL_GLES3)
    if (argc > 1 && std::string(argv[1]) == "--gpu-check") {
        WindowSettings settings;
        settings.width = settings.height = 64;
        return tcCoreTest::runDisplayTest<RecorderApp>(settings, failures, [] {
            check(gpuCompleted, "recorder display checks complete");
        });
    }
#endif
    // Media Foundation's H.264 encoders need at least 64x64 on some devices.
    constexpr size_t width = 64, height = 64;
    const auto packed = internal::recorderPackedLayout(width, height, F::BGRA8);
    const size_t paddedRowBytes = packed.rowBytes + 16;
    const size_t paddedBytes = paddedRowBytes * height;
    const size_t nv12Stride = width + 8;
    const size_t nv12PaddedBytes = nv12Stride * height * 3 / 2;
    const size_t nv12Span = nv12PaddedBytes - (nv12Stride - width);
    const size_t max = std::numeric_limits<size_t>::max();
    size_t bytes = 0;
    check(!internal::recorderPackedLayout(max / 4 + 1, 2, F::BGRA8).byteSize &&
          !internal::recorderPackedLayout(width, max / packed.rowBytes + 1, F::BGRA8).byteSize &&
          !internal::recorderByteSize(max, 2, bytes),
          "recorder checks row and total byte arithmetic");
    check(internal::recorderNV12ByteSize(width, height, bytes) && bytes == width * height * 3 / 2 &&
          !internal::recorderNV12ByteSize(width - 1, height, bytes) &&
          !internal::recorderNV12ByteSize(width, height - 1, bytes) &&
          !internal::recorderNV12ByteSize(max - 1, 2, bytes) &&
          !internal::recorderNV12ByteSize((max / 4) & ~size_t(1), 4, bytes),
          "recorder checks complete NV12 byte arithmetic");

    check(internal::recorderNV12ByteSize(nv12Stride, height, bytes) && bytes == nv12PaddedBytes &&
          internal::recorderNV12ImageSpan(width, height, nv12Stride, height, bytes) && bytes == nv12Span &&
          internal::recorderNV12ImageFits(width, height, nv12Stride, height, nv12Span),
          "recorder accepts complete NV12 pixels without final row padding");
    check(!internal::recorderNV12ImageFits(width, height, nv12Stride, height, nv12Span - 1) &&
          !internal::recorderNV12ImageSpan(width + 1, height, nv12Stride, height, bytes) &&
          !internal::recorderNV12ImageSpan(width, 0, nv12Stride, height, bytes) &&
          !internal::recorderNV12ImageSpan(width, height, width - 8, height, bytes) &&
          !internal::recorderNV12ImageSpan(width, height, nv12Stride, height - 2, bytes) &&
          !internal::recorderNV12ImageSpan(2, 2, max - 1, 2, bytes),
          "recorder refuses incomplete or invalid NV12 pixel spans");

#if !defined(__EMSCRIPTEN__)
    const auto dir = fs::temp_directory_path() /
        ("tc-recorder-frame-size-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    VideoWriter writer;
    VideoRecordSettings settings;
    settings.fps = 30;
    const auto opened = writer.open(dir / "frames.mp4", width, height, settings);
    bool unsupportedMediaType = false;
#if !defined(__linux__)
    // SaveResult has no media-type error enum. Only explicit backend media-type
    // diagnostics qualify; file errors and generic backend failures must fail.
    // Linux/GStreamer must open this fixture, even for these diagnostics.
    unsupportedMediaType = opened.error == SaveError::WriteFailed &&
        (opened.message.find("0xC00D36B4") != std::string::npos ||
         opened.message.find("MF_E_INVALIDMEDIATYPE") != std::string::npos ||
         opened.message.find("unsupported media type") != std::string::npos ||
         opened.message.find("Unsupported media type") != std::string::npos);
#endif
    if (unsupportedMediaType) {
        std::printf("SKIP: encoder rejected a %zux%zu take (%s)\n", width, height, opened.message.c_str());
    } else {
        check(opened.ok(), "recorder opens a fixed size take");
    }
    if (!opened) { fs::remove_all(dir); return failures ? 1 : 0; }
    int warnings = 0, errors = 0;
    std::mutex writerMutex;
    bool diagnosticsUnlocked = true;
    auto logs = getLogger().onLog.listen([&](LogEventArgs& event) {
        if (event.message.find("frame layout does not match") != std::string::npos && event.level == LogLevel::Warning) {
            ++warnings;
            std::thread probe([&] {
                if (writerMutex.try_lock()) writerMutex.unlock();
                else diagnosticsUnlocked = false;
            });
            probe.join();
        }
        if (event.level == LogLevel::Error) ++errors;
    });
    int acquires = 0, copies = 0, discards = 0, submits = 0;
    std::vector<unsigned char> destination(packed.byteSize, 0x7b);
    RecorderFrameLayout destinationLayout = packed;
    bool readbackAvailable = true;
    auto complete = [&](const RecorderFrameLayout& source) {
        return internal::RecorderFrameTestAccess::complete(writer, source, writerMutex,
            [&] {
                ++acquires;
                return RecorderFrameBuffer{destination.data(), destinationLayout};
            },
            [&](const RecorderFrameBuffer& buffer) {
                ++copies;
                if (!readbackAvailable) return false;
                // A readback copies its captured extent, including when the
                // test supplies dimensions different from the take's size.
                for (size_t y = 0; y < source.height; ++y)
                    std::memset(buffer.data + y * buffer.layout.rowBytes, 0x35, source.width * 4);
                return true;
            },
            [&] { ++discards; },
            [&] {
                ++submits;
                Pixels pixels;
                pixels.allocate(width, height, 4);
                for (size_t y = 0; y < height; ++y)
                    std::memcpy(pixels.getData() + y * packed.rowBytes,
                                destination.data() + y * destinationLayout.rowBytes, packed.rowBytes);
                return writer.addFrame(pixels).ok();
            });
    };
    check(complete(packed) && writer.getFrameCount() == 1,
          "recorder encodes frames that match the take size");
    std::fill(destination.begin(), destination.end(), static_cast<unsigned char>(0x7b));
    const std::vector<RecorderFrameLayout> differentSources = {
        internal::recorderPackedLayout(width, height * 2, F::BGRA8),
        internal::recorderPackedLayout(width * 2, height, F::BGRA8),
        internal::recorderPackedLayout(width * 2, height * 2, F::BGRA8),
        internal::recorderPackedLayout(width / 2, height / 2, F::BGRA8),
        {width, height, packed.rowBytes - 1, packed.byteSize, F::BGRA8},
        {width, height, packed.rowBytes, packed.byteSize - 1, F::BGRA8},
        {width, height, max, max, F::BGRA8},
        {width, height, packed.rowBytes, packed.byteSize, F::Unknown},
        {width, height, packed.rowBytes, packed.byteSize, F::RGBA8},
        {0, height, packed.rowBytes, packed.byteSize, F::BGRA8},
    };
    bool refused = true;
    for (const auto& source : differentSources) refused &= !complete(source);
    check(refused && copies == 1 && acquires == 1 && submits == 1 && discards == 0 &&
          std::all_of(destination.begin(), destination.end(), [](unsigned char b) { return b == 0x7b; }),
          "recorder rejects frames that do not match the take size before copying");
    const std::vector<RecorderFrameLayout> differentDestinations = {
        {width, height / 2, packed.rowBytes, packed.byteSize / 2, F::BGRA8},
        {width / 2, height, packed.rowBytes, packed.byteSize, F::BGRA8},
        {width, height, packed.rowBytes - 1, packed.byteSize, F::BGRA8},
        {width, height, packed.rowBytes, packed.byteSize - 1, F::BGRA8},
        {width, height, paddedRowBytes, paddedBytes - 1, F::BGRA8},
        {width, height, max, max, F::BGRA8},
        {width, height, packed.rowBytes, packed.byteSize, F::RGBA8},
    };
    for (const auto& layout : differentDestinations) {
        destinationLayout = layout;
        refused &= !complete(packed);
    }
    check(refused && copies == 1 && discards == (int)differentDestinations.size() &&
          std::all_of(destination.begin(), destination.end(), [](unsigned char b) { return b == 0x7b; }),
          "recorder checks locked destination dimensions layout format and capacity");
    destinationLayout = packed;
    readbackAvailable = false;
    check(!complete(packed) && discards == (int)differentDestinations.size() + 1 && submits == 1,
          "recorder releases the locked buffer when readback validation fails");
    readbackAvailable = true;
    Pixels floatPixels;
    floatPixels.allocate(width, height, 4, PixelFormat::F32);
    check(!writer.addFrame(floatPixels) && writer.getFrameCount() == 1,
          "recorder refuses a different pixel element format");
    Pixels differentSize;
    differentSize.allocate(width * 2, height, 4);
    for (int i = 0; i < 50; ++i) refused &= !writer.addFrame(differentSize);
    check(diagnosticsUnlocked, "recorder emits layout diagnostics after releasing the writer mutex");
    check(refused && warnings == 1 && errors == 0 && writer.getRejectedFrameCount() ==
          differentSources.size() + differentDestinations.size() + 52,
          "recorder counts mismatches and logs one Warning per take");
    destinationLayout.rowBytes = paddedRowBytes;
    destinationLayout.byteSize = paddedBytes;
    destination.assign(destinationLayout.byteSize, 0x7b);
    auto tenBit = packed;
    tenBit.format = F::RGB10A2;
    check(complete(tenBit) && complete(packed) && writer.getFrameCount() == 3 && warnings == 1 &&
          writer.close().ok(), "matching frames keep encoding after refused frames with padded destination rows");
    check(fs::file_size(dir / "frames.mp4") > 0, "recorder finalizes matching encoded frames");
    check(writer.open(dir / "next.mp4", width, height, settings).ok() &&
          writer.getRejectedFrameCount() == 0, "recorder resets mismatch count for a new take");
    check(!complete(differentSources.front()) && warnings == 2 && writer.getRejectedFrameCount() == 1 &&
          complete(packed) && writer.close().ok(), "recorder logs once again in the next take");
    fs::remove_all(dir);
#endif
    return failures ? 1 : 0;
}
