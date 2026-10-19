#include "tc/3d/tcIesParse.h"
#include "../../common/tcCoreTest.h"

#include <cstdio>

namespace {

using trussc::internal::IesParseResult;
using trussc::internal::parseIes;

int failures = 0;

void check(const std::string& name, bool ok) {
    std::printf("%-64s %s\n", name.c_str(), ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

std::string header(const std::string& vertical, const std::string& horizontal) {
    return "1 1000 2 " + vertical + " " + horizontal + " 1 1 0 0 0 1 1 10\n";
}

std::string minimal() {
    return header("1", "1") + "0 0 42\n";
}

void rejected(const std::string& name, const std::string& text) {
    IesParseResult result;
    std::string error;
    check(name, !parseIes(text, result, error) && !error.empty() &&
          result.vertAngles.empty() && result.horizAngles.empty() && result.candela.empty());
}

} // namespace

TC_CORE_TEST_MAIN() {
    IesParseResult result;
    std::string error = "old error";
    check("minimal file parses", parseIes("IESNA:LM-63-2002\nTILT=NONE\n" + minimal(), result, error));
    check("minimal result and multiplier", error.empty() &&
          result.vertAngles == std::vector<float>{0} &&
          result.horizAngles == std::vector<float>{0} &&
          result.candela == std::vector<std::vector<float>>{{84}} && result.maxCandela == 84);

    const std::string grid = header("3", "2") + "0 90 180\n0 90\n1 2 3\n6 5 4\n";
    check("multiple horizontal rows parse", parseIes("TILT=NONE\n" + grid, result, error));
    check("angles, row order, scaled candela and peak preserved",
          result.vertAngles == std::vector<float>({0, 90, 180}) &&
          result.horizAngles == std::vector<float>({0, 90}) &&
          result.candela == std::vector<std::vector<float>>({{2, 4, 6}, {12, 10, 8}}) &&
          result.maxCandela == 12);

    for (const std::string count : {"0", "-5", "1e10", "2147483648", "0.5", "4"}) {
        rejected("vertical count " + count, "TILT=NONE\n" + header(count, "1") + "0 0 42\n");
        rejected("horizontal count " + count, "TILT=NONE\n" + header("1", count) + "0 0 42\n");
    }
    std::string large = "TILT=NONE\n" + header("65536", "65536");
    for (int i = 0; i < 131072; ++i) large += "0 ";
    rejected("65536 x 65536 angles without candela", large);

    // A large complete profile still works: no arbitrary count cap.
    large = "TILT=NONE\n" + header("65536", "1");
    for (int i = 0; i < 65536; ++i) large += "0 ";
    large += "0 ";
    for (int i = 0; i < 65536; ++i) large += "1 ";
    check("large complete profile parses", parseIes(large, result, error) &&
          result.vertAngles.size() == 65536 && result.candela.size() == 1 &&
          result.candela.front().size() == 65536 && result.maxCandela == 2);

    rejected("missing TILT line", minimal());
    rejected("truncated header", "TILT=NONE\n1 1000 2 1 1\n");
    rejected("truncated angles", "TILT=NONE\n" + header("3", "2") + "0 90 180 0\n");
    rejected("truncated candela", "TILT=NONE\n" + header("3", "2") +
             "0 90 180 0 90 1 2 3 6 5\n");

    check("TILT block skipped before profile header",
          parseIes("IESNA:LM-63-2002\r\n  TILT=INCLUDE\r\n1 2\n0 90\n1 0.5\n" +
                   minimal(), result, error) && result.maxCandela == 84 &&
          result.candela == std::vector<std::vector<float>>{{84}});
    check("zero TILT pairs retain existing behavior",
          parseIes("TILT=INCLUDE\n1 0\n" + minimal(), result, error) && result.maxCandela == 84);
    rejected("negative TILT count", "TILT=INCLUDE\n1 -1\n" + minimal());
    rejected("huge TILT count", "TILT=INCLUDE\n1 1e10\n" + minimal());
    rejected("TILT count exceeds remaining data", "TILT=INCLUDE\n1 3\n0 90\n");
    rejected("truncated TILT factors", "TILT=INCLUDE\n1 2\n0 90\n1\n");
    rejected("missing TILT count", "TILT=INCLUDE\n1\n");
    rejected("nonnumeric TILT count", "TILT=INCLUDE\n1 bad\n" + minimal());
    rejected("nonnumeric TILT data", "TILT=INCLUDE\n1 2\n0 bad\n1 0.5\n" + minimal());

    check("failure leaves previous parse result intact",
          !parseIes("TILT=NONE\n" + header("0", "1") + "0 0 42\n", result, error) &&
          !error.empty() && result.maxCandela == 84 &&
          result.candela == std::vector<std::vector<float>>{{84}});
    check("success clears earlier parse error",
          parseIes("TILT=NONE\n" + minimal(), result, error) && error.empty());

    std::printf("\n%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
