// =============================================================================
// tcxObj tests - headless behavioral test (no window).
//
// Built and run by CI on every push/PR across macOS / Windows / Linux via
// examples/build_all.py --addon-tests-only (exit 0 = pass, non-zero = fail).
//
// Each case writes a small .obj to a temp directory and loads it with
// ObjLoader. No materials or textures, so nothing needs a graphics context.
//
// It checks that ObjLoader validates face indices before reading:
//   - valid faces load as before: positive and relative (negative) indices,
//     normals and texcoords, quads split into two triangles, a concave
//     pentagon split into three by ear clipping
//   - a face vertex index of 0, or a negative index before the first vertex,
//     fails the load (tinyobjloader rejects the line) with an error logged
//   - a face whose vertex, normal or texcoord index is past the end of its
//     list is skipped with a warning; the other faces still load
//   - a normal index in a file with no normals is ignored, as before
// =============================================================================

#include <tcxObj.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

using namespace std;
using namespace tc;
using tcx::obj::ObjLoader;

static int g_pass = 0, g_fail = 0;
static void check(const string& name, bool ok) {
    printf("%-60s %s\n", name.c_str(), ok ? "PASS" : "FAIL");
    fflush(stdout);  // flush each line so CI logs survive a later crash
    ok ? ++g_pass : ++g_fail;
}

// Counts warnings (and worse) logged while it is alive, and how many of them
// are ObjLoader's own note about skipped faces.
struct WarningCounter {
    int count = 0;
    int skipped = 0;
    EventListener listener;
    WarningCounter() {
        listener = getLogger().onLog.listen([this](LogEventArgs& e) {
            if (e.level < LogLevel::Warning) return;
            count++;
            if (e.message.find("ObjLoader: skipped") != string::npos) skipped++;
        });
    }
};

static fs::path g_dir;
static int g_fileNo = 0;

static fs::path writeObj(const string& text) {
    fs::path p = g_dir / ("model" + to_string(g_fileNo++) + ".obj");
    ofstream(p, ios::binary) << text;
    return p;
}

struct ObjLoadResult {
    bool ok = false;
    int warnings = 0;
    int skippedWarnings = 0;
    int groups = 0;
    Mesh mesh;   // merged
};

static ObjLoadResult loadObjText(const string& text) {
    ObjLoadResult r;
    ObjLoader loader;
    WarningCounter warnings;
    r.ok = loader.load(writeObj(text));
    r.warnings = warnings.count;
    r.skippedWarnings = warnings.skipped;
    r.groups = loader.getNumGroups();
    r.mesh = loader.getMesh();
    return r;
}

static bool vecNear(const Vec3& v, float x, float y, float z) {
    return fabs(v.x - x) < 1e-5f && fabs(v.y - y) < 1e-5f && fabs(v.z - z) < 1e-5f;
}

// The three vertices of the test triangle
static const string TRI_V =
    "v 0 0 0\n"
    "v 1 0 0\n"
    "v 0 1 0\n";

int main() {
    g_dir = fs::temp_directory_path() /
            ("tcxObj-tests-" + to_string(chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(g_dir);

    // ----- valid faces load as before -----------------------------------------
    {
        auto r = loadObjText(TRI_V + "f 1 2 3\n");
        check("valid triangle: loads without warnings", r.ok && r.warnings == 0);
        check("valid triangle: 3 vertices, 3 indices",
              r.mesh.getNumVertices() == 3 && r.mesh.getNumIndices() == 3);
        check("valid triangle: vertex data",
              r.mesh.getNumVertices() == 3 && vecNear(r.mesh.getVertices()[1], 1, 0, 0) &&
              vecNear(r.mesh.getVertices()[2], 0, 1, 0));
    }
    {
        auto r = loadObjText(TRI_V + "f -3 -2 -1\n");
        check("valid relative indices: loads without warnings", r.ok && r.warnings == 0);
        check("valid relative indices: same triangle",
              r.mesh.getNumVertices() == 3 && r.mesh.getNumIndices() == 3 &&
              vecNear(r.mesh.getVertices()[0], 0, 0, 0) && vecNear(r.mesh.getVertices()[1], 1, 0, 0) &&
              vecNear(r.mesh.getVertices()[2], 0, 1, 0));
    }
    {
        // Relative indices count back from the vertices seen so far
        auto r = loadObjText("v 9 9 9\n" + TRI_V + "f -3 -2 -1\nv 8 8 8\n");
        check("valid relative indices mid-file: refer to the preceding vertices",
              r.ok && r.mesh.getNumVertices() == 3 && vecNear(r.mesh.getVertices()[0], 0, 0, 0) &&
              vecNear(r.mesh.getVertices()[2], 0, 1, 0));
    }
    {
        auto r = loadObjText(TRI_V +
                      "vt 0 0\nvt 1 0\nvt 0 1\n"
                      "vn 0 0 1\n"
                      "f 1/1/1 2/2/1 3/3/1\n");
        check("valid normals and texcoords: loads without warnings", r.ok && r.warnings == 0);
        check("valid normals and texcoords: data",
              r.mesh.getNumNormals() == 3 && r.mesh.getTexCoords().size() == 3 &&
              vecNear(r.mesh.getNormals()[2], 0, 0, 1) &&
              fabs(r.mesh.getTexCoords()[1].x - 1.0f) < 1e-5f &&
              fabs(r.mesh.getTexCoords()[2].y - 0.0f) < 1e-5f);   // V is flipped
    }
    {
        auto r = loadObjText(TRI_V + "v 1 1 0\nf 1 2 4 3\n");
        check("valid quad: two triangles",
              r.ok && r.mesh.getNumVertices() == 4 && r.mesh.getNumIndices() == 6);
    }
    {
        // A concave pentagon goes through tinyobjloader's ear clipping
        auto r = loadObjText("v 0 0 0\nv 2 0 0\nv 2 2 0\nv 1 1 0\nv 0 2 0\nf 1 2 3 4 5\n");
        check("valid concave pentagon: three triangles",
              r.ok && r.warnings == 0 && r.mesh.getNumVertices() == 5 &&
              r.mesh.getNumIndices() == 9);
    }

    // ----- indices tinyobjloader rejects ----------------------------------------
    {
        auto r = loadObjText(TRI_V + "f 0 1 2\n");
        check("vertex index 0: load fails", !r.ok && r.groups == 0);
        check("vertex index 0: logged", r.warnings > 0);
    }
    {
        auto r = loadObjText(TRI_V + "f -4 1 2\n");
        check("negative index before the first vertex: load fails", !r.ok && r.groups == 0);
        check("negative index before the first vertex: logged", r.warnings > 0);
    }

    // ----- indices past the end: the face is skipped, the rest loads -------------
    {
        auto r = loadObjText(TRI_V + "f 1 2 3\nf 1 2 9\n");
        check("vertex index past the end: loads", r.ok);
        check("vertex index past the end: only the good face",
              r.mesh.getNumVertices() == 3 && r.mesh.getNumIndices() == 3);
        check("vertex index past the end: skipped face logged", r.skippedWarnings == 1);
    }
    {
        auto r = loadObjText(TRI_V + "f 1 2 4\n");
        check("vertex index one past the end: face skipped",
              r.ok && r.mesh.getNumIndices() == 0 && r.skippedWarnings == 1);
    }
    {
        auto r = loadObjText(TRI_V + "vn 0 0 1\nf 1//1 2//1 3//1\nf 1//1 2//1 3//2\n");
        check("normal index past the end: only the good face",
              r.ok && r.mesh.getNumIndices() == 3 && r.mesh.getNumNormals() == 3);
        check("normal index past the end: skipped face logged", r.skippedWarnings == 1);
    }
    {
        auto r = loadObjText(TRI_V + "vt 0 0\nf 1/1 2/1 3/1\nf 1/1 2/1 3/2\n");
        check("texcoord index past the end: only the good face",
              r.ok && r.mesh.getNumIndices() == 3 && r.mesh.getTexCoords().size() == 3);
        check("texcoord index past the end: skipped face logged", r.skippedWarnings == 1);
    }
    {
        // With no vn lines at all the normal index was never read; the face
        // still loads (normals are computed instead).
        auto r = loadObjText(TRI_V + "f 1//1 2//2 3//3\n");
        check("normal index with no normals in the file: face still loads",
              r.ok && r.mesh.getNumIndices() == 3 && r.mesh.getNumNormals() == 3 &&
              r.skippedWarnings == 0);
    }

    error_code ec;
    fs::remove_all(g_dir, ec);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
