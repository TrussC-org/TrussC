// #327: FBO passes reset only sokol_gl's stacks and report its errors.
// Run with --gpu-check on a display (Xvfb works on Linux).
#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include <cstdio>
#include <cstring>

using namespace std;
using namespace tc;

namespace {
int failures = 0;
bool finished = false;
int stackWarnings = 0;

void check(const char* name, bool ok) {
    printf("%-64s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

bool sameMatrix(const Mat4& a, const Mat4& b) {
    for (int i = 0; i < 16; ++i) {
        if (a.m[i] != b.m[i]) return false;
    }
    return true;
}

class FboStackApp : public App {
public:
    void setup() override {
        setFps(VSYNC);
        first_.allocate(64, 64);
        second_.allocate(64, 64);
    }

    void update() override {
        if (!errorChecked_) {
            first_.begin(0, 0, 0, 1);
            sgl_pop_matrix();  // Raw error on the FBO context, before present().
            check("FBO underflow was induced", sgl_error().stack_underflow);
            const int before = stackWarnings;
            first_.end();
            check("Fbo::end reports the error before present", stackWarnings > before);
            errorChecked_ = true;
            return;  // sg_commit clears the error before the leaking passes.
        }
        if (leaks_ >= 200) return;

        auto& rc = internal::getDefaultContext();
        const size_t depth = rc.getMatrixStackDepth();
        first_.begin(0, 0, 0, 1);
        pushMatrix();  // Deliberately left open, once per update for 200 frames.
        drawRect(0, 0, 8, 8);
        first_.end();
        depthsPreserved_ &= rc.getMatrixStackDepth() == depth + 1;
        ++leaks_;
        leakedThisFrame_ = true;
    }

    void draw() override {
        clear(0.0f);
        if (leakedThisFrame_) {
            leakedThisFrame_ = false;
            second_.begin(0, 0, 0, 1);
            setColor(1.0f);
            pushMatrix();
            translate(16, 16);
            drawRect(0, 0, 16, 16);
            popMatrix();
            drawsHealthy_ &= !sgl_error().any;
            second_.end();
            if (leaks_ == 200) {
                check("second FBO has no errors across 200 leaking passes", drawsHealthy_);
                check("second FBO still draws at frame 200", pixelIsWhite(second_, 20, 20));
                check("FBO boundary leaves TrussC's leaked push untouched", depthsPreserved_);
            }
            return;
        }
        if (leaks_ < 200) return;

        // On the following frame the entry/frame guards and sg_commit have
        // cleaned up the intentional leak. Test a balanced cross-pass pair.
        auto& rc = internal::getDefaultContext();
        const size_t depth = rc.getMatrixStackDepth();
        const Mat4 original = getMatrix();
        const int warningsBefore = stackWarnings;
        pushMatrix();
        first_.begin(0, 0, 0, 1);
        translate(16, 16);
        const Mat4 inside = getMatrix();
        setColor(1.0f);
        drawRect(0, 0, 16, 16);
        first_.end();
        check("end preserves the matrix and push from outside the pass",
              rc.getMatrixStackDepth() == depth + 1 && sameMatrix(getMatrix(), inside));
        popMatrix();
        check("pop outside the pass restores the screen matrix",
              rc.getMatrixStackDepth() == depth && sameMatrix(getMatrix(), original));
        check("cross-pass push/pop leaves screen sokol_gl healthy", !sgl_error().any);
        check("cross-pass push/pop emits no stack error", stackWarnings == warningsBefore);

        // Draw the result later into the second FBO and verify its pixels.
        second_.begin(0, 0, 0, 1);
        first_.draw(0, 0);
        second_.end();
        check("cross-pass push/pop renders the expected rectangle", pixelIsWhite(second_, 20, 20));
        finished = true;
        exitApp();
    }

private:
    static bool pixelIsWhite(const Fbo& fbo, int x, int y) {
        vector<unsigned char> pixels(64 * 64 * 4);
        if (!fbo.readPixels(pixels.data())) return false;
        const auto* p = &pixels[(y * 64 + x) * 4];
        return p[0] >= 250 && p[1] >= 250 && p[2] >= 250 && p[3] >= 250;
    }

    Fbo first_, second_;
    int leaks_ = 0;
    bool errorChecked_ = false;
    bool leakedThisFrame_ = false;
    bool depthsPreserved_ = true;
    bool drawsHealthy_ = true;
};
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc < 2 || strcmp(argv[1], "--gpu-check") != 0) {
        printf("SKIP: fboStack needs --gpu-check and a display\n");
        return 0;
    }
    auto listener = getLogger().onLog.listen([](LogEventArgs& e) {
        if (e.level == LogLevel::Warning && e.message.find("matrix stack") != string::npos) {
            ++stackWarnings;
        }
    });
    WindowSettings settings;
    settings.setSize(64, 64);
    settings.setHighDpi(false);
    settings.setSwapInterval(0);
    runApp<FboStackApp>(settings);
    check("completed all GPU checks", finished);
    return failures ? 1 : 0;
}
