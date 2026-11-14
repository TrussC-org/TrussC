#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>

namespace trussc::internal {
// Clockwise quarter turns in image coordinates (origin at the upper left).
// A reflection is decomposed as a source horizontal flip, then rotation.
struct VideoRotation {
    int degrees = 0;
    bool mirrored = false;
    static int normalize(double degrees) {
        if (!std::isfinite(degrees)) return 0;
        int turns = int(std::round(std::remainder(degrees, 360.0) / 90.0));
        return ((turns % 4 + 4) % 4) * 90;
    }
    static VideoRotation fromMatrix(double a, double b, double c, double d) {
        const double determinant = a * d - b * c;
        if (!std::isfinite(determinant) || determinant == 0) return {};
        const bool reflected = determinant < 0;
        if (reflected) { a = -a; b = -b; }
        return {normalize(std::atan2(b, a) * 180.0 / std::acos(-1.0)), reflected};
    }
    bool swapsSize() const { return degrees == 90 || degrees == 270; }
    bool active() const { return degrees != 0 || mirrored; }
    void size(int& width, int& height) const { if (swapsSize()) std::swap(width, height); }
    // Pixel copies may alias byte buffers and need no alignment beyond a byte.
#if defined(__GNUC__) || defined(__clang__)
    typedef uint32_t Pixel __attribute__((__may_alias__, __aligned__(1)));
#else
    using Pixel = uint32_t;
#endif
    template<bool ReverseX, bool ReverseY>
    static void transpose(const Pixel* source, Pixel* target, int width, int height) {
        // Bound both row working sets; edge tiles also handle small/odd frames.
        constexpr int tileSize = 16;
        for (int tileX = 0; tileX < width;) {
            const int xEnd = tileX + (width - tileX > tileSize ? tileSize : width - tileX);
            for (int tileY = 0; tileY < height;) {
                const int yEnd = tileY + (height - tileY > tileSize ? tileSize : height - tileY);
                for (int x = tileX; x < xEnd; ++x) {
                    Pixel* row = target + size_t(ReverseX ? width - 1 - x : x) * height;
                    for (int y = tileY; y < yEnd; ++y) {
                        row[ReverseY ? height - 1 - y : y] = source[size_t(y) * width + x];
                    }
                }
                tileY = yEnd;
            }
            tileX = xEnd;
        }
    }
    // Non-overlapping tightly packed RGBA buffers, admitted by the caller.
    void apply(const uint8_t* source, uint8_t* target, int width, int height) const {
        const auto* input = reinterpret_cast<const Pixel*>(source);
        auto* output = reinterpret_cast<Pixel*>(target);
        if (degrees == 90) {
            if (mirrored) transpose<true, true>(input, output, width, height);
            else transpose<false, true>(input, output, width, height);
        } else if (degrees == 270) {
            if (mirrored) transpose<false, false>(input, output, width, height);
            else transpose<true, false>(input, output, width, height);
        } else if (degrees == 180) {
            if (mirrored) {
                for (int y = 0; y < height; ++y) {
                    std::memcpy(output + size_t(height - 1 - y) * width,
                                input + size_t(y) * width, size_t(width) * 4);
                }
            } else {
                for (int y = 0; y < height; ++y) {
                    const Pixel* row = input + size_t(y) * width;
                    Pixel* destination = output + size_t(height - 1 - y) * width;
                    for (int x = 0; x < width; ++x) destination[width - 1 - x] = row[x];
                }
            }
        } else if (mirrored) {
            for (int y = 0; y < height; ++y) {
                const Pixel* row = input + size_t(y) * width;
                Pixel* destination = output + size_t(y) * width;
                for (int x = 0; x < width; ++x) destination[width - 1 - x] = row[x];
            }
        } else {
            for (int y = 0; y < height; ++y) {
                std::memcpy(output + size_t(y) * width, input + size_t(y) * width, size_t(width) * 4);
            }
        }
    }
};
} // namespace trussc::internal
