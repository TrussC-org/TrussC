// =============================================================================
// core/tests/scopedStack — regression test for #492: scopedMatrix() /
// scopedStyle() push when called and pop when the returned guard goes out of
// scope, also on an early return.
//
// Headless, console, exit code = pass/fail (build_all.py runs it in CI).
// Plain main(): drives the default RenderContext's matrix and style stacks
// directly and reads their depth.
//
// Guards:
//   1. Depth before / inside / after a scope, for both guards.
//   2. An early return (and a return from a loop) pops.
//   3. Nested guards pop in reverse order, back to the outer depth.
//   4. The matrix and the color set inside the scope are undone after it.
//   5. A guard opened inside a pushMatrix()/popMatrix() pair pops only its own
//      entry (depth back to 1, not 0).
//   6. An exception leaving the scope pops (stack unwinding).
//
// pushMatrix() also calls sokol_gl, which headless mode doesn't set up:
// sokol_gl ignores the call with no context, but asserts on it in a debug
// build, so this test runs its checks only in a release build (as CI builds
// core/tests).
// =============================================================================

#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include <cstdio>
#include <stdexcept>

using namespace std;
using namespace tc;

namespace {

static int g_fail = 0;
static void checkn(const char* name, bool ok, long long got) {
    std::printf("%-64s %s  (%lld)\n", name, ok ? "PASS" : "FAIL", got);
    std::fflush(stdout);
    if (!ok) ++g_fail;
}
static void check(const char* name, bool ok) {
    std::printf("%-64s %s\n", name, ok ? "PASS" : "FAIL");
    std::fflush(stdout);
    if (!ok) ++g_fail;
}

static size_t matrixDepth() { return internal::getDefaultContext().getMatrixStackDepth(); }
static size_t styleDepth() { return internal::getDefaultContext().getStyleStackDepth(); }
static bool isIdentity(const Mat4& m) {
    const Mat4 id;
    for (int i = 0; i < 16; ++i) if (m.m[i] != id.m[i]) return false;
    return true;
}

// ---------------------------------------------------------------------------
// 1. Before / inside / after
// ---------------------------------------------------------------------------
static void testBasic() {
    checkn("basic: matrix depth before is 0", matrixDepth() == 0, (long long)matrixDepth());
    checkn("basic: style depth before is 0", styleDepth() == 0, (long long)styleDepth());
    {
        auto m = scopedMatrix();
        checkn("basic: scopedMatrix() pushed (matrix depth 1)", matrixDepth() == 1, (long long)matrixDepth());
        checkn("basic: scopedMatrix() leaves the style stack alone", styleDepth() == 0, (long long)styleDepth());
        translate(100, 50);
        check("basic: translate inside the scope applies", !isIdentity(getMatrix()));
    }
    checkn("basic: matrix depth after the scope is 0", matrixDepth() == 0, (long long)matrixDepth());
    check("basic: matrix after the scope is the one before (identity)", isIdentity(getMatrix()));

    setColor(0.25f, 0.5f, 0.75f);
    {
        auto s = scopedStyle();
        checkn("basic: scopedStyle() pushed (style depth 1)", styleDepth() == 1, (long long)styleDepth());
        checkn("basic: scopedStyle() leaves the matrix stack alone", matrixDepth() == 0, (long long)matrixDepth());
        setColor(1.0f, 0.0f, 0.0f);
    }
    checkn("basic: style depth after the scope is 0", styleDepth() == 0, (long long)styleDepth());
    Color c = getColor();
    check("basic: color after the scope is the one before",
          c.r == 0.25f && c.g == 0.5f && c.b == 0.75f);
}

// ---------------------------------------------------------------------------
// 2. Early return / several exits
// ---------------------------------------------------------------------------
static size_t g_insideMatrix = 0, g_insideStyle = 0;
static int earlyReturn(bool leaveEarly) {
    auto m = scopedMatrix();
    auto s = scopedStyle();
    translate(10, 0);
    g_insideMatrix = matrixDepth();
    g_insideStyle = styleDepth();
    if (leaveEarly) return 1;
    translate(0, 10);
    return 2;
}

static int returnFromLoop() {
    for (int i = 0; i < 4; ++i) {
        auto m = scopedMatrix();
        translate((float)i, 0);
        if (i == 2) return i;
    }
    return -1;
}

static void testEarlyReturn() {
    int r = earlyReturn(true);
    checkn("early return: took the early exit", r == 1, r);
    checkn("early return: matrix depth inside was 1", g_insideMatrix == 1, (long long)g_insideMatrix);
    checkn("early return: style depth inside was 1", g_insideStyle == 1, (long long)g_insideStyle);
    checkn("early return: matrix depth after is 0", matrixDepth() == 0, (long long)matrixDepth());
    checkn("early return: style depth after is 0", styleDepth() == 0, (long long)styleDepth());
    check("early return: matrix after is identity", isIdentity(getMatrix()));

    r = earlyReturn(false);
    checkn("normal return: took the normal exit", r == 2, r);
    checkn("normal return: matrix depth after is 0", matrixDepth() == 0, (long long)matrixDepth());
    checkn("normal return: style depth after is 0", styleDepth() == 0, (long long)styleDepth());

    r = returnFromLoop();
    checkn("return from a loop: returned at i == 2", r == 2, r);
    checkn("return from a loop: matrix depth after is 0", matrixDepth() == 0, (long long)matrixDepth());
    check("return from a loop: matrix after is identity", isIdentity(getMatrix()));
}

// ---------------------------------------------------------------------------
// 3. Nesting
// ---------------------------------------------------------------------------
static void testNesting() {
    {
        auto outer = scopedMatrix();
        translate(5, 0);
        const Mat4 afterOuter = getMatrix();
        {
            auto inner = scopedMatrix();
            checkn("nesting: matrix depth inside the inner scope is 2", matrixDepth() == 2, (long long)matrixDepth());
            translate(0, 5);
        }
        checkn("nesting: matrix depth after the inner scope is 1", matrixDepth() == 1, (long long)matrixDepth());
        const Mat4 now = getMatrix();
        bool same = true;
        for (int i = 0; i < 16; ++i) if (now.m[i] != afterOuter.m[i]) same = false;
        check("nesting: matrix after the inner scope is the outer one", same);
    }
    checkn("nesting: matrix depth after both scopes is 0", matrixDepth() == 0, (long long)matrixDepth());
    check("nesting: matrix after both scopes is identity", isIdentity(getMatrix()));
}

// ---------------------------------------------------------------------------
// 5. Inside a manual push/pop pair
// ---------------------------------------------------------------------------
static void testInsideManualPush() {
    pushMatrix();
    pushStyle();
    {
        auto m = scopedMatrix();
        auto s = scopedStyle();
        checkn("inside manual push: matrix depth is 2", matrixDepth() == 2, (long long)matrixDepth());
        checkn("inside manual push: style depth is 2", styleDepth() == 2, (long long)styleDepth());
    }
    checkn("inside manual push: matrix depth after the scope is 1", matrixDepth() == 1, (long long)matrixDepth());
    checkn("inside manual push: style depth after the scope is 1", styleDepth() == 1, (long long)styleDepth());
    popStyle();
    popMatrix();
    checkn("inside manual push: matrix depth after popMatrix() is 0", matrixDepth() == 0, (long long)matrixDepth());
    checkn("inside manual push: style depth after popStyle() is 0", styleDepth() == 0, (long long)styleDepth());
}

// ---------------------------------------------------------------------------
// 6. Exception
// ---------------------------------------------------------------------------
static void testException() {
    bool caught = false;
    try {
        auto m = scopedMatrix();
        auto s = scopedStyle();
        translate(1, 2);
        throw runtime_error("test");
    } catch (const runtime_error&) {
        caught = true;
    }
    check("exception: was thrown and caught", caught);
    checkn("exception: matrix depth after is 0", matrixDepth() == 0, (long long)matrixDepth());
    checkn("exception: style depth after is 0", styleDepth() == 0, (long long)styleDepth());
    check("exception: matrix after is identity", isIdentity(getMatrix()));
}

} // namespace

TC_CORE_TEST_MAIN() {
#ifndef NDEBUG
    std::printf("SKIP: needs a release build (sokol_gl asserts it is set up on pushMatrix())\n");
    return 0;
#else
    testBasic();
    testEarlyReturn();
    testNesting();
    testInsideManualPush();
    testException();

    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
#endif
}
