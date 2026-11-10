#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <cstdio>
#include <cstring>

using namespace tc;
namespace {
int failures = 0;
bool completed = false;
bool cacheWarmed = false;
std::string evidenceDirectory;
void check(const char* label, bool ok) {
    std::printf("stencilRoundTrip %s: %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
Pixels read(const Fbo& fbo) {
    Pixels result;
    result.allocate(fbo.getWidth(), fbo.getHeight(), 4);
    check("color readback", fbo.readPixels(result.getData()));
    return result;
}
bool sameBytes(const Pixels& a, const Pixels& b) {
    return a.getWidth() == b.getWidth() && a.getHeight() == b.getHeight() &&
        std::memcmp(a.getData(), b.getData(), size_t(a.getWidth()) * a.getHeight() * 4) == 0;
}
void saveGray(const Pixels& rgba, const char* name) {
    if (evidenceDirectory.empty()) return;
    Pixels gray;
    gray.allocate(rgba.getWidth(), rgba.getHeight(), 1);
    for (int i = 0; i < rgba.getWidth() * rgba.getHeight(); ++i)
        gray.getData()[i] = rgba.getData()[i * 4];
    check("PNG evidence", static_cast<bool>(gray.save(evidenceDirectory + "/" + name)));
}
unsigned char pattern(int x, int y, bool ramp, bool inverse) {
    const int value = ramp ? x + 16 * y : (x * 37 + y * 71 + (x ^ y) * 13) & 255;
    return static_cast<unsigned char>(inverse ? 255 - value : value);
}
void fillGray(Fbo& gray, bool ramp, bool inverse) {
    gray.begin(0, 0, 0);
    setupScreenOrtho();
    setBlendMode(BlendMode::Disabled);
    for (int y = 0; y < gray.getHeight(); ++y) {
        for (int x = 0; x < gray.getWidth(); ++x) {
            setColor(pattern(x, y, ramp, inverse) / 255.0f);
            drawRect(float(x), float(y), 1, 1);
        }
    }
    gray.end();
}
void roundTrip(int width, int height, bool ramp) {
    Fbo gray, stencil, result;
    gray.allocate(width, height, 1, TextureFormat::R8);
    stencil.allocate(width, height);
    result.allocate(width, height, 1, TextureFormat::R8);
    stencil.begin(0.25f, 0.5f, 0.75f, 1); stencil.end();
    const Pixels original = read(stencil);
    for (bool inverse : {false, true}) {
        fillGray(gray, ramp, inverse);
        const Pixels input = read(gray);
        // Check source rendering and top-left pixel orientation separately
        // from the stencil round trip.
        bool inputExact = true;
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                inputExact &= input.getData()[(y * width + x) * 4] == pattern(x, y, ramp, inverse);
        check("gray input bytes and orientation", inputExact);
        const auto pipelinesBefore = sg_query_stats().total.pipelines.allocated;
        check("gray to stencil", internal::FboStencilAccess::writeGray(gray, stencil));
        check("stencil to R8", internal::FboStencilAccess::readR8(stencil, result));
        const auto pipelinesAfter = sg_query_stats().total.pipelines.allocated;
        check("fixed 16 pipelines created once", pipelinesAfter - pipelinesBefore == (cacheWarmed ? 0u : 16u));
        cacheWarmed = true;
        const Pixels output = read(result);
        check(ramp ? "all 256 values byte-exact" : "asymmetric pattern byte-exact", sameBytes(input, output));
        check("read again preserves stencil", internal::FboStencilAccess::readR8(stencil, result));
        check("repeat read reuses pipelines", sg_query_stats().total.pipelines.allocated == pipelinesAfter);
        check("second read byte-exact", sameBytes(output, read(result)));
        check("original color preserved", sameBytes(original, read(stencil)));
        check("gray source preserved", sameBytes(input, read(gray)));
        if (!ramp && !inverse) {
            saveGray(input, "00_gray-input.png");
            saveGray(output, "01_stencil-readback.png");
        }
    }
    // Ordinary capability and usage boundaries fail before opening a pass.
    Fbo absent, wrongSize, wrongFormat, msaa;
    wrongSize.allocate(width + 1, height, 1, TextureFormat::R8);
    wrongFormat.allocate(width, height);
    check("unallocated source refused", !internal::FboStencilAccess::readR8(absent, result));
    check("size mismatch refused", !internal::FboStencilAccess::writeGray(wrongSize, stencil));
    check("non-R8 source refused", !internal::FboStencilAccess::writeGray(wrongFormat, stencil));
    check("non-R8 target refused", !internal::FboStencilAccess::readR8(stencil, wrongFormat));
    check("alias refused", !internal::FboStencilAccess::writeGray(gray, gray) &&
          !internal::FboStencilAccess::readR8(result, result));
    gray.begin();
    check("active source refused", !internal::FboStencilAccess::writeGray(gray, stencil));
    gray.end();
    stencil.begin();
    check("active stencil refused", !internal::FboStencilAccess::readR8(stencil, result));
    stencil.end();
    if (sg_query_pixelformat(SG_PIXELFORMAT_RGBA8).msaa) {
        msaa.allocate(width, height, 4);
        check("MSAA allocated", msaa.isAllocated());
        check("MSAA write refused", !internal::FboStencilAccess::writeGray(gray, msaa));
        check("MSAA read refused", !internal::FboStencilAccess::readR8(msaa, result));
    }
}
class StencilApp : public App {
    void draw() override {
        sg_enable_stats();
        // A parked parent has deferred draws and a restrictive scissor. The
        // diagnostic passes must cover their full extent and restore it.
        roundTrip(16, 16, true);
        roundTrip(37, 19, false);
        Fbo gray, stencil, result;
        gray.allocate(16, 16, 1, TextureFormat::R8);
        stencil.allocate(16, 16);
        result.allocate(16, 16, 1, TextureFormat::R8);
        fillGray(gray, true, false);
        const Pixels input = read(gray);
        Fbo parent;
        parent.allocate(8, 8);
        parent.begin(0, 0, 0);
        setupScreenOrtho(); setColor(1, 0, 0); drawRect(0, 0, 4, 8);
        pushScissor(0, 0, 4, 8);
        auto* target = &internal::currentTarget();
        check("write with parked parent", internal::FboStencilAccess::writeGray(gray, stencil));
        check("read with parked parent", internal::FboStencilAccess::readR8(stencil, result));
        check("parent target and pass restored", &internal::currentTarget() == target && target->inPass);
        setColor(0, 1, 0); drawRect(0, 0, 8, 8);
        popScissor(); parent.end();
        check("parked-parent round trip", sameBytes(input, read(result)));
        const Pixels px = read(parent);
        check("parent scissor restored", px.getData()[0] == 0 && px.getData()[1] == 255 &&
              px.getData()[7 * 4] == 0 && px.getData()[7 * 4 + 1] == 0);
        completed = true;
        exitApp();
    }
};
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc < 2 || std::strcmp(argv[1], "--gpu-check") != 0) {
        std::puts("SKIP: stencilRoundTrip needs --gpu-check and a display");
        return 0;
    }
    if (argc == 3) evidenceDirectory = argv[2];
    WindowSettings settings;
    settings.setSize(64, 64).setHighDpi(false).setSampleCount(1).setSwapInterval(0);
    return tcCoreTest::runDisplayTest<StencilApp>(settings, failures, [] {
        check("test completed", completed);
    });
}
