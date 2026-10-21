// --screen: real window capture; array allocation probes prevent an allocator
// returning the same address from hiding an unnecessary reallocation.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if defined(__ANDROID__)
#include <GLES3/gl3.h>
#elif defined(__linux__) && !defined(__EMSCRIPTEN__)
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#endif
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
#include <X11/Xlib.h>
#endif

using namespace tc;

namespace {
std::atomic<void*> watched{nullptr};
std::atomic<int> releases{0}, allocations{0};
std::atomic<bool> armed{false};
int failures = 0;
bool completed = false;

void releaseArray(void* ptr) {
    if (ptr && ptr == watched.load()) ++releases;
    std::free(ptr);
}

void check(const char* name, bool ok) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}

class CaptureApp : public App {
    Pixels pixels;
    EventListener afterFrame;
    int step = 0, resizeFrames = 0;
    int fromWidth = 0, fromHeight = 0;

    void capture(bool reuse) {
        const int width = getFramebufferWidth(), height = getFramebufferHeight();
        auto* before = pixels.getDataVoid();
        watched = before;
        releases = 0;
        allocations = 0;
        armed = true;
        const bool ok = grabScreen(pixels);
        armed = false;
        watched = nullptr;
        check("grabScreen succeeds", ok);
        check("capture has the framebuffer's RGBA8 layout", pixels.isAllocated() &&
              pixels.getWidth() == width && pixels.getHeight() == height &&
              pixels.getChannels() == 4 && pixels.getFormat() == PixelFormat::U8 &&
              pixels.getTotalBytes() == size_t(width) * height * 4);
        if (reuse) {
#if defined(__APPLE__) && TARGET_OS_OSX
            std::printf("SKIP matching capture pointer/allocation stability: macOS synchronous capture moves a fresh result into the destination so a timed-out capture never writes to it\n");
#else
            check("matching capture preserves the data pointer", pixels.getDataVoid() == before);
            check("matching capture neither allocates nor releases an array",
                  allocations == 0 && releases == 0);
#endif
        } else {
            check("mismatching capture allocates a new array", allocations > 0);
            if (before) check("mismatching capture releases the previous buffer", releases == 1);
        }
        // Check every byte: the second frame swaps red/green and blue/yellow,
        // with distinct top/bottom halves to also catch stale rows or inversion.
        bool correct = ok && pixels.getData() && pixels.getTotalBytes() == size_t(width) * height * 4;
        if (correct) {
            for (int y = 0; y < height && correct; ++y) {
                const bool top = y < height / 2;
                const unsigned char first[] = {static_cast<unsigned char>(top ? 255 : 0), 0,
                                               static_cast<unsigned char>(top ? 0 : 255), 255};
                const unsigned char second[] = {static_cast<unsigned char>(top ? 0 : 255), 255, 0, 255};
                for (int x = 0; x < width; ++x) {
                    if (std::memcmp(pixels.getData() + (size_t(y) * width + x) * 4,
                                    step == 0 ? first : second, 4) != 0) {
                        correct = false;
                        break;
                    }
                }
            }
        }
        check("every captured byte matches the current scene", correct);
    }

    void captureFailure() {
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
        // The preceding successful capture has distinct top/bottom rows, so
        // comparing every byte also detects a flip after a failed GL read.
        const std::vector<unsigned char> previous(pixels.getData(),
                                                  pixels.getData() + pixels.getTotalBytes());
        auto* before = pixels.getDataVoid();
        GLint previousReadFbo, previousDrawFbo;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFbo);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFbo);
        GLuint incomplete, caller;
        glGenFramebuffers(1, &incomplete);
        glGenFramebuffers(1, &caller);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, incomplete);
        glReadBuffer(GL_NONE);
        check("unattached source framebuffer is incomplete",
              glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE);
        // PR 5 explicitly selects the window source; an unrelated bound FBO
        // must neither redirect the read nor supply the injected failure.
        auto& ctx = internal::currentWindowContext();
        const auto previousSource = ctx.lastSwapchain.gl.framebuffer;
        ctx.lastSwapchain.gl.framebuffer = incomplete;
        glBindFramebuffer(GL_READ_FRAMEBUFFER, caller);
        GLint previousRenderbuffer;
        glGetIntegerv(GL_RENDERBUFFER_BINDING, &previousRenderbuffer);
        GLuint color;
        glGenRenderbuffers(1, &color);
        glBindRenderbuffer(GL_RENDERBUFFER, color);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, pixels.getWidth(), pixels.getHeight());
        glFramebufferRenderbuffer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color);
        glBindRenderbuffer(GL_RENDERBUFFER, previousRenderbuffer);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        check("unrelated caller read framebuffer is complete",
              glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, caller);
        const GLenum packNames[] = {GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH,
            GL_PACK_SKIP_ROWS, GL_PACK_SKIP_PIXELS, GL_PIXEL_PACK_BUFFER_BINDING};
        GLint previousPack[5];
        for (int i = 0; i < 5; ++i) glGetIntegerv(packNames[i], &previousPack[i]);
        GLuint packBuffer;
        glGenBuffers(1, &packBuffer);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, packBuffer);
        glBufferData(GL_PIXEL_PACK_BUFFER, 16, nullptr, GL_STREAM_READ);
        glPixelStorei(GL_PACK_ALIGNMENT, 8);
        glPixelStorei(GL_PACK_ROW_LENGTH, pixels.getWidth() + 13);
        glPixelStorei(GL_PACK_SKIP_ROWS, 2);
        glPixelStorei(GL_PACK_SKIP_PIXELS, 3);
        GLint expectedPack[5];
        for (int i = 0; i < 5; ++i) glGetIntegerv(packNames[i], &expectedPack[i]);
        // A pending unrelated error must be drained even on the failure path.
        glEnable(GL_TEXTURE_2D);
        int errors = 0;
        bool errorCode = false;
        auto logs = getLogger().onLog.listen([&](LogEventArgs& event) {
            if (event.level == LogLevel::Error) {
                ++errors;
                errorCode = event.message.find("glReadPixels failed (GL error 0x506)") != std::string::npos;
            }
        });
        watched = before;
        releases = 0;
        allocations = 0;
        armed = true;
        const bool ok = grabScreen(pixels);
        armed = false;
        watched = nullptr;
        ctx.lastSwapchain.gl.framebuffer = previousSource;
        GLint restoredRead, restoredDraw, restoredReadBuffer;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &restoredRead);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &restoredDraw);
        glGetIntegerv(GL_READ_BUFFER, &restoredReadBuffer);
        check("failed readback restores caller read/draw bindings and read buffer",
              restoredRead == GLint(caller) && restoredDraw == GLint(caller) &&
              restoredReadBuffer == GL_COLOR_ATTACHMENT0);
        bool packRestored = true;
        for (int i = 0; i < 5; ++i) {
            GLint value;
            glGetIntegerv(packNames[i], &value);
            packRestored = packRestored && value == expectedPack[i];
        }
        check("failed readback restores pixel-pack state", packRestored);
        for (int i = 0; i < 4; ++i) glPixelStorei(packNames[i], previousPack[i]);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, previousPack[4]);
        glDeleteBuffers(1, &packBuffer);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, incomplete);
        glGetIntegerv(GL_READ_BUFFER, &restoredReadBuffer);
        check("failed readback restores the selected source's read buffer", restoredReadBuffer == GL_NONE);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, previousReadFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, previousDrawFbo);
        glDeleteFramebuffers(1, &incomplete);
        glDeleteFramebuffers(1, &caller);
        glDeleteRenderbuffers(1, &color);
        check("incomplete source framebuffer makes grabScreen return false", !ok);
        check("failed readback reports one Error with the GL error code", errors == 1 && errorCode);
        check("failed readback preserves the matching allocation",
              pixels.getDataVoid() == before && allocations == 0 && releases == 0);
        check("failed readback does not flip retained pixels",
              pixels.getTotalBytes() == previous.size() &&
              std::memcmp(pixels.getData(), previous.data(), previous.size()) == 0);
        check("failed readback consumes its GL error", glGetError() == GL_NO_ERROR);

        // Match Fbo readback: an unrelated pending error must not make a valid
        // capture fail. This also verifies recovery after the incomplete FBO.
        glEnable(GL_TEXTURE_2D); // Invalid capability in GL core and GLES3.
        capture(true);
        check("successful readback drains unrelated GL errors", glGetError() == GL_NO_ERROR);
#endif
    }

public:
    void setup() override {
        afterFrame = events().afterFrame.listen([this] { captureFrame(); });
    }
    void update() override {
        if (step == 2) {
            if (++resizeFrames == 1) {
                fromWidth = getFramebufferWidth();
                fromHeight = getFramebufferHeight();
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
                // The desktop Linux public resize function is currently a stub.
                auto* display = (Display*)sapp_x11_get_display();
                XResizeWindow(display, (::Window)(uintptr_t)sapp_x11_get_window(), 280, 220);
                XFlush(display);
#else
                setWindowSize(280, 220);
#endif
            }
            if (resizeFrames > 120) {
                check("window resize completes within 120 frames", false);
                exitApp();
            }
        }
    }

    void draw() override {
        clear(0, 0, 0, 1);
        setColor(step == 0 ? Color(1.0f, 0.0f, 0.0f) : Color(0.0f, 1.0f, 0.0f));
        drawRect(0, 0, getWindowWidth(), getWindowHeight() / 2);
        setColor(step == 0 ? Color(0.0f, 0.0f, 1.0f) : Color(1.0f, 1.0f, 0.0f));
        drawRect(0, getWindowHeight() / 2, getWindowWidth(), getWindowHeight() / 2);
    }

    void captureFrame() {
        // Windows clamps tiny client sizes, so wait for whichever size was applied.
        if (step == 2 && (resizeFrames == 0 || (getFramebufferWidth() == fromWidth &&
                                                getFramebufferHeight() == fromHeight))) return;
        capture(step == 1);
        if (step == 1) captureFailure();
        if (++step < 3) return;

        // Matching dimensions alone must not reuse RGB or floating storage.
        pixels.allocate(getFramebufferWidth(), getFramebufferHeight(), 3);
        capture(false);
        pixels.allocate(getFramebufferWidth(), getFramebufferHeight(), 4, PixelFormat::F32);
        capture(false);
        completed = true;
        exitApp();
    }
};
} // namespace

// Capture on macOS writes from a completion thread, so probes are atomic.
void* operator new[](size_t bytes) {
    if (armed) ++allocations;
    if (void* ptr = std::malloc(bytes ? bytes : 1)) return ptr;
    throw std::bad_alloc();
}
void operator delete[](void* ptr) noexcept { releaseArray(ptr); }
void operator delete[](void* ptr, size_t) noexcept { releaseArray(ptr); }

TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc <= 1 || std::strcmp(argv[1], "--screen") != 0) {
        std::printf("grabScreenReuse: use --screen for window capture checks (display required)\n");
        return 0;
    }
    WindowSettings settings;
    settings.setSize(200, 160);
    settings.setHighDpi(false);
    return tcCoreTest::runDisplayTest<CaptureApp>(settings, failures, [] {
        check("all capture checks completed", completed);
    });
}
