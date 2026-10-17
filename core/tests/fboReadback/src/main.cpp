// #270 / #641: run with --gpu-check (Xvfb works on Linux; Metal needs a Mac).
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <cstdio>
#include <cstring>

using namespace std;
using namespace tc;

namespace {
int failures = 0;
int commits = 0;
bool completed = false;
void check(const char* name, bool ok) {
    printf("%-64s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
void onCommit(void*) { ++commits; }
const sg_commit_listener listener = {onCommit, nullptr};

class ReadbackApp : public App {
public:
    void setup() override {
        setFps(VSYNC);
        checkAllocationFailure();
        fbo_.allocate(16, 16);
        half_.allocate(16, 16, 1, TextureFormat::RGBA16F);
        check("fullscreen shader loads", fullscreen_.load(tc_fbomip_blit_shader_desc));
        fullscreen_.setParams(tc_fbomip_mip_params_t{});
        check("commit listener installed", sg_add_commit_listener(listener));
        // A writable temp folder: not a macOS app bundle's data folder, and
        // not the working directory, which is "/" on iOS.
        path_ = filesystem::temp_directory_path() / "fbo-readback-test.png";
        screenPath_ = filesystem::temp_directory_path() / "fbo-readback-screen.png";
        filesystem::remove(path_);
        filesystem::remove(screenPath_);
    }

    void draw() override {
        if (completed) return; // exitApp() is a no-op on iOS; draw keeps running
        if (frames_ > 0) {
            check("exactly one commit since previous draw", commits == frames_);
            Image screen;
            check("previous frame screenshot loads", static_cast<bool>(screen.load(screenPath_)));
            if (screen.isAllocated()) {
                const Color red = screen.getPixels().getColor(32, 32);
                const Color secondRed = screen.getPixels().getColor(96, 32);
                const Color green = screen.getPixels().getColor(160, 32);
                // The previous frame's blit covers this probe (see blitFrame()).
                if (!blitFrame(frames_ - 1))
                    check("2D before no-pass readback remains visible", red.r > 0.9f && red.g < 0.1f && red.b < 0.1f);
                check("2D before open-pass readback remains visible",
                      secondRed.r > 0.9f && secondRed.g < 0.1f && secondRed.b < 0.1f);
                check("2D after readback remains visible", green.r < 0.1f && green.g > 0.9f && green.b < 0.1f);
            }
        }
        if (frames_ == 4) {
            completed = true;
            exitApp();
            return;
        }
        clear(0.1f);
        setColor(1, 0, 0);
        drawRect(8, 8, 48, 48);
        // Deferred screen 2D must survive readback before any swapchain pass.
        renderFbo(false);
        readAll(false);

        if (blitFrame(frames_)) {
            fullscreen_.setTexture(0, fbo_.getTextureView(), fbo_.getSampler());
            fullscreen_.draw();
            check("FullscreenShader opens swapchain pass", isInSwapchainPass());
        } else {
            internal::ensureCurrentPass();
        }
        const uint32_t firstVersion = fbo_.getColorImage().id;
        setColor(1, 1, 1);
        fbo_.draw(184, 56, 8, 8); // marks this version as used by a recorded draw
        setColor(1, 0, 0);
        drawRect(72, 8, 48, 48);
        renderFbo(true); // suspends and resumes the pass before readback does so
        check("second begin uses a different pool texture", fbo_.getColorImage().id != firstVersion);
        readAll(true);

        half_.begin(0.25f, 0.5f, 1.0f, 1.0f);
#if defined(__linux__) || defined(__APPLE__)
        checkReadbackDuringFboPass();
#endif
        half_.end();
        vector<float> floats(16 * 16 * 4);
        check("RGBA16F float readback succeeds", half_.readPixelsFloat(floats.data()));
        check("RGBA16F values are correct", floats[0] == 0.25f && floats[1] == 0.5f && floats[2] == 1.0f);
        vector<unsigned char> bytes(16 * 16 * 4, 123);
        check("RGBA16F byte readback succeeds", half_.readPixels(bytes.data()));
#if defined(SOKOL_GLCORE)
        const int roundingAllowance = 1;
#else
        const int roundingAllowance = 0;
#endif
        check("RGBA16F byte conversion is correct",
              abs(int(bytes[0]) - 64) <= roundingAllowance && abs(int(bytes[1]) - 128) <= roundingAllowance &&
              bytes[2] == 255 && bytes[3] == 255);
        check("float/byte read preserves swapchain pass", isInSwapchainPass());
        check("readbacks never commit the frame", commits == frames_);
        setColor(0, 1, 0);
        drawCircle(160, 32, 20);
        check("screen capture queued for after present", saveScreenshot(screenPath_));
        ++frames_;
    }

    void exit() override {
        sg_remove_commit_listener(listener);
        filesystem::remove(path_);
        filesystem::remove(screenPath_);
    }

private:
    void checkAllocationFailure() {
        // Setup has no open screen pass yet. Start one so probe.begin() flushes
        // queued probe.draw() calls before the pool tests drain published textures.
        internal::ensureCurrentPass();
        const int limit = sg_query_limits().max_image_size_2d;
        printf("device texture limit: %d\n", limit);
        Fbo probe;
        for (int samples : {1, 4}) {
            for (bool mips : {false, true}) {
                for (bool tall : {false, true}) {
                    // Start with valid resources, including a version-pool entry.
                    probe.allocate(8, 8, samples, TextureFormat::RGBA8, mips);
                    probe.begin(1, 0, 0); probe.end();
                    probe.draw(0, 0);
                    probe.begin(0, 1, 0); probe.end();
                    const int w = tall ? 1 : limit + 1;
                    const int h = tall ? limit + 1 : 1;
                    int warnings = 0, errors = 0;
                    EventListener sub = getLogger().onLog.listen([&](LogEventArgs& e) {
                        if (e.level == LogLevel::Warning &&
                            e.message == "[Fbo] allocate failed: size " + to_string(w) + "x" + to_string(h) +
                                " exceeds the device texture limit " + to_string(limit)) ++warnings;
                        if (e.level == LogLevel::Error) ++errors;
                    });
                    probe.allocate(w, h, samples, TextureFormat::RGBA8, mips);
                    check("oversize allocation logs exactly one size/limit Warning", warnings == 1 && errors == 0);
                    check("oversize allocation leaves no texture or dimensions",
                          !probe.isAllocated() && !probe.getTexture().isAllocated() &&
                          probe.getColorImage().id == 0 && probe.getWidth() == 0 && probe.getHeight() == 0);
                    auto* parent = internal::currentWindowContext().currentTarget;
                    const int vertices = sgl_num_vertices();
                    unsigned char byte = 123;
                    float value = -1;
                    Image copy;
                    probe.begin(); probe.begin(0, 0, 0); probe.end();
                    probe.draw(0, 0); probe.draw(0, 0, 1, 1);
                    check("failed Fbo begin/draw leave parent and vertices unchanged",
                          !probe.isActive() && internal::currentWindowContext().currentTarget == parent &&
                          sgl_num_vertices() == vertices);
                    check("failed Fbo readbacks leave tiny destinations unchanged",
                          !probe.readPixels(&byte) && byte == 123 &&
                          !probe.readPixelsFloat(&value) && value == -1 &&
                          !probe.copyTo(copy) && !copy.isAllocated());
                    check("failed operations produce no further warnings/errors", warnings == 1 && errors == 0);
                    probe.allocate(8, 8, samples, TextureFormat::RGBA8, mips);
                    check("valid allocation recovers after failure", probe.isAllocated());
                    probe.begin(1, 0, 0); probe.end();
                    unsigned char pixels[8 * 8 * 4] = {};
                    check("recovered Fbo renders and reads", probe.readPixels(pixels) && pixels[0] == 255);
                }
            }
        }
        // A lazily allocated version may fail with a zero image handle. Keep
        // the current valid version and render safely with the existing fallback.
        for (int samples : {1, 4}) {
            probe.allocate(8, 8, samples);
            probe.begin(1, 0, 0); probe.end();
            probe.draw(0, 0);
            const uint32_t image = probe.getColorImage().id;
            vector<sg_image> held;
            for (;;) {
                sg_image slot = sg_alloc_image();
                if (!slot.id) break;
                held.push_back(slot);
            }
            probe.begin(0, 1, 0);
            check("version allocation failure keeps current valid image",
                  probe.isActive() && probe.getColorImage().id == image);
            probe.end();
            for (sg_image slot : held) sg_dealloc_image(slot);
            unsigned char pixels[8 * 8 * 4] = {};
            check("version failure fallback renders and reads",
                  probe.readPixels(pixels) && pixels[1] == 255);
        }
        // Exhaust image slots without allocating GPU storage. Leave just enough
        // for selected earlier attachments, forcing each partial failure path.
        for (int samples : {1, 4}) {
            for (int available = 0; available < (samples > 1 ? 3 : 2); ++available) {
                probe.clear();
                internal::drainPendingGpuDestroys();
                vector<sg_image> held;
                for (;;) {
                    sg_image image = sg_alloc_image();
                    if (!image.id) break;
                    held.push_back(image);
                }
                for (int i = 0; i < available; ++i) {
                    sg_dealloc_image(held.back()); held.pop_back();
                }
                const size_t slots = held.size() + available;
                probe.allocate(8, 8, samples);
                check("partial colour/MSAA/depth failure leaves Fbo unallocated", !probe.isAllocated());
                for (sg_image image : held) sg_dealloc_image(image);
                held.clear();
                for (;;) {
                    sg_image image = sg_alloc_image();
                    if (!image.id) break;
                    held.push_back(image);
                }
                check("partial failure returns every image slot", held.size() == slots);
                for (sg_image image : held) sg_dealloc_image(image);
                probe.allocate(8, 8, samples);
                check("valid allocate after partial failure recovers", probe.isAllocated());
            }
        }
        // Leave exactly the images needed by a normal Fbo, but no views.
        // Free only the held views after failure: retry must work in this frame.
        probe.clear();
        internal::drainPendingGpuDestroys();
        vector<sg_image> heldImages;
        for (;;) {
            sg_image image = sg_alloc_image();
            if (!image.id) break;
            heldImages.push_back(image);
        }
        for (int i = 0; i < 2; ++i) {
            sg_dealloc_image(heldImages.back()); heldImages.pop_back();
        }
        vector<sg_view> heldViews;
        for (;;) {
            sg_view view = sg_alloc_view();
            if (!view.id) break;
            heldViews.push_back(view);
        }
        int rejected = 0, sizeWarnings = 0;
        EventListener poolSub = getLogger().onLog.listen([&](LogEventArgs& e) {
            if (e.level != LogLevel::Warning) return;
            if (e.message == "[Fbo] allocate failed: the GPU rejected the 8x8 attachments") ++rejected;
            if (e.message.find("limit") != string::npos) ++sizeWarnings;
        });
        probe.allocate(8, 8);
        poolSub.disconnect();
        check("pool-pressure allocation fails without views", !probe.isAllocated());
        check("backend failure logs one rejection Warning without a size limit",
              rejected == 1 && sizeWarnings == 0);
        for (sg_view view : heldViews) sg_dealloc_view(view);
        probe.allocate(8, 8);
        check("pool-pressure retry succeeds in same frame without drain", probe.isAllocated());
        for (sg_image image : heldImages) sg_dealloc_image(image);
        // Exhaust views too: Texture can have a valid image but missing views.
        for (int samples : {1, 4}) {
            probe.clear();
            internal::drainPendingGpuDestroys();
            vector<sg_view> held;
            for (;;) {
                sg_view view = sg_alloc_view();
                if (!view.id) break;
                held.push_back(view);
            }
            probe.allocate(8, 8, samples, TextureFormat::RGBA8, true);
            check("view failure leaves Fbo and colour texture unallocated",
                  !probe.isAllocated() && !probe.getTexture().isAllocated());
            const size_t slots = held.size();
            for (sg_view view : held) sg_dealloc_view(view);
            held.clear();
            for (;;) {
                sg_view view = sg_alloc_view();
                if (!view.id) break;
                held.push_back(view);
            }
            check("partial failure returns every view slot", held.size() == slots);
            for (sg_view view : held) sg_dealloc_view(view);
            probe.allocate(8, 8, samples, TextureFormat::RGBA8, true);
            check("valid allocate after view failure recovers", probe.isAllocated());
        }
        // Preserve the no-pass starting state required by the readback tests.
        internal::currentTarget().endPass();
    }

    // The fullscreen blit covers the whole screen opaquely, so 2D drawn before
    // it is never visible. Only frames without it can show that deferred 2D
    // from before a no-pass readback reaches the screen (#833).
    static bool blitFrame(int frame) { return frame % 2 == 1; }

    void checkReadbackDuringFboPass() {
        // Read a different Fbo: checking only the source's active_ misses this.
        int errors = 0;
        EventListener logSub = getLogger().onLog.listen([&](LogEventArgs& e) {
            if (e.level == LogLevel::Error &&
                e.message == "[Fbo] readPixels() inside fbo.begin()/end() is not supported; call it after end()") ++errors;
        });
        vector<unsigned char> bytes(16 * 16 * 4, 123);
        vector<float> floats(16 * 16 * 4, -1.0f);
        check("readPixels during another Fbo pass is rejected", !fbo_.readPixels(bytes.data()));
        check("rejected readPixels logs the Fbo error", errors == 1);
        check("rejected byte read leaves destination unchanged",
              all_of(bytes.begin(), bytes.end(), [](unsigned char v) { return v == 123; }));
        check("readPixelsFloat during another Fbo pass is rejected", !fbo_.readPixelsFloat(floats.data()));
        check("rejected readPixelsFloat logs the Fbo error", errors == 2);
        check("rejected float read leaves destination unchanged",
              all_of(floats.begin(), floats.end(), [](float v) { return v == -1.0f; }));
        Image copy;
        check("copyTo during another Fbo pass is rejected", !fbo_.copyTo(copy));
        check("rejected copyTo logs the Fbo error", errors == 3);
        check("save during another Fbo pass is rejected", !fbo_.save(path_));
        check("rejected save logs the Fbo error", errors == 4);
        check("rejected reads leave the Fbo pass open",
              internal::currentWindowContext().currentTarget->isFbo && half_.isActive());
    }

    void renderFbo(bool secondPass) {
        // Change colour both between frames and between versions in one frame.
        // Reading the previous pool texture must not accidentally pass (#641).
        expectRed_ = (frames_ % 2 != 0) != secondPass;
        fbo_.begin(expectRed_ ? 1.0f : 0.0f, 0, expectRed_ ? 0.0f : 1.0f, 1);
        fbo_.end();
    }
    bool expected(const unsigned char* p) const {
        return p[0] == (expectRed_ ? 255 : 0) && p[1] == 0
            && p[2] == (expectRed_ ? 0 : 255) && p[3] == 255;
    }
    void readAll(bool passOpen) {
        const int vertices = sgl_num_vertices();
        const int before = commits;
        vector<unsigned char> bytes(16 * 16 * 4);
        check("RGBA8 readPixels succeeds", fbo_.readPixels(bytes.data()));
        bool correct = true;
        for (size_t i = 0; i < bytes.size(); i += 4) correct &= expected(bytes.data() + i);
        check("readPixels sees the current version at every pixel", correct);
        vector<float> floats(16 * 16 * 4);
        check("RGBA8 float readback succeeds", fbo_.readPixelsFloat(floats.data()));
        check("RGBA8 float conversion is correct", floats[0] == bytes[0] / 255.0f && floats[2] == bytes[2] / 255.0f);
        Image copy;
        check("copyTo succeeds", fbo_.copyTo(copy));
        check("copyTo has correct pixels", copy.isAllocated() && expected(copy.getPixelsData()));
        check("save succeeds synchronously", fbo_.save(path_).ok());
        Image saved;
        check("saved file loads immediately", static_cast<bool>(saved.load(path_)));
        check("saved file has correct pixels", saved.isAllocated() && expected(saved.getPixelsData()));
        check("readbacks retain screen 2D vertices", vertices > 0 && sgl_num_vertices() == vertices);
        check("readbacks do not call commit listeners", commits == before);
        check("readbacks restore swapchain pass state", isInSwapchainPass() == passOpen);
    }
    Fbo fbo_, half_;
    FullscreenShader fullscreen_;
    filesystem::path path_, screenPath_;
    int frames_ = 0;
    bool expectRed_ = false;
};
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc <= 1 || strcmp(argv[1], "--gpu-check") != 0) {
        printf("SKIP: fboReadback needs --gpu-check and a display\n");
        return 0;
    }
    WindowSettings settings;
    settings.setSize(192, 64);
    settings.setHighDpi(false);
    return tcCoreTest::runDisplayTest<ReadbackApp>(settings, failures, [] {
        check("all four frames completed", completed);
    });
}
