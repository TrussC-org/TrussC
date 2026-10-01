#pragma once

// =============================================================================
// tcCoreTest.h — entry point of a core test (core/tests/<name>/src/main.cpp)
//
// A core test writes its entry as
//
//     TC_CORE_TEST_MAIN() { ... return g_fail ? 1 : 0; }
//     TC_CORE_TEST_MAIN(int argc, char** argv) { ... }
//
// instead of main(), after closing the anonymous namespace that holds the
// rest of the file.
//
// - Built alone (trusscli project of the test itself), the macro is plain
//   main() with the same parameters.
// - Built into core/tests/allCoreTests, its local.cmake defines
//   TC_CORE_TEST_NAME="<dir name>" for this source. The macro then defines a
//   file-local entry function and registers it under that name at static
//   initialization. `allCoreTests <name> [args...]` runs it in its own process
//   with argv[0] kept and the test name removed, so the test sees the same
//   argc/argv as when it is built alone.
//
// The registration touches no TrussC state: the registry lives in the runner
// (allCoreTests/src/main.cpp) and only stores a name and a function pointer.
// =============================================================================

namespace tcCoreTest {

using Entry = int (*)(int argc, char** argv);

// Defined in core/tests/allCoreTests/src/main.cpp (only used, and only
// linked, when a test is built into allCoreTests).
bool registerTest(const char* name, Entry entry);

// Calls an entry written as main() or as main(int, char**).
inline int invoke(int (*entry)(), int, char**) { return entry(); }
inline int invoke(int (*entry)(int, char**), int argc, char** argv) { return entry(argc, argv); }

} // namespace tcCoreTest

#ifdef TC_CORE_TEST_NAME

#define TC_CORE_TEST_MAIN(...)                                                   \
    static int tcCoreTestEntry(__VA_ARGS__);                                     \
    [[maybe_unused]] static const bool tcCoreTestRegistered =                    \
        ::tcCoreTest::registerTest(TC_CORE_TEST_NAME, [](int argc, char** argv) { \
            return ::tcCoreTest::invoke(tcCoreTestEntry, argc, argv);            \
        });                                                                      \
    static int tcCoreTestEntry(__VA_ARGS__)

#else

#define TC_CORE_TEST_MAIN(...) int main(__VA_ARGS__)

#endif
