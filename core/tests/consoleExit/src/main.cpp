// #684: the core test runner checks this process's exit code, including
// static destruction. No window or timing assertion is needed.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdlib>

namespace {
void exitWithConsoleRunning() {
    tc::console::start();
    std::exit(0);
}
} // namespace

TC_CORE_TEST_MAIN() {
    exitWithConsoleRunning();
    return 1; // The test must exit through std::exit().
}
