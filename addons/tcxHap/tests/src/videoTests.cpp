#include "tcxHapPlayer.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int failures = 0;

void check(const char* name, bool ok) {
    std::printf("%-72s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

void snappyRoundTripTests() {
    using namespace tcx::hap;
    // Repeated BC1 blocks ensure HAP uses Snappy instead of storing raw data.
    const std::array<uint8_t, 8> block = {0x00, 0xf8, 0x00, 0x00, 0, 0, 0, 0};
    std::vector<uint8_t> texture(64 * 64 / 2);
    for (size_t i = 0; i < texture.size(); ++i) texture[i] = block[i % block.size()];

    for (unsigned int chunks : {1u, 4u}) {
        const void* input = texture.data();
        unsigned long inputBytes = static_cast<unsigned long>(texture.size());
        unsigned int format = HapTextureFormat_RGB_DXT1;
        unsigned int compressor = HapCompressorSnappy;
        std::vector<uint8_t> encoded(HapMaxEncodedLength(1, &inputBytes, &format, &chunks));
        unsigned long encodedBytes = 0;
        const unsigned int status = HapEncode(1, &input, &inputBytes, &format,
            &compressor, &chunks, encoded.data(), static_cast<unsigned long>(encoded.size()),
            &encodedBytes);
        check("Snappy: HAP encoding succeeds", status == HapResult_No_Error);
        if (status != HapResult_No_Error) continue;
        check("Snappy: HAP frame is compressed", encodedBytes < inputBytes);

        HapDecoder decoder;
        HapDecodedFrame decoded;
        check("Snappy: HAP allocating decode round trip",
              decoder.decode(encoded.data(), encodedBytes, 64, 64, decoded) &&
              decoded.format == HapFormat::DXT1 && decoded.data == texture);
        std::vector<uint8_t> output(texture.size());
        HapFormat outputFormat = HapFormat::Unknown;
        check("Snappy: HAP preallocated decode round trip",
              decoder.decodeToBuffer(encoded.data(), encodedBytes, 64, 64,
                  output.data(), output.size(), outputFormat) &&
              outputFormat == HapFormat::DXT1 && output == texture);
    }
}

tc::Vec4 clipPosition(const tcx::hap::detail::YCoCgDrawData& data, int index) {
    tc::Mat4 matrix;
    std::memcpy(matrix.m, data.uniforms.mvp, sizeof(matrix.m));
    const auto& vertex = data.vertices[index];
    return matrix.transposed() * tc::Vec4(vertex.x, vertex.y, vertex.z, 1);
}

bool approxEq(float a, float b) { return std::abs(a - b) < 1e-5f; }

void drawTests() {
    using namespace tc;
    const Mat4 model = Mat4::translate(10, 20, 0) * Mat4::scale(2, 2, 1);
    const Color color(0.25f, 0.5f, 0.75f, 0.4f);
    const auto data = tcx::hap::detail::makeYCoCgDrawData(1, 2, 3, 4,
        Mat4::identity(), Mat4::identity(), model, color);
    const Vec2 corners[] = {{12, 24}, {18, 24}, {18, 32}, {12, 32}};
    const Vec2 uv[] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    bool cornersOk = true, colorsOk = true, uvOk = true;
    for (int i = 0; i < 4; ++i) {
        const auto clip = clipPosition(data, i);
        const auto& vertex = data.vertices[i];
        cornersOk &= approxEq(clip.x, corners[i].x) && approxEq(clip.y, corners[i].y) && approxEq(clip.w, 1);
        colorsOk &= approxEq(vertex.r, color.r) && approxEq(vertex.g, color.g) &&
                    approxEq(vertex.b, color.b) && approxEq(vertex.a, color.a);
        uvOk &= approxEq(vertex.u, uv[i].x) && approxEq(vertex.v, uv[i].y);
    }
    check("HAP-Q: translate + scale applied once via column-major MVP", cornersOk);
    check("HAP-Q: tint and alpha on every vertex", colorsOk);
    check("HAP-Q: quad texture coordinates", uvOk);

    // Fbo::begin supplies its own projection; no window-size uniform is needed.
    const auto fbo = tcx::hap::detail::makeYCoCgDrawData(0, 0, 1920, 1080,
        Mat4::ortho(0, 1920, 1080, 0, -1, 1), Mat4::identity(), Mat4::identity(), color);
    const auto tl = clipPosition(fbo, 0), br = clipPosition(fbo, 2);
    check("HAP-Q: Fbo-sized projection maps corners to clip bounds",
          approxEq(tl.x, -1) && approxEq(tl.y, 1) && approxEq(br.x, 1) && approxEq(br.y, -1));

    const Mat4 projection = Mat4::perspective(TAU / 6, 1.5f, 1, 100);
    const Mat4 view = Mat4::lookAt(Vec3(0, 0, 10), Vec3(0, 0, 0), Vec3(0, 1, 0));
    const auto camera = tcx::hap::detail::makeYCoCgDrawData(1, 2, 3, 4, projection, view, model, color);
    const auto clip = clipPosition(camera, 0);
    const Vec4 expected = projection * (view * Vec4(12, 24, 0, 1));
    check("HAP-Q: perspective camera uses projection, view and model",
          approxEq(clip.x, expected.x) && approxEq(clip.y, expected.y) &&
          approxEq(clip.z, expected.z) && approxEq(clip.w, expected.w) && !approxEq(clip.w, 1));

    static_assert(sizeof(tcx::hap::YCoCgVsParams) == sizeof(vs_params_t));
}

} // namespace

int runVideoTests() {
    snappyRoundTripTests();
    drawTests();
    return failures;
}
