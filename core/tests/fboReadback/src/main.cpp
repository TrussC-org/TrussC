// #270: run with --gpu-check (Xvfb works on Linux; Metal needs a Mac).
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
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
        fbo_.allocate(16, 16);
        half_.allocate(16, 16, 1, TextureFormat::RGBA16F);
        check("fullscreen shader loads", fullscreen_.load(tc_fbomip_blit_shader_desc));
        check("commit listener installed", sg_add_commit_listener(listener));
        // Use the working directory, not a macOS app bundle's data folder.
        path_ = filesystem::current_path() / "fbo-readback-test.png";
        screenPath_ = filesystem::current_path() / "fbo-readback-screen.png";
        filesystem::remove(path_);
        filesystem::remove(screenPath_);
    }

    void draw() override {
        if (frames_ > 0) {
            check("exactly one commit since previous draw", commits == frames_);
            Image screen;
            check("previous frame screenshot loads", static_cast<bool>(screen.load(screenPath_)));
            if (screen.isAllocated()) {
                const Color red = screen.getPixels().getColor(32, 32);
                const Color secondRed = screen.getPixels().getColor(96, 32);
                const Color green = screen.getPixels().getColor(160, 32);
                check("2D before no-pass readback remains visible", red.r > 0.9f && red.g < 0.1f && red.b < 0.1f);
                check("2D before open-pass readback remains visible",
                      secondRed.r > 0.9f && secondRed.g < 0.1f && secondRed.b < 0.1f);
                check("2D after readback remains visible", green.r < 0.1f && green.g > 0.9f && green.b < 0.1f);
            }
        }
        if (frames_ == 3) {
            completed = true;
            exitApp();
            return;
        }
        clear(0.1f);
        setColor(1, 0, 0);
        drawRect(8, 8, 48, 48);
        // Deferred screen 2D must survive readback before any swapchain pass.
        renderFbo();
        readAll(false);

        fullscreen_.setTexture(0, fbo_.getTextureView(), fbo_.getSampler());
        fullscreen_.draw();
        check("FullscreenShader opens swapchain pass", isInSwapchainPass());
        setColor(1, 0, 0);
        drawRect(72, 8, 48, 48);
        renderFbo(); // suspends and resumes the pass before readback does so
        readAll(true);

        half_.begin(0.25f, 0.5f, 1.0f, 1.0f);
#if defined(__linux__) || defined(__APPLE__)
        checkReadbackDuringFboPass();
#endif
        half_.end();
        vector<float> floats(16 * 16 * 4);
        check("RGBA16F float readback succeeds", half_.readPixelsFloat(floats.data()));
        check("RGBA16F values are correct", floats[0] == 0.25f && floats[1] == 0.5f && floats[2] == 1.0f);
#if defined(SOKOL_METAL)
        vector<unsigned char> bytes(16 * 16 * 4, 123);
        check("RGBA16F byte readback is rejected", !half_.readPixels(bytes.data()));
        check("rejected read leaves destination unchanged",
              all_of(bytes.begin(), bytes.end(), [](unsigned char v) { return v == 123; }));
#endif
        check("float/rejected read preserves swapchain pass", isInSwapchainPass());
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
    void checkReadbackDuringFboPass() {
        // Read a different Fbo: checking only the source's active_ misses this.
        int errors = 0;
        EventListener logSub = getLogger().onLog.listen([&](LogEventArgs& e) {
            if (e.level == LogLevel::Error &&
                e.message == "[Fbo] read back after fbo.end()") ++errors;
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
              internal::currentWindowContext().inFboPass && half_.isActive());
    }

    void renderFbo() {
        // Alternate contents each frame to expose stale GPU reads.
        fbo_.begin(frames_ % 2 ? 1.0f : 0.0f, 0, frames_ % 2 ? 0.0f : 1.0f, 1);
        fbo_.end();
    }
    bool expected(const unsigned char* p) const {
        return p[0] == (frames_ % 2 ? 255 : 0) && p[1] == 0
            && p[2] == (frames_ % 2 ? 0 : 255) && p[3] == 255;
    }
    void readAll(bool passOpen) {
        const int vertices = sgl_num_vertices();
        const int before = commits;
        vector<unsigned char> bytes(16 * 16 * 4);
        check("RGBA8 readPixels succeeds", fbo_.readPixels(bytes.data()));
        check("readPixels sees this frame's render", expected(bytes.data()));
        vector<float> floats(16 * 16 * 4);
        check("RGBA8 float readback succeeds", fbo_.readPixelsFloat(floats.data()));
        check("RGBA8 float conversion is correct", floats[0] == bytes[0] / 255.0f && floats[2] == bytes[2] / 255.0f);
        Image copy;
        check("copyTo succeeds", fbo_.copyTo(copy));
        check("copyTo has correct pixels", copy.isAllocated() && expected(copy.getPixelsData()));
        check("save succeeds synchronously", fbo_.save(path_));
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
    runApp<ReadbackApp>(settings);
    check("all three frames completed", completed);
    return failures ? 1 : 0;
}
