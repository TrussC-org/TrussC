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
            // Keep compressed blocks aligned for bcdec's native-word reads.
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

tc::Vec4 clipPosition(const tcx::hap::detail::YCoCgDrawData& data, int index) {
    tc::Mat4 matrix;
    std::memcpy(matrix.m, data.uniforms.mvp, sizeof(matrix.m));
    const auto& vertex = data.vertices[index];
    return matrix.transposed() * tc::Vec4(vertex.x, vertex.y, vertex.z, 1);
}

bool near(float a, float b) { return std::abs(a - b) < 1e-5f; }

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
        cornersOk &= near(clip.x, corners[i].x) && near(clip.y, corners[i].y) && near(clip.w, 1);
        colorsOk &= near(vertex.r, color.r) && near(vertex.g, color.g) &&
                    near(vertex.b, color.b) && near(vertex.a, color.a);
        uvOk &= near(vertex.u, uv[i].x) && near(vertex.v, uv[i].y);
    }
    check("HAP-Q: translate + scale applied once via column-major MVP", cornersOk);
    check("HAP-Q: tint and alpha on every vertex", colorsOk);
    check("HAP-Q: quad texture coordinates", uvOk);

    // Fbo::begin supplies its own projection; no window-size uniform is needed.
    const auto fbo = tcx::hap::detail::makeYCoCgDrawData(0, 0, 1920, 1080,
        Mat4::ortho(0, 1920, 1080, 0, -1, 1), Mat4::identity(), Mat4::identity(), color);
    const auto tl = clipPosition(fbo, 0), br = clipPosition(fbo, 2);
    check("HAP-Q: Fbo-sized projection maps corners to clip bounds",
          near(tl.x, -1) && near(tl.y, 1) && near(br.x, 1) && near(br.y, -1));

    const Mat4 projection = Mat4::perspective(TAU / 6, 1.5f, 1, 100);
    const Mat4 view = Mat4::lookAt(Vec3(0, 0, 10), Vec3(0, 0, 0), Vec3(0, 1, 0));
    const auto camera = tcx::hap::detail::makeYCoCgDrawData(1, 2, 3, 4, projection, view, model, color);
    const auto clip = clipPosition(camera, 0);
    const Vec4 expected = projection * (view * Vec4(12, 24, 0, 1));
    check("HAP-Q: perspective camera uses projection, view and model",
          near(clip.x, expected.x) && near(clip.y, expected.y) &&
          near(clip.z, expected.z) && near(clip.w, expected.w) && !near(clip.w, 1));

    static_assert(sizeof(tcx::hap::YCoCgVsParams) == sizeof(vs_params_t));
}

} // namespace

int runVideoTests() {
    blockTests();
    drawTests();
    return failures;
}
