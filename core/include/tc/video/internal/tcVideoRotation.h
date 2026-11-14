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
    // Non-overlapping tightly packed RGBA buffers, admitted by the caller.
    void apply(const uint8_t* source, uint8_t* target, int width, int height) const {
        const int outputWidth = swapsSize() ? height : width;
        // Small tiles keep transposed source and destination rows in cache.
        for (int tileY = 0; tileY < height;) {
            const int rowEnd = tileY + (height - tileY > 32 ? 32 : height - tileY);
            for (int tileX = 0; tileX < width;) {
                const int columnEnd = tileX + (width - tileX > 32 ? 32 : width - tileX);
                for (int y = tileY; y < rowEnd; ++y) {
                    for (int x = tileX; x < columnEnd; ++x) {
                        const int reflectedX = mirrored ? width - 1 - x : x;
                        int dx = reflectedX, dy = y;
                        if (degrees == 90) { dx = height - 1 - y; dy = reflectedX; }
                        else if (degrees == 180) { dx = width - 1 - reflectedX; dy = height - 1 - y; }
                        else if (degrees == 270) { dx = y; dy = width - 1 - reflectedX; }
                        std::memcpy(target + (size_t(dy) * outputWidth + dx) * 4,
                                    source + (size_t(y) * width + x) * 4, 4);
                    }
                }
                tileX = columnEnd;
            }
            tileY = rowEnd;
        }
    }
};
} // namespace trussc::internal
