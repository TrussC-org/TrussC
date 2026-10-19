#include "tcxHapPlayer.h"

#include <array>
#include <cstdio>
#include <vector>

namespace {

int failures = 0;

void check(const char* name, bool ok) {
    std::printf("%-72s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

using Block = std::array<uint8_t, 16>;
using Pixel = std::array<uint8_t, 4>;

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

Block colorBlock(int blockIndex, bool bc3) {
    Block block{};
    const uint16_t endpoints[] = {0xf800, 0x07e0, 0x001f, 0xffff};
    const uint16_t endpoint = endpoints[blockIndex % 4];
    const int offset = bc3 ? 8 : 0;
    if (bc3) block[0] = static_cast<uint8_t>(32 + blockIndex * 32);
    block[offset] = static_cast<uint8_t>(endpoint);
    block[offset + 1] = static_cast<uint8_t>(endpoint >> 8);
    // Alternate the first endpoint and black, including variation within rows.
    for (int pixel = 0; pixel < 16; ++pixel) {
        block[offset + 4 + pixel / 4] |= ((pixel + blockIndex) % 2) << ((pixel % 4) * 2);
    }
    return block;
}

Block bc7Block(int blockIndex) {
    // BC7 mode 6: equal RGBA endpoints, zero p-bits and zero indices.
    Block block{};
    int bit = 0;
    const auto write = [&](unsigned value, int count) {
        for (int i = 0; i < count; ++i, ++bit) {
            block[bit / 8] |= ((value >> i) & 1) << (bit % 8);
        }
    };
    write(1u << 6, 7);
    const unsigned channels[] = {unsigned(40 + blockIndex * 40), 80, 120, 254};
    for (unsigned value : channels) {
        write(value / 2, 7);
        write(value / 2, 7);
    }
    return block;
}

Pixel expectedPixel(int block, int pixel, int format) {
    if (format == 2) return {uint8_t(40 + block * 40), 80, 120, 254};
    const Pixel colors[] = {{255, 0, 0, 255}, {0, 255, 0, 255},
                            {0, 0, 255, 255}, {255, 255, 255, 255}};
    Pixel result = (pixel + block) % 2 ? Pixel{0, 0, 0, 255} : colors[block % 4];
    if (format == 1) result[3] = static_cast<uint8_t>(32 + block * 32);
    return result;
}

void blockTests() {
    const char* names[] = {"BC1", "BC3 (Hap Alpha / HAP-Q)", "BC7"};
    const size_t blockSizes[] = {8, 16, 16};
    void (*decoders[])(const void*, void*, int) = {bcdec_bc1, bcdec_bc3, bcdec_bc7};
    // Both edges, each edge alone, whole blocks, and the smallest partial block.
    for (const auto size : {std::array{6, 5}, std::array{6, 8}, std::array{8, 5},
                            std::array{8, 8}, std::array{1, 1}}) {
        const int width = size[0], height = size[1];
        const int blocksX = (width + 3) / 4, blocksY = (height + 3) / 4;
        for (int format = 0; format < 3; ++format) {
            // Aligned input; unalignedBlockTests covers every byte offset.
            std::vector<Block> blocks(blocksX * blocksY);
            std::vector<uint64_t> compressed((blocksX * blocksY * blockSizes[format]) / 8);
            auto* src = reinterpret_cast<uint8_t*>(compressed.data());
            for (int i = 0; i < blocksX * blocksY; ++i) {
                blocks[i] = format == 2 ? bc7Block(i) : colorBlock(i, format == 1);
                std::memcpy(src + i * blockSizes[format], blocks[i].data(), blockSizes[format]);
            }
            const size_t bytes = static_cast<size_t>(width) * height * 4;
            std::vector<uint8_t> guarded(bytes + 64, 0xcd);
            tcx::hap::detail::decodeRgbaBlocks(src, guarded.data(), width, height,
                                               blockSizes[format], decoders[format]);
            bool pixelsOk = true;
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const int block = (y / 4) * blocksX + x / 4;
                    const auto expected = expectedPixel(block, (y % 4) * 4 + x % 4, format);
                    pixelsOk &= std::memcmp(guarded.data() + (y * width + x) * 4,
                                            expected.data(), 4) == 0;
                }
            }
            char name[128];
            std::snprintf(name, sizeof(name), "%s %dx%d: pixels and 64-byte guard", names[format], width, height);
            check(name, pixelsOk && std::all_of(guarded.begin() + bytes, guarded.end(),
                                               [](uint8_t value) { return value == 0xcd; }));
            // Exact-sized allocation lets ASan catch even the first excess write.
            std::vector<uint8_t> exact(bytes);
            tcx::hap::detail::decodeRgbaBlocks(src, exact.data(), width, height,
                                               blockSizes[format], decoders[format]);
            check("BC decode: exact-sized allocation matches guarded output",
                  std::memcmp(exact.data(), guarded.data(), bytes) == 0);
        }
    }
}

void unalignedBlockTests() {
    const char* names[] = {"BC1", "BC3", "BC7", "BC4 (Hap Q Alpha)"};
    const size_t blockSizes[] = {8, 16, 16, 8};
    void (*decoders[])(const void*, void*, int) = {bcdec_bc1, bcdec_bc3, bcdec_bc7, bcdec_bc4};
    for (int format = 0; format < 4; ++format) {
        Block block = format == 2 ? bc7Block(1) : colorBlock(1, format == 1);
        if (format == 3) {
            // Alpha endpoints 224 and 32; alternate endpoint indices 0 and 1.
            block = {};
            block[0] = 224;
            block[1] = 32;
            for (int pixel = 0; pixel < 16; ++pixel) {
                const int bit = 16 + pixel * 3;
                block[bit / 8] |= (pixel % 2) << (bit % 8);
            }
        }
        for (size_t offset = 0; offset < 8; ++offset) {
            // Allocation starts aligned; the compressed block ends at its boundary.
            std::vector<uint8_t> compressed(offset + blockSizes[format]);
            std::memcpy(compressed.data() + offset, block.data(), blockSizes[format]);
            // BC1/BC3 still require aligned output for their native-word stores.
            std::array<uint32_t, 16> decoded{};
            decoders[format](compressed.data() + offset, decoded.data(), format == 3 ? 4 : 16);
            const auto* pixels = reinterpret_cast<const uint8_t*>(decoded.data());
            bool pixelsOk = true;
            for (int pixel = 0; pixel < 16; ++pixel) {
                if (format == 3) {
                    pixelsOk &= pixels[pixel] == (pixel % 2 ? 32 : 224);
                } else {
                    const auto expected = expectedPixel(1, pixel, format);
                    pixelsOk &= std::memcmp(pixels + pixel * 4, expected.data(), 4) == 0;
                }
            }
            char name[128];
            std::snprintf(name, sizeof(name), "%s: compressed input byte offset %zu", names[format], offset);
            check(name, pixelsOk);
        }
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
    blockTests();
    unalignedBlockTests();
    drawTests();
    return failures;
}
