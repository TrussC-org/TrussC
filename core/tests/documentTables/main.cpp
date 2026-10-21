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
}
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
        "TILT=NONE\n1 1 2 1 1 1 1 1 1 1 1 1 1\n0 0 42 trailing\n",
        "TILT=NONE\n1 1 3e38 1 1 1 1 1 1 1 1 1 1\n0 0 3e38\n"}) {
        largestAllocation = 0;
        const bool ok = parseIes(source, ies, error);
        check("invalid IES counts or samples reject without a partial result", !ok && !error.empty() && ies.candela[0][0] == 84);
        check("IES counts never request a declaration-sized allocation", largestAllocation < 1024u * 1024u);
    }
    for (const char* tail : {"1e999", "4e"}) {
        check("malformed final IES token at EOF rejects the whole table", !parseIes(valid + tail, ies, error) && !error.empty() && ies.candela[0][0] == 84);
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
    std::istringstream cubeFailureInput(data);
    failAllocation = true;
    const bool cubeFailure = tcx::lut::detail::parseCube(cubeFailureInput, cube, error);
    failAllocation = false;
    check("LUT allocation failure returns a clean failure", !cubeFailure && !error.empty() && cube.rgb.size() == 24);
    std::istringstream fractionalSize("LUT_3D_SIZE 2.5\n" + data.substr(data.find("0 0 0")));
    check("LUT dimensions require complete integer tokens", !tcx::lut::detail::parseCube(fractionalSize, cube, error));
    size_t count = 0;
    check("LUT dimension products reject overflow before allocation", !tcx::lut::detail::cubeElements(std::numeric_limits<int>::max(), 4, count));
    check("LUT dimension products accept structurally valid large tables", tcx::lut::detail::cubeElements(257, 3, count) && count == size_t(257) * 257 * 257 * 3);
    std::printf("%d failed\n", failures);
    return failures ? 1 : 0;
}
