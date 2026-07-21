// =============================================================================
// clipSpace — regression test for the clip-space Z convention (#134)
//
// TrussC stores projection matrices backend-native (GL: clip z in [-1,1];
// Metal/D3D11/WebGPU: [0,1]) and converts on readback through the
// tcClipSpace.h helpers. This test drives BOTH conventions headlessly via
// internal::clipZOverrideForTests() and asserts that every readback API
// (CameraContext::worldToScreen / screenPointToRay, free worldToScreen /
// screenToWorld) returns identical results regardless of the convention.
// =============================================================================

#include <TrussC.h>

#include <cmath>
#include <cstdio>

using namespace std;
using namespace tc;

static int g_fail = 0;
static void check(const char* name, bool ok) {
    printf("%-64s %s\n", name, ok ? "PASS" : "FAIL");
    fflush(stdout);
    if (!ok) ++g_fail;
}

static bool nearf(float a, float b, float eps = 1e-3f) { return fabsf(a - b) < eps; }
static bool nearv(const Vec3& a, const Vec3& b, float eps = 1e-3f) {
    return nearf(a.x, b.x, eps) && nearf(a.y, b.y, eps) && nearf(a.z, b.z, eps);
}

// Distance from point to the (infinite) line of a ray
static float rayPointDistance(const Ray& ray, const Vec3& p) {
    Vec3 d = ray.direction.normalized();
    Vec3 v = p - ray.origin;
    Vec3 closest = ray.origin + d * v.dot(d);
    return (p - closest).length();
}

// Build a CameraContext under the CURRENT convention override.
static CameraContext makeContext(const Mat4& glProj, const Mat4& view, float w, float h) {
    CameraContext ctx;
    ctx.projection = internal::toBackendClip(glProj);
    ctx.view = view;
    ctx.viewW = w;
    ctx.viewH = h;
    return ctx;
}

int main() {
    const float W = 800.0f, H = 600.0f;
    const Vec3 probe(120.0f, -40.0f, 100.0f);

    const Mat4 view = Mat4::lookAt(Vec3(0, 0, 600), Vec3(0, 0, 0), Vec3(0, 1, 0));
    const Mat4 glOrtho = Mat4::ortho(-W / 2, W / 2, -H / 2, H / 2, 0.1f, 10000.0f);
    const Mat4 glFrustum = [] {
        float nearD = 0.5f, farD = 5000.0f;
        float top = nearD * tanf(0.25f);
        float right = top * (800.0f / 600.0f);
        return Mat4::frustum(-right, right, -top, top, nearD, farD);
    }();

    // --- 1. toBackendClip endpoint mapping --------------------------------
    internal::clipZOverrideForTests() = 1;
    {
        Mat4 m = internal::toBackendClip(Mat4::identity());
        Vec4 nearC = m * Vec4(0, 0, -1.0f, 1.0f);
        Vec4 farC  = m * Vec4(0, 0,  1.0f, 1.0f);
        check("[01] toBackendClip maps ndc z -1 -> 0", nearf(nearC.z / nearC.w, 0.0f));
        check("[01] toBackendClip maps ndc z +1 -> 1", nearf(farC.z / farC.w, 1.0f));
        check("[01] ndcNearZ/ndcMidZ are 0 / 0.5",
              nearf(internal::ndcNearZ(), 0.0f) && nearf(internal::ndcMidZ(), 0.5f));
        check("[01] depthFromNdcZ passes z through", nearf(internal::depthFromNdcZ(0.25f), 0.25f));
    }
    internal::clipZOverrideForTests() = 0;
    {
        Mat4 m = internal::toBackendClip(glOrtho);
        bool same = true;
        for (int i = 0; i < 16; ++i) same = same && nearf(m.m[i], glOrtho.m[i], 1e-6f);
        check("[GL] toBackendClip is identity", same);
        check("[GL] ndcNearZ/ndcMidZ are -1 / 0",
              nearf(internal::ndcNearZ(), -1.0f) && nearf(internal::ndcMidZ(), 0.0f));
        check("[GL] depthFromNdcZ remaps [-1,1] -> [0,1]", nearf(internal::depthFromNdcZ(0.0f), 0.5f));
    }

    // --- 2. readback converges across conventions -------------------------
    struct Case { const char* name; const Mat4& glProj; };
    const Case cases[] = { {"ortho", glOrtho}, {"frustum", glFrustum} };
    for (const Case& c : cases) {
        internal::clipZOverrideForTests() = 0;
        CameraContext glCtx = makeContext(c.glProj, view, W, H);
        Vec3 glScreen = glCtx.worldToScreen(probe);
        Ray glRay = glCtx.screenPointToRay(glScreen.x, glScreen.y);

        internal::clipZOverrideForTests() = 1;
        CameraContext nativeCtx = makeContext(c.glProj, view, W, H);
        Vec3 nativeScreen = nativeCtx.worldToScreen(probe);
        Ray nativeRay = nativeCtx.screenPointToRay(nativeScreen.x, nativeScreen.y);

        char buf[128];
        snprintf(buf, sizeof(buf), "[%s] worldToScreen equal across conventions", c.name);
        check(buf, nearv(glScreen, nativeScreen));
        snprintf(buf, sizeof(buf), "[%s] depth is inside (0, 1)", c.name);
        check(buf, glScreen.z > 0.0f && glScreen.z < 1.0f);
        snprintf(buf, sizeof(buf), "[%s] pick ray hits the probe (GL)", c.name);
        check(buf, rayPointDistance(glRay, probe) < 0.5f);
        snprintf(buf, sizeof(buf), "[%s] pick ray hits the probe (zero-to-one)", c.name);
        check(buf, rayPointDistance(nativeRay, probe) < 0.5f);
        snprintf(buf, sizeof(buf), "[%s] ray origins equal across conventions", c.name);
        check(buf, nearv(glRay.origin, nativeRay.origin, 0.5f));
    }

    // --- 3. free worldToScreen / screenToWorld through WindowContext ------
    for (int conv = 0; conv <= 1; ++conv) {
        internal::clipZOverrideForTests() = conv;
        auto& wc = internal::currentWindowContext();
        wc.currentProjectionMatrix = internal::toBackendClip(glOrtho);
        wc.currentViewMatrix = view;
        wc.currentViewW = W;
        wc.currentViewH = H;

        Vec3 s = worldToScreen(probe);
        Vec3 back = screenToWorld(Vec2(s.x, s.y), probe.z);
        char buf[128];
        snprintf(buf, sizeof(buf), "[conv=%d] screenToWorld(worldToScreen(p)) == p", conv);
        check(buf, nearv(back, probe, 0.5f));
    }

    // Convergence of the free function across conventions
    {
        internal::clipZOverrideForTests() = 0;
        internal::currentWindowContext().currentProjectionMatrix = internal::toBackendClip(glOrtho);
        Vec3 glS = worldToScreen(probe);
        internal::clipZOverrideForTests() = 1;
        internal::currentWindowContext().currentProjectionMatrix = internal::toBackendClip(glOrtho);
        Vec3 nS = worldToScreen(probe);
        check("free worldToScreen equal across conventions", nearv(glS, nS));
    }

    internal::clipZOverrideForTests() = -1;   // restore auto

    printf("\n%s  (%d failure%s)\n", g_fail ? "FAILED" : "PASSED",
           g_fail, g_fail == 1 ? "" : "s");
    fflush(stdout);
    return g_fail ? 1 : 0;
}
