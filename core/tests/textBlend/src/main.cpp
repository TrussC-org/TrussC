// Coverage text follows the same blend equations as shapes (#1434).
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <cstdlib>
#include <fstream>
#if defined(SOKOL_GLCORE)
#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#endif
#elif defined(SOKOL_GLES3)
#if defined(__APPLE__)
#include <OpenGLES/ES3/gl.h>
#else
#include <GLES3/gl3.h>
#endif
#endif
using namespace tc;
namespace {
int failures = 0;
bool completed = false, alphaOnly = false;
constexpr int cell = 32, width = cell * 9, height = cell * 2;
const Color source(0.6f, 0.3f, 0.8f, 0.45f), destination(0.3f, 0.4f, 0.2f, 0.35f);
const BlendMode modes[] = {BlendMode::Alpha, BlendMode::Add, BlendMode::Subtract,
    BlendMode::Multiply, BlendMode::Screen, BlendMode::Disabled,
    BlendMode::Custom, BlendMode::Custom, BlendMode::Custom};
void check(const char* label, bool passed) {
    std::printf("%s %s\n", passed ? "PASS" : "FAIL", label);
    if (!passed) ++failures;
}
BlendDesc custom(int index) {
    auto desc = index == 6 ? BlendDesc::addKeepAlpha() : BlendDesc::screenKeepAlpha();
    desc.premultiplySource = index == 8;
    return desc;
}
void select(int index) {
    setCustomBlendDesc(custom(index));
    setBlendMode(modes[index]);
}
// Independent numeric oracle; coverage has already scaled source alpha.
Color expected(int index, float coverage) {
    const float alpha = source.a * coverage;
    const float src[] = {source.r, source.g, source.b};
    const float dst[] = {destination.r, destination.g, destination.b};
    float rgb[3]{};
    for (int channel = 0; channel < 3; ++channel) {
        const float s = src[channel], d = dst[channel];
        switch (index) {
            case 0: rgb[channel] = s*alpha + d*(1-alpha); break;
            case 1: case 6: rgb[channel] = s*alpha + d; break;
            case 2: rgb[channel] = d-s*alpha; break;
            case 3: rgb[channel] = s*alpha*d + d*(1-alpha); break;
            case 4: case 8: rgb[channel] = s*alpha + d*(1-s*alpha); break;
            case 5: rgb[channel] = s; break;
            case 7: rgb[channel] = s + d*(1-s); break;
        }
    }
    const float a = index >= 6 ? destination.a : index == 5 ? alpha :
        (index == 1 || index == 2) ? alpha+destination.a : alpha+destination.a*(1-alpha);
    return Color(rgb[0], rgb[1], rgb[2], a);
}
bool approxEq(Color actual, Color wanted, bool compareAlpha) {
    return std::abs(actual.r-wanted.r) < 0.012f && std::abs(actual.g-wanted.g) < 0.012f &&
        std::abs(actual.b-wanted.b) < 0.012f && (!compareAlpha || std::abs(actual.a-wanted.a) < 0.012f);
}
#if !defined(__EMSCRIPTEN__)
// Match customBlend: query the actual visual, not its nominal RGBA8 format.
bool swapchainAlpha() {
    int bits = 0;
    const auto format = internal::currentTarget().colorFormat;
    if (format == SG_PIXELFORMAT_RGBA8 || format == SG_PIXELFORMAT_BGRA8) bits = 8;
#if defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)
    GLint previous = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previous);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
#if defined(SOKOL_GLES3)
    const GLenum attachment = GL_BACK;
#else
    const GLenum attachment = GL_BACK_LEFT;
#endif
    glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, attachment,
        GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE, &bits);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(previous));
#endif
    std::printf("SWAPCHAIN alpha_bits=%d; compare alpha=%d\n", bits, bits >= 8);
    return bits >= 8;
}
#endif
class TextBlendApp : public App {
    Font font_;
    GlyphBatch glyphs_, maskGlyphs_;
    Pixels mask_;
    void text(float x, float y) {
        setColor(source);
        font_.drawString("Ag", x, y);
        glyphs_.draw(Mat4::translate(x, y+cell, 0));
    }
    void cases() {
        for (int index = 0; index < 9; ++index) {
            select(index);
            text(float(index*cell), 0);
        }
    }
    template<class ReadPixel>
    void pixels(ReadPixel readPixel, bool compareAlpha, bool normalized) {
        for (int index = 0; index < 9; ++index) for (int row = 0; row < 2; ++row) {
            bool matches = true;
            int fractional = 0, transparent = 0, ink = 0;
            for (int y = 0; y < cell; ++y) for (int x = 0; x < cell; ++x) {
                const auto mask = mask_.getColor(x, y+row*cell);
                auto wanted = mask.r > 0.5f ? expected(index, mask.a) : destination;
                // Float targets retain subtraction's negative RGB.
                if (normalized) {
                    wanted.r = std::clamp(wanted.r, 0.f, 1.f);
                    wanted.g = std::clamp(wanted.g, 0.f, 1.f);
                    wanted.b = std::clamp(wanted.b, 0.f, 1.f);
                }
                const auto actual = readPixel(x+index*cell, y+row*cell);
                if (matches && !approxEq(actual, wanted, compareAlpha))
                    std::printf("PIXEL %d,%d actual=%g,%g,%g,%g expected=%g,%g,%g,%g\n",
                        x, y, actual.r, actual.g, actual.b, actual.a, wanted.r, wanted.g, wanted.b, wanted.a);
                matches &= approxEq(actual, wanted, compareAlpha);
                fractional += mask.a > 0 && mask.a < 1;
                ink += mask.a > 0;
                transparent += mask.r > 0.5f && mask.a == 0;
            }
            std::printf("MODE %d %s\n", index, row ? "GlyphBatch" : "Font");
            check("all pixels match blend equation (ink, fractional and zero coverage)",
                matches && ink && fractional && transparent);
        }
    }
    void fboChecks() {
        Fbo mask;
        mask.allocate(cell, height);
        mask.begin(0, 0, 0, 0);
        setupScreenOrtho();
        setBlendMode(BlendMode::Disabled);
        setColor(1);
        font_.drawString("Ag", 0, 0);
        maskGlyphs_.draw(Mat4::translate(0, cell, 0));
        mask.end();
        mask_.allocate(cell, height, 4);
        check("coverage and quad mask readback", mask.readPixels(mask_.getData()));
        for (auto format : {TextureFormat::RGBA8, TextureFormat::RGBA16F}) for (int samples : {1, 4}) {
            Fbo target;
            target.allocate(width, height, samples, format);
            std::printf("TARGET format=%d samples=%d\n", int(format), samples);
            target.begin(destination.r, destination.g, destination.b, destination.a);
            setupScreenOrtho();
            if (!alphaOnly) cases();
            target.end();
            std::vector<float> data(width*height*4);
            check("Fbo float readback", target.readPixelsFloat(data.data()));
            if (!alphaOnly) pixels([&](int x, int y) {
                const auto i = (y*width+x)*4;
                return Color(data[i], data[i+1], data[i+2], data[i+3]);
            }, true, format == TextureFormat::RGBA8);
            if (!alphaOnly) {
                target.begin(destination.r, destination.g, destination.b, destination.a);
                setupScreenOrtho();
                counters();
                target.end();
            }
            // The whole scene (both APIs, fractional positions, overlap) is
            // compared byte-for-byte against origin/main by TC_TEXT_ALPHA_DUMP.
            target.begin(destination.r, destination.g, destination.b, destination.a);
            setupScreenOrtho();
            setBlendMode(BlendMode::Alpha);
            text(2.25f, 1.5f);
            text(8.5f, 4.25f);
            target.end();
            std::vector<float> alpha(width*height*4);
            check("Alpha scene float readback", target.readPixelsFloat(alpha.data()));
            if (const auto* dir = std::getenv("TC_TEXT_ALPHA_DUMP")) {
                const auto path = fs::path(dir) / (std::to_string(int(format))+"-"+std::to_string(samples)+".rgba");
                std::ofstream file(path, std::ios::binary);
                file.write(reinterpret_cast<const char*>(alpha.data()), alpha.size()*sizeof(float));
                check("Alpha snapshot written", file.good());
            }
        }
    }
    void counters() {
        auto& resources = internal::currentTarget().resources();
        cases(); // warm both immediate and raw selections and glyph resources
        internal::currentTarget().flushLayers();
        const auto lookups = resources.canonicalLookups;
        const auto immediate = resources.cache.size();
        const auto raw = resources.rawCache->cache.size();
        for (int repeat = 0; repeat < 16; ++repeat) cases();
        check("warmed text alternation has zero canonical lookups", resources.canonicalLookups == lookups);
        check("warmed text alternation creates zero pipelines", resources.cache.size() == immediate && resources.rawCache->cache.size() == raw);
        bool noCustom = true;
        for (const auto& entry : resources.immediateCache)
            noCustom &= entry.first.blend != BlendMode::Custom;
        check("small immediate cache excludes Custom", noCustom);
        for (int index = 0; index < 6; ++index)
            check("standard coverage selection uses small immediate cache", std::any_of(
                resources.immediateCache.begin(), resources.immediateCache.end(), [&](const auto& entry) {
                    return entry.first.role == internal::PipelineRole::Coverage && entry.first.blend == modes[index];
                }));
    }
public:
    void setup() override {
#if defined(__EMSCRIPTEN__)
        const fs::path fixture = "/data/textBlend-fixture.ttf";
#else
        const auto fixture = fs::path(__FILE__).parent_path()/"../../renderTargetState/src/fixture.ttf";
#endif
        check("font fixture loads", font_.load(fixture, 20).ok());
        glyphs_.setFont(font_);
        glyphs_.add("Ag", source);
        maskGlyphs_.setFont(font_);
        maskGlyphs_.add("Ag", Color(1));
    }
    void draw() override {
        disableDepthTest();
        fboChecks();
        if (!alphaOnly) {
            setupScreenOrtho();
            clear(destination);
            cases();
#if !defined(__EMSCRIPTEN__)
            Pixels screen;
            check("swapchain readback", grabScreen(screen));
            if (screen.isAllocated()) pixels([&](int x, int y) { return screen.getColor(x, y); }, swapchainAlpha(), true);
#endif
            counters();
        }
        completed = true;
        exitApp();
    }
};
}
TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc == 1) {
        std::puts("SKIP: pixel checks require --gpu-check (see display-test)");
        return 0;
    }
    alphaOnly = std::strcmp(argv[1], "--alpha-only") == 0;
    WindowSettings settings;
    settings.setSize(width, height);
    settings.setHighDpi(false);
    settings.sampleCount = argc > 2 && std::strcmp(argv[2], "--msaa4") == 0 ? 4 : 1;
    return tcCoreTest::runDisplayTest<TextBlendApp>(settings, failures, [] { check("suite completed", completed); });
}
