// Only this TU includes Highway. Web keeps the scalar path until SIMD128
// is approved; other architectures can use the same explicit fallback.
#include "tcSimd.h"

#ifndef TRUSSC_SIMD_SCALAR
#if !defined(__EMSCRIPTEN__) && !defined(__ARM_BIG_ENDIAN) && \
    !defined(__AARCH64EB__) && !defined(__ARMEB__) && (defined(__i386__) || defined(__x86_64__) || \
    defined(_M_IX86) || defined(_M_X64) || defined(__aarch64__) || \
    defined(_M_ARM64) || defined(__ARM_NEON))
#define TRUSSC_SIMD_SCALAR 0
#else
#define TRUSSC_SIMD_SCALAR 1
#endif
#endif

#if !TRUSSC_SIMD_SCALAR
// Keep the baseline fallback and SSE4/AVX2/AVX-512 on x86. The full target
// set can be measured with -DTRUSSC_SIMD_FULL_TARGETS=1.
#if !defined(TRUSSC_SIMD_FULL_TARGETS) && (defined(__i386__) || \
    defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64))
#define HWY_DISABLED_TARGETS (HWY_SSE2 | HWY_SSSE3 | HWY_AVX3_DL | HWY_AVX3_ZEN4 | HWY_AVX3_SPR)
#endif
#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "tc/utils/tcSimd.cpp"
#include "hwy/foreach_target.h"
#include "hwy/highway.h"
// targets.cc references VectorBytes even on x86 in unoptimized builds.
// Include its per-target implementation during our existing target pass.
#include "hwy/per_target.cc"
#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "tc/utils/tcSimd.cpp"

HWY_BEFORE_NAMESPACE();
namespace trussc::simd {
namespace HWY_NAMESPACE {
namespace hn = hwy::HWY_NAMESPACE;

void Swizzle(const std::uint8_t* src, std::uint8_t* dst, std::size_t pixels,
             bool forceAlpha) {
#if HWY_TARGET == HWY_SCALAR
    scalar::bgraToRgba(src, dst, pixels, forceAlpha);
#else
    const hn::ScalableTag<std::uint8_t> d;
    const std::size_t blockPixels = hn::Lanes(d) / 4;
    alignas(16) static constexpr std::uint8_t indices[16] =
        {2, 1, 0, 3, 6, 5, 4, 7, 10, 9, 8, 11, 14, 13, 12, 15};
    alignas(16) static constexpr std::uint8_t alphaBytes[16] =
        {0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255};
    const auto lookup = hn::LoadDup128(d, indices);
    const auto alpha = forceAlpha ? hn::LoadDup128(d, alphaBytes) : hn::Zero(d);
    std::size_t i = 0;
    for (; pixels - i >= blockPixels; i += blockPixels) {
        const auto input = hn::LoadU(d, src + 4 * i);
        const auto output = hn::Or(hn::TableLookupBytes(input, lookup), alpha);
        hn::StoreU(output, d, dst + 4 * i);
    }
    if (i != pixels) scalar::bgraToRgba(src + 4 * i, dst + 4 * i, pixels - i, forceAlpha);
#endif
}

void Yuyv(const std::uint8_t* src, std::uint8_t* dst, std::size_t pixels) {
#if HWY_TARGET == HWY_SCALAR
    scalar::yuyvToRgba(src, dst, pixels);
#else
    const hn::ScalableTag<std::int32_t> d;
    const hn::Rebind<std::uint8_t, decltype(d)> d8;
    const hn::Rebind<std::uint32_t, decltype(d)> du;
    const std::size_t lanes = hn::Lanes(d);
    std::size_t i = 0;
    for (; pixels - i >= 2 * lanes; i += 2 * lanes) {
        hn::Vec<decltype(d8)> y0, u, y1, v;
        hn::LoadInterleaved4(d8, src + 2 * i, y0, u, y1, v);
        const auto chromaU = hn::Sub(hn::PromoteTo(d, u), hn::Set(d, 128));
        const auto chromaV = hn::Sub(hn::PromoteTo(d, v), hn::Set(d, 128));
        const auto red = hn::Mul(chromaV, hn::Set(d, 409));
        const auto green = hn::Add(hn::Mul(chromaU, hn::Set(d, -100)),
                                   hn::Mul(chromaV, hn::Set(d, -208)));
        const auto blue = hn::Mul(chromaU, hn::Set(d, 516));
        const auto pack = [&](hn::Vec<decltype(d8)> y) {
            const auto luminance = hn::Add(hn::Mul(hn::Sub(hn::PromoteTo(d, y),
                hn::Set(d, 16)), hn::Set(d, 298)), hn::Set(d, 128));
            const auto channel = [&](hn::Vec<decltype(d)> chroma) {
                return hn::BitCast(du, hn::Min(hn::Set(d, 255), hn::Max(hn::Zero(d),
                    hn::ShiftRight<8>(hn::Add(luminance, chroma)))));
            };
            return hn::Or(hn::Or(channel(red), hn::ShiftLeft<8>(channel(green))),
                hn::Or(hn::ShiftLeft<16>(channel(blue)), hn::Set(du, 0xFF000000u)));
        };
        // x86 and the supported ARM targets are little-endian. Store bytes
        // through a byte pointer (no alignment or uint32_t alias requirement).
        const auto first = pack(y0);
        const auto second = pack(y1);
        const hn::Repartition<std::uint8_t, decltype(d)> bytes;
        hn::StoreU(hn::BitCast(bytes, hn::InterleaveWholeLower(du, first, second)), bytes, dst + 4 * i);
        hn::StoreU(hn::BitCast(bytes, hn::InterleaveWholeUpper(du, first, second)), bytes,
                   dst + 4 * i + 4 * lanes);
    }
    if (i != pixels) scalar::yuyvToRgba(src + 2 * i, dst + 4 * i, pixels - i);
#endif
}

} // namespace HWY_NAMESPACE
} // namespace trussc::simd
HWY_AFTER_NAMESPACE();
#endif // !TRUSSC_SIMD_SCALAR

#if TRUSSC_SIMD_SCALAR || HWY_ONCE
namespace trussc::simd {
namespace scalar {
void bgraToRgba(const std::uint8_t* src, std::uint8_t* dst, std::size_t pixels,
                bool forceAlpha) {
    for (std::size_t i = 0; i < pixels; ++i) {
        const auto b = src[4 * i], g = src[4 * i + 1], r = src[4 * i + 2];
        const auto a = forceAlpha ? std::uint8_t(255) : src[4 * i + 3];
        dst[4 * i] = r; dst[4 * i + 1] = g; dst[4 * i + 2] = b; dst[4 * i + 3] = a;
    }
}
void rgbaToBgra(const std::uint8_t* src, std::uint8_t* dst, std::size_t pixels,
                bool forceAlpha) {
    bgraToRgba(src, dst, pixels, forceAlpha);
}
void yuyvToRgba(const std::uint8_t* src, std::uint8_t* dst, std::size_t pixels) {
    std::size_t i = 0;
    for (; pixels - i >= 2; i += 2) {
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

        dst[0] = static_cast<std::uint8_t>((r < 0) ? 0 : (r > 255) ? 255 : r);
        dst[1] = static_cast<std::uint8_t>((g < 0) ? 0 : (g > 255) ? 255 : g);
        dst[2] = static_cast<std::uint8_t>((b < 0) ? 0 : (b > 255) ? 255 : b);
        dst[3] = 255;
        dst += 4;

        c = y1 - 16;
        r = (298 * c + 409 * e + 128) >> 8;
        g = (298 * c - 100 * d - 208 * e + 128) >> 8;
        b = (298 * c + 516 * d + 128) >> 8;

        dst[0] = static_cast<std::uint8_t>((r < 0) ? 0 : (r > 255) ? 255 : r);
        dst[1] = static_cast<std::uint8_t>((g < 0) ? 0 : (g > 255) ? 255 : g);
        dst[2] = static_cast<std::uint8_t>((b < 0) ? 0 : (b > 255) ? 255 : b);
        dst[3] = 255;
        dst += 4;
    }
    if (i != pixels) {
        const int c = src[0] - 16, d = src[1] - 128, e = src[3] - 128;
        const auto clamp = [](int value) -> std::uint8_t {
            return static_cast<std::uint8_t>(value < 0 ? 0 : value > 255 ? 255 : value);
        };
        dst[0] = clamp((298 * c + 409 * e + 128) >> 8);
        dst[1] = clamp((298 * c - 100 * d - 208 * e + 128) >> 8);
        dst[2] = clamp((298 * c + 516 * d + 128) >> 8);
        dst[3] = 255;
    }
}
} // namespace scalar

#if !TRUSSC_SIMD_SCALAR
HWY_EXPORT(Swizzle);
HWY_EXPORT(Yuyv);
#endif
void bgraToRgba(const std::uint8_t* src, std::uint8_t* dst, std::size_t pixels, bool forceAlpha) {
#if TRUSSC_SIMD_SCALAR
    scalar::bgraToRgba(src, dst, pixels, forceAlpha);
#else
    HWY_DYNAMIC_DISPATCH(Swizzle)(src, dst, pixels, forceAlpha);
#endif
}
void rgbaToBgra(const std::uint8_t* src, std::uint8_t* dst, std::size_t pixels, bool forceAlpha) {
    bgraToRgba(src, dst, pixels, forceAlpha);
}
void yuyvToRgba(const std::uint8_t* src, std::uint8_t* dst, std::size_t pixels) {
#if TRUSSC_SIMD_SCALAR
    scalar::yuyvToRgba(src, dst, pixels);
#else
    HWY_DYNAMIC_DISPATCH(Yuyv)(src, dst, pixels);
#endif
}
} // namespace trussc::simd

#if !TRUSSC_SIMD_SCALAR
#include "hwy/targets.cc"
#include "hwy/abort.cc"
#endif
#endif // TRUSSC_SIMD_SCALAR || HWY_ONCE
