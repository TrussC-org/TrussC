// #1422: raw sampling, public readback and drawing agree on canonical storage.
#include <TrussC.h>
#include <tcxImGui.h>
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
std::string imageDirectory;
constexpr int side = 32;
void check(const char* label, bool ok) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
void expect(const Pixels& pixels, int x, int y, Color wanted) {
    const auto actual = pixels.getColor(x, y);
    const bool ok = std::abs(actual.r - wanted.r) < 0.01f &&
        std::abs(actual.g - wanted.g) < 0.01f && std::abs(actual.b - wanted.b) < 0.01f;
    check("sample has expected marker color", ok);
    if (!ok) std::printf("pixel (%d,%d) actual=(%.3f,%.3f,%.3f) wanted=(%.3f,%.3f,%.3f)\n",
        x, y, actual.r, actual.g, actual.b, wanted.r, wanted.g, wanted.b);
}
class OrientationApp : public App {
    void rectangleChecks(FullscreenShader& shader) {
        for (int samples : {1, 4}) {
            Fbo fbo;
            fbo.allocate(side, side, samples);
            fbo.begin(0, 0, 0, 1);
            auto& target = internal::currentTarget();
            target.viewport = {4, 3, 16, 12, true};
            internal::applyTargetDrawingState();
            setColor(1, 0, 0);
            drawRect(0, 0, side, side);
            target.flushLayers();
            target.viewport = {0, 0, side, side, true};
            internal::applyTargetDrawingState();
            // Asymmetric clips for deferred sgl drawing and a direct fullscreen quad.
            setScissor(22, 2, 6, 8);
            setColor(0, 1, 0);
            drawRect(0, 0, side, side);
            resetScissor();
            shader.setParams(Vec4(0));
            Fbo source;
            // Finish the first draws before preparing the sampled source.
            target.flushLayers();
            fbo.end();
            source.allocate(side, side);
            source.begin(0, 0, 1, 1);
            source.end();
            fbo.begin();
            setScissor(22, 20, 6, 8);
            internal::applyTargetDrawingState();
            shader.setTexture(0, source.getTextureView(), source.getSampler());
            // FullscreenShader issues a direct draw through the current viewport/scissor.
            shader.draw();
            resetScissor();
            fbo.end();
            Pixels pixels;
            pixels.allocate(side, side, 4);
            check("rectangle readback", fbo.readPixels(pixels.getData()));
            expect(pixels, 6, 5, Color(1, 0, 0));
            expect(pixels, 6, 18, Color(0, 0, 0));
            expect(pixels, 24, 5, Color(0, 1, 0));
            expect(pixels, 24, 24, Color(0, 0, 1));
            expect(pixels, 20, 24, Color(0, 0, 0));
            expect(pixels, 4, 19, Color(0, 0, 0));
        }
    }
    void windingChecks() {
        Fbo fbo;
        fbo.allocate(side, side);
        fbo.begin(0, 0, 0, 1);
        auto key = internal::PipelineKey::immediate(internal::fillSelection(BlendMode::Disabled, true));
        key.desc.cull_mode = SG_CULLMODE_BACK;
        key.desc.face_winding = SG_FACEWINDING_CCW;
        internal::loadTemporaryPipeline(internal::currentTarget().pipeline(key).immediate);
        setColor(1, 0, 0);
        drawTriangle(2, 2, 2, 14, 14, 2);
        setColor(0, 1, 0);
        drawTriangle(18, 2, 30, 2, 18, 14);
        fbo.end();
        Pixels pixels;
        pixels.allocate(side, side, 4);
        check("culling readback", fbo.readPixels(pixels.getData()));
        expect(pixels, 4, 4, Color(1, 0, 0));
        expect(pixels, 20, 4, Color(0, 0, 0));
    }
    void normalMapChecks() {
        Pixels texels;
        texels.allocate(2, 2, 4);
        texels.setColor(0, 0, Color(0.85f, 0.6f, 0.85f));
        texels.setColor(1, 0, Color(0.65f, 0.8f, 0.85f));
        texels.setColor(0, 1, Color(0.2f, 0.6f, 0.85f));
        texels.setColor(1, 1, Color(0.4f, 0.2f, 0.85f));
        Texture normalMap;
        normalMap.allocate(texels);
        Material material;
        material.setBaseColor(0.7f, 0.6f, 0.5f).setRoughness(0.6f).setNormalMap(&normalMap);
        Light light;
        light.setDirectional(-0.4f, -0.2f, -1);
        addLight(light);
        auto sphere = createSphere(44, 32);
        sphere.clearTangents(); // Exercise derivative TBN, not supplied tangents.
        auto drawSphere = [&] {
            setupScreenOrtho();
            setMaterial(material);
            pushMatrix();
            translate(64, 64, 0);
            sphere.draw();
            popMatrix();
            clearMaterial();
        };
        clear(0, 0, 0, 1);
        drawSphere();
        internal::parkWindowPass();
        Pixels window;
        check("normal-mapped sphere window capture", grabScreen(window));
        Fbo fbo;
        fbo.allocate(128, 128);
        fbo.begin(0, 0, 0, 1);
        drawSphere();
        fbo.end();
        Pixels pixels;
        pixels.allocate(128, 128, 4);
        check("normal-mapped sphere Fbo readback", fbo.readPixels(pixels.getData()));
        int changed = 0, maxDelta = 0, lit = 0;
        for (int y = 22; y < 106; ++y) for (int x = 22; x < 106; ++x) {
            const auto a = pixels.getColor(x, y), b = window.getColor(x, y);
            const float delta = std::max({std::abs(a.r-b.r), std::abs(a.g-b.g), std::abs(a.b-b.b)});
            changed += delta > 2.0f/255.0f;
            maxDelta = std::max(maxDelta, int(std::round(delta * 255)));
            lit += a.r + a.g + a.b > 0.05f;
        }
        std::printf("sphere window/Fbo: >2-level pixels=%d maxDelta=%d lit=%d\n", changed, maxDelta, lit);
        check("derivative normal map agrees with window", changed < 20 && lit > 1000);
        if (!imageDirectory.empty()) {
            check("save sphere Fbo", bool(pixels.save(imageDirectory + "/sphere-fbo.png")));
            check("save sphere window", bool(window.save(imageDirectory + "/sphere-window.png")));
        }
        removeLight(light);
    }
    void imguiChecks() {
        tcx::imgui::ImGuiManager ui;
        ui.setupOffscreen();
        Fbo fbo;
        fbo.allocate(side, side);
        fbo.begin(0, 0, 0, 1);
        {
            auto scope = ui.scoped();
            auto* list = ImGui::GetForegroundDrawList();
            list->PushClipRect({3, 2}, {13, 9});
            list->AddRectFilled({0, 0}, {32, 32}, IM_COL32(255, 0, 0, 255));
            list->PopClipRect();
        }
        // Ensure the target rectangle state was restored after ImGui.
        setScissor(20, 22, 6, 7);
        setColor(0, 1, 0);
        drawRect(0, 0, side, side);
        resetScissor();
        fbo.end();
        Pixels pixels;
        pixels.allocate(side, side, 4);
        check("ImGui Fbo readback", fbo.readPixels(pixels.getData()));
        expect(pixels, 5, 4, Color(1, 0, 0));
        expect(pixels, 5, 24, Color(0, 0, 0));
        expect(pixels, 15, 4, Color(0, 0, 0));
        expect(pixels, 22, 24, Color(0, 1, 0));
        if (!imageDirectory.empty()) check("save ImGui Fbo", bool(pixels.save(imageDirectory + "/imgui-fbo.png")));
    }
    void draw() override {
        if (completed) return;
        const bool canonical = internal::fboStorageCanonical();
#if defined(__linux__) && !defined(__ANDROID__) && (defined(SOKOL_GLCORE) || defined(SOKOL_GLES3))
        bool mesaFlipY = false, clipExtension = false;
        GLint extensions = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &extensions);
        for (GLint i = 0; i < extensions; ++i) {
            const auto* name = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, GLuint(i)));
            mesaFlipY |= std::strcmp(name, "GL_MESA_framebuffer_flip_y") == 0;
#if defined(SOKOL_GLCORE)
            clipExtension |= std::strcmp(name, "GL_ARB_clip_control") == 0;
#else
            clipExtension |= std::strcmp(name, "GL_EXT_clip_control") == 0;
#endif
        }
#if defined(SOKOL_GLCORE)
        GLint major = 0, minor = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        clipExtension |= major > 4 || (major == 4 && minor >= 5);
#endif
        check("driver capabilities select canonical storage", canonical == (mesaFlipY || clipExtension));
        check("Mesa takes precedence over clip control", internal::fboClipControlAvailable() == (!mesaFlipY && clipExtension));
#endif
        std::printf("orientation=%s\n", canonical ? "canonical" : "native");
        if (!canonical) std::puts("SKIP: canonical expectation (flip_y and clip control unavailable); checking native orientation");
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
                    // then resumes the parent with LOAD and reapplies orientation.
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
                        expect(screen, 16, topAtZero ? 16 : 112, Color(1, 0, 0));
                        expect(screen, 16, topAtZero ? 112 : 16, Color(0, 1, 0));
                        expect(screen, 112, topAtZero ? 112 : 16, Color(0, 0, 0));
                    }
                }
            }
        }
        rectangleChecks(shader);
        windingChecks();
        normalMapChecks();
        imguiChecks();
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
    if (argc > 2) imageDirectory = argv[2];
    WindowSettings settings;
    settings.setSize(128, 128);
    settings.setHighDpi(false);
    settings.setSampleCount(1);
    return tcCoreTest::runDisplayTest<OrientationApp>(settings, failures, [] {
        check("all cases completed", completed);
    });
}
