// #1525: many growing flushes retain distinct storage and correct pixels.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <cstdio>
#include <cstring>
#include <vector>

using namespace tc;

namespace {
int failures = 0;
bool completed = false;
void check(const char* label, bool ok) {
    std::printf("%s: %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

class FlushApp : public App {
public:
    void setup() override { target_.allocate(128, 32, 1); }
    void draw() override {
        clear(0.0f);
        target_.begin(0, 0, 0, 1);
        // Cumulative uploads increase within each frame. The second frame is
        // much larger, forcing reuse and growth of the same pool slots.
        for (int batch = 0; batch < 16; ++batch) {
            setColor((batch & 1) ? 0.0f : 1.0f, (batch & 1) ? 1.0f : 0.0f, 0.0f, 1.0f);
            const int repeats = (frame_ == 1 ? 128 : 1) * (batch + 1);
            for (int i = 0; i < repeats; ++i) drawRect(batch * 8, 0, 8, 32);
            sgl_draw();
            sgl_tc_draw_rewind();
            check("flush has no sokol_gl error", !sgl_error().any);
        }
        target_.end();
        std::vector<unsigned char> pixels(128 * 32 * 4);
        const bool read = target_.readPixels(pixels.data());
        check("readback succeeds", read);
        if (read) {
            bool correct = true;
            for (int batch = 0; batch < 16; ++batch) {
                const auto* p = &pixels[(16 * 128 + batch * 8 + 4) * 4];
                correct &= p[0] == ((batch & 1) ? 0 : 255)
                    && p[1] == ((batch & 1) ? 255 : 0) && p[2] == 0 && p[3] == 255;
            }
            check("all earlier and later flush pixels survive", correct);
        }
        if (++frame_ == 3) { completed = true; exitApp(); }
    }
private:
    Fbo target_;
    int frame_ = 0;
};
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc < 2 || std::strcmp(argv[1], "--gpu-check") != 0) {
        std::printf("SKIP: sglFlushBuffers needs --gpu-check and a display\n");
        return 0;
    }
    WindowSettings settings;
    settings.setSize(128, 32);
    settings.setHighDpi(false);
    settings.setSwapInterval(0);
    return tcCoreTest::runDisplayTest<FlushApp>(settings, failures, [] {
        check("completed all flush frames", completed);
    });
}
