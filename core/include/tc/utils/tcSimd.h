#pragma once

#include <cstddef>
#include <cstdint>

namespace trussc::simd {

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

// Portable references, also used by builds without Highway support.
namespace scalar {
void bgraToRgba(const std::uint8_t* src, std::uint8_t* dst, std::size_t pixels,
                bool forceAlpha = false);
void rgbaToBgra(const std::uint8_t* src, std::uint8_t* dst, std::size_t pixels,
                bool forceAlpha = false);
void yuyvToRgba(const std::uint8_t* src, std::uint8_t* dst, std::size_t pixels);
} // namespace scalar

} // namespace trussc::simd
