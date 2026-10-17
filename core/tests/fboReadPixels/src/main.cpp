// #875: run --gpu-check on GLCORE, GLES3, D3D11 and Metal.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include "../../../platform/tcFboConversion.h"
#include <cstdio>
#include <cstring>
#include <limits>
#if defined(SOKOL_GLES3)
#include <GLES3/gl3.h>
#elif defined(SOKOL_GLCORE)
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#endif
#include <GL/gl.h>
#include <GL/glext.h>
#endif
using namespace tc;

namespace {
int failures = 0, checks = 0;
bool completed = false;
void check(const char* name, bool ok) {
    ++checks;
    if (!ok) ++failures;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
}

void cpuChecks() {
    using namespace trussc::internal;
    check("half subnormals, signs, normals, infinities, NaN",
          fboHalfToFloat(1) == std::ldexp(1.0f, -24) &&
          fboHalfToFloat(0x3ff) == std::ldexp(1023.0f, -24) &&
          fboHalfToFloat(0x8001) == -std::ldexp(1.0f, -24) &&
          std::signbit(fboHalfToFloat(0x8000)) &&
          fboHalfToFloat(0x3800) == 0.5f &&
          std::isinf(fboHalfToFloat(0x7c00)) && std::isnan(fboHalfToFloat(0x7e00)));
    check("clamp and nearest rounding, ties up",
          fboUnorm8(-1) == 0 && fboUnorm8(2) == 255 &&
          fboUnorm8(0.25f) == 64 && fboUnorm8(0.5f) == 128 && fboUnorm8(0.75f) == 191 &&
          fboUnorm8(std::numeric_limits<float>::quiet_NaN()) == 0 &&
          fboUnorm8(std::numeric_limits<float>::infinity()) == 255 &&
          fboUnorm8(-std::numeric_limits<float>::infinity()) == 0);
    size_t rowBytes = 0, totalBytes = 0;
    check("readback layout rejects non-positive dimensions",
          !fboReadbackLayout(0, 2, 4, rowBytes, totalBytes) &&
          !fboReadbackLayout(2, -1, 4, rowBytes, totalBytes));
    const size_t max = std::numeric_limits<size_t>::max();
    check("readback layout checks both multiplications",
          !fboReadbackLayout(2, 1, max, rowBytes, totalBytes) &&
          !fboReadbackLayout(1, 2, max, rowBytes, totalBytes));
    check("readback layout accepts a valid image",
          fboReadbackLayout(3, 131, 16, rowBytes, totalBytes) && rowBytes == 48 && totalBytes == 6288);
    // An unaligned row guards against typed pointer loads in the converter.
    unsigned char raw[33] = {};
    const uint16_t half[] = {0x3800, 0xbc00, 0x4000, 0x3400};
    std::memcpy(raw + 1, half, sizeof(half));
    unsigned char out[4];
    fboRowToRgba8(raw + 1, out, 1, TextureFormat::RGBA16F);
    check("unaligned half row converts every component", out[0] == 128 && out[1] == 0 && out[2] == 255 && out[3] == 64);
    const unsigned char bgra[] = {17, 83, 201, 249};
    fboRowToRgba8(bgra, out, 1, TextureFormat::BGRA8);
    check("BGRA swizzles to RGBA", out[0] == 201 && out[1] == 83 && out[2] == 17 && out[3] == 249);
    const uint16_t unorm[] = {0, 16384, 32768, 65535};
    fboRowToRgba8(unorm, out, 1, TextureFormat::RGBA16);
    check("16-bit UNORM quantises", out[0] == 0 && out[1] == 64 && out[2] == 128 && out[3] == 255);
}

class ReadPixelsApp : public App {
    void checkFailedReadback(const Fbo& fbo) {
        // Single-element destinations make any accidental full-image read unsafe.
        // The public guard must return before GL/GLES chunk or Metal staging work.
        unsigned char byte = 123;
        float value = -7.0f;
        auto* parent = internal::currentWindowContext().currentTarget;
        const int vertices = sgl_num_vertices();
        check("failed allocation remains unallocated", !fbo.isAllocated());
#if defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)
        while (glGetError() != GL_NO_ERROR) {}
        glEnable(0xffffffffu);
#endif
        check("failed allocation rejects byte read without writing", !fbo.readPixels(&byte) && byte == 123);
        check("failed allocation rejects float read without writing", !fbo.readPixelsFloat(&value) && value == -7.0f);
#if defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)
        // The readback driver helper drains errors. An intact error proves that
        // neither read reached the driver, even for converted staging formats.
        check("failed readbacks never reach GL", glGetError() == GL_INVALID_ENUM);
#endif
        check("failed readbacks preserve target and queued vertices",
              internal::currentWindowContext().currentTarget == parent && sgl_num_vertices() == vertices);
    }

    void checkAllocationFailures() {
        const int limit = sg_query_limits().max_image_size_2d;
        for (auto format : {TextureFormat::RGBA8, TextureFormat::RGBA16F, TextureFormat::RGBA32F}) {
            if (!sg_query_pixelformat(toSokolFormat(format)).render) continue;
            Fbo fbo;
            for (bool tall : {false, true}) {
                fbo.allocate(tall ? 1 : limit + 1, tall ? limit + 1 : 1, 1, format);
                checkFailedReadback(fbo);
            }
            // A resource failure with otherwise valid dimensions must take the
            // same cheap path as a rejected size, including after a valid Fbo.
            fbo.allocate(3, 131, 1, format);
            check("allocation before pool exhaustion succeeds", fbo.isAllocated());
            std::vector<sg_image> held;
            for (;;) {
                sg_image image = sg_alloc_image();
                if (!image.id) break;
                held.push_back(image);
            }
            fbo.allocate(3, 131, 1, format);
            checkFailedReadback(fbo);
            for (auto image : held) sg_dealloc_image(image);
            fbo.allocate(3, 131, 1, format);
            check("allocation recovers after failed readbacks", fbo.isAllocated());
        }
    }
    void draw() override {
        if (completed) return;
        // Some devices render RGBA32F without supporting blending on it.
        // Select the disabled role before begin() creates a format pipeline.
        setBlendMode(BlendMode::Disabled);
        checkAllocationFailures();
        // Distinct top/bottom rows, odd width, and several reads catch stride,
        // vertical orientation and stale-version errors as well as conversion.
        struct Format { TextureFormat value; const char* name; };
        const Format formats[] = {
            {TextureFormat::RGBA16F, "RGBA16F"}, {TextureFormat::RGBA32F, "RGBA32F"},
            {TextureFormat::RGBA8, "RGBA8"}, {TextureFormat::R8, "R8"}, {TextureFormat::RG8, "RG8"},
            {TextureFormat::R16F, "R16F"}, {TextureFormat::RG16F, "RG16F"},
            {TextureFormat::R32F, "R32F"}, {TextureFormat::RG32F, "RG32F"},
            {TextureFormat::BGRA8, "BGRA8"}, {TextureFormat::RGBA16, "RGBA16"}
        };
        for (const auto& fmt : formats) {
            if (!sg_query_pixelformat(toSokolFormat(fmt.value)).render) {
                // RGBA16F is the required regression; RGBA32F is optional.
                if (fmt.value == TextureFormat::RGBA16F)
                    check("required HDR attachment is renderable", false);
                else std::printf("SKIP %s attachment unsupported by this device\n", fmt.name);
                continue;
            }
            Fbo fbo;
            fbo.allocate(3, 2, 1, fmt.value);
            const float values[][4] = {{-0.5f, 2.0f, 0.5f, 1.0f},
                                      {0.25f, 0.75f, 0.125f, 1.0f},
                                      {0.0f, 0.5f, 1.0f, 0.25f}};
            for (const auto& v : values) {
                fbo.begin(v[0], v[1], v[2], v[3]);
                // Opaque lower row ensures no premultiplication affects values.
                setColor(0.75f, 0.25f, 0.5f, 1.0f);
                drawRect(0, 1, 3, 1);
                fbo.end();
                unsigned char guarded[28];
                std::memset(guarded, 0xcd, sizeof(guarded));
                std::memset(guarded + 2, 0, 24); // main GLES leaves these zeros untouched
                const bool ok = fbo.readPixels(guarded + 2);
                check(fmt.name, ok);
                check("readPixels writes exactly width*height*4 bytes",
                      guarded[0] == 0xcd && guarded[1] == 0xcd && guarded[26] == 0xcd && guarded[27] == 0xcd);
                const int channels = channelCount(fmt.value);
                bool bytesMatch = true;
                for (int y = 0; y < 2; ++y) {
                    // Independent expected bytes; no production helper oracle.
                    const unsigned char top[][4] = {{0,255,128,255}, {64,191,32,255}, {0,128,255,64}};
                    const unsigned char bottom[] = {191,64,128,255};
                    const auto* expected = y == 0 ? top[&v - values] : bottom;
                    for (int x = 0; x < 3; ++x) for (int c = 0; c < 4; ++c) {
                        const int want = c < channels ? expected[c] : c == 3 ? 255 : 0;
                        const int got = guarded[2 + (y * 3 + x) * 4 + c];
                        // GPU UNORM writes can round one LSB either way on any backend.
                        // GLCORE also performs native float-to-byte conversion.
#if defined(SOKOL_GLCORE)
                        const int tolerance = 1;
#else
                        const int tolerance = isFloatFormat(fmt.value) ? 0 : 1;
#endif
                        if (std::abs(got - want) > tolerance) {
                            std::printf("mismatch %s row=%d component=%d got=%d want=%d\n", fmt.name, y, c, got, want);
                            bytesMatch = false;
                        }
                    }
                }
                check("every pixel is clamped, quantised RGBA8 in top-left order", bytesMatch);
#if defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)
                if (fmt.value == TextureFormat::RGBA8) {
                    unsigned char stale[24] = {};
                    glEnable(0xffffffffu); // An unrelated raw-GL caller's pending error.
                    check("pending GL error does not fail a successful read", fbo.readPixels(stale));
                    check("pending GL error does not skip the vertical flip",
                          std::memcmp(stale, guarded + 2, sizeof(stale)) == 0);
                }
#endif
                if (isFloatFormat(fmt.value)) {
                    std::vector<float> floats(3 * 2 * channels);
                    check("readPixelsFloat retains full precision", fbo.readPixelsFloat(floats.data()) && floats[0] == v[0] &&
                          (channels < 2 || floats[1] == v[1]));
                }
            }
        }
        // More than two staging chunks, including a partial final chunk.
        for (auto format : {TextureFormat::RGBA8, TextureFormat::RGBA16F}) {
            if (!sg_query_pixelformat(toSokolFormat(format)).render) continue;
            Fbo tall;
            tall.allocate(3, 131, 1, format);
            tall.begin(0.25f, 0.5f, 0.75f, 1.0f);
            setColor(1, 0, 0, 1);
            drawRect(0, 65, 3, 66);
            tall.end();
            std::vector<unsigned char> bytes(3 * 131 * 4);
            std::vector<float> floats(bytes.size());
            check("tall image byte read succeeds", tall.readPixels(bytes.data()));
            check("tall image float read succeeds", tall.readPixelsFloat(floats.data()));
            bool byteRows = true, floatRows = true;
            const unsigned char top[] = {64, 128, 191, 255}, bottom[] = {255, 0, 0, 255};
            for (int y = 0; y < 131; ++y) for (int x = 0; x < 3; ++x) for (int c = 0; c < 4; ++c) {
                const size_t i = (size_t(y) * 3 + x) * 4 + c;
                const int want = (y < 65 ? top : bottom)[c];
                byteRows &= std::abs(int(bytes[i]) - want) <= 1;
                // Include float arithmetic error at the one-UNORM-step boundary.
                floatRows &= std::abs(floats[i] - want / 255.0f) <=
                             1.0f / 255.0f + std::numeric_limits<float>::epsilon();
            }
            check("tall image byte rows preserve order across chunks", byteRows);
            check("tall image float rows preserve order across chunks", floatRows);
        }
        completed = true;
        exitApp();
    }
};
}
TC_CORE_TEST_MAIN(int argc, char** argv) {
    cpuChecks();
    if (argc <= 1 || std::strcmp(argv[1], "--gpu-check")) {
        std::printf("SKIP GPU: pass --gpu-check with a display\n");
        return failures ? 1 : 0;
    }
    WindowSettings settings;
    settings.setSize(32, 32);
    settings.setHighDpi(false);
    return tcCoreTest::runDisplayTest<ReadPixelsApp>(settings, failures, [] {
        check("GPU conversion checks completed", completed);
        std::printf("fboReadPixels: %d checks, %d failures\n", checks, failures);
    });
}
