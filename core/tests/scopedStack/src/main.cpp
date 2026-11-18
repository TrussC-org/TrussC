// =============================================================================
// core/tests/scopedStack — regression test for #492: scopedMatrix() /
// scopedStyle() push when called and pop when the returned guard goes out of
// scope, also on an early return.
//
// GPU-free console fixture with an addressed Window; exit code = pass/fail.
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
#include "../../common/tcWindowTest.h"
#include "../../common/tcCoreTest.h"

#include <cstdio>
#include <stdexcept>
#include <type_traits>
#include <cstring>
#include "../../common/tcDisplayTest.h"

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

template<class T> constexpr bool scopeTraits =
    !std::is_copy_constructible_v<T> && !std::is_copy_assignable_v<T> &&
    !std::is_move_constructible_v<T> && !std::is_move_assignable_v<T>;
static_assert(scopeTraits<FboScope>);
static_assert(scopeTraits<ShaderScope>);
static_assert(scopeTraits<ShapeScope>);
static_assert(scopeTraits<StrokeScope>);

void points() {
    vertex(8, 8);
    vertex(24, 8);
    vertex(24, 24);
}
void earlyFbo(Fbo& fbo) {
    auto f = fbo.scoped(0, 0, 1);
    setColor(1, 0, 0);
    drawRect(8, 8, 16, 16);
    return;
}
void earlyShader(Shader& shader) {
    auto s = shader.scoped();
    return;
}
void earlyShape() {
    auto s = scopedShape(true);
    points();
    return;
}
void earlyStroke() {
    auto s = scopedStroke(true);
    points();
    return;
}
bool gpuCompleted = false;
class PairApp : public App {
public:
    void draw() override {
        Fbo fbo;
        fbo.allocate(32, 32);
        earlyFbo(fbo);
        check("Fbo early return ends activation", !fbo.isActive());
        Pixels pixels;
        pixels.allocate(32, 32, 4);
        check("Fbo scoped clear/draw readback", fbo.readPixels(pixels.getData()));
        auto clearColor = pixels.getColor(2, 2);
        auto drawnColor = pixels.getColor(16, 16);
        check("Fbo scoped RGB clears with default alpha", clearColor.b > 0.99f &&
              clearColor.r < 0.01f && clearColor.a > 0.99f);
        check("Fbo scoped draws into attachment", drawnColor.r > 0.99f && drawnColor.b < 0.01f);
        {
            auto f = fbo.scoped();
        }
        check("Fbo no-clear scope readback", fbo.readPixels(pixels.getData()));
        check("Fbo scoped() preserves content", pixels.getColor(16, 16).r > 0.99f);

        Shader shader, other;
        check("load shader A", shader.load(tc_fbomip_downsample_shader_desc));
        check("load shader B", other.load(tc_fbomip_downsample_shader_desc));
        auto& window = internal::currentWindowContext();
        const auto depth = window.shaderStack.size();
        earlyShader(shader);
        check("Shader early return restores stack", window.shaderStack.size() == depth);

        int warnings = 0;
        std::string warning;
        auto listener = getLogger().onLog.listen([&](LogEventArgs& e) {
            if (e.level == LogLevel::Warning) { ++warnings; warning = e.message; }
        });
        {
            auto f = fbo.scoped();
            fbo.end();
            check("manual Fbo end clears activation", !fbo.isActive());
        }
        check("Fbo guard after manual end stays inactive", !fbo.isActive());
        {
            auto s = shader.scoped();
            shader.end();
        }
        check("Shader guard after manual end keeps stack", window.shaderStack.size() == depth);

        // Compare the emitted geometry to the unchanged explicit close=true pair.
        noFill();
        setStrokeWeight(2);
        beginShape();
        points();
        int start = sgl_num_vertices();
        endShape(true);
        const int closedShapeCount = sgl_num_vertices() - start;
        start = sgl_num_vertices();
        earlyShape();
        check("Shape early return ends and forwards close=true", !window.shapeStarted &&
              closedShapeCount > 0 && sgl_num_vertices() - start == closedShapeCount);
        beginStroke();
        points();
        start = sgl_num_vertices();
        endStroke(true);
        const int closedStrokeCount = sgl_num_vertices() - start;
        start = sgl_num_vertices();
        earlyStroke();
        check("Stroke early return ends and forwards close=true", !window.strokeStarted &&
              closedStrokeCount > 0 && sgl_num_vertices() - start == closedStrokeCount);
        int afterManual = 0;
        {
            auto s = scopedShape();
            points();
            start = sgl_num_vertices();
            endShape();
            afterManual = sgl_num_vertices();
            check("manual Shape end emits geometry", afterManual > start);
        }
        check("Shape guard after manual end emits nothing", !window.shapeStarted &&
              sgl_num_vertices() == afterManual);
        {
            auto s = scopedStroke();
            points();
            start = sgl_num_vertices();
            endStroke();
            afterManual = sgl_num_vertices();
            check("manual Stroke end emits geometry", afterManual > start);
        }
        check("Stroke guard after manual end emits nothing", !window.strokeStarted &&
              sgl_num_vertices() == afterManual);
        check("all four manual ends produce no warnings", warnings == 0);
        {
            auto s = shader.scoped();
            shader.end();
            other.begin();
        }
        check("EXPECTED: A guard warns once and leaves B open", warnings == 1 &&
              warning.find("end() called on wrong shader") != std::string::npos &&
              window.shaderStack.size() == depth + 1 && window.shaderStack.back() == &other);
        other.end();
        check("B can end normally after EXPECTED warning", window.shaderStack.size() == depth);
        fill();
        gpuCompleted = true;
        exitApp();
    }
};

} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--gpu") == 0) {
        WindowSettings settings;
        settings.width = 64;
        settings.height = 64;
        return tcCoreTest::runDisplayTest<PairApp>(settings, g_fail, [] {
            check("scoped pair GPU checks completed", gpuCompleted);
        });
    }

    tcCoreTest::WindowFixture fixture;
#ifndef NDEBUG
    std::printf("SKIP: needs a release build (sokol_gl asserts it is set up on pushMatrix())\n");
    return 0;
#else
    {
        Fbo unallocated;
        Shader unloaded;
        auto f = unallocated.scoped();
        auto shader = unloaded.scoped();
        check("unallocated Fbo follows begin/end", !unallocated.isActive());
        check("unloaded Shader follows begin/end", internal::currentWindowContext().shaderStack.empty());
    }
    testBasic();
    testEarlyReturn();
    testNesting();
    testInsideManualPush();
    testException();

    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
#endif
}
