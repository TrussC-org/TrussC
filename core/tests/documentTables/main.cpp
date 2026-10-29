#include "../common/tcTestFailure.h"
// Pure parser contracts; no framework setup or GPU is needed.
#include "tc/3d/tcIesParse.h"
#include "tcLutParse.h"
#include <cstdio>
#include <cstdlib>
#include <new>

namespace {
size_t largestAllocation = 0;
bool failAllocation = false;
int failures = 0;
void check(const char* label, bool ok) {
    std::printf("%-72s %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

void testCubeRanges() {
    // Resolve-style full-range export, R varies fastest. The LUT maps RGB to BRG.
    const std::string fixture =
        "# Resolve full-range fixture\n\n"
        "TITLE \"Resolve full range\"\n"
        "LUT_3D_SIZE 2\n"
        "LUT_3D_INPUT_RANGE 0.0 1.0 # full range\n"
        "DOMAIN_MIN 0 0 0 # default domain\n"
        "DOMAIN_MAX 1 1 1\n"
        "0 0 0\n0 1 0\n0 0 1\n0 1 1\n"
        "1 0 0\n1 1 0\n1 0 1\n1 1 1\n";
    tcx::lut::detail::CubeData cube;
    std::string error;
    for (bool crlf : {false, true}) {
        std::string source;
        for (char ch : fixture) {
            if (crlf && ch == '\n') source += '\r';
            source += ch;
        }
        std::istringstream input(source);
        const bool loaded = tcx::lut::detail::parseCube(input, cube, error);
        check(crlf ? "Resolve CRLF fixture loads" : "Resolve LF fixture loads",
              loaded && error.empty() && cube.side == 2 && cube.rgb.size() == 24 &&
              cube.title == "Resolve full range");
        if (!loaded || cube.rgb.size() != 24) continue;
        // CPU trilinear reference sample at RGB=(0.25, 0.5, 0.75).
        double sample[3] = {};
        for (int b = 0; b < 2; ++b) {
            for (int g = 0; g < 2; ++g) {
                for (int r = 0; r < 2; ++r) {
                    const double weight = (r ? 0.25 : 0.75) * 0.5 * (b ? 0.75 : 0.25);
                    const size_t offset = size_t((b * 2 + g) * 2 + r) * 3;
                    for (int channel = 0; channel < 3; ++channel)
                        sample[channel] += weight * cube.rgb[offset + size_t(channel)];
                }
            }
        }
        check("Resolve fixture samples the expected BRG output",
              sample[0] == 0.75 && sample[1] == 0.25 && sample[2] == 0.5);
    }

    const std::string table = "LUT_3D_SIZE 2\n" + fixture.substr(fixture.find("0 0 0\n0 1 0"));
    for (const char* values : {"0.0 1.0", "0 1 # full range", "0 1# full range"}) {
        std::istringstream input(std::string("LUT_1D_INPUT_RANGE ") + values + "\n" + table);
        tcx::lut::detail::CubeData inputRangeCube;
        check("3D-only fixture with default LUT_1D_INPUT_RANGE loads",
              tcx::lut::detail::parseCube(input, inputRangeCube, error) && error.empty() &&
              inputRangeCube.side == 2 && inputRangeCube.rgb == cube.rgb);
    }
    const auto rejects = [&](const std::string& line, const std::string& expectedError) {
        std::istringstream input(line + "\n" + table);
        const auto previous = cube.rgb;
        const auto previousTitle = cube.title;
        check(line.c_str(), !tcx::lut::detail::parseCube(input, cube, error) &&
              error == expectedError && cube.side == 2 && cube.rgb == previous && cube.title == previousTitle);
    };
    for (const char* keyword : {"LUT_1D_INPUT_RANGE", "LUT_3D_INPUT_RANGE"}) {
        for (const char* values : {"-1 3", "-0.1 1", "0 1.1", "1 0", "0 0", "0 1.00000001"}) {
            rejects(std::string(keyword) + " " + values,
                    std::string("unsupported LUT range (") + keyword + "): only 0..1 is supported");
        }
    }
    for (const char* values : {"-0.1 0 0", "0 0.1 0", "0 0 -0.1"}) {
        rejects(std::string("DOMAIN_MIN ") + values,
                "unsupported LUT range (DOMAIN_MIN): only 0..1 is supported");
    }
    for (const char* values : {"0.9 1 1", "1 1.1 1", "1 1 1.00000001"}) {
        rejects(std::string("DOMAIN_MAX ") + values,
                "unsupported LUT range (DOMAIN_MAX): only 0..1 is supported");
    }
    for (const char* keyword : {"LUT_IN_VIDEO_RANGE", "LUT_OUT_VIDEO_RANGE"}) {
        const std::string unsupported = std::string("unsupported LUT range (") + keyword + "): only full range is supported";
        rejects(keyword, unsupported);
        rejects(std::string(keyword) + " # legal range", unsupported);
        // Resolve's flags have no arguments; do not invent a full-range flag value.
        for (const char* values : {"0", "1", "0 1", "nan", "garbage"})
            rejects(std::string(keyword) + " " + values, std::string("invalid LUT range (") + keyword + ")");
    }
    for (const char* keyword : {"LUT_1D_INPUT_RANGE", "LUT_3D_INPUT_RANGE", "DOMAIN_MIN", "DOMAIN_MAX"}) {
        const bool inputRange = std::string(keyword) == "LUT_1D_INPUT_RANGE" || std::string(keyword) == "LUT_3D_INPUT_RANGE";
        const std::string invalid = std::string("invalid LUT range (") + keyword + ")";
        for (const char* values : {"", "0", "nan 1 1", "0 inf 1", "0 1 1e999", "0 1 junk", "0 1 1 extra", "0+1", "0 0+0"}) {
            rejects(std::string(keyword) + " " + values, invalid);
        }
        if (!inputRange) {
            rejects(std::string(keyword) + " 0 1", invalid);
            rejects(std::string(keyword) + " 0 1 # missing component", invalid);
        }
        rejects(std::string(keyword) + (inputRange ? " 0 1e" : " 0 0 1e"), invalid);
    }
    rejects("LUT_1D_SIZE 2", "1D LUT not supported");
    rejects("LUT_1D_INPUT_RANGE 0 1\nLUT_1D_SIZE 2", "1D LUT not supported");
    rejects("LUT_3D_SIZE", "invalid LUT dimensions");
    rejects("UNKNOWN_KEYWORD 0 1", "invalid LUT row");
    for (const auto& source : {table + "0 0 0\n", std::string("LUT_3D_SIZE 2\n0 0 0 extra\n"),
                              std::string("LUT_3D_SIZE 2\n0 0 inf\n")}) {
        std::istringstream input(source);
        check("extra or malformed LUT rows remain rejected", !tcx::lut::detail::parseCube(input, cube, error) && error == "invalid LUT row");
    }
}
}
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
void* operator new(size_t bytes) {
    if (failAllocation) throw std::bad_alloc();
    largestAllocation = std::max(largestAllocation, bytes);
    if (void* pointer = std::malloc(bytes ? bytes : 1)) return pointer;
    throw std::bad_alloc();
}
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
void operator delete(void* pointer) noexcept { std::free(pointer); }
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
void operator delete(void* pointer, size_t) noexcept { std::free(pointer); }

int main() {
    using trussc::internal::IesParseResult;
    using trussc::internal::parseIes;
    IesParseResult ies;
    std::string error;
    const std::string valid = "TILT=NONE\n1 1 2 1 1 1 1 1 1 1 1 1 1\n0 0 42\n";
    check("valid IES table parses completely", parseIes(valid, ies, error) && ies.candela[0][0] == 84);
    for (const auto& source : {
        "TILT=NONE\n1 1 2 1.5 1 1 1 1 1 1 1 1 1\n0 0 42\n",
        "TILT=NONE\n1 1 2 2147483647 2147483647 1 1 1 1 1 1 1 1\n0 0 42\n",
        "TILT=NONE\n1 1 2 1 1 1 1 1 1 1 1 1 1\n0 0\n",
        "TILT=NONE\n1 1 3e38 1 1 1 1 1 1 1 1 1 1\n0 0 3e38\n"}) {
        largestAllocation = 0;
        const bool ok = parseIes(source, ies, error);
        check("invalid IES counts or samples reject without a partial result", !ok && !error.empty() && ies.candela[0][0] == 84);
        check("IES counts never request a declaration-sized allocation", largestAllocation < 1024u * 1024u);
    }
    for (const char* tail : {"1e999", "4e"}) {
        const std::string incomplete = "TILT=NONE\n1 1 2 1 1 1 1 1 1 1 1 1 1\n0 0 ";
        check("malformed required IES token at EOF rejects the whole table", !parseIes(incomplete + tail, ies, error) && !error.empty() && ies.candela[0][0] == 84);
    }
    check("IES final value without trailing whitespace succeeds", parseIes(valid.substr(0, valid.size() - 1), ies, error));
    failAllocation = true;
    // A short literal avoids allocation at the caller before parser entry.
    const bool iesFailure = parseIes(valid, ies, error);
    failAllocation = false;
    check("IES allocation failure returns a clean failure", !iesFailure && !error.empty() && ies.candela[0][0] == 84);

    tcx::lut::detail::CubeData cube;
    std::string data = "TITLE \"fixture\"\nLUT_3D_SIZE 2\n";
    for (int i = 0; i < 8; ++i) data += "0 0 0\n";
    std::istringstream cubeInput(data);
    check("valid LUT table parses completely", tcx::lut::detail::parseCube(cubeInput, cube, error) && cube.side == 2 && cube.rgb.size() == 24 && cube.title == "fixture");
    for (const char* source : {"LUT_3D_SIZE 256\n0 0 0\n", "LUT_3D_SIZE 2147483647\n0 0 0\n", "LUT_3D_SIZE 2\n0 0\n", "LUT_3D_SIZE 2\n0 0 0\nLUT_3D_SIZE 2\n", "LUT_3D_SIZE 2\n0 0 nan\n", "LUT_3D_SIZE 2\n0 0 0 extra\n"}) {
        std::istringstream input(source);
        largestAllocation = 0;
        const bool ok = tcx::lut::detail::parseCube(input, cube, error);
        check("incomplete or invalid LUT import preserves previous result", !ok && !error.empty() && cube.side == 2 && cube.rgb.size() == 24);
        check("LUT declarations never request a declaration-sized allocation", largestAllocation < 1024u * 1024u);
    }
    if (!tcTest::skipPersistentAllocationFailure("LUT parser scratch container construction")) {
        std::istringstream cubeFailureInput(data);
        failAllocation = true;
        const bool cubeFailure = tcx::lut::detail::parseCube(cubeFailureInput, cube, error);
        failAllocation = false;
        check("LUT allocation failure returns a clean failure", !cubeFailure && !error.empty() && cube.rgb.size() == 24);
    }
    std::istringstream fractionalSize("LUT_3D_SIZE 2.5\n" + data.substr(data.find("0 0 0")));
    check("LUT dimensions require complete integer tokens", !tcx::lut::detail::parseCube(fractionalSize, cube, error));
    size_t count = 0;
    check("LUT dimension products reject overflow before allocation", !tcx::lut::detail::cubeElements(std::numeric_limits<int>::max(), 4, count));
    check("LUT dimension products accept structurally valid large tables", tcx::lut::detail::cubeElements(257, 3, count) && count == size_t(257) * 257 * 257 * 3);
    testCubeRanges();
    std::printf("%d failed\n", failures);
    return failures ? 1 : 0;
}
