// =============================================================================
// allCoreTests — every combinable core test in one executable
//
// Each core/tests/<name>/src (with a main.cpp and no `own-binary` marker) is
// compiled into this app by local.cmake. Its TC_CORE_TEST_MAIN registers the
// test under <name> (see core/tests/common/tcCoreTest.h). The app runs ONE
// test per process:
//
//     allCoreTests --list              registered names, one per line
//     allCoreTests <name> [args...]    run <name>; its exit code is ours
//
// so process-wide state (Logger, AudioEngine, MCP server, atexit, signal
// handlers, the app runtime) is never shared between tests. build_all.py
// --core-tests-only builds this once and starts one process per test.
//
// This runner must not touch TrussC before it calls the entry: several tests
// check the state a fresh process starts with (the elapsed-clock origin, no
// Logger listener, no window, no thread, no open socket). Plain C I/O only,
// and main() returns normally so static destructors still run (onceGate
// checks its verdict there).
// =============================================================================

#include "../../common/tcCoreTest.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

struct Registered {
    const char* name;
    tcCoreTest::Entry entry;
};

// Fixed storage: registration runs during static initialization, in any
// order relative to other translation units, so it must not depend on
// another object having been constructed.
constexpr int kMaxTests = 256;
Registered g_tests[kMaxTests];
int g_count = 0;

const Registered* findTest(const char* name) {
    for (int i = 0; i < g_count; ++i) {
        if (std::strcmp(g_tests[i].name, name) == 0) return &g_tests[i];
    }
    return nullptr;
}

void sortTests() {
    // Insertion sort by name, so --list is stable whatever the link order.
    for (int i = 1; i < g_count; ++i) {
        Registered t = g_tests[i];
        int j = i - 1;
        while (j >= 0 && std::strcmp(g_tests[j].name, t.name) > 0) {
            g_tests[j + 1] = g_tests[j];
            --j;
        }
        g_tests[j + 1] = t;
    }
}

int usage(const char* exe) {
    std::fprintf(stderr,
                 "usage: %s --list\n"
                 "       %s <testName> [args...]\n",
                 exe, exe);
    return 2;
}

} // namespace

namespace tcCoreTest {

bool registerTest(const char* name, Entry entry) {
    if (findTest(name)) {
        std::fprintf(stderr, "allCoreTests: test \"%s\" registered twice\n", name);
        std::abort();
    }
    if (g_count >= kMaxTests) {
        std::fprintf(stderr, "allCoreTests: more than %d tests, raise kMaxTests\n", kMaxTests);
        std::abort();
    }
    g_tests[g_count++] = {name, entry};
    return true;
}

} // namespace tcCoreTest

int main(int argc, char** argv) {
    const char* exe = argc > 0 ? argv[0] : "allCoreTests";
    if (argc < 2) return usage(exe);

    sortTests();
    if (std::strcmp(argv[1], "--list") == 0) {
        for (int i = 0; i < g_count; ++i) std::printf("%s\n", g_tests[i].name);
        return 0;
    }

    const Registered* test = findTest(argv[1]);
    if (!test) {
        std::fprintf(stderr, "allCoreTests: unknown test \"%s\" (see --list)\n", argv[1]);
        return usage(exe);
    }

    // Drop the test name: the test sees argv[0] and its own arguments, as
    // when it is built alone. argv[argc] stays the terminating null.
    for (int i = 1; i < argc; ++i) argv[i] = argv[i + 1];
    return test->entry(argc - 1, argv);
}
