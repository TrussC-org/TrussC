#include "tc/video/internal/tcVideoRotation.h"
#include "../../common/tcCoreTest.h"
#include <array>
#include <cstdio>
#include <limits>

namespace {
using trussc::internal::VideoRotation;
int failures = 0;
void check(bool value, const char* message) {
    std::printf("%s: %s\n", value ? "PASS" : "FAIL", message);
    if (!value) ++failures;
}
} // namespace

TC_CORE_TEST_MAIN() {
    check(VideoRotation::normalize(-90) == 270 && VideoRotation::normalize(450) == 90,
          "normalize signed and wrapped quarter turns");
    check(VideoRotation::normalize(std::numeric_limits<double>::quiet_NaN()) == 0,
          "invalid angle has no rotation");
    check(!VideoRotation::fromMatrix(0, 0, 0, 0).active(), "singular matrix");
    const double angle = 60.0 * std::acos(-1.0) / 180.0;
    const std::array<VideoRotation, 2> affineRotations{{
        VideoRotation::fromMatrix(2, 0, 0, 2),
        VideoRotation::fromMatrix(std::cos(angle), std::sin(angle), -std::sin(angle), std::cos(angle))}};
    for (int i = 0; i < 2; ++i) {
        const auto rotation = affineRotations[i];
        check(rotation.degrees == (i == 0 ? 0 : 90) && !rotation.mirrored,
              "scale-2 and non-quarter matrices use the nearest quarter turn");
        int width = 640, height = 360;
        rotation.size(width, height);
        check(width == (i == 0 ? 640 : 360) && height == (i == 0 ? 360 : 640) &&
              width * height == 640 * 360, "decomposed output preserves encoded area");
    }
    const std::array<VideoRotation, 4> rotations{{
        VideoRotation::fromMatrix(1, 0, 0, 1), VideoRotation::fromMatrix(0, 1, -1, 0),
        VideoRotation::fromMatrix(-1, 0, 0, -1), VideoRotation::fromMatrix(0, -1, 1, 0)}};
    // Non-square frame with six distinct RGBA pixels; expected destination order
    // is written explicitly rather than calculated by the implementation.
    const int expected[4][6] = {{1,2,3,4,5,6}, {4,1,5,2,6,3},
                               {6,5,4,3,2,1}, {3,6,2,5,1,4}};
    std::array<uint8_t, 24> source{}, target{};
    for (int i = 0; i < 6; ++i) for (int c = 0; c < 4; ++c) source[i*4+c] = uint8_t(i+1);
    for (int turn = 0; turn < 4; ++turn) {
        const auto rotation = rotations[turn];
        check(rotation.degrees == turn*90 && !rotation.mirrored, "parse orthogonal matrix");
        int width = 3, height = 2;
        rotation.size(width, height);
        check(width == (turn%2 ? 2 : 3) && height == (turn%2 ? 3 : 2), "display dimensions");
        rotation.apply(source.data(), target.data(), 3, 2);
        bool matches = true;
        for (int i = 0; i < 6; ++i) for (int c = 0; c < 4; ++c)
            matches = matches && target[i*4+c] == expected[turn][i];
        check(matches, "full RGBA image orientation");
    }
    const auto mirrored = VideoRotation::fromMatrix(0, -1, -1, 0);
    check(mirrored.mirrored && mirrored.degrees == 90, "reflection is source horizontal flip then rotation");
    mirrored.apply(source.data(), target.data(), 3, 2);
    const int reflected[6] = {6,3,5,2,4,1};
    bool matches = true;
    for (int i = 0; i < 6; ++i) matches = matches && target[i*4] == reflected[i];
    check(matches, "reflected frame pixels");
    return failures ? 1 : 0;
}
