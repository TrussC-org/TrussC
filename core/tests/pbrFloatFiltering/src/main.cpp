#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include "tc/3d/tcIblUpload.h"
#if defined(SOKOL_GLCORE)
#include <GL/gl.h>
#endif

using namespace tc;

namespace {
int failures = 0;
fs::path capturePath;
void check(const char* label, bool ok) {
    std::printf("%s: %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

void unitChecks() {
#if defined(SOKOL_GLCORE)
    const auto* shader = tc_pbr_pbr_mesh_shader_desc(SG_BACKEND_GLCORE);
    check("shadow view is unfilterable float", shader->views[VIEW_tc_pbr_shadowMap].texture.sample_type ==
          SG_IMAGESAMPLETYPE_UNFILTERABLE_FLOAT);
    check("shadow sampler is nonfiltering", shader->samplers[SMP_tc_pbr_shadowMapSmp].sampler_type ==
          SG_SAMPLERTYPE_NONFILTERING);
#endif
    Pixels source;
    source.allocate(1, 1, 4, PixelFormat::F32);
    Environment environment;
    check("HDR upload without a GPU returns false", !environment.loadFromHDR(source));
    check("filterable float32 retains RGBA32F", internal::iblEquirectFormat(true) == TextureFormat::RGBA32F);
    check("unfilterable float32 selects RGBA16F", internal::iblEquirectFormat(false) == TextureFormat::RGBA16F);
    bool roundTrip = true;
    for (uint32_t bits = 0; bits < 65536; ++bits) {
        const int exponent = (bits >> 10) & 31;
        if (exponent == 31) continue;
        const int fraction = bits & 1023;
        float value = exponent ? std::ldexp(float(1024 + fraction), exponent - 25)
                               : std::ldexp(float(fraction), -24);
        if (bits & 0x8000) value = -value;
        roundTrip &= internal::iblFloatToHalf(value) == bits;
    }
    check("all finite binary16 values round trip", roundTrip);
    check("HDR range and saturation", internal::iblFloatToHalf(18.0f) == 0x4c80 &&
          internal::iblFloatToHalf(65504.0f) == 0x7bff &&
          internal::iblFloatToHalf(1.0e30f) == 0x7bff &&
          internal::iblFloatToHalf(-1.0e30f) == 0xfbff &&
          internal::iblFloatToHalf(std::numeric_limits<float>::infinity()) == 0x7bff);
    check("round ties to even", internal::iblFloatToHalf(1.0f + std::ldexp(1.0f, -11)) == 0x3c00 &&
          internal::iblFloatToHalf(1.0f + 3 * std::ldexp(1.0f, -11)) == 0x3c02 &&
          internal::iblFloatToHalf(std::ldexp(1.0f, -25)) == 0 &&
          internal::iblFloatToHalf(3 * std::ldexp(1.0f, -25)) == 2);
    check("NaN stays NaN", internal::iblFloatToHalf(std::numeric_limits<float>::quiet_NaN()) == 0x7e00);
}

#if defined(SOKOL_GLCORE)
bool completed = false;
// Capture every baked texel, including all prefilter mips, for an exact
// before/after comparison on the same desktop GL device.
bool captureTexture(const Texture& texture, bool cube, int levels, std::ofstream& file) {
    bool finite = true;
    bool hdr = false;
    const auto info = sg_gl_query_image_info(texture.getImage());
    const GLenum target = cube ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
    glBindTexture(target, info.tex[info.active_slot]);
    for (int mip = 0; mip < levels; ++mip) {
        const int size = std::max(1, texture.getWidth() >> mip);
        std::vector<float> pixels(size_t(size) * size * 4);
        for (int face = 0; face < (cube ? 6 : 1); ++face) {
            glGetTexImage(cube ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + face : target,
                          mip, GL_RGBA, GL_FLOAT, pixels.data());
            check("bake readback succeeds", glGetError() == GL_NO_ERROR);
            for (float value : pixels) {
                finite &= std::isfinite(value);
                hdr |= value > 1.0f;
            }
            if (file.is_open()) file.write(reinterpret_cast<const char*>(pixels.data()),
                                          pixels.size() * sizeof(float));
        }
    }
    glBindTexture(target, 0);
    sg_reset_state_cache();
    check("baked texels are finite", finite);
    return hdr;
}
class BakeApp : public App {
    Environment environment_;
public:
    void draw() override {
        if (completed) return;
        std::printf("caps: R32F filter=%d, RGBA32F filter=%d, RGBA16F filter=%d\n",
                    int(sg_query_pixelformat(SG_PIXELFORMAT_R32F).filter),
                    int(sg_query_pixelformat(SG_PIXELFORMAT_RGBA32F).filter),
                    int(sg_query_pixelformat(SG_PIXELFORMAT_RGBA16F).filter));
        check("procedural HDR bake", environment_.loadProcedural() && environment_.isLoaded());
        std::ofstream file;
        if (!capturePath.empty()) file.open(capturePath, std::ios::binary);
        check("capture opens", capturePath.empty() || file.is_open());
        check("irradiance retains HDR range", captureTexture(environment_.getIrradianceMap(), true, 1, file));
        check("prefilter retains HDR range", captureTexture(environment_.getPrefilterMap(), true, environment_.getPrefilterMipLevels(), file));
        captureTexture(environment_.getBrdfLut(), false, 1, file);
        check("capture writes", !file.is_open() || file.good());
        completed = true;
        exitApp();
    }
};
#endif
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
    unitChecks();
    if (argc == 1) return failures ? 1 : 0;
    if (std::strcmp(argv[1], "--gpu-check") != 0 || argc > 3) return 2;
    if (argc == 3) capturePath = argv[2];
#if defined(SOKOL_GLCORE)
    WindowSettings settings;
    settings.setSize(64, 64);
    settings.setSwapInterval(0);
    return tcCoreTest::runDisplayTest<BakeApp>(settings, failures, [] {
        check("environment display check completed", completed);
    });
#else
    std::puts("SKIP: environment readback requires desktop GL");
    return 0;
#endif
}
