// =============================================================================
// tcPixels.cpp - Pixels implementation (requires TrussC.h for getDataPath)
// =============================================================================

#include "TrussC.h"

namespace trussc {

bool Pixels::save(const fs::path& path) const {
    if (!allocated_ || !data_ || format_ == PixelFormat::F32) return false;

    // Convert relative paths to data path
    fs::path savePath = path;
    if (path.is_relative()) {
        savePath = getDataPath(path);
    }

    auto ext = savePath.extension().string();
    // UTF-8 for stb (STBIW_WINDOWS_UTF8 makes stb wide-open it on Windows)
    auto pathStr = internal::pathToUtf8(savePath);
    int result = 0;

    if (ext == ".png" || ext == ".PNG") {
        result = stbi_write_png(pathStr.c_str(), width_, height_, channels_, data_, width_ * channels_);
    } else if (ext == ".jpg" || ext == ".jpeg" || ext == ".JPG" || ext == ".JPEG") {
        result = stbi_write_jpg(pathStr.c_str(), width_, height_, channels_, data_, 90);
    } else if (ext == ".bmp" || ext == ".BMP") {
        result = stbi_write_bmp(pathStr.c_str(), width_, height_, channels_, data_);
    } else {
        // Default is PNG
        result = stbi_write_png(pathStr.c_str(), width_, height_, channels_, data_, width_ * channels_);
    }

    return result != 0;
}

// Platform-specific image loader fallback (non-Apple stub)
#ifndef __APPLE__
bool Pixels::loadPlatform(const fs::path& /*path*/) {
    return false;
}
#endif

// =============================================================================
// Image operations: halve / resize / crop / mirror
// =============================================================================
//
// Math is done in linear light for U8 buffers. The convention used by the
// gamma helpers (sRGBToLinear / linearToSRGB above in the header) is gamma
// 2.2 — same as the FBO mipmap downsample shader and the rest of the
// gamma-correct paths. F32 buffers are assumed to already be linear (HDR
// sources, intermediate compute), so no conversion is applied.
//
// "isLinearChannel_(c)" decides whether channel `c` needs sRGB
// decode/encode: only R/G/B of a 3- or 4-channel buffer go through the
// curve; alpha and single-/two-channel buffers (masks, normals, etc.)
// stay linear.

namespace {

// Catmull-Rom bicubic kernel weight at signed distance d. The result is
// undefined outside [-2, 2]; callers are expected to only sample within
// that window. Sum of weights over -1, 0, 1, 2 is exactly 1, so the
// caller can skip normalisation.
inline float catmullRomWeight(float d) {
    float a = std::abs(d);
    if (a <= 1.0f) return ((1.5f * a - 2.5f) * a) * a + 1.0f;
    if (a <  2.0f) return (((-0.5f * a) + 2.5f) * a - 4.0f) * a + 2.0f;
    return 0.0f;
}

// sRGB <-> linear conversion (gamma 2.2 approximation), same convention
// as the rest of the gamma-correct pipeline.
inline float sRGBToLinear(float s) { return std::pow(s, 2.2f); }
inline float linearToSRGB(float l) { return std::pow(l, 1.0f / 2.2f); }

// For 3- and 4-channel buffers, the R/G/B channels are sRGB-encoded;
// alpha (channel 3 in RGBA) and anything in a single-/two-channel buffer
// (masks, normals, etc.) stays linear.
inline bool isLinearChannel(int channels, int c) {
    if (channels <= 2) return true;
    return c == 3;
}

inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

} // anonymous namespace

void Pixels::halve() {
    if (!allocated_) return;
    int newW = std::max(width_ / 2, 1);
    int newH = std::max(height_ / 2, 1);

    Pixels dst;
    dst.allocate(newW, newH, channels_, format_);

    const bool isF32 = (format_ == PixelFormat::F32);
    const int   ch    = channels_;
    const int   sw    = width_;
    const int   sh    = height_;

    auto srcF = static_cast<const float*>(data_);
    auto srcU = static_cast<const unsigned char*>(data_);
    auto dstF = static_cast<float*>(dst.data_);
    auto dstU = static_cast<unsigned char*>(dst.data_);

    for (int y = 0; y < newH; y++) {
        int sy0 = std::min(2 * y,     sh - 1);
        int sy1 = std::min(2 * y + 1, sh - 1);
        for (int x = 0; x < newW; x++) {
            int sx0 = std::min(2 * x,     sw - 1);
            int sx1 = std::min(2 * x + 1, sw - 1);
            for (int c = 0; c < ch; c++) {
                int i00 = (sy0 * sw + sx0) * ch + c;
                int i01 = (sy0 * sw + sx1) * ch + c;
                int i10 = (sy1 * sw + sx0) * ch + c;
                int i11 = (sy1 * sw + sx1) * ch + c;
                if (isF32) {
                    // F32 = already linear, average directly.
                    float avg = (srcF[i00] + srcF[i01] + srcF[i10] + srcF[i11]) * 0.25f;
                    dstF[(y * newW + x) * ch + c] = avg;
                } else {
                    bool linear = isLinearChannel(ch, c);
                    auto fetch = [&](int i) -> float {
                        float v = srcU[i] / 255.0f;
                        return linear ? v : sRGBToLinear(v);
                    };
                    float avg = (fetch(i00) + fetch(i01) + fetch(i10) + fetch(i11)) * 0.25f;
                    if (!linear) avg = linearToSRGB(avg);
                    avg = clampf(avg, 0.0f, 1.0f);
                    dstU[(y * newW + x) * ch + c] = static_cast<unsigned char>(std::lround(avg * 255.0f));
                }
            }
        }
    }

    *this = std::move(dst);
}

void Pixels::crop(int x, int y, int w, int h) {
    if (!allocated_ || w <= 0 || h <= 0) return;

    Pixels dst;
    dst.allocate(w, h, channels_, format_);

    const size_t bpp = (format_ == PixelFormat::F32)
                       ? (size_t)channels_ * sizeof(float)
                       : (size_t)channels_;
    auto srcBytes = static_cast<const unsigned char*>(data_);
    auto dstBytes = static_cast<unsigned char*>(dst.data_);

    const int sw = width_;
    const int sh = height_;

    for (int dy = 0; dy < h; dy++) {
        // clamp-to-edge: rows outside the source repeat the nearest in-bounds row.
        int sy = y + dy;
        if (sy < 0) sy = 0;
        else if (sy >= sh) sy = sh - 1;
        for (int dx = 0; dx < w; dx++) {
            int sx = x + dx;
            if (sx < 0) sx = 0;
            else if (sx >= sw) sx = sw - 1;
            std::memcpy(dstBytes + ((size_t)dy * w + dx) * bpp,
                        srcBytes + ((size_t)sy * sw + sx) * bpp,
                        bpp);
        }
    }

    *this = std::move(dst);
}

void Pixels::mirror(bool horizontal, bool vertical) {
    if (!allocated_ || (!horizontal && !vertical)) return;

    const size_t bpp = (format_ == PixelFormat::F32)
                       ? (size_t)channels_ * sizeof(float)
                       : (size_t)channels_;
    auto bytes = static_cast<unsigned char*>(data_);
    std::vector<unsigned char> tmp(bpp);

    if (horizontal) {
        for (int yy = 0; yy < height_; yy++) {
            unsigned char* row = bytes + (size_t)yy * width_ * bpp;
            for (int a = 0, b = width_ - 1; a < b; a++, b--) {
                std::memcpy(tmp.data(),   row + (size_t)a * bpp, bpp);
                std::memcpy(row + (size_t)a * bpp, row + (size_t)b * bpp, bpp);
                std::memcpy(row + (size_t)b * bpp, tmp.data(),   bpp);
            }
        }
    }
    if (vertical) {
        const size_t rowBytes = (size_t)width_ * bpp;
        std::vector<unsigned char> rowTmp(rowBytes);
        for (int a = 0, b = height_ - 1; a < b; a++, b--) {
            std::memcpy(rowTmp.data(),                    bytes + (size_t)a * rowBytes, rowBytes);
            std::memcpy(bytes + (size_t)a * rowBytes,     bytes + (size_t)b * rowBytes, rowBytes);
            std::memcpy(bytes + (size_t)b * rowBytes,     rowTmp.data(),                rowBytes);
        }
    }
}

// One-axis resample helper for Pixels::resize.
//
// Resamples `src` along the X axis only into `dst`. Both buffers must
// match in height, channels and format; only the widths differ. Picks
// BoxArea when downscaling (`dstW < srcW`) and Catmull-Rom bicubic when
// upscaling. For U8 buffers, RGB channels run through the gamma 2.2
// curve so the average lives in linear light; alpha and non-RGB buffers
// stay linear.
static void resample1D_X(const Pixels& src, Pixels& dst) {
    const int sw = src.getWidth();
    const int sh = src.getHeight();
    const int dw = dst.getWidth();
    const int ch = src.getChannels();
    const bool isF32 = src.isFloat();
    const bool downscale = dw < sw;
    const float scale = (float)sw / (float)dw;  // > 1 downscale, < 1 upscale

    const float* srcF = isF32 ? static_cast<const float*>(src.getDataVoid()) : nullptr;
    const unsigned char* srcU = isF32 ? nullptr : static_cast<const unsigned char*>(src.getDataVoid());
    float* dstF = isF32 ? static_cast<float*>(dst.getDataVoid()) : nullptr;
    unsigned char* dstU = isF32 ? nullptr : static_cast<unsigned char*>(dst.getDataVoid());

    auto fetchLinear = [&](int y, int x, int c, bool isLinearCh) -> float {
        if (isF32) return srcF[((size_t)y * sw + x) * ch + c];
        float v = srcU[((size_t)y * sw + x) * ch + c] / 255.0f;
        return isLinearCh ? v : sRGBToLinear(v);
    };
    auto storeLinear = [&](int y, int x, int c, bool isLinearCh, float v) {
        if (isF32) {
            dstF[((size_t)y * dw + x) * ch + c] = v;
            return;
        }
        if (!isLinearCh) v = linearToSRGB(v);
        if (v < 0.0f) v = 0.0f;
        else if (v > 1.0f) v = 1.0f;
        dstU[((size_t)y * dw + x) * ch + c] = static_cast<unsigned char>(std::lround(v * 255.0f));
    };

    auto isLinearChannel = [&](int c) -> bool {
        if (ch <= 2) return true;
        return c == 3;  // alpha
    };

    for (int y = 0; y < sh; y++) {
        for (int x = 0; x < dw; x++) {
            for (int c = 0; c < ch; c++) {
                bool linCh = isLinearChannel(c);
                float v = 0.0f;
                if (downscale) {
                    // BoxArea — every source texel inside [startF, endF) contributes
                    // proportional to its overlap with that interval.
                    float startF = x * scale;
                    float endF   = (x + 1) * scale;
                    int startI = (int)std::floor(startF);
                    int endI   = std::min((int)std::ceil(endF), sw);
                    float sum = 0.0f, total = 0.0f;
                    for (int si = startI; si < endI; si++) {
                        float w = std::min(endF, (float)(si + 1)) - std::max(startF, (float)si);
                        if (w <= 0.0f) continue;
                        sum   += fetchLinear(y, si, c, linCh) * w;
                        total += w;
                    }
                    v = (total > 0.0f) ? (sum / total) : fetchLinear(y, std::min(startI, sw - 1), c, linCh);
                } else {
                    // Catmull-Rom bicubic — 4 neighbours of the sub-pixel position
                    // in source space, clamped to source bounds.
                    float center = (x + 0.5f) * scale - 0.5f;
                    int cf = (int)std::floor(center);
                    float t = center - (float)cf;
                    float sum = 0.0f;
                    for (int k = -1; k <= 2; k++) {
                        int si = cf + k;
                        if (si < 0) si = 0;
                        else if (si >= sw) si = sw - 1;
                        sum += fetchLinear(y, si, c, linCh) * catmullRomWeight((float)k - t);
                    }
                    v = sum;
                }
                storeLinear(y, x, c, linCh, v);
            }
        }
    }
}

// One-axis resample helper for Pixels::resize.
//
// Resamples `src` along the Y axis only into `dst`. Mirrors `resample1D_X`;
// only width is shared between buffers, heights differ.
static void resample1D_Y(const Pixels& src, Pixels& dst) {
    const int sw = src.getWidth();
    const int sh = src.getHeight();
    const int dh = dst.getHeight();
    const int ch = src.getChannels();
    const bool isF32 = src.isFloat();
    const bool downscale = dh < sh;
    const float scale = (float)sh / (float)dh;

    const float* srcF = isF32 ? static_cast<const float*>(src.getDataVoid()) : nullptr;
    const unsigned char* srcU = isF32 ? nullptr : static_cast<const unsigned char*>(src.getDataVoid());
    float* dstF = isF32 ? static_cast<float*>(dst.getDataVoid()) : nullptr;
    unsigned char* dstU = isF32 ? nullptr : static_cast<unsigned char*>(dst.getDataVoid());

    auto fetchLinear = [&](int y, int x, int c, bool isLinearCh) -> float {
        if (isF32) return srcF[((size_t)y * sw + x) * ch + c];
        float v = srcU[((size_t)y * sw + x) * ch + c] / 255.0f;
        return isLinearCh ? v : sRGBToLinear(v);
    };
    auto storeLinear = [&](int y, int x, int c, bool isLinearCh, float v) {
        if (isF32) {
            dstF[((size_t)y * sw + x) * ch + c] = v;
            return;
        }
        if (!isLinearCh) v = linearToSRGB(v);
        if (v < 0.0f) v = 0.0f;
        else if (v > 1.0f) v = 1.0f;
        dstU[((size_t)y * sw + x) * ch + c] = static_cast<unsigned char>(std::lround(v * 255.0f));
    };

    auto isLinearChannel = [&](int c) -> bool {
        if (ch <= 2) return true;
        return c == 3;
    };

    for (int y = 0; y < dh; y++) {
        for (int c = 0; c < ch; c++) {
            // weights / indices for this destination row, reused across all x.
            int wIndex[4];
            float wValue[4];
            int wCount = 0;
            float startF = 0.0f, endF = 0.0f;

            if (downscale) {
                startF = y * scale;
                endF   = (y + 1) * scale;
            } else {
                float center = (y + 0.5f) * scale - 0.5f;
                int cf = (int)std::floor(center);
                float t = center - (float)cf;
                for (int k = -1; k <= 2; k++) {
                    int si = cf + k;
                    if (si < 0) si = 0;
                    else if (si >= sh) si = sh - 1;
                    wIndex[wCount] = si;
                    wValue[wCount] = catmullRomWeight((float)k - t);
                    wCount++;
                }
            }

            for (int x = 0; x < sw; x++) {
                bool linCh = isLinearChannel(c);
                float v = 0.0f;
                if (downscale) {
                    int startI = (int)std::floor(startF);
                    int endI   = std::min((int)std::ceil(endF), sh);
                    float sum = 0.0f, total = 0.0f;
                    for (int si = startI; si < endI; si++) {
                        float w = std::min(endF, (float)(si + 1)) - std::max(startF, (float)si);
                        if (w <= 0.0f) continue;
                        sum   += fetchLinear(si, x, c, linCh) * w;
                        total += w;
                    }
                    v = (total > 0.0f) ? (sum / total) : fetchLinear(std::min(startI, sh - 1), x, c, linCh);
                } else {
                    float sum = 0.0f;
                    for (int k = 0; k < wCount; k++) {
                        sum += fetchLinear(wIndex[k], x, c, linCh) * wValue[k];
                    }
                    v = sum;
                }
                storeLinear(y, x, c, linCh, v);
            }
        }
    }
}

void Pixels::resize(int newW, int newH) {
    if (!allocated_ || newW <= 0 || newH <= 0) return;
    if (newW == width_ && newH == height_) return;

    // 2-pass separable: horizontal first into an intermediate of size
    // (newW x height_), then vertical into the final (newW x newH).
    Pixels intermediate;
    intermediate.allocate(newW, height_, channels_, format_);
    resample1D_X(*this, intermediate);

    Pixels final;
    final.allocate(newW, newH, channels_, format_);
    resample1D_Y(intermediate, final);

    *this = std::move(final);
}

} // namespace trussc
