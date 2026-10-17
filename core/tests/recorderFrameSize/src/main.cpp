#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include <X11/Xlib.h>
#endif
#include <cstdio>
#include <limits>

using namespace tc;
using trussc::internal::RecorderFrameLayout;
using trussc::internal::RecorderFrameBuffer;
using F = trussc::internal::RecorderPixelFormat;

namespace trussc::internal {
struct RecorderFrameTestAccess {
    template<class Acquire, class Copy, class Discard, class Submit>
    static bool complete(VideoWriter& writer, const RecorderFrameLayout& source,
                         std::mutex& mutex, Acquire acquire, Copy copy, Discard discard, Submit submit) {
        VideoWriter::DiagnosticLock lock(writer, mutex);
        return writer.completeCapture(source, acquire, copy, discard, submit);
    }
};
}

namespace {
int failures = 0;
void check(bool ok, const char* name) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}
}

#if defined(__linux__) && !defined(__EMSCRIPTEN__)
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

TC_CORE_TEST_MAIN(int argc, char** argv) {
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    if (argc > 1 && std::string(argv[1]) == "--gpu-check") {
        WindowSettings settings;
        settings.width = settings.height = 64;
        return tcCoreTest::runDisplayTest<RecorderApp>(settings, failures, [] {
            check(gpuCompleted, "recorder display checks complete");
        });
    }
#endif
    constexpr size_t width = 16, height = 16;
    const auto packed = internal::recorderPackedLayout(width, height, F::BGRA8);
    const size_t max = std::numeric_limits<size_t>::max();
    size_t bytes = 0;
    check(!internal::recorderPackedLayout(max / 4 + 1, 2, F::BGRA8).byteSize &&
          !internal::recorderPackedLayout(16, max / 64 + 1, F::BGRA8).byteSize &&
          !internal::recorderByteSize(max, 2, bytes),
          "recorder checks row and total byte arithmetic");
    check(internal::recorderNV12ByteSize(16, 16, bytes) && bytes == 384 &&
          !internal::recorderNV12ByteSize(15, 16, bytes) &&
          !internal::recorderNV12ByteSize(16, 15, bytes) &&
          !internal::recorderNV12ByteSize(max - 1, 2, bytes) &&
          !internal::recorderNV12ByteSize((max / 4) & ~size_t(1), 4, bytes),
          "recorder checks complete NV12 byte arithmetic");

    check(internal::recorderNV12ByteSize(1088, 1920, bytes) && bytes == 3133440 &&
          internal::recorderNV12ImageSpan(1080, 1920, 1088, 1920, bytes) && bytes == 3133432 &&
          internal::recorderNV12ImageFits(1080, 1920, 1088, 1920, 3133432),
          "recorder accepts complete NV12 pixels without final row padding");
    check(!internal::recorderNV12ImageFits(1080, 1920, 1088, 1920, 3133431) &&
          !internal::recorderNV12ImageSpan(1081, 1920, 1088, 1920, bytes) &&
          !internal::recorderNV12ImageSpan(1080, 0, 1088, 1920, bytes) &&
          !internal::recorderNV12ImageSpan(1080, 1920, 1072, 1920, bytes) &&
          !internal::recorderNV12ImageSpan(1080, 1920, 1088, 1918, bytes) &&
          !internal::recorderNV12ImageSpan(2, 2, max - 1, 2, bytes),
          "recorder refuses incomplete or invalid NV12 pixel spans");

#if !defined(__EMSCRIPTEN__)
    const auto dir = fs::temp_directory_path() /
        ("tc-recorder-frame-size-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    VideoWriter writer;
    VideoRecordSettings settings;
    settings.fps = 30;
    check(writer.open(dir / "frames.mp4", width, height, settings).ok(), "recorder opens a fixed size take");
    if (!writer.isOpen()) { fs::remove_all(dir); return 1; }
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
    std::fill(destination.begin(), destination.end(), 0x7b);
    const std::vector<RecorderFrameLayout> differentSources = {
        internal::recorderPackedLayout(16, 32, F::BGRA8),
        internal::recorderPackedLayout(32, 16, F::BGRA8),
        internal::recorderPackedLayout(32, 32, F::BGRA8),
        internal::recorderPackedLayout(8, 8, F::BGRA8),
        {16, 16, 63, 1024, F::BGRA8}, {16, 16, 64, 1023, F::BGRA8},
        {16, 16, max, max, F::BGRA8}, {16, 16, 64, 1024, F::Unknown},
        {16, 16, 64, 1024, F::RGBA8},
        {0, 16, 64, 1024, F::BGRA8},
    };
    bool refused = true;
    for (const auto& source : differentSources) refused &= !complete(source);
    check(refused && copies == 1 && acquires == 1 && submits == 1 && discards == 0 &&
          std::all_of(destination.begin(), destination.end(), [](unsigned char b) { return b == 0x7b; }),
          "recorder rejects frames that do not match the take size before copying");
    const std::vector<RecorderFrameLayout> differentDestinations = {
        {16, 8, 64, 512, F::BGRA8}, {8, 16, 64, 1024, F::BGRA8},
        {16, 16, 63, 1024, F::BGRA8}, {16, 16, 64, 1023, F::BGRA8},
        {16, 16, 80, 1279, F::BGRA8}, {16, 16, max, max, F::BGRA8},
        {16, 16, 64, 1024, F::RGBA8},
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
    differentSize.allocate(32, 16, 4);
    for (int i = 0; i < 50; ++i) refused &= !writer.addFrame(differentSize);
    check(diagnosticsUnlocked, "recorder emits layout diagnostics after releasing the writer mutex");
    check(refused && warnings == 1 && errors == 0 && writer.getRejectedFrameCount() ==
          differentSources.size() + differentDestinations.size() + 52,
          "recorder counts mismatches and logs one Warning per take");
    destinationLayout.rowBytes = 80;
    destinationLayout.byteSize = 80 * height;
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
