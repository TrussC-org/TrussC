// #1422: raw sampling, public readback and drawing agree on canonical storage.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include "orientation.glsl.h"
#include <cstdio>
#include <cstring>
#if defined(__linux__) && !defined(__ANDROID__)
#if defined(SOKOL_GLES3)
#include <GLES3/gl3.h>
#elif defined(SOKOL_GLCORE)
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#endif
#include <GL/gl.h>
#include <GL/glext.h>
#endif
#endif
using namespace tc;
namespace {
int failures = 0;
bool completed = false;
constexpr int side = 32;
void check(const char* label, bool ok) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
void expect(const Pixels& pixels, int x, int y, Color wanted) {
    const auto actual = pixels.getColor(x, y);
    check("raw sample has expected marker color", std::abs(actual.r - wanted.r) < 0.01f &&
        std::abs(actual.g - wanted.g) < 0.01f && std::abs(actual.b - wanted.b) < 0.01f);
}
class OrientationApp : public App {
    void draw() override {
        if (completed) return;
        const bool canonical = internal::fboStorageCanonical();
#if defined(__linux__) && !defined(__ANDROID__) && (defined(SOKOL_GLCORE) || defined(SOKOL_GLES3))
        bool mesaFlipY = false;
        GLint extensions = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &extensions);
        for (GLint i = 0; i < extensions; ++i)
            mesaFlipY |= std::strcmp(reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, GLuint(i))),
                "GL_MESA_framebuffer_flip_y") == 0;
        check("driver Mesa capability selects canonical storage", canonical == mesaFlipY);
#endif
        std::printf("orientation=%s\n", canonical ? "canonical" : "native");
        if (!canonical) std::puts("SKIP: canonical expectation (Mesa flip_y unavailable); checking native orientation");
        FullscreenShader shader;
        check("custom sampling shader loads", shader.load(orientation_sample_shader_desc));
        const bool nativeGl = sg_query_backend() == SG_BACKEND_GLCORE || sg_query_backend() == SG_BACKEND_GLES3;
        const bool topAtZero = canonical || !nativeGl;
        for (int samples : {1, 4}) {
            for (bool nested : {false, true}) {
                std::printf("CASE samples=%d nested=%d (native mip helper after public pass)\n", samples, nested);
                Fbo fbo, child;
                fbo.allocate(side, side, samples, TextureFormat::RGBA8, true);
                child.allocate(8, 8, samples, TextureFormat::RGBA8, true);
                check("requested samples allocated", fbo.isAllocated() && fbo.getSampleCount() == samples);
                fbo.begin(0, 0, 0, 1);
                if (nested) clear(0, 0, 0, 1); // Explicit clear also restarts the pass.
                setColor(1, 0, 0);
                drawRect(0, 0, side, 8);
                if (nested) {
                    // Ends/resolves the parent, runs public + fixed mip passes,
                    // then resumes the parent with LOAD and reapplies flip_y.
                    child.begin(0, 0, 1, 1);
                    child.end();
                    internal::parkWindowPass();
                    check("parked public pass resumes", internal::ensureCurrentPass());
                }
                setColor(0, 1, 0);
                drawRect(0, 24, 8, 8);
                fbo.end(); // Fixed mip writers must stay native, including mip 1.
                Pixels readback;
                readback.allocate(side, side, 4);
                check("public readback succeeds", fbo.readPixels(readback.getData()));
                expect(readback, 4, 4, Color(1, 0, 0));
                expect(readback, 4, 28, Color(0, 1, 0));
                std::vector<float> converted(side * side * 4);
                check("converted readback succeeds", fbo.readPixelsFloat(converted.data()));
                check("converted top row is red", converted[4 * (4 * side + 4)] == 1.0f &&
                    converted[4 * (4 * side + 4) + 1] == 0.0f);
                setColor(1, 1, 1);
                fbo.draw(0, 0);
                internal::parkWindowPass();
                Pixels drawn;
                check("Fbo draw window capture succeeds", grabScreen(drawn));
                if (drawn.isAllocated()) {
                    expect(drawn, 4, 4, Color(1, 0, 0));
                    expect(drawn, 4, 28, Color(0, 1, 0));
                }
                for (int mip : {0, 1}) {
                    std::printf("sample mip=%d\n", mip);
                    shader.setTexture(0, fbo.getTextureView(), fbo.getSampler());
                    shader.setParams(Vec4(float(mip), 0, 0, 0));
                    shader.draw();
                    internal::ensureCurrentPass();
                    internal::parkWindowPass();
                    Pixels screen;
                    check("window capture succeeds", grabScreen(screen));
                    if (screen.isAllocated()) {
                        expect(screen, 4, topAtZero ? 4 : 28, Color(1, 0, 0));
                        expect(screen, 4, topAtZero ? 28 : 4, Color(0, 1, 0));
                        expect(screen, 28, topAtZero ? 28 : 4, Color(0, 0, 0));
                    }
                }
            }
        }
        completed = true;
        exitApp();
    }
};
} // namespace
TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc < 2 || std::strcmp(argv[1], "--gpu-check") != 0) {
        std::puts("SKIP: fboOrientation needs --gpu-check and a display");
        return 0;
    }
    check("no canonical storage before graphics setup", !internal::fboStorageCanonical());
    WindowSettings settings;
    settings.setSize(side, side);
    settings.setHighDpi(false);
    settings.setSampleCount(1);
    return tcCoreTest::runDisplayTest<OrientationApp>(settings, failures, [] {
        check("all cases completed", completed);
    });
}
