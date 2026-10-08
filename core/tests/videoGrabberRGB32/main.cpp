#include "../../platform/win/tcVideoGrabberPixels.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <limits>

namespace {
using trussc::internal::copyGrabberRGB32;
int failures = 0;

void check(const char* name, bool ok) {
    std::printf("%s: %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

void conversion() {
    // Distinct colors/alpha in both rows; expected output is hand-written.
    const std::array<unsigned char, 16> packed = {
        1, 2, 3, 4, 5, 6, 7, 8,
        9, 10, 11, 12, 13, 14, 15, 16
    };
    const std::array<unsigned char, 16> expected = {
        11, 10, 9, 255, 15, 14, 13, 255,
        3, 2, 1, 255, 7, 6, 5, 255
    };
    std::array<unsigned char, 16> dst{};
    for (std::int32_t stride : {0, 8, -8}) {
        dst.fill(0);
        check("packed RGB32 preserves channel swap, opaque alpha and flip",
              copyGrabberRGB32(dst.data(), packed.data(), packed.size(), 2, 2, stride)
              && dst == expected);
    }

    const std::array<unsigned char, 20> padded = {
        1, 2, 3, 4, 5, 6, 7, 8, 99, 99, 99, 99,
        9, 10, 11, 12, 13, 14, 15, 16
    };
    for (std::int32_t stride : {12, -12}) {
        dst.fill(0);
        check("padding skipped with either stride sign; flip unchanged",
              copyGrabberRGB32(dst.data(), padded.data(), padded.size(), 2, 2, stride)
              && dst == expected);
    }
    std::array<unsigned char, 8> row{};
    const std::array<unsigned char, 8> expectedRow = {3, 2, 1, 255, 7, 6, 5, 255};
    check("single row needs no trailing padding",
          copyGrabberRGB32(row.data(), packed.data(), 8, 2, 1, 12)
          && row == expectedRow);

    dst.fill(42);
    check("truncated padded frame rejected",
          !copyGrabberRGB32(dst.data(), padded.data(), 19, 2, 2, 12));
    check("packed-size buffer is insufficient for padded rows",
          !copyGrabberRGB32(dst.data(), packed.data(), 16, 2, 2, 12));
    check("truncated packed frame rejected",
          !copyGrabberRGB32(dst.data(), packed.data(), 15, 2, 2, 0));
    check("stride smaller than row rejected",
          !copyGrabberRGB32(dst.data(), packed.data(), 16, 2, 2, 4));
    check("minimum signed stride rejected without negation overflow",
          !copyGrabberRGB32(dst.data(), packed.data(), 16, 2, 2,
                           std::numeric_limits<std::int32_t>::min()));
    check("empty dimensions rejected",
          !copyGrabberRGB32(dst.data(), packed.data(), 16, 0, 2, 0)
          && !copyGrabberRGB32(dst.data(), packed.data(), 16, 2, 0, 0));
    check("null buffers rejected",
          !copyGrabberRGB32(nullptr, packed.data(), 16, 2, 2, 0)
          && !copyGrabberRGB32(dst.data(), nullptr, 16, 2, 2, 0));
    check("rejected frames leave destination untouched",
          std::all_of(dst.begin(), dst.end(), [](unsigned char b) { return b == 42; }));
}
} // namespace

int main() {
    conversion();
    return failures ? 1 : 0;
}
