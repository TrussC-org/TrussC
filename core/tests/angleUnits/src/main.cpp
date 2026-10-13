// #236: radians are canonical; explicit Deg companions preserve degree input.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include <cstdio>
#include <cstring>
#include <limits>

using namespace tc;

namespace {
int failures = 0;
int fovWarnings = 0;
bool gpuFinished = false;

void check(const char* name, bool ok) {
    std::printf("%s: %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
bool approxEq(float a, float b) { return std::abs(a - b) < 0.0001f; }
bool sameMatrix(const Mat4& a, const Mat4& b) {
    for (int i = 0; i < 16; ++i) if (!approxEq(a.m[i], b.m[i])) return false;
    return true;
}
bool samePath(const Path& a, const Path& b) {
    const auto& av = a.getVertices();
    const auto& bv = b.getVertices();
    if (av.empty() || av.size() != bv.size()) return false;
    for (size_t i = 0; i < av.size(); ++i) {
        if (!approxEq(av[i].x, bv[i].x) || !approxEq(av[i].y, bv[i].y) ||
            !approxEq(av[i].z, bv[i].z)) return false;
    }
    return true;
}

void testArcs() {
    for (bool clockwise : {true, false}) {
        Path radians, degrees;
        radians.arc(Vec3{3, 4, 5}, 20, 10, 0.0f, 3 * TAU / 4, clockwise, 32);
        degrees.arcDeg(Vec3{3, 4, 5}, 20, 10, 0.0f, 270.0f, clockwise, 32);
        check("Vec3 elliptical radian/Deg vertices match in both directions", samePath(radians, degrees));
        const auto& end = radians.getVertices().back();
        check("elliptical endpoint has the expected angle and height",
              approxEq(end.x, 3) && approxEq(end.y, clockwise ? -6 : 14) && approxEq(end.z, 5));
    }
    Path radXY, degXY, radVec, degVec;
    radXY.arc(3.0f, 4.0f, 20.0f, 10.0f, TAU / 8, TAU / 4, 32);
    degXY.arcDeg(3, 4, 20, 10, 45, 90, 32);
    radVec.arc(Vec2{3, 4}, 20.0f, 10.0f, TAU / 8, TAU / 4, 32);
    degVec.arcDeg(Vec2{3, 4}, 20, 10, 45, 90, 32);
    check("XY elliptical radians/Deg match", samePath(radXY, degXY));
    check("Vec2 elliptical radians/Deg match", samePath(radVec, degVec));
    check("elliptical overloads agree", samePath(radXY, radVec));
}

void testDefaults() {
    check("initial default is TAU/8 radians", approxEq(getDefaultScreenFov(), TAU / 8));
    setDefaultScreenFov(TAU / 6);
    check("radian default reads as 60 degrees", approxEq(getDefaultScreenFovDeg(), 60));
    setDefaultScreenFovDeg(30);
    check("degree default reads as TAU/12 radians", approxEq(getDefaultScreenFov(), TAU / 12));
    setDefaultScreenFov(TAU / 8);
}

void testInvalid() {
    auto& ctx = internal::currentWindowContext();
    const Mat4 projection = ctx.currentProjectionMatrix;
    const Mat4 view = ctx.currentViewMatrix;
    const float fov = ctx.currentScreenFov;
    const float defaultFov = getDefaultScreenFov();
    const float width = ctx.currentViewW, height = ctx.currentViewH;
    const auto unchanged = [&] {
        return sameMatrix(projection, ctx.currentProjectionMatrix) &&
               sameMatrix(view, ctx.currentViewMatrix) && fov == ctx.currentScreenFov &&
               defaultFov == getDefaultScreenFov() && width == ctx.currentViewW && height == ctx.currentViewH;
    };
    setupScreenPerspective(60);
    setupScreenPerspective(60);
    check("60 radians warns once and preserves projection", fovWarnings == 1 && unchanged());
    for (float bad : {60.0f, 0.0f, -1.0f, HALF_TAU,
                      std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        setupScreenFov(bad);
        setupScreenPerspective(bad);
        setupScreenFovWithSize(bad, 123, 456);
        setDefaultScreenFov(bad);
        check("all radian public entry points ignore invalid FOV", unchanged());
    }
    for (float bad : {0.0f, -1.0f, 180.0f, 360.0f,
                      std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        setupScreenFovDeg(bad);
        setupScreenPerspectiveDeg(bad);
        setupScreenFovWithSizeDeg(bad, 123, 456);
        setDefaultScreenFovDeg(bad);
        check("all degree public entry points ignore invalid FOV", unchanged());
    }
    check("invalid screen FOV warnings remain once-only", fovWarnings == 1);
}

class AngleApp : public App {
public:
    void setup() override { fbo_.allocate(32, 32); }
    void draw() override {
        auto& ctx = internal::currentWindowContext();
        check("window starts with radians", approxEq(ctx.currentScreenFov, TAU / 8));
        setupScreenPerspective();
        const Mat4 defaultProjection = ctx.currentProjectionMatrix;
        setupScreenPerspectiveDeg();
        check("no-argument perspective defaults match", sameMatrix(defaultProjection, ctx.currentProjectionMatrix) &&
              approxEq(ctx.currentScreenFov, TAU / 8));
        setupScreenPerspective(TAU / 6);
        const Mat4 projection = ctx.currentProjectionMatrix;
        const Mat4 view = ctx.currentViewMatrix;
        setupScreenPerspectiveDeg(60);
        check("perspective radian/Deg projection and camera match",
              sameMatrix(projection, ctx.currentProjectionMatrix) && sameMatrix(view, ctx.currentViewMatrix));
        setupScreenFov(TAU / 6);
        setupScreenFovDeg(60);
        check("screen FOV radian/Deg projection matches perspective",
              sameMatrix(projection, ctx.currentProjectionMatrix));
        setupScreenFovWithSize(TAU / 6, 80, 40, 0.25f, 750);
        const Mat4 sized = ctx.currentProjectionMatrix;
        setupScreenFovWithSizeDeg(60, 80, 40, 0.25f, 750);
        check("explicit-size radian/Deg projection matches", sameMatrix(sized, ctx.currentProjectionMatrix));
        testInvalid();

        // The FBO saves/restores radians, then the fullscreen-shader restore
        // path feeds the saved FOV back to the internal projection setup.
        setupScreenPerspective(TAU / 6);
        setDefaultScreenFovDeg(30);
        fbo_.begin();
        check("FBO takes its default FOV in radians", approxEq(ctx.currentScreenFov, TAU / 12));
        fbo_.end();
        check("FBO restores radians and screen projection", approxEq(ctx.currentScreenFov, TAU / 6) &&
              sameMatrix(projection, ctx.currentProjectionMatrix));
        internal::setupScreenFovWithSize(ctx.currentScreenFov, ctx.currentViewW, ctx.currentViewH);
        check("shader-style restore retains the projection", sameMatrix(projection, ctx.currentProjectionMatrix));
        setupScreenOrtho();
        check("public ortho still selects the internal zero sentinel", ctx.currentScreenFov == 0.0f);
        const Mat4 ortho = ctx.currentProjectionMatrix;
        internal::setupScreenFov(0);
        check("internal zero still restores orthographic projection", sameMatrix(ortho, ctx.currentProjectionMatrix));
        setDefaultScreenFov(TAU / 8);
        gpuFinished = true;
        exitApp();
    }
private:
    Fbo fbo_;
};
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
    auto listener = getLogger().onLog.listen([](LogEventArgs& e) {
        if (e.level == LogLevel::Warning && e.message.find("FOV out of range") != std::string::npos) ++fovWarnings;
    });
    testArcs();
    testDefaults();
    if (argc > 1 && std::strcmp(argv[1], "--gpu-check") == 0) {
        WindowSettings settings;
        settings.setSize(64, 64);
        settings.setHighDpi(false);
        settings.sampleCount = 1;
        runApp<AngleApp>(settings);
        check("projection checks completed", gpuFinished);
    } else {
        testInvalid();
        std::printf("SKIP: projection checks need --gpu-check and a display\n");
    }
    return failures ? 1 : 0;
}
