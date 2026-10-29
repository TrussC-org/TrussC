// =============================================================================
// tcxObj tests - headless behavioral test (no window).
//
// Built and run by CI on every push/PR across macOS / Windows / Linux via
// examples/build_all.py --addon-tests-only (exit 0 = pass, non-zero = fail).
//
// Each case writes a small .obj to a temp directory and loads it with
// ObjLoader. Texture cases use the dummy GPU backend, without a window.
// TCX_OBJ_TEST_GPU additionally checks lit drawing in a native graphics context.
//
// It checks face index validation and computed normals:
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
//   - computed normals keep their direction and area weighting at small and
//     large scales; zero-length sums keep the fallback normal
// =============================================================================

#include <tcxObj.h>
#include "tiny_obj_loader.h"
#include "../../../../core/tests/common/tcModelAllocationProbe.h"

// Taking addresses with the exact upstream types detects public API widening.
using VendorFileLoader = bool (*)(tinyobj::attrib_t*, std::vector<tinyobj::shape_t>*,
    std::vector<tinyobj::material_t>*, std::string*, std::string*, const char*,
    const char*, bool, bool);
using VendorStreamLoader = bool (*)(tinyobj::attrib_t*, std::vector<tinyobj::shape_t>*,
    std::vector<tinyobj::material_t>*, std::string*, std::string*, std::istream*,
    tinyobj::MaterialReader*, bool, bool);
static_assert(requires { static_cast<VendorFileLoader>(&tinyobj::LoadObj); });
static_assert(requires { static_cast<VendorStreamLoader>(&tinyobj::LoadObj); });

#ifdef TCX_OBJ_TEST_GPU
#include "../../../../core/tests/common/tcDisplayTest.h"
#endif

#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <sstream>
#ifndef _WIN32
#include <unistd.h>
#endif

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
    r.ok = loader.load(writeObj(text)).ok();
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

// Export failures must preserve previous text files and report exactly once.
static void testExporterSaves() {
    Mesh mesh;
    mesh.addVertex(0, 0, 0).addVertex(1, 0, 0).addVertex(0, 1, 0);
    mesh.addTexCoord(0, 0).addTexCoord(1, 0).addTexCoord(0, 1);
    mesh.addColor(Color(1, 0, 0)).addColor(Color(1, 0, 0)).addColor(Color(1, 0, 0));
    tcx::obj::ObjExporter exporter;
    exporter.addMesh(mesh, "triangle");
    exporter.setVertexColorMode(tcx::obj::VertexColorMode::BakeTexture).setBakeTextureSize(4);
    const auto obj = g_dir / "saved.obj";
    const auto mtl = g_dir / "saved.mtl";
    const auto png = g_dir / "saved_triangle.png";
    int errors = 0;
    auto listener = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.level == LogLevel::Error) ++errors;
    });
    auto readFile = [](const fs::path& path) {
        ifstream file(path, ios::binary);
        return string(istreambuf_iterator<char>(file), istreambuf_iterator<char>());
    };
    auto tempPath = [](fs::path path) { path += ".tmp"; return path; };

    auto result = exporter.save(obj);
    check("export: baked material succeeds without Errors", result.ok() && errors == 0);
    const auto previousObj = readFile(obj);
    const auto previousMtl = readFile(mtl);
    const auto previousPng = readFile(png);
    check("export: no temporary files after success",
          !fs::exists(tempPath(obj)) && !fs::exists(tempPath(mtl)) && !fs::exists(tempPath(png)));
    exporter.setScale(2); // Would change the OBJ if it were replaced too early.

    for (const auto& blocked : {obj, mtl, png}) {
        const auto blocker = tempPath(blocked);
        fs::create_directory(blocker);
        ofstream(blocker / "keep") << "pre-existing blocker";
        errors = 0;
        result = exporter.save(obj);
        check("export: blocked " + pathToUtf8(blocked.extension()) + " fails with one Error",
              // A directory at <file>.tmp is EISDIR on POSIX (WriteFailed) but
              // EACCES on Windows (AccessDenied): check the failure, not the code.
              !result.ok() && !result.message.empty() && errors == 1);
        check("export: failure preserves previous OBJ, MTL and texture",
              readFile(obj) == previousObj && readFile(mtl) == previousMtl &&
              readFile(png) == previousPng);
        check("export: preserves pre-existing temporary blocker", readFile(blocker / "keep") == "pre-existing blocker");
        fs::remove_all(blocker); // Only the test owns this directory.
        check("export: no generated temporary files after failure",
              !fs::exists(tempPath(obj)) && !fs::exists(tempPath(mtl)) && !fs::exists(tempPath(png)));
    }

    errors = 0;
    Image emptyImage;
    tcx::obj::ObjExporter emptyTexture;
    emptyTexture.addMesh(mesh, "triangle", emptyImage);
    result = emptyTexture.save(obj);
    check("export: empty texture fails with one Error", result.error == SaveError::WriteFailed && errors == 1);
    check("export: empty texture preserves text and cleans temporaries",
          readFile(obj) == previousObj && readFile(mtl) == previousMtl &&
          !fs::exists(tempPath(obj)) && !fs::exists(tempPath(mtl)));

    errors = 0;
    result = tcx::obj::ObjExporter().save(obj);
    check("export: no meshes fails with one Error", result.error == SaveError::WriteFailed && errors == 1);
    errors = 0;
    result = exporter.save({});
    check("export: empty path fails with one Error", result.error == SaveError::WriteFailed && errors == 1);

#ifndef _WIN32
    if (geteuid() == 0) {
        printf("export: unwritable folder SKIP (root bypasses permissions)\n");
    } else {
        const auto locked = g_dir / "unwritable";
        fs::create_directory(locked);
        fs::permissions(locked, fs::perms::owner_read | fs::perms::owner_exec);
        errors = 0;
        result = exporter.save(locked / "model.obj");
        fs::permissions(locked, fs::perms::owner_all);
        check("export: unwritable folder returns AccessDenied and one Error",
              result.error == SaveError::AccessDenied && errors == 1);
        check("export: unwritable folder leaves no files", fs::is_empty(locked));
    }
#else
    printf("export: unwritable folder SKIP (requires a Windows ACL fixture)\n");
#endif

    errors = 0;
    result = exporter.save(obj);
    check("export: retry replaces previous OBJ successfully",
          result.ok() && errors == 0 && readFile(obj) != previousObj);
    tcx::obj::ObjExporter geometryOnly;
    geometryOnly.addMesh(mesh);
    const auto geometryPath = g_dir / "geometry.obj";
    check("export: geometry without material succeeds", geometryOnly.save(geometryPath).ok());
    check("export: geometry without material leaves no MTL or temporary",
          !fs::exists(g_dir / "geometry.mtl") && !fs::exists(tempPath(geometryPath)));
}

static int runTests() {
#ifndef TCX_OBJ_TEST_GPU
    sg_desc graphics = {};
    sg_setup(&graphics);
#endif
    g_dir = fs::temp_directory_path() /
            utf8ToPath("tcxObj-テスト-" + to_string(chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(g_dir);
    // Match a running app: Material destruction lazily initializes core's
    // window context. Do that before injecting addon allocation failures.
    { ObjLoader warm; warm.load(writeObj(TRI_V + "f 1 2 3\n")); }

    {
        const string input = TRI_V + "t name 0/0/1 #label\nf 1 2 3\n";
        auto path = writeObj(input);
        tinyobj::attrib_t attrib;
        vector<tinyobj::shape_t> shapes;
        vector<tinyobj::material_t> materials;
        string warnings, errors;
        bool ok = tinyobj::LoadObj(&attrib, &shapes, &materials, &warnings, &errors,
                                   pathToUtf8(path).c_str());
        check("vendor file tags accept a value starting with hash", ok && shapes.size() == 1 &&
              shapes[0].mesh.tags[0].stringValues == vector<string>{"#label"});
        istringstream stream(input);
        ok = tinyobj::LoadObj(&attrib, &shapes, &materials, &warnings, &errors, &stream);
        check("vendor stream tags accept a value starting with hash", ok && shapes.size() == 1 &&
              shapes[0].mesh.tags[0].stringValues == vector<string>{"#label"});
        ObjLoader loader;
        check("framework tags accept a value starting with hash",
              loader.load(path).ok() && loader.getNumGroups() == 1);
    }
    {
        vector<LogLevel> levels;
        string warning;
        auto listener = getLogger().onLog.listen([&](LogEventArgs& e) {
            if (e.level >= LogLevel::Warning) levels.push_back(e.level);
            if (e.level == LogLevel::Warning) warning += e.message;
        });
        ObjLoader loader;
        auto result = loader.load(writeObj(TRI_V +
            "mtllib missing-rejection.mtl\nt name 2/0/0 1\nf 1 2 3\n"));
        check("upstream-compatible tags retain missing-MTL Warning",
              result.ok() && loader.getNumGroups() == 1 &&
              warning.find("missing-rejection.mtl") != string::npos &&
              levels == vector<LogLevel>{LogLevel::Warning});
    }

    // Upstream clamps counts and supplies missing values instead of rejecting.
    for (const string& counts : {string("1000000000/0/0"), string("-1/0/0"),
                                 string("0/1000000000/0"), string("0/0/1000000000"),
                                 string("2147483648/0/0"), string("2/0/0")}) {
        auto path = writeObj(TRI_V + "f 1 2 3\nt sample " + counts + " 1\n");
        ObjLoader loader;
        modelAllocationProbe::begin();
        auto result = loader.load(path);
        modelAllocationProbe::end();
        check("upstream-compatible tag counts load geometry", result.ok() && loader.getNumGroups() == 1);
    }
    {
        string input = TRI_V + "t sample 9000/0/0";
        for (int i = 0; i < 9000; ++i) input += " 7";
        input += "\nf 1 2 3\n";
        tinyobj::attrib_t attrib;
        vector<tinyobj::shape_t> shapes;
        vector<tinyobj::material_t> materials;
        string warnings, errors;
        istringstream stream(input);
        bool ok = tinyobj::LoadObj(&attrib, &shapes, &materials, &warnings, &errors, &stream);
        check("default vendor tags retain the upstream clamp", ok && shapes.size() == 1 &&
              shapes[0].mesh.tags.size() == 1 && shapes[0].mesh.tags[0].intValues.size() == 8192 &&
              shapes[0].mesh.tags[0].intValues.back() == 7);

        ObjLoader loader;
        auto path = writeObj(input);
        for (bool persistent : {false, true}) {
            if (persistent && tcTest::skipPersistentAllocationFailure("OBJ load entry")) continue;
            check("OBJ starts allocation test with a loaded model", loader.load(path).ok());
            LoadResult result;
            bool escaped = false;
            modelAllocationProbe::begin(1, persistent);
            try { result = loader.load(path); }
            catch (const bad_alloc&) { escaped = true; }
            modelAllocationProbe::end();
            check(persistent ? "OBJ persistent allocation failure returns empty failure" :
                               "OBJ allocation failure returns clean empty failure",
                  modelAllocationProbe::failed && !escaped && !result &&
                  result.error == LoadError::DecodeFailed && loader.getNumGroups() == 0);
            check("OBJ retry after allocation failure succeeds", loader.load(path).ok());
        }
    }
    {
        string tag = "t sample 9000/0/0";
        for (int i = 0; i < 9000; ++i) tag += " 7";
        for (int groupCount : {1, 32}) {
            string input = TRI_V + tag + "\n";
            for (int i = 0; i < groupCount; ++i)
                input += "g group" + to_string(i) + "\nf 1 2 3\n";
            tinyobj::attrib_t attrib;
            vector<tinyobj::shape_t> shapes;
            vector<tinyobj::material_t> materials;
            string warnings, errors;
            istringstream stream(input);
            bool ok = tinyobj::LoadObj(&attrib, &shapes, &materials, &warnings, &errors,
                                       &stream);
            size_t retainedTagValues = 0;
            for (const auto& shape : shapes)
                for (const auto& value : shape.mesh.tags)
                    retainedTagValues += value.intValues.size() + value.floatValues.size() + value.stringValues.size();
            check("default vendor tag clamp is preserved across groups",
                  ok && shapes.size() == static_cast<size_t>(groupCount) &&
                  retainedTagValues == static_cast<size_t>(groupCount) * 8192);
            ObjLoader loader;
            auto path = writeObj(input);
            modelAllocationProbe::begin();
            auto result = loader.load(path);
            modelAllocationProbe::end();
            check("OBJ framework path preserves all tagged groups",
                  result.ok() && loader.getNumGroups() == groupCount);
        }
    }
    {
        // Warm core context first; sweep fresh loaders so reserve/emplace and
        // every later group allocation are covered, including point-only OBJ.
        Pixels pixels;
        pixels.allocate(2, 2, 4);
        pixels.save(g_dir / "sweep.png");
        ofstream(g_dir / "sweep.mtl") << "newmtl sample\nmap_Kd sweep.png\n";
        for (const string& input : {TRI_V + "mtllib sweep.mtl\nusemtl sample\ng first\nf 1 2 3\ng second\nf 1 2 3\n", TRI_V}) {
            auto path = writeObj(input);
            { ObjLoader warm; warm.load(path); }
            size_t requests = 0;
            { ObjLoader model;
              modelAllocationProbe::begin();
              model.load(path);
              modelAllocationProbe::end();
              requests = modelAllocationProbe::requests; }
            for (bool persistent : {false, true}) {
                if (persistent && tcTest::skipPersistentAllocationFailure("OBJ every allocation")) continue;
                if (!persistent && tcTest::skipProxyAllocationSweep("OBJ every allocation: single recovery and retry")) continue;
                bool clean = requests > 0;
                for (size_t nth = 1; nth <= requests; ++nth) {
                    // Retire prior dummy resources between cases, as an app
                    // does after a frame, before enabling fault injection.
                    sg_commit();
                    tc::internal::drainPendingGpuDestroys();
                    ObjLoader model;
                    LoadResult result;
                    bool escaped = false;
                    modelAllocationProbe::beginNth(nth, persistent);
                    try { result = model.load(path); } catch (...) { escaped = true; }
                    modelAllocationProbe::end();
                    // Optional texture failure can still produce a valid model.
                    clean = clean && modelAllocationProbe::failed && !escaped &&
                            (result.ok() || (result.error == LoadError::DecodeFailed && model.getNumGroups() == 0));
                    clean = clean && model.load(path).ok();
                }
                check(persistent ? "OBJ every allocation: persistent recovery and retry" :
                                   "OBJ every allocation: single recovery and retry", clean);
            }
        }
    }
    for (const string& tag : {string("t crease 2/ 0/ 0 1 2"),
                              string("t crease 1/0 5"), string("t subdiv")}) {
        tinyobj::attrib_t attrib;
        vector<tinyobj::shape_t> shapes;
        vector<tinyobj::material_t> materials;
        string warnings, errors;
        istringstream stream(TRI_V + tag + "\nf 1 2 3\n");
        check("base-compatible tag grammar: " + tag,
              tinyobj::LoadObj(&attrib, &shapes, &materials, &warnings, &errors, &stream) &&
              shapes.size() == 1 && shapes[0].mesh.tags.size() == 1);
        ObjLoader loader;
        check("framework accepts base-compatible tag grammar: " + tag,
              loader.load(writeObj(TRI_V + tag + "\nf 1 2 3\n")).ok());
    }
    if (!tcTest::skipProxyAllocationSweep("MTL open allocation failure warns when geometry survives")) {
        // An MTL stream-open allocation can be translated into failbit by
        // iostreams. Losing optional materials must still issue a Warning.
        ofstream(g_dir / "oom.mtl") << "newmtl red\nKd 1 0 0\n";
        auto path = writeObj("mtllib oom.mtl\n" + TRI_V + "usemtl red\nf 1 2 3\n");
        size_t warnings = 0;
        auto listener = getLogger().onLog.listen([&](LogEventArgs& e) {
            if (e.level == LogLevel::Warning) ++warnings;
        });
        size_t requests;
        { ObjLoader loader;
          modelAllocationProbe::begin();
          loader.load(path);
          modelAllocationProbe::end();
          requests = modelAllocationProbe::requests; }
        bool reported = true, sawMissingMaterials = false;
        for (size_t nth = 1; nth <= requests; ++nth) {
            ObjLoader loader;
            warnings = 0;
            modelAllocationProbe::beginNth(nth, false);
            auto result = loader.load(path);
            modelAllocationProbe::end();
            if (result && loader.getNumGroups() &&
                loader.getGroups()[0].material.getBaseColor().g != 0) {
                sawMissingMaterials = true;
                reported = reported && warnings > 0;
            }
        }
        check("MTL open allocation failure warns when geometry survives",
              sawMissingMaterials && reported);
    }
    // Sparse on-disk comments keep this fixture cheap to create. Both the OBJ
    // and its MTL are local inputs and may exceed the former stream threshold.
    for (bool materialFile : {false, true}) {
        const size_t commentBytes = size_t(256) * 1024 * 1024 + 1;
        auto path = g_dir / (materialFile ? "large.mtl" : "large.obj");
        ofstream(path, ios::binary) << '#';
        fs::resize_file(path, commentBytes);
        ofstream(path, ios::binary | ios::app) << (materialFile ? "\nnewmtl sample\nKd 1 0 0\n" : "\n" + TRI_V + "f 1 2 3\n");
        ObjLoader loader;
        auto obj = materialFile ? writeObj("mtllib large.mtl\n" + TRI_V + "usemtl sample\nf 1 2 3\n") : path;
        auto result = loader.load(obj);
        check("large local " + string(materialFile ? "MTL" : "OBJ") + " loads without a byte cap",
              result.ok() && loader.getMesh().getNumVertices() == 3);
        if (materialFile && result && loader.getNumGroups())
            check("large local MTL keeps its material",
                  loader.getGroups()[0].material.getBaseColor().r == 1 &&
                  loader.getGroups()[0].material.getBaseColor().g == 0 &&
                  loader.getGroups()[0].material.getBaseColor().b == 0);
        fs::remove(path);
    }

    // The shared entry reports missing and broken files exactly once.
    {
        fs::path oldRoot = getDataPathRoot();
        setDataPathRoot(g_dir);
        vector<string> errors;
        auto listener = getLogger().onLog.listen([&](LogEventArgs& e) {
            if (e.level == LogLevel::Error) errors.push_back(e.message);
        });
        ObjLoader loader;
        auto result = loader.load("missing.obj");
        string expected = "file not found: " + pathToUtf8(g_dir / "missing.obj");
        check("missing OBJ: FileNotFound and exactly one shared Error",
              result.error == LoadError::FileNotFound && result.message == expected &&
              errors.size() == 1 && errors[0] == "[ObjLoader] " + expected);
        errors.clear();
        result = loader.load(writeObj(TRI_V + "f 0 1 2\n"));
        check("broken OBJ: DecodeFailed and exactly one Error",
              result.error == LoadError::DecodeFailed && !result.message.empty() &&
              errors.size() == 1 && errors[0] == "[ObjLoader] " + result.message && loader.getNumGroups() == 0);
        // Material parse errors keep usable geometry and issue a Warning.
        errors.clear();
        ofstream(g_dir / "broken.mtl", ios::binary) << "newmtl red\nKd x y z\n";
        result = loader.load(writeObj("mtllib broken.mtl\n" + TRI_V + "f 1 2 3\n"));
        check("OBJ with a malformed .mtl still loads without an Error",
              result.ok() && errors.empty() && loader.getNumGroups() > 0);
        size_t warnings = 0;
        auto warningListener = getLogger().onLog.listen([&](LogEventArgs& e) {
            if (e.level == LogLevel::Warning) ++warnings;
        });
        for (const string& statement : {string("d -halo 0.5"), string("Tf xyz 1 1 1"),
                                         string("Ka spectral file.rfl"), string("unknown_property 1")}) {
            errors.clear();
            warnings = 0;
            ofstream(g_dir / "valid.mtl") << "newmtl red\n" << statement << '\n';
            result = loader.load(writeObj("mtllib valid.mtl\n" + TRI_V + "usemtl red\nf 1 2 3\n"));
            check("valid MTL form keeps geometry with Warning: " + statement,
                  result.ok() && errors.empty() && warnings > 0 &&
                  loader.getMesh().getNumVertices() == 3);
        }
        setDataPathRoot(oldRoot);
    }

    testExporterSaves();

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
        bool loaded = loader.load(utf8ToPath("三角.obj")).ok();
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
                  exporter.save(utf8ToPath("モデル.obj")).ok());
            ifstream obj(g_dir / utf8ToPath("モデル.obj"), ios::binary);
            string line;
            bool utf8Mtl = false;
            while (getline(obj, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line == "mtllib モデル.mtl") utf8Mtl = true;
            }
            check("UTF-8 OBJ: mtllib contains UTF-8 bytes", utf8Mtl);
            ObjLoader roundTrip;
            bool reloaded = roundTrip.load(utf8ToPath("モデル.obj")).ok();
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

    // ----- mixed attributes stay aligned with their vertices (#437) -----------
    {
        // The exact reproducer from the issue: missing normals come first.
        auto r = loadObjText(TRI_V + "v 1 1 0\nvn 0 0 1\nf 1 2 3\nf 2//1 4//1 3//1\n");
        check("issue #437: six aligned normals", r.ok && r.warnings == 0 &&
              r.mesh.getNumVertices() == 6 && r.mesh.getNumNormals() == 6);
        bool directions = r.mesh.getNumNormals() == 6;
        for (const auto& n : r.mesh.getNormals()) directions &= vecNear(n, 0, 0, 1);
        check("issue #437: computed and authored directions", directions);
    }
    for (bool authoredFirst : {false, true}) {
        // A non-unit normal with a different direction catches recomputation
        // or normalization of file normals, regardless of face order.
        const string missing = "f 1 2 3\n";
        const string authored = "f 2//1 4//1 3//1\n";
        auto r = loadObjText(TRI_V + "v 1 1 0\nvn 2 0 0\n" +
                             (authoredFirst ? authored + missing : missing + authored));
        bool preserved = r.ok && r.mesh.getNumNormals() == 6;
        bool computed = preserved;
        for (int i = 0; i < 3; ++i) {
            const auto n = r.mesh.getNormal((authoredFirst ? 0 : 3) + i);
            preserved &= n.x == 2 && n.y == 0 && n.z == 0;
            computed &= vecNear(r.mesh.getNormal((authoredFirst ? 3 : 0) + i), 0, 0, 1);
        }
        check("mixed normals: file values unchanged", preserved);
        check("mixed normals: missing values follow face direction", computed);
    }
    {
        // Two missing-normal faces share vertices with a 1:4 area ratio.
        // A third, authored face ensures this exercises the mixed path.
        auto r = loadObjText("v 0 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 4\n"
                             "vn 2 0 0\nf 1 2 3\nf 1 4 2\nf 1//1 2//1 3//1\n");
        const float invLen = 1.0f / sqrt(17.0f);
        check("mixed normals: shared vertices use area weighting",
              r.ok && r.mesh.getNumVertices() == 7 && r.mesh.getNumNormals() == 7 &&
              vecNear(r.mesh.getNormal(0), 0, 4 * invLen, invLen) &&
              vecNear(r.mesh.getNormal(1), 0, 4 * invLen, invLen) &&
              vecNear(r.mesh.getNormal(2), 0, 0, 1) &&
              vecNear(r.mesh.getNormal(3), 0, 1, 0));
        check("mixed normals: authored face keeps non-unit normal",
              vecNear(r.mesh.getNormal(4), 2, 0, 0) &&
              vecNear(r.mesh.getNormal(5), 2, 0, 0) &&
              vecNear(r.mesh.getNormal(6), 2, 0, 0));
    }
    {
        auto r = loadObjText(TRI_V + "vn 2 0 0\nf 1//1 2 3//1\n");
        check("mixed within one face: only missing normal computed",
              r.ok && r.mesh.getNumNormals() == 3 &&
              vecNear(r.mesh.getNormal(0), 2, 0, 0) &&
              vecNear(r.mesh.getNormal(1), 0, 0, 1) &&
              vecNear(r.mesh.getNormal(2), 2, 0, 0));
    }
    {
        auto r = loadObjText("v 0 0 0\nv 1 0 0\nv 2 0 0\nvn 0 2 0\nf 1//1 2 3\n");
        check("mixed degenerate face: fallback only for missing normals",
              r.ok && r.mesh.getNumNormals() == 3 &&
              vecNear(r.mesh.getNormal(0), 0, 2, 0) &&
              vecNear(r.mesh.getNormal(1), 0, 0, 1) &&
              vecNear(r.mesh.getNormal(2), 0, 0, 1));
    }
    for (bool texturedFirst : {false, true}) {
        const string missing = "f 1 2 3\n";
        const string textured = "f 2/1 4/2 3/3\n";
        auto r = loadObjText(TRI_V + "v 1 1 0\nvt 0.2 0.3\nvt 0.4 0.5\nvt 0.6 0.7\n" +
                             (texturedFirst ? textured + missing : missing + textured));
        const auto& uv = r.mesh.getTexCoords();
        bool aligned = r.ok && r.mesh.getNumVertices() == 6 && uv.size() == 6;
        if (aligned) {
            for (int i = 0; i < 3; ++i) {
                const auto& absent = uv[(texturedFirst ? 3 : 0) + i];
                const auto& present = uv[(texturedFirst ? 0 : 3) + i];
                aligned &= absent.x == 0 && absent.y == 0 &&
                           fabs(present.x - (0.2f + 0.2f * i)) < 1e-5f &&
                           fabs(present.y - (0.7f - 0.2f * i)) < 1e-5f;
            }
        }
        check("mixed texcoords: zero defaults, authored UVs stay aligned", aligned);
    }
    {
        auto r = loadObjText(TRI_V + "vt 0.25 0.5\nf 1/1 2 3/1\n");
        const auto& uv = r.mesh.getTexCoords();
        check("mixed within one face: only missing UV is zero",
              r.ok && uv.size() == 3 && uv[0].x == 0.25f && uv[0].y == 0.5f &&
              uv[1].x == 0 && uv[1].y == 0 && uv[2].x == 0.25f && uv[2].y == 0.5f);
    }
    {
        auto r = loadObjText(TRI_V + "vn 2 0 0\nf 1//1 2//1 3//1\n");
        check("all authored normals: kept exactly, absent UVs stay absent",
              r.ok && r.mesh.getNumNormals() == 3 && r.mesh.getTexCoords().empty() &&
              vecNear(r.mesh.getNormal(0), 2, 0, 0) &&
              vecNear(r.mesh.getNormal(1), 2, 0, 0) &&
              vecNear(r.mesh.getNormal(2), 2, 0, 0));
    }
    {
        auto r = loadObjText(TRI_V + "vn 2 0 0\nf 1 2 3\n");
        check("unused file normals: all missing normals still computed",
              r.ok && r.mesh.getNumNormals() == 3 &&
              vecNear(r.mesh.getNormal(0), 0, 0, 1) &&
              vecNear(r.mesh.getNormal(1), 0, 0, 1) &&
              vecNear(r.mesh.getNormal(2), 0, 0, 1));
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

    // ----- computed normals are independent of model scale -------------------
    {
        // These triangles meet along the X axis. Their cross products point
        // along +Z and +Y, with areas in a 1:4 ratio. At the shared vertices,
        // area weighting must therefore give normalize(0, 4, 1).
        auto small = loadObjText(
            "v 0 0 0\nv 0.001 0 0\nv 0 0.001 0\nv 0 0 0.004\n"
            "f 1 2 3\nf 1 4 2\n");
        auto large = loadObjText(
            "v 0 0 0\nv 100 0 0\nv 0 100 0\nv 0 0 400\n"
            "f 1 2 3\nf 1 4 2\n");
        check("computed normals: small and large models load",
              small.ok && large.ok && small.warnings == 0 && large.warnings == 0 &&
              small.mesh.getNumVertices() == 4 && large.mesh.getNumVertices() == 4 &&
              small.mesh.getNumIndices() == 6 && large.mesh.getNumIndices() == 6 &&
              small.mesh.getNumNormals() == 4 && large.mesh.getNumNormals() == 4);

        auto correctNormals = [](const Mesh& mesh) {
            if (mesh.getNumNormals() != 4) return false;
            const auto& normals = mesh.getNormals();
            float invLen = 1.0f / sqrt(17.0f);
            return vecNear(normals[0], 0, 4 * invLen, invLen) &&
                   vecNear(normals[1], 0, 4 * invLen, invLen) &&
                   vecNear(normals[2], 0, 0, 1) && vecNear(normals[3], 0, 1, 0);
        };
        check("small model: expected area-weighted normal directions", correctNormals(small.mesh));
        check("large model: expected area-weighted normal directions", correctNormals(large.mesh));

        bool unitLength = small.mesh.getNumNormals() == 4 && large.mesh.getNumNormals() == 4;
        for (const auto* mesh : {&small.mesh, &large.mesh}) {
            for (const auto& n : mesh->getNormals()) {
                unitLength = unitLength && fabs(n.length() - 1.0f) < 1e-5f;
            }
        }
        check("computed normals: unit length at both scales", unitLength);

        bool sameDirections = small.mesh.getNumNormals() == 4 && large.mesh.getNumNormals() == 4;
        if (sameDirections) {
            for (size_t i = 0; i < small.mesh.getNormals().size(); ++i) {
                const auto& n = large.mesh.getNormals()[i];
                sameDirections = sameDirections && vecNear(small.mesh.getNormals()[i], n.x, n.y, n.z);
            }
        }
        check("computed normals: small and large directions agree", sameDirections);
    }
    {
        auto r = loadObjText("v 0 0 0\nv 1 0 0\nv 2 0 0\nf 1 2 3\n");
        bool fallback = r.ok && r.mesh.getNumNormals() == 3;
        for (const auto& n : r.mesh.getNormals()) fallback = fallback && vecNear(n, 0, 0, 1);
        check("degenerate face: zero sum keeps fallback normal", fallback);
    }
    {
        auto r = loadObjText("v 0 0 0\nv 1 0 0\nv 0 0 1\nf 1 2 3\nf 1 3 2\n");
        bool fallback = r.ok && r.mesh.getNumNormals() == 3 && r.mesh.getNumIndices() == 6;
        for (const auto& n : r.mesh.getNormals()) fallback = fallback && vecNear(n, 0, 0, 1);
        check("opposite faces: cancelling sums keep fallback normal", fallback);
    }

#ifdef TCX_OBJ_TEST_GPU
    {
        auto r = loadObjText(TRI_V + "v 1 1 0\nvn 0 0 1\nf 1 2 3\nf 2//1 4//1 3//1\n");
        auto& draws = internal::currentWindowContext().currentTarget->deferredPbrDraws;
        const auto before = draws.size();
        Material material;
        setMaterial(material);
        r.mesh.draw();
        clearMaterial();
        check("issue #437: material draw queues a valid lit PBR draw",
              r.ok && draws.size() == before + 1 &&
              draws.back().cmd.indexCount == 6 &&
              sg_query_pipeline_state(draws.back().cmd.pip) == SG_RESOURCESTATE_VALID);
    }
#endif

    error_code ec;
    fs::remove_all(g_dir, ec);

#ifndef TCX_OBJ_TEST_GPU
    sg_shutdown();
#endif
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}

#ifdef TCX_OBJ_TEST_GPU
static bool g_completed = false;
class ObjTestApp : public App {
    void draw() override {
        if (g_completed) return;
        runTests();
        g_completed = true;
        exitApp();
    }
};

int main() {
    WindowSettings settings;
    settings.setSize(64, 64);
    settings.setTitle("tcxObj lit path test");
    return tcCoreTest::runDisplayTest<ObjTestApp>(settings, g_fail, [] {
        check("GPU tests completed", g_completed);
    });
}
#else
int main() {
    return runTests();
}
#endif
