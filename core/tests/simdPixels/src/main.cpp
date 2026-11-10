#include <tc/utils/tcSimd.h>
#include "../../../platform/win/tcVideoGrabberPixels.h"
#include "../../common/tcCoreTest.h"
#include <algorithm>
#include <cstdio>
#include <chrono>
#include <cstring>
#include <vector>

// Match tcSimd.cpp target policy so GetIndex describes its dispatch table.
#if !defined(TRUSSC_SIMD_FULL_TARGETS) && (defined(__i386__) || \
    defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64))
#define HWY_DISABLED_TARGETS (HWY_SSE2 | HWY_SSSE3 | HWY_AVX3_DL | HWY_AVX3_ZEN4 | HWY_AVX3_SPR)
#endif
#if !defined(__EMSCRIPTEN__) && !defined(__ARM_BIG_ENDIAN) && \
    !defined(__AARCH64EB__) && !defined(__ARMEB__) && (defined(__i386__) || defined(__x86_64__) || \
    defined(_M_IX86) || defined(_M_X64) || defined(__aarch64__) || defined(_M_ARM64) || defined(__ARM_NEON))
#if !defined(TRUSSC_SIMD_SCALAR) || !TRUSSC_SIMD_SCALAR
#define TC_BENCH_HIGHWAY 1
#include "hwy/targets.h"
#endif
#endif

namespace {
namespace simd = trussc::simd;
using Bytes = std::vector<std::uint8_t>;
using Swizzle = void (*)(const std::uint8_t*, std::uint8_t*, std::size_t, bool);
int failures = 0;
void check(bool ok, const char* label, std::size_t pixels) {
    if (!ok) { std::printf("FAIL %s (%zu pixels)\n", label, pixels); ++failures; }
}
void fill(Bytes& bytes) {
    std::uint32_t value = 1361;
    for (auto& b : bytes) {
        value = value * 1664525u + 1013904223u;
        b = static_cast<std::uint8_t>(value >> 24);
    }
}
void swizzles(std::size_t pixels, std::size_t srcOffset, std::size_t dstOffset) {
    Bytes src(srcOffset + 4 * pixels + 19);
    fill(src);
    const auto original = src;
    for (bool opaque : {false, true}) {
        for (Swizzle convert : {simd::bgraToRgba, simd::rgbaToBgra}) {
            Bytes expected(dstOffset + 4 * pixels + 19, 0xA5), result = expected;
            simd::scalar::bgraToRgba(src.data() + srcOffset, expected.data() + dstOffset, pixels, opaque);
            convert(src.data() + srcOffset, result.data() + dstOffset, pixels, opaque);
            check(result == expected && src == original, "swizzle + guards", pixels);
            auto inPlace = src;
            auto inPlaceExpected = src;
            simd::scalar::bgraToRgba(inPlaceExpected.data() + srcOffset,
                inPlaceExpected.data() + srcOffset, pixels, opaque);
            convert(inPlace.data() + srcOffset, inPlace.data() + srcOffset, pixels, opaque);
            check(inPlace == inPlaceExpected, "in-place + guards", pixels);
            if (!opaque) {
                convert(inPlace.data() + srcOffset, inPlace.data() + srcOffset, pixels, false);
                check(inPlace == src, "round trip", pixels);
            }
        }
    }
}
void yuyv(std::size_t pixels, std::size_t srcOffset, std::size_t dstOffset) {
    // Source ends at the last complete YUYV group, catching vector overreads
    // under ASan as well as writes beyond the exact output extent.
    Bytes src(srcOffset + ((pixels + 1) / 2) * 4);
    fill(src);
    const auto original = src;
    Bytes expected(dstOffset + 4 * pixels + 19, 0xA5), result = expected;
    simd::scalar::yuyvToRgba(src.data() + srcOffset, expected.data() + dstOffset, pixels);
    simd::yuyvToRgba(src.data() + srcOffset, result.data() + dstOffset, pixels);
    check(result == expected && src == original, "YUYV + guards", pixels);
}
// Original loops copied verbatim from origin/main (#1361 baseline).
// BYTE is the Windows byte type; keep these references runnable on every OS.
using BYTE = unsigned char;
static void convertYUYVtoRGBA(const unsigned char* src, unsigned char* dst, size_t pixelCount) {
    for (size_t i = 0; i < pixelCount; i += 2) {
        int y0 = src[0];
        int u  = src[1];
        int y1 = src[2];
        int v  = src[3];
        src += 4;

        // YUV to RGB conversion
        int c = y0 - 16;
        int d = u - 128;
        int e = v - 128;

        int r = (298 * c + 409 * e + 128) >> 8;
        int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
        int b = (298 * c + 516 * d + 128) >> 8;

        dst[0] = (r < 0) ? 0 : (r > 255) ? 255 : r;
        dst[1] = (g < 0) ? 0 : (g > 255) ? 255 : g;
        dst[2] = (b < 0) ? 0 : (b > 255) ? 255 : b;
        dst[3] = 255;
        dst += 4;

        c = y1 - 16;
        r = (298 * c + 409 * e + 128) >> 8;
        g = (298 * c - 100 * d - 208 * e + 128) >> 8;
        b = (298 * c + 516 * d + 128) >> 8;

        dst[0] = (r < 0) ? 0 : (r > 255) ? 255 : r;
        dst[1] = (g < 0) ? 0 : (g > 255) ? 255 : g;
        dst[2] = (b < 0) ? 0 : (b > 255) ? 255 : b;
        dst[3] = 255;
        dst += 4;
    }
}

inline bool copyGrabberRGB32(unsigned char* dst, const unsigned char* src,
                             std::size_t length, int width, int height,
                             std::int32_t stride) {
    if (!dst || !src || width <= 0 || height <= 0) return false;
    if (static_cast<std::size_t>(width) > length / 4) return false;
    const std::size_t rowBytes = static_cast<std::size_t>(width) * 4;
    const std::size_t pitch = stride == 0 ? rowBytes : static_cast<std::size_t>(
        stride < 0 ? -static_cast<std::int64_t>(stride) : stride);
    // The last row need not include trailing padding. Division avoids
    // overflowing when checking the span of all rows before any reads.
    if (pitch < rowBytes || static_cast<std::size_t>(height - 1) >
            (length - rowBytes) / pitch) return false;

    for (int y = 0; y < height; ++y) {
        const int sourceY = stride < 0 ? height - 1 - y : y;
        const auto* srcRow = src + static_cast<std::size_t>(sourceY) * pitch;
        auto* dstRow = dst + static_cast<std::size_t>(y) * rowBytes;
        for (std::size_t x = 0; x < rowBytes; x += 4) {
            dstRow[x + 0] = srcRow[x + 2];
            dstRow[x + 1] = srcRow[x + 1];
            dstRow[x + 2] = srcRow[x + 0];
            dstRow[x + 3] = 255;
        }
    }
    return true;
}

void originalRecorder(const unsigned char* rgba, unsigned char* dst) {
    const int w = 1920, h = 1080;
    const size_t stride = w * 4;
    for (int y = 0; y < h; ++y) {
        const unsigned char* srow = rgba + (size_t)y * stride;
        BYTE* drow = dst + (size_t)(h - 1 - y) * stride;
        for (size_t x = 0; x < (size_t)w; ++x) {
            drow[x * 4 + 0] = srow[x * 4 + 2];  // B
            drow[x * 4 + 1] = srow[x * 4 + 1];  // G
            drow[x * 4 + 2] = srow[x * 4 + 0];  // R
            drow[x * 4 + 3] = srow[x * 4 + 3];  // A
        }
    }
}

constexpr int benchWidth = 1920, benchHeight = 1080;
constexpr size_t benchPixels = benchWidth * benchHeight;
constexpr size_t benchPitch = benchWidth * 4 + 64;
using Kernel = void (*)(const std::uint8_t*, std::uint8_t*);
volatile unsigned benchChecksum = 0;

double batch(Kernel kernel, const Bytes& src, Bytes& dst) {
    constexpr int frames = 20;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < frames; ++i) kernel(src.data(), dst.data());
    const auto stop = std::chrono::steady_clock::now();
    benchChecksum = dst[0]; // Keep the stores observable outside the timed batch.
    return std::chrono::duration<double, std::milli>(stop - start).count() / frames;
}

int benchmark() {
    Bytes src(benchPitch * benchHeight), before(src.size(), 0xA5), after = before;
    fill(src);
    struct Site { const char* name; Kernel before; Kernel after; };
    const Site sites[] = {
        {"linux_yuyv", [](auto s, auto d) { convertYUYVtoRGBA(s, d, benchPixels); },
            [](auto s, auto d) { simd::yuyvToRgba(s, d, benchPixels); }},
        {"windows_grabber_positive", [](auto s, auto d) {
            copyGrabberRGB32(d, s, benchPitch * benchHeight, benchWidth, benchHeight, int(benchPitch)); },
            [](auto s, auto d) {
            trussc::internal::copyGrabberRGB32(d, s, benchPitch * benchHeight, benchWidth, benchHeight, int(benchPitch)); }},
        {"windows_grabber_negative", [](auto s, auto d) {
            copyGrabberRGB32(d, s, benchPitch * benchHeight, benchWidth, benchHeight, -int(benchPitch)); },
            [](auto s, auto d) {
            trussc::internal::copyGrabberRGB32(d, s, benchPitch * benchHeight, benchWidth, benchHeight, -int(benchPitch)); }},
        {"windows_recorder", originalRecorder, [](auto s, auto d) {
            for (int y = 0; y < benchHeight; ++y)
                simd::rgbaToBgra(s + y * benchWidth * 4,
                    d + (benchHeight - 1 - y) * benchWidth * 4, benchWidth);
        }},
        // The symmetric swizzle is the shared BGRA/RGBA kernel.
        {"swizzle_kernel", [](auto s, auto d) { simd::scalar::bgraToRgba(s, d, benchPixels); },
            [](auto s, auto d) { simd::bgraToRgba(s, d, benchPixels); }},
        {"yuyv_kernel", [](auto s, auto d) { simd::scalar::yuyvToRgba(s, d, benchPixels); },
            [](auto s, auto d) { simd::yuyvToRgba(s, d, benchPixels); }},
    };
    // Initialize the module's dispatch before reporting the chosen target.
    simd::bgraToRgba(src.data(), after.data(), benchPixels);
    const char* target = "SCALAR";
#ifdef TC_BENCH_HIGHWAY
    const auto index = hwy::GetChosenTarget().GetIndex();
    for (const auto candidate : hwy::SupportedAndGeneratedTargets()) {
        hwy::ChosenTarget choice;
        choice.Update(candidate);
        if (choice.GetIndex() == index) { target = hwy::TargetName(candidate); break; }
    }
#endif
#if defined(__clang__)
    std::printf("compiler=Clang %s\n", __clang_version__);
#elif defined(_MSC_VER)
    std::printf("compiler=MSVC %d\n", _MSC_FULL_VER);
#elif defined(__GNUC__)
    std::printf("compiler=GCC %s\n", __VERSION__);
#endif
    std::printf("target=%s size=1920x1080 batches=9 frames_per_batch=20 warmup_frames=3\n", target);
    std::puts("site before_ms after_ms speedup");
    for (const auto& site : sites) {
        std::fill(before.begin(), before.end(), 0xA5);
        std::fill(after.begin(), after.end(), 0xA5);
        site.before(src.data(), before.data());
        site.after(src.data(), after.data());
        check(before == after, site.name, benchPixels);
        if (failures) return 1;
        for (int i = 0; i < 3; ++i) {
            site.before(src.data(), before.data());
            site.after(src.data(), after.data());
        }
        double a[9], b[9];
        for (int i = 0; i < 9; ++i) { // A B A B: shared load affects both paths.
            a[i] = batch(site.before, src, before);
            b[i] = batch(site.after, src, after);
        }
        std::sort(std::begin(a), std::end(a));
        std::sort(std::begin(b), std::end(b));
        std::printf("%s %.3f %.3f %.2fx\n", site.name, a[4], b[4], a[4] / b[4]);
    }
    return 0;
}
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--bench") == 0) return benchmark();
    simd::bgraToRgba(nullptr, nullptr, 0);
    simd::rgbaToBgra(nullptr, nullptr, 0, true);
    simd::yuyvToRgba(nullptr, nullptr, 0);
    for (std::size_t count : {0u, 1u, 2u, 3u, 7u, 15u, 16u, 17u, 31u, 33u, 1000u, 1920u * 1080u}) {
        for (std::size_t srcOffset : {0u, 1u, 3u, 7u}) {
            for (std::size_t dstOffset : {0u, 1u, 2u, 5u}) {
                swizzles(count, srcOffset, dstOffset);
                yuyv(count, srcOffset, dstOffset);
            }
        }
    }
    const std::uint8_t bgra[] = {7, 19, 33, 47, 61, 79, 97, 113};
    const Bytes rgba = {33, 19, 7, 47, 97, 79, 61, 113};
    Bytes converted(8);
    simd::bgraToRgba(bgra, converted.data(), 2);
    check(converted == rgba, "known swizzle", 2);
    const std::uint8_t neutral[] = {16, 128, 235, 128};
    simd::yuyvToRgba(neutral, converted.data(), 2);
    check(converted == Bytes({0, 0, 0, 255, 255, 255, 255, 255}), "BT.601 black/white", 2);
    std::printf("simdPixels: %d failures\n", failures);
    return failures ? 1 : 0;
}
