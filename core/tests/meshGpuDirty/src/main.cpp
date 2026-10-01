// =============================================================================
// core/tests/meshGpuDirty — behavioral regression test for the Mesh data
// revision (#267).
//
// Headless, console, exit code = pass/fail (build_all.py runs it in CI).
//
// Guards the invariants:
//   - Every mutator changes getDataRevision(): clear() / clearXxx(), add*,
//     setNormal, translate / rotateX/Y/Z / scale / transform, append, setMode.
//     This includes clear() followed by re-adding the same vertex count.
//   - Non-const getters (getVertices / getColors / getNormals / getIndices /
//     getTexCoords / getTangents) change it; const getters and other const
//     reads do not.
//   - markGpuDirty() changes it.
// =============================================================================

#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include <cstdint>
#include <cstdio>
#include <functional>
#include <utility>

using namespace std;
using namespace tc;

namespace {

static int g_fail = 0;
static void check(const char* name, bool ok) {
    printf("%-66s %s\n", name, ok ? "PASS" : "FAIL");
    fflush(stdout);
    if (!ok) ++g_fail;
}

static Mesh makeMesh(int n) {
    Mesh m;
    for (int i = 0; i < n; i++) {
        m.addVertex(Vec3(float(i), float(i % 7), 0.0f));
        m.addNormal(0.0f, 0.0f, 1.0f);
        m.addColor(1.0f, 1.0f, 1.0f);
        m.addTexCoord(0.0f, 0.0f);
        m.addTangent(1.0f, 0.0f, 0.0f);
    }
    for (int i = 0; i + 2 < n; i += 3) m.addTriangle(i, i + 1, i + 2);
    return m;
}

// The revision changes across op(m).
static void expectChange(const char* name, const function<void(Mesh&)>& op) {
    Mesh m = makeMesh(99);
    uint64_t before = m.getDataRevision();
    op(m);
    check(name, m.getDataRevision() != before);
}

// The revision stays the same across op(m).
static void expectSame(const char* name, const function<void(Mesh&)>& op) {
    Mesh m = makeMesh(99);
    uint64_t before = m.getDataRevision();
    op(m);
    check(name, m.getDataRevision() == before);
}

} // namespace

TC_CORE_TEST_MAIN() {
    printf("=== meshGpuDirty (#267) ===\n");

    // Same-size rebuild: clear() then re-add the same 100 vertices.
    {
        Mesh m;
        m.setMode(PrimitiveMode::Points);
        for (int i = 0; i < 100; i++) m.addVertex(float(i), 0.0f, 0.0f);
        uint64_t r0 = m.getDataRevision();
        m.clear();
        for (int i = 0; i < 100; i++) m.addVertex(float(i) + 1.0f, 0.0f, 0.0f);
        check("clear() + re-add same count changes revision", m.getDataRevision() != r0);
        check("same-size rebuild keeps vertex count", m.getNumVertices() == 100);
    }

    expectChange("setMode",             [](Mesh& m) { m.setMode(PrimitiveMode::Points); });
    expectChange("addVertex(x,y,z)",    [](Mesh& m) { m.addVertex(1, 2, 3); });
    expectChange("addVertex(Vec2)",     [](Mesh& m) { m.addVertex(Vec2(1, 2)); });
    expectChange("addVertex(Vec3)",     [](Mesh& m) { m.addVertex(Vec3(1, 2, 3)); });
    expectChange("addVertices",         [](Mesh& m) { m.addVertices({Vec3(1, 2, 3)}); });
    expectChange("addColor(Color)",     [](Mesh& m) { m.addColor(Color(1, 0, 0)); });
    expectChange("addColor(r,g,b)",     [](Mesh& m) { m.addColor(1, 0, 0); });
    expectChange("addColors",           [](Mesh& m) { m.addColors({Color(1, 0, 0)}); });
    expectChange("addIndex",            [](Mesh& m) { m.addIndex(0); });
    expectChange("addIndices",          [](Mesh& m) { m.addIndices({0, 1, 2}); });
    expectChange("addTriangle",         [](Mesh& m) { m.addTriangle(0, 1, 2); });
    expectChange("addNormal(x,y,z)",    [](Mesh& m) { m.addNormal(0, 1, 0); });
    expectChange("addNormal(Vec3)",     [](Mesh& m) { m.addNormal(Vec3(0, 1, 0)); });
    expectChange("addNormals",          [](Mesh& m) { m.addNormals({Vec3(0, 1, 0)}); });
    expectChange("setNormal",           [](Mesh& m) { m.setNormal(0, Vec3(0, 1, 0)); });
    expectChange("addTexCoord(u,v)",    [](Mesh& m) { m.addTexCoord(0.5f, 0.5f); });
    expectChange("addTexCoord(Vec2)",   [](Mesh& m) { m.addTexCoord(Vec2(0.5f, 0.5f)); });
    expectChange("addTangent(x,y,z,w)", [](Mesh& m) { m.addTangent(1, 0, 0, 1); });
    expectChange("addTangent(Vec4)",    [](Mesh& m) { m.addTangent(Vec4(1, 0, 0, 1)); });
    expectChange("addTangent(Vec3,w)",  [](Mesh& m) { m.addTangent(Vec3(1, 0, 0), 1.0f); });
    expectChange("clear",               [](Mesh& m) { m.clear(); });
    expectChange("clearVertices",       [](Mesh& m) { m.clearVertices(); });
    expectChange("clearNormals",        [](Mesh& m) { m.clearNormals(); });
    expectChange("clearColors",         [](Mesh& m) { m.clearColors(); });
    expectChange("clearIndices",        [](Mesh& m) { m.clearIndices(); });
    expectChange("clearTexCoords",      [](Mesh& m) { m.clearTexCoords(); });
    expectChange("clearTangents",       [](Mesh& m) { m.clearTangents(); });
    expectChange("translate(x,y,z)",    [](Mesh& m) { m.translate(1, 0, 0); });
    expectChange("translate(Vec3)",     [](Mesh& m) { m.translate(Vec3(1, 0, 0)); });
    expectChange("rotateX",             [](Mesh& m) { m.rotateX(0.02f); });
    expectChange("rotateY",             [](Mesh& m) { m.rotateY(0.02f); });
    expectChange("rotateZ",             [](Mesh& m) { m.rotateZ(0.02f); });
    expectChange("scale(x,y,z)",        [](Mesh& m) { m.scale(2, 1, 1); });
    expectChange("scale(s)",            [](Mesh& m) { m.scale(2.0f); });
    expectChange("scale(Vec3)",         [](Mesh& m) { m.scale(Vec3(2, 2, 2)); });
    expectChange("transform",           [](Mesh& m) { m.transform(Mat4::translate(1, 0, 0)); });
    expectChange("append",              [](Mesh& m) { m.append(makeMesh(3)); });
    expectChange("markGpuDirty",        [](Mesh& m) { m.markGpuDirty(); });

    // Non-const getters mark the mesh changed (writes through the reference).
    expectChange("non-const getVertices",  [](Mesh& m) { m.getVertices()[0].x += 1.0f; });
    expectChange("non-const getColors",    [](Mesh& m) { m.getColors()[0].r = 0.5f; });
    expectChange("non-const getNormals",   [](Mesh& m) { m.getNormals()[0].z = -1.0f; });
    expectChange("non-const getIndices",   [](Mesh& m) { m.getIndices()[0] = 2; });
    expectChange("non-const getTexCoords", [](Mesh& m) { m.getTexCoords()[0].x = 1.0f; });
    expectChange("non-const getTangents",  [](Mesh& m) { m.getTangents()[0].w = -1.0f; });

    // Const reads leave the revision unchanged.
    expectSame("const getVertices / getColors / getNormals", [](Mesh& m) {
        const Mesh& c = m;
        (void)c.getVertices(); (void)c.getColors(); (void)c.getNormals();
    });
    expectSame("as_const getIndices / getTexCoords / getTangents", [](Mesh& m) {
        (void)as_const(m).getIndices(); (void)as_const(m).getTexCoords();
        (void)as_const(m).getTangents();
    });
    expectSame("counts / has* / getNormal / getMode", [](Mesh& m) {
        (void)m.getNumVertices(); (void)m.getNumIndices(); (void)m.hasColors();
        (void)m.hasNormals(); (void)m.getNormal(0); (void)m.getMode();
    });

    // Each change gets a new revision (repeated same-size edits keep changing it).
    {
        Mesh m = makeMesh(10);
        uint64_t a = m.getDataRevision();
        m.rotateY(0.02f);
        uint64_t b = m.getDataRevision();
        m.rotateY(0.02f);
        uint64_t c = m.getDataRevision();
        check("repeated rotateY gives distinct revisions", a != b && b != c && a != c);
    }

    printf("=== %s (%d failure%s) ===\n", g_fail ? "FAILED" : "ALL PASSED",
           g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
