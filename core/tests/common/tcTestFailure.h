#pragma once

#include <cstdio>
#include <string> // Load MSVC's default _ITERATOR_DEBUG_LEVEL before testing it.

// Debug test processes must exit on CRT/OS faults even when run unattended.
// No change to Release reporting or exit codes, or to non-MSVC builds.
#if defined(_MSC_VER) && defined(_DEBUG)
#include <cstdlib>
#include <crtdbg.h>

// UINT WINAPI SetErrorMode(UINT), from errhandlingapi.h. Use the documented
// underlying types/calling convention so windows.h can appear before or after
// this header, without importing it (and its macros) into every test TU.
extern "C" __declspec(dllimport) unsigned int __stdcall SetErrorMode(unsigned int);

namespace tcTest {
inline void configureFailureReports() {
    _set_error_mode(_OUT_TO_STDERR); // assert() also uses this in GUI executables.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    // SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX. Keep the default fault
    // and invalid-parameter termination handlers; only suppress OS fault UI.
    SetErrorMode(0x0001u | 0x0002u);
    const int kinds[] = {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT};
    for (int kind : kinds) {
        _CrtSetReportMode(kind, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(kind, _CRTDBG_FILE_STDERR);
    }
}
} // namespace tcTest

namespace {
// Internal, immutable initialization token; no header-inline mutable state.
[[maybe_unused]] const bool tcTestFailureReportsConfigured = [] {
    tcTest::configureFailureReports();
    return true;
}();
}
#endif

namespace tcTest {
inline bool skipPersistentAllocationFailure(const char* test) {
#if defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL != 0
    std::printf("SKIP: %s: persistent allocation failure is not supported with MSVC "
                "iterator debugging (_ITERATOR_DEBUG_LEVEL=%d): std::string/container "
                "construction and moves allocate a container proxy\n", test, _ITERATOR_DEBUG_LEVEL);
    return true;
#else
    (void)test;
    return false;
#endif
}

// An exhaustive one-shot sweep also targets noexcept STL proxy construction
// and moves. Keep targeted one-shot payload failures running instead.
inline bool skipProxyAllocationSweep(const char* test) {
#if defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL != 0
    std::printf("SKIP: %s: exhaustive allocation failure targets noexcept "
                "std::string/container proxy construction and moves with MSVC "
                "iterator debugging (_ITERATOR_DEBUG_LEVEL=%d)\n", test, _ITERATOR_DEBUG_LEVEL);
    return true;
#else
    (void)test;
    return false;
#endif
}
} // namespace tcTest
