// =============================================================================
// tcxObj tests - headless behavioral test (no window).
//
// Built and run by CI on every push/PR across macOS / Windows / Linux via
// examples/build_all.py --addon-tests-only (exit 0 = pass, non-zero = fail).
//
// Each case writes a small .obj to a temp directory and loads it with
// ObjLoader. Texture cases use the dummy GPU backend, without a window.
//
// It checks that ObjLoader validates face indices before reading:
//   - valid faces load as before: positive and relative (negative) indices,
//     normals and texcoords, quads split into two triangles, a concave
//     pentagon split into three by ear clipping
//   - a face vertex index of 0, or a negative index before the first vertex,
//     fails the load (tinyobjloader rejects the line) with an error logged
//   - a face whose vertex, normal or texcoord index is past the end of its
//     list is skipped with a warning; the other faces still load
//   - a quad or pentagon with a vertex index one past the end: tinyobjloader
//     drops the quad; ear clipping of the pentagon does not read the missing
//     vertex (AddressSanitizer builds check this), and ObjLoader skips the
//     triangles that use it
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

// Counts warnings (and worse) logged while it is alive, how many of them
// are ObjLoader's own note about skipped faces, and how many carry
// tinyobjloader's note about a face it dropped while triangulating.
struct WarningCounter {
    int count = 0;
    int skipped = 0;
    int invalidFace = 0;
    EventListener listener;
    WarningCounter() {
        listener = getLogger().onLog.listen([this](LogEventArgs& e) {
            if (e.level < LogLevel::Warning) return;
            count++;
            if (e.message.find("ObjLoader: skipped") != string::npos) skipped++;
            if (e.message.find("Face with invalid vertex index") != string::npos) invalidFace++;
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
    int invalidFaceWarnings = 0;
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
    r.invalidFaceWarnings = warnings.invalidFace;
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

// Five vertices in the z = 0 plane. With five, vertex 6 lies outside the
// position array's allocation, so an AddressSanitizer build checks that it
// is never read.
static const string PENTA_V =
    "v 0 0 0\n"
    "v 2 0 0\n"
    "v 2 2 0\n"
    "v 1 1 0\n"
    "v 0 2 0\n";

// True when every vertex of `mesh` is one of the PENTA_V positions.
static bool onlyPentaVertices(const Mesh& mesh) {
    for (const auto& p : mesh.getVertices()) {
        if (!vecNear(p, 0, 0, 0) && !vecNear(p, 2, 0, 0) && !vecNear(p, 2, 2, 0) &&
            !vecNear(p, 1, 1, 0) && !vecNear(p, 0, 2, 0)) {
            return false;
        }
    }
    return true;
}

int main() {
    sg_desc graphics = {};
    sg_setup(&graphics);
    g_dir = fs::temp_directory_path() /
            utf8ToPath("tcxObj-テスト-" + to_string(chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(g_dir);

    // ----- UTF-8 model, material and texture paths ----------------------------
    {
        fs::path oldRoot = getDataPathRoot();
        setDataPathRoot(g_dir);
        Pixels pixels;
        pixels.allocate(2, 2, 4);
        for (int y = 0; y < 2; ++y) {
            for (int x = 0; x < 2; ++x) pixels.setColor(x, y, Color(1, 0, 0, 1));
        }
        check("UTF-8 fixture: PNG saved", bool(pixels.save(g_dir / utf8ToPath("テクスチャ.png"))));
        ofstream(g_dir / utf8ToPath("材質.mtl"), ios::binary)
            << "newmtl red\nKd 1 0 0\nmap_Kd テクスチャ.png\n";
        ofstream(g_dir / utf8ToPath("三角.obj"), ios::binary)
            << "mtllib 材質.mtl\n" << TRI_V
            << "vt 0 0\nvt 1 0\nvt 0 1\nusemtl red\nf 1/1 2/2 3/3\n";
        ObjLoader loader;
        bool loaded = loader.load(utf8ToPath("三角.obj"));
        check("UTF-8 OBJ: triangle and material loaded",
              loaded && loader.getNumGroups() == 1 && loader.getMesh().getNumIndices() == 3);
        bool textured = loaded && loader.getNumGroups() == 1 && loader.getGroups()[0].hasTexture;
        check("UTF-8 OBJ: texture pixels loaded",
              textured && loader.getGroups()[0].texture.getWidth() == 2 &&
              loader.getGroups()[0].texture.getColor(0, 0).r == 1.0f);
        if (textured) {
            auto& group = loader.getGroups()[0];
            tcx::obj::ObjExporter exporter;
            exporter.addMesh(group.mesh, "triangle", group.texture);
            check("UTF-8 OBJ: export relative Japanese filename",
                  exporter.save(utf8ToPath("モデル.obj")));
            ifstream obj(g_dir / utf8ToPath("モデル.obj"), ios::binary);
            string line;
            bool utf8Mtl = false;
            while (getline(obj, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line == "mtllib モデル.mtl") utf8Mtl = true;
            }
            check("UTF-8 OBJ: mtllib contains UTF-8 bytes", utf8Mtl);
            ObjLoader roundTrip;
            bool reloaded = roundTrip.load(utf8ToPath("モデル.obj"));
            check("UTF-8 OBJ: export loads with its texture",
                  reloaded && roundTrip.getNumGroups() == 1 &&
                  roundTrip.getGroups()[0].mesh.getNumIndices() == 3 &&
                  roundTrip.getGroups()[0].hasTexture &&
                  roundTrip.getGroups()[0].texture.getColor(0, 0).r == 1.0f);
        }
        setDataPathRoot(oldRoot);
    }

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

    // ----- faces tinyobjloader splits, with a vertex index one past the end ----
    // These reach tinyobjloader's own index checks in triangulation (a
    // triangle is passed through without them).
    {
        // A quad is split directly; tinyobjloader drops it with a warning
        // before reading any position, so ObjLoader never sees it
        auto r = loadObjText(PENTA_V + "f 1 2 3\nf 1 2 3 6\n");
        check("quad with a vertex index one past the end: loads", r.ok);
        check("quad with a vertex index one past the end: only the good face",
              r.mesh.getNumIndices() == 3 && onlyPentaVertices(r.mesh));
        check("quad with a vertex index one past the end: dropped by tinyobjloader",
              r.invalidFaceWarnings == 1 && r.skippedWarnings == 0);
    }
    {
        // A pentagon goes through ear clipping, which skips the missing
        // vertex when it picks the projection axes, when it tests an ear
        // and when it tests other vertices against an ear (this vertex order
        // reaches all three). The triangles that use the missing vertex are
        // then skipped by ObjLoader
        auto r = loadObjText(PENTA_V + "f 6 1 2 3 4\n");
        check("pentagon with a vertex index one past the end: loads", r.ok);
        check("pentagon with a vertex index one past the end: only good faces",
              r.mesh.getNumIndices() % 3 == 0 && r.mesh.getNumIndices() < 9 &&
              onlyPentaVertices(r.mesh));
        check("pentagon with a vertex index one past the end: skipped faces logged",
              r.skippedWarnings == 1);
    }

    {
        // With no vn lines, the normal index is ignored; the face still
        // loads (normals are computed instead).
        auto r = loadObjText(TRI_V + "f 1//1 2//2 3//3\n");
        check("normal index with no normals in the file: face still loads",
              r.ok && r.mesh.getNumIndices() == 3 && r.mesh.getNumNormals() == 3 &&
              r.skippedWarnings == 0);
    }

    error_code ec;
    fs::remove_all(g_dir, ec);

    sg_shutdown();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
