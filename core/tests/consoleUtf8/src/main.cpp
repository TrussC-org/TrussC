// #754: exercise the real guards and handlers on the runner's Windows console.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>

#ifdef _WIN32
namespace {
int failures = 0;

void check(const char* name, bool ok) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}

void setPages(UINT output, UINT input) {
    check("SetConsoleOutputCP", SetConsoleOutputCP(output) != 0);
    check("SetConsoleCP", SetConsoleCP(input) != 0);
}

bool pagesAre(UINT output, UINT input) {
    return GetConsoleOutputCP() == output && GetConsoleCP() == input;
}
} // namespace
#endif

TC_CORE_TEST_MAIN() {
#ifndef _WIN32
    std::puts("SKIP: console code pages require Windows");
    return 0;
#else
    using namespace tc;
    const UINT input = GetConsoleCP();
    if (input == 0) {
        std::puts("SKIP: process has no Windows console (GetConsoleCP() == 0)");
        return 0;
    }
    const UINT output = GetConsoleOutputCP();
    {
        internal::ConsoleCPCtrlGuard guard;
        setPages(CP_UTF8, CP_UTF8);
        check("windowed Ctrl+C falls through",
              internal::restoreConsoleCPOnCtrl(CTRL_C_EVENT) == FALSE);
        check("windowed Ctrl+C restores both originals", pagesAre(output, input));
        setPages(CP_UTF8, CP_UTF8);
    }
    check("windowed destructor restores both originals", pagesAre(output, input));
    setPages(CP_UTF8, CP_UTF8);
    internal::restoreConsoleCP();
    check("windowed guard disarms restoration", pagesAre(CP_UTF8, CP_UTF8));
    setPages(output, input);

    const bool wasRunning = headless::running.exchange(true);
    {
        internal::HeadlessConsoleUtf8 guard;
        check("headless guard sets both UTF-8", pagesAre(CP_UTF8, CP_UTF8));
        check("first headless Ctrl+C stops the loop",
              headless::consoleHandler(CTRL_C_EVENT) == TRUE && !headless::running);
        check("first headless Ctrl+C leaves restoration to teardown",
              pagesAre(CP_UTF8, CP_UTF8));
        check("second headless Ctrl+C falls through",
              headless::consoleHandler(CTRL_C_EVENT) == FALSE);
        check("headless Ctrl+C restores both originals", pagesAre(output, input));
        setPages(CP_UTF8, CP_UTF8);
    }
    check("headless destructor restores both originals", pagesAre(output, input));
    setPages(CP_UTF8, CP_UTF8);
    check("headless Ctrl+C still falls through after destruction",
          headless::consoleHandler(CTRL_C_EVENT) == FALSE);
    check("headless guard disarms restoration", pagesAre(CP_UTF8, CP_UTF8));
    setPages(output, input);
    headless::running = wasRunning;
    return failures ? 1 : 0;
#endif
}
