// #861: an MSAA resolve must cover the whole attachment after a clipped draw.
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
constexpr int width = 32, height = 24;
constexpr int clipX = 2, clipY = 3;

void check(const char* name, bool ok) {
    std::printf("msaaResolveScissor: %s: %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}

// Check every RGBA byte. Red exposes stale resolve contents from the seed
// pass. The separate single-sample control also requires one blue clip pixel.
bool matches(const unsigned char* pixels, bool clipped) {
    if (!pixels) return false;
    int wrong = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const bool blue = clipped && x == clipX && y == clipY;
            const auto* p = pixels + (y * width + x) * 4;
            if (p[0] != 0 || p[1] != (blue ? 0 : 255) ||
                p[2] != (blue ? 255 : 0) || p[3] != 255) {
                if (wrong < 8) std::printf("  (%d,%d)=%u,%u,%u,%u expected %s\n",
                    x, y, p[0], p[1], p[2], p[3], blue ? "blue" : "green");
                ++wrong;
            }
        }
    }
    std::printf("msaaResolveScissor: mismatched pixels=%d/%d\n", wrong, width * height);
    return wrong == 0;
}

class ResolveApp : public App {
public:
    void draw() override {
        if (completed) return;
        Fbo following;
        following.allocate(width, height);
        check("following single-sample Fbo allocated", following.isAllocated());
        for (int samples : {1, 4}) {
            for (bool mips : {false, true}) run(samples, mips);
        }
        // This pass follows the last MSAA resolve and its mip passes. A missing
        // scissor re-enable would let the fullscreen blue draw overwrite green.
        following.begin(0, 1, 0, 1);
        setScissor(clipX, clipY, 1, 1);
        setColor(0, 0, 1, 1);
        drawRect(0, 0, width, height);
        following.end();
        std::vector<unsigned char> pixels(width * height * 4);
        check("following single-sample read succeeds", following.readPixels(pixels.data()));
        check("scissor still clips drawing after MSAA resolve", matches(pixels.data(), true));
        completed = true;
        exitApp();
    }

private:
    void run(int samples, bool mips) {
        std::printf("msaaResolveScissor: samples=%d mipmaps=%d\n", samples, mips);
        Fbo fbo;
        fbo.allocate(width, height, samples, TextureFormat::RGBA8, mips);
        check("requested sample count allocated", fbo.isAllocated() && fbo.getSampleCount() == samples);
        fbo.begin(1, 0, 0, 1);
        fbo.end();
        std::vector<unsigned char> pixels(width * height * 4);
        check("seed read succeeds", fbo.readPixels(pixels.data()));
        bool red = true;
        for (size_t i = 0; i < pixels.size(); i += 4)
            red &= pixels[i] == 255 && pixels[i + 1] == 0 && pixels[i + 2] == 0 && pixels[i + 3] == 255;
        check("resolve destination seeded red everywhere", red);

        fbo.begin(0, 1, 0, 1);
        setScissor(clipX, clipY, 1, 1);
        // Draw the clear colour so the full-image expectation is exact even
        // on drivers whose MSAA edge coverage reaches adjacent clip pixels.
        setColor(0, 1, 0, 1);
        drawRect(0, 0, width, height);
        // Deliberately leave the final draw's 1x1 scissor active at end().
        fbo.end();

        // A small parent clip also must not restrict readPixels or copyTo.
        setScissor(0, 0, 1, 1);
        internal::ensureCurrentPass();
        sg_apply_scissor_rect(0, 0, 1, 1, true);
        check("clipped-pass read succeeds", fbo.readPixels(pixels.data()));
        check("whole resolve has the green clear colour", matches(pixels.data(), false));
        Image copy;
        check("copyTo under a parent scissor succeeds", fbo.copyTo(copy));
        check("copyTo reads the whole resolve", copy.isAllocated() && matches(copy.getPixelsData(), false));
        resetScissor();

        // Repeat without reseeding the resolve destination.
        fbo.begin(0, 1, 0, 1);
        setScissor(clipX, clipY, 1, 1);
        setColor(0, 1, 0, 1);
        drawRect(0, 0, width, height);
        fbo.end();
        check("following pass read succeeds", fbo.readPixels(pixels.data()));
        check("repeated clipped pass resolves the whole clear", matches(pixels.data(), false));

        // Empty CLEAR also resolves fully, including when no pipeline is applied.
        fbo.begin(0, 1, 0, 1);
        internal::ensureCurrentPass();
        sg_apply_scissor_rect(0, 0, 1, 1, false);
        fbo.end();
        check("empty clipped pass read succeeds", fbo.readPixels(pixels.data()));
        check("empty clipped pass resolves the whole clear", matches(pixels.data(), false));
    }
};
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc <= 1 || std::strcmp(argv[1], "--gpu-check") != 0) {
        std::printf("SKIP: msaaResolveScissor needs --gpu-check and a display\n");
        return 0;
    }
    WindowSettings settings;
    settings.setSize(64, 64);
    settings.setHighDpi(false);
    return tcCoreTest::runDisplayTest<ResolveApp>(settings, failures, [] {
        check("all cases completed", completed);
    });
}
