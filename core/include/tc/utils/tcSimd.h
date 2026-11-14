#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

namespace trussc::simd {

// Name of the target selected for the dispatched kernels.
const char* targetName();

// Packed 4-byte pixels. src == dst is supported; other overlap is not.
// forceAlpha replaces alpha with 255; otherwise alpha is preserved.
// A zero count does not access either pointer (nullptr is allowed).
void bgraToRgba(const std::uint8_t* src, std::uint8_t* dst, std::size_t pixels,
                bool forceAlpha = false);
void rgbaToBgra(const std::uint8_t* src, std::uint8_t* dst, std::size_t pixels,
                bool forceAlpha = false);

// BT.601 limited-range integer conversion, matching the V4L2 capture path.
// Source holds ceil(pixels / 2) complete Y0,U,Y1,V groups; an odd count
// outputs only Y0 from the final group. Source and destination must not overlap.
void yuyvToRgba(const std::uint8_t* src, std::uint8_t* dst, std::size_t pixels);

// Statistics: count == 0 does not access pointers (nullptr is allowed).
// mse requires count elements in both inputs. Variance divides by count.
float sum(const float* values, std::size_t count);
float mean(const float* values, std::size_t count);
float variance(const float* values, std::size_t count);
float stddev(const float* values, std::size_t count);
std::pair<float, float> minMax(const float* values, std::size_t count);
float rms(const float* values, std::size_t count);
float mse(const float* a, const float* b, std::size_t count);

// Portable references, also used by builds without Highway support.
namespace scalar {
float sum(const float* values, std::size_t count);
float mean(const float* values, std::size_t count);
float variance(const float* values, std::size_t count);
float stddev(const float* values, std::size_t count);
std::pair<float, float> minMax(const float* values, std::size_t count);
float rms(const float* values, std::size_t count);
float mse(const float* a, const float* b, std::size_t count);

void bgraToRgba(const std::uint8_t* src, std::uint8_t* dst, std::size_t pixels,
                bool forceAlpha = false);
void rgbaToBgra(const std::uint8_t* src, std::uint8_t* dst, std::size_t pixels,
                bool forceAlpha = false);
void yuyvToRgba(const std::uint8_t* src, std::uint8_t* dst, std::size_t pixels);
} // namespace scalar

} // namespace trussc::simd
