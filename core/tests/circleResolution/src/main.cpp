#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include <cstdio>

namespace {
int failures = 0;

void check(const char* label, bool passed) {
    std::printf("%s: %s\n", label, passed ? "PASS" : "FAIL");
    if (!passed) ++failures;
}

// Compatibility coverage must also compile with TC_DEPRECATED_ERRORS=1.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4996)
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
void checkAliases() {
    tc::setCircleResolution(7);
    check("global legacy set/get round trip", tc::getCircleResolution() == 7);
    check("global curve resolution matches", tc::getCurveResolution() == 7);
    tc::setCurveResolution(11);
    check("global legacy getter follows curve setter", tc::getCircleResolution() == 11);

    tc::RenderContext context;
    context.setCircleResolution(7);
    check("context legacy set/get round trip", context.getCircleResolution() == 7);
    check("context curve resolution matches", context.getCurveResolution() == 7);
    context.setCurveResolution(11);
    check("context legacy getter follows curve setter", context.getCircleResolution() == 11);
}
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
} // namespace

TC_CORE_TEST_MAIN() {
    checkAliases();
    return failures ? 1 : 0;
}
