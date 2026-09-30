// =============================================================================
// tcxGltf tests - headless behavioral test (no window).
//
// Built and run by CI on every push/PR across macOS / Windows / Linux via
// examples/build_all.py --addon-tests-only (exit 0 = pass, non-zero = fail).
//
// Each case writes a small .gltf (buffer embedded as a base64 data URI) to a
// temp directory and loads it with GltfModel. No texture is ever created, so
// nothing needs a graphics context.
//
// It checks that GltfModel validates model data before reading it:
//   - valid models (indexed, non-indexed, node hierarchy, sparse) load as before;
//     sparse values are read tightly packed, also on a strided view
//   - an accessor or buffer view that runs past its buffer view / buffer, or a
//     reference to an accessor / buffer view that does not exist, fails to load
//   - counts large enough to wrap the size arithmetic fail to load, also on
//     an accessor without a buffer view
//   - attribute counts that differ within a primitive fail to load
//   - an index past the primitive's vertices fails to load (caught by
//     cgltf_validate() or, for vertices the loader cannot read, by the loader)
//   - a primitive without POSITION, or whose POSITION or index accessor has
//     no data in memory (no buffer view, or a buffer without data), is
//     skipped with one warning per load; no array is allocated from its count
//   - a sparse accessor with more values than elements fails to load
//   - an image in a buffer without data is skipped
//   - a component type glTF 2.0 does not allow fails validation
//   - a file with no scene loads from its root nodes; with no nodes it fails
//   - a 20000-deep node chain loads; node cycles and repeated scene nodes
//     fail to load
// Every failed load logs a warning and leaves the model empty.
// =============================================================================

#include <tcxGltf.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace std;
using namespace tc;
using tcx::gltf::GltfModel;

static int g_pass = 0, g_fail = 0;
static void check(const string& name, bool ok) {
    printf("%-60s %s\n", name.c_str(), ok ? "PASS" : "FAIL");
    fflush(stdout);  // flush each line so CI logs survive a later crash
    ok ? ++g_pass : ++g_fail;
}

// Counts warnings (and worse) logged while it is alive.
struct WarningCounter {
    int count = 0;
    string last;  // text of the last warning
    EventListener listener;
    WarningCounter() {
        listener = getLogger().onLog.listen([this](LogEventArgs& e) {
            if (e.level >= LogLevel::Warning) {
                count++;
                last = e.message;
            }
        });
    }
};

static string base64(const vector<uint8_t>& in) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    string out;
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        uint32_t v = (in[i] << 16) | (in[i + 1] << 8) | in[i + 2];
        out += T[(v >> 18) & 63]; out += T[(v >> 12) & 63];
        out += T[(v >> 6) & 63];  out += T[v & 63];
    }
    if (i + 1 == in.size()) {
        uint32_t v = in[i] << 16;
        out += T[(v >> 18) & 63]; out += T[(v >> 12) & 63]; out += "==";
    } else if (i + 2 == in.size()) {
        uint32_t v = (in[i] << 16) | (in[i + 1] << 8);
        out += T[(v >> 18) & 63]; out += T[(v >> 12) & 63];
        out += T[(v >> 6) & 63];  out += "=";
    }
    return out;
}

// Builds one glTF document: a single buffer, its buffer views, accessors, and
// one mesh drawn by the scene's nodes.
struct GltfBuilder {
    vector<uint8_t> bin;
    vector<string> views;
    vector<string> accessors;
    string primitive;                    // JSON of the one primitive
    string nodes = R"([{"mesh":0}])";   // empty string = no "nodes" key
    string scenes = R"([{"nodes":[0]}])";  // empty string = no "scenes" key
    long long bufferLength = -1;         // -1 = bin.size()
    vector<string> extraBuffers;         // JSON of buffers 1, 2, ...
    string extra;                        // more top-level members, each ending in ","

    // Append raw bytes (padded to 4) and a buffer view over them.
    int addView(const void* data, size_t size, int byteStride = 0) {
        size_t offset = bin.size();
        const uint8_t* p = static_cast<const uint8_t*>(data);
        bin.insert(bin.end(), p, p + size);
        while (bin.size() % 4) bin.push_back(0);
        return addViewJson(offset, size, byteStride);
    }
    int addViewJson(size_t offset, size_t size, int byteStride = 0) {
        string v = "{\"buffer\":0,\"byteOffset\":" + to_string(offset) +
                   ",\"byteLength\":" + to_string(size);
        if (byteStride) v += ",\"byteStride\":" + to_string(byteStride);
        views.push_back(v + "}");
        return (int)views.size() - 1;
    }
    int addAccessor(const string& json) {
        accessors.push_back(json);
        return (int)accessors.size() - 1;
    }

    string json() const {
        auto join = [](const vector<string>& v) {
            string s;
            for (size_t i = 0; i < v.size(); i++) s += (i ? "," : "") + v[i];
            return s;
        };
        long long len = bufferLength >= 0 ? bufferLength : (long long)bin.size();
        string j = "{\"asset\":{\"version\":\"2.0\"},";
        if (!scenes.empty()) j += "\"scenes\":" + scenes + ",";
        if (!nodes.empty()) j += "\"nodes\":" + nodes + ",";
        j += extra;
        j += "\"meshes\":[{\"primitives\":[" + primitive + "]}],";
        j += "\"accessors\":[" + join(accessors) + "],";
        j += "\"bufferViews\":[" + join(views) + "],";
        j += "\"buffers\":[{\"byteLength\":" + to_string(len) +
             ",\"uri\":\"data:application/octet-stream;base64," + base64(bin) + "\"}";
        for (const auto& eb : extraBuffers) j += "," + eb;
        j += "]}";
        return j;
    }
};

static string accessorJson(int view, int componentType, const string& count,
                           const char* type, const string& extra = "") {
    string a = "{";
    if (view >= 0) a += "\"bufferView\":" + to_string(view) + ",";
    a += "\"componentType\":" + to_string(componentType) + ",\"count\":" + count +
         ",\"type\":\"" + type + "\"" + extra + "}";
    return a;
}

static const int FLOAT = 5126, USHORT = 5123, UINT = 5125;
static const int INT = 5124;  // not a glTF 2.0 component type

// The three vertices of the test triangle, its indices, and one normal each.
static const float POSITIONS[9] = { 0, 0, 0,  1, 0, 0,  0, 1, 0 };
static const uint16_t INDICES[3] = { 0, 1, 2 };
static const float NORMALS[9] = { 0, 0, 1,  0, 0, 1,  0, 0, 1 };

// A builder holding the triangle: accessor 0 = POSITION (view 0), 1 = indices
// (view 1), 2 = NORMAL (view 2). The primitive uses all three.
static GltfBuilder triangle() {
    GltfBuilder b;
    int pv = b.addView(POSITIONS, sizeof(POSITIONS));
    int iv = b.addView(INDICES, sizeof(INDICES));
    int nv = b.addView(NORMALS, sizeof(NORMALS));
    b.addAccessor(accessorJson(pv, FLOAT, "3", "VEC3"));
    b.addAccessor(accessorJson(iv, USHORT, "3", "SCALAR"));
    b.addAccessor(accessorJson(nv, FLOAT, "3", "VEC3"));
    b.primitive = R"({"attributes":{"POSITION":0,"NORMAL":2},"indices":1})";
    return b;
}

static fs::path g_dir;
static int g_fileNo = 0;

static fs::path writeGltf(const GltfBuilder& b) {
    fs::path p = g_dir / ("model" + to_string(g_fileNo++) + ".gltf");
    ofstream(p, ios::binary) << b.json();
    return p;
}

// Load `b` and check the result. When the load should fail, also check that
// it logged a warning and left the model empty.
// `lastWarning` (when given) receives the text of the last warning logged.
static bool loadCase(const string& name, const GltfBuilder& b, bool expectOk, GltfModel& model,
                     string* lastWarning = nullptr) {
    fs::path p = writeGltf(b);
    WarningCounter warnings;
    bool ok = model.load(p.string());
    if (expectOk) {
        check(name + ": loads", ok && model.isLoaded());
    } else {
        check(name + ": rejected", !ok && !model.isLoaded() && model.getNodeCount() == 0);
        check(name + ": warning logged", warnings.count > 0);
    }
    if (lastWarning) *lastWarning = warnings.last;
    return ok;
}

static bool vecNear(const Vec3& v, float x, float y, float z) {
    return fabs(v.x - x) < 1e-5f && fabs(v.y - y) < 1e-5f && fabs(v.z - z) < 1e-5f;
}

int main() {
    g_dir = fs::temp_directory_path() /
            ("tcxGltf-tests-" + to_string(chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(g_dir);

    // A count whose size arithmetic wraps to a small number on 64-bit:
    // 12 * (2^62) and 4 * (2^62) are multiples of 2^64.
    const string WRAP_COUNT = "4611686018427387905";  // 2^62 + 1
    const bool is64 = sizeof(size_t) == 8;

    // ----- valid models load as before ---------------------------------------
    {
        GltfModel m;
        if (loadCase("valid indexed triangle", triangle(), true, m)) {
            const auto& mesh = m.getNode(0).mesh;
            check("valid indexed triangle: one node", m.getNodeCount() == 1);
            check("valid indexed triangle: 3 vertices, 3 indices",
                  mesh.getNumVertices() == 3 && mesh.getNumIndices() == 3);
            check("valid indexed triangle: vertex data",
                  vecNear(mesh.getVertices()[1], 1, 0, 0) && vecNear(mesh.getVertices()[2], 0, 1, 0) &&
                  vecNear(mesh.getNormals()[0], 0, 0, 1));
            check("valid indexed triangle: index data",
                  mesh.getIndices()[0] == 0 && mesh.getIndices()[1] == 1 && mesh.getIndices()[2] == 2);
        }
    }
    {
        GltfBuilder b = triangle();
        b.primitive = R"({"attributes":{"POSITION":0}})";
        GltfModel m;
        if (loadCase("valid non-indexed triangle", b, true, m)) {
            check("valid non-indexed triangle: 3 vertices, no indices",
                  m.getNodeCount() == 1 && m.getNode(0).mesh.getNumVertices() == 3 &&
                  m.getNode(0).mesh.getNumIndices() == 0);
        }
    }
    {
        // Default scene named explicitly, and a child node with a translation
        GltfBuilder b = triangle();
        b.nodes = R"([{"children":[1]},{"mesh":0,"translation":[0,0,2]}])";
        b.scenes = R"([{"nodes":[0]}],"scene":0)";
        GltfModel m;
        if (loadCase("valid node hierarchy", b, true, m)) {
            check("valid node hierarchy: child transform baked",
                  m.getNodeCount() == 1 && vecNear(m.getNode(0).mesh.getVertices()[1], 1, 0, 2));
        }
    }
    {
        // Sparse accessor: base positions with vertex 1 replaced
        GltfBuilder b = triangle();
        const uint16_t sparseIdx[1] = { 1 };
        const float sparseVal[3] = { 5, 5, 5 };
        int siv = b.addView(sparseIdx, sizeof(sparseIdx));
        int svv = b.addView(sparseVal, sizeof(sparseVal));
        b.addAccessor(accessorJson(0, FLOAT, "3", "VEC3",
            ",\"sparse\":{\"count\":1,\"indices\":{\"bufferView\":" + to_string(siv) +
            ",\"componentType\":5123},\"values\":{\"bufferView\":" + to_string(svv) + "}}"));
        b.primitive = R"({"attributes":{"POSITION":3},"indices":1})";
        GltfModel m;
        if (loadCase("valid sparse accessor", b, true, m)) {
            const auto& v = m.getNode(0).mesh.getVertices();
            check("valid sparse accessor: sparse value applied",
                  v.size() == 3 && vecNear(v[0], 0, 0, 0) && vecNear(v[1], 5, 5, 5) && vecNear(v[2], 0, 1, 0));
        }
    }

    // ----- ranges and references ----------------------------------------------
    {
        GltfBuilder b = triangle();
        b.accessors[0] = accessorJson(0, FLOAT, "4", "VEC3");
        b.accessors[2] = accessorJson(2, FLOAT, "4", "VEC3");
        GltfModel m;
        loadCase("accessor count past its buffer view", b, false, m);
    }
    {
        GltfBuilder b = triangle();
        b.accessors[0] = accessorJson(0, FLOAT, "3", "VEC3", ",\"byteOffset\":4");
        GltfModel m;
        loadCase("accessor offset past its buffer view", b, false, m);
    }
    {
        GltfBuilder b = triangle();
        b.addViewJson(b.bin.size() - 8, 16);  // runs 8 bytes past the buffer
        GltfModel m;
        loadCase("buffer view past its buffer", b, false, m);
    }
    {
        GltfBuilder b = triangle();
        b.accessors[1] = accessorJson(7, USHORT, "3", "SCALAR");
        GltfModel m;
        loadCase("reference to a missing buffer view", b, false, m);
    }
    {
        GltfBuilder b = triangle();
        b.primitive = R"({"attributes":{"POSITION":9},"indices":1})";
        GltfModel m;
        loadCase("reference to a missing accessor", b, false, m);
    }
    if (is64) {
        // POSITION count that wraps: its float array cannot be allocated
        GltfBuilder b = triangle();
        b.accessors[0] = accessorJson(0, FLOAT, WRAP_COUNT, "VEC3");
        b.primitive = R"({"attributes":{"POSITION":0}})";
        GltfModel m;
        loadCase("position count that wraps the size arithmetic", b, false, m);
    }
    if (is64) {
        // Index count that wraps: every index would be read
        GltfBuilder b = triangle();
        const uint32_t idx32[1] = { 0 };
        int v = b.addView(idx32, sizeof(idx32));
        b.addAccessor(accessorJson(v, UINT, WRAP_COUNT, "SCALAR"));
        b.primitive = R"({"attributes":{"POSITION":0},"indices":3})";
        GltfModel m;
        loadCase("index count that wraps the size arithmetic", b, false, m);
    }
    if (is64) {
        // Sparse count that wraps
        GltfBuilder b = triangle();
        const uint32_t sparseIdx[1] = { 0 };
        const float sparseVal[3] = { 5, 5, 5 };
        int siv = b.addView(sparseIdx, sizeof(sparseIdx));
        int svv = b.addView(sparseVal, sizeof(sparseVal));
        b.addAccessor(accessorJson(-1, FLOAT, "3", "VEC3",
            ",\"sparse\":{\"count\":" + WRAP_COUNT + ",\"indices\":{\"bufferView\":" + to_string(siv) +
            ",\"componentType\":5125},\"values\":{\"bufferView\":" + to_string(svv) + "}}"));
        b.primitive = R"({"attributes":{"POSITION":3}})";
        GltfModel m;
        loadCase("sparse count that wraps the size arithmetic", b, false, m);
    }
    if (is64) {
        // Sparse accessor without a buffer view whose own count wraps:
        // 3 * 6148914691236517206 = 2^64 + 2. No buffer view bounds the
        // count, and cgltf_validate() only checks sparse indices against it.
        GltfBuilder b = triangle();
        const uint16_t sparseIdx[1] = { 0 };
        const float sparseVal[3] = { 5, 5, 5 };
        int siv = b.addView(sparseIdx, sizeof(sparseIdx));
        int svv = b.addView(sparseVal, sizeof(sparseVal));
        b.addAccessor(accessorJson(-1, FLOAT, "6148914691236517206", "VEC3",
            ",\"sparse\":{\"count\":1,\"indices\":{\"bufferView\":" + to_string(siv) +
            ",\"componentType\":5123},\"values\":{\"bufferView\":" + to_string(svv) + "}}"));
        b.primitive = R"({"attributes":{"POSITION":3}})";
        GltfModel m;
        string warning;
        loadCase("accessor without a buffer view whose count wraps", b, false, m, &warning);
        check("accessor without a buffer view whose count wraps: reported as too large",
              warning.find("too large to address") != string::npos);
    }
    {
        // Sparse accessor on a view with byteStride 16: glTF packs the sparse
        // values tightly (12 bytes apart), not at the base stride. A view
        // after the values makes a read at the base stride land on other data.
        GltfBuilder b = triangle();
        const float strided[12] = { 0, 0, 0, 0,  1, 0, 0, 0,  0, 1, 0, 0 };
        int basev = b.addView(strided, sizeof(strided), 16);
        const uint16_t sparseIdx[2] = { 0, 2 };
        int siv = b.addView(sparseIdx, sizeof(sparseIdx));
        const float sparseVal[6] = { 5, 5, 5,  6, 7, 8 };
        int svv = b.addView(sparseVal, sizeof(sparseVal));
        const float after[4] = { 9, 9, 9, 9 };
        b.addView(after, sizeof(after));
        b.addAccessor(accessorJson(basev, FLOAT, "3", "VEC3",
            ",\"sparse\":{\"count\":2,\"indices\":{\"bufferView\":" + to_string(siv) +
            ",\"componentType\":5123},\"values\":{\"bufferView\":" + to_string(svv) + "}}"));
        b.primitive = R"({"attributes":{"POSITION":3}})";
        GltfModel m;
        if (loadCase("valid sparse accessor on a strided view", b, true, m)) {
            const auto& v = m.getNode(0).mesh.getVertices();
            check("valid sparse accessor on a strided view: values read tightly packed",
                  v.size() == 3 && vecNear(v[0], 5, 5, 5) && vecNear(v[1], 1, 0, 0) && vecNear(v[2], 6, 7, 8));
        }
    }
    {
        // Two VEC3 sparse values need 24 bytes; the values view holds 20
        GltfBuilder b = triangle();
        const uint16_t sparseIdx[2] = { 0, 1 };
        int siv = b.addView(sparseIdx, sizeof(sparseIdx));
        const float sparseVal[6] = { 5, 5, 5,  6, 6, 6 };
        int svv = b.addView(sparseVal, 20);
        b.addAccessor(accessorJson(0, FLOAT, "3", "VEC3",
            ",\"sparse\":{\"count\":2,\"indices\":{\"bufferView\":" + to_string(siv) +
            ",\"componentType\":5123},\"values\":{\"bufferView\":" + to_string(svv) + "}}"));
        b.primitive = R"({"attributes":{"POSITION":3}})";
        GltfModel m;
        loadCase("sparse values past their buffer view", b, false, m);
    }
    {
        // A component type glTF 2.0 does not allow is reported as a
        // validation failure, not as a range problem
        GltfBuilder b = triangle();
        b.addAccessor(accessorJson(2, INT, "3", "VEC3"));
        GltfModel m;
        string warning;
        loadCase("unsupported component type", b, false, m, &warning);
        check("unsupported component type: reported by validation",
              warning.find("failed validation") != string::npos);
    }
    {
        // Same for a sparse indices component type
        GltfBuilder b = triangle();
        const uint16_t sparseIdx[1] = { 1 };
        int siv = b.addView(sparseIdx, sizeof(sparseIdx));
        const float sparseVal[3] = { 5, 5, 5 };
        int svv = b.addView(sparseVal, sizeof(sparseVal));
        b.addAccessor(accessorJson(0, FLOAT, "3", "VEC3",
            ",\"sparse\":{\"count\":1,\"indices\":{\"bufferView\":" + to_string(siv) +
            ",\"componentType\":5124},\"values\":{\"bufferView\":" + to_string(svv) + "}}"));
        GltfModel m;
        string warning;
        loadCase("unsupported sparse index type", b, false, m, &warning);
        check("unsupported sparse index type: reported by validation",
              warning.find("failed validation") != string::npos);
    }

    // ----- mesh consistency ---------------------------------------------------
    {
        GltfBuilder b = triangle();
        b.accessors[2] = accessorJson(2, FLOAT, "2", "VEC3");  // 2 normals, 3 positions
        GltfModel m;
        loadCase("attribute counts differ", b, false, m);
    }
    {
        GltfBuilder b = triangle();
        const uint16_t idx[3] = { 0, 1, 5 };
        int v = b.addView(idx, sizeof(idx));
        b.addAccessor(accessorJson(v, USHORT, "3", "SCALAR"));
        b.primitive = R"({"attributes":{"POSITION":0},"indices":3})";
        GltfModel m;
        loadCase("index past the vertex count", b, false, m);
    }
    {
        // POSITION declared VEC2: cgltf_validate() bounds the indices by the
        // accessor count (3), but only 2 whole vertices come out of 6 floats,
        // so index 2 is caught by the loader
        GltfBuilder b = triangle();
        b.addAccessor(accessorJson(0, FLOAT, "3", "VEC2"));
        b.primitive = R"({"attributes":{"POSITION":3},"indices":1})";
        GltfModel m;
        string warning;
        loadCase("index past the vertices read", b, false, m, &warning);
        check("index past the vertices read: reported by the loader",
              warning.find("points past its vertices") != string::npos);
    }
    {
        // A primitive without POSITION is skipped; the rest of the mesh loads
        GltfBuilder b = triangle();
        b.primitive = R"({"attributes":{"POSITION":0,"NORMAL":2},"indices":1},)"
                      R"({"attributes":{"NORMAL":2},"indices":1})";
        GltfModel m;
        string warning;
        if (loadCase("primitive without positions", b, true, m, &warning)) {
            check("primitive without positions: skipped, the other one loaded",
                  m.getNodeCount() == 1 && m.getNode(0).mesh.getNumVertices() == 3 &&
                  m.getNode(0).mesh.getNumIndices() == 3);
            check("primitive without positions: one warning",
                  warning.find("skipped 1 primitive") != string::npos);
        }
    }
    {
        // Two primitives without POSITION: still one warning, with the count
        GltfBuilder b = triangle();
        b.primitive = R"({"attributes":{"NORMAL":2}},)"
                      R"({"attributes":{"POSITION":0,"NORMAL":2},"indices":1},)"
                      R"({"attributes":{"NORMAL":2},"indices":1})";
        GltfModel m;
        WarningCounter warnings;
        bool ok = m.load(writeGltf(b).string());
        check("two primitives without positions: loads, one node",
              ok && m.isLoaded() && m.getNodeCount() == 1);
        check("two primitives without positions: one warning with the count",
              warnings.count == 1 && warnings.last.find("skipped 2 primitive") != string::npos);
    }
    {
        // POSITION accessor without a buffer view has no vertex data of its
        // own: the primitive is skipped like one without POSITION
        GltfBuilder b = triangle();
        b.addAccessor(accessorJson(-1, FLOAT, "3", "VEC3"));
        b.primitive = R"({"attributes":{"POSITION":3}},)"
                      R"({"attributes":{"POSITION":0,"NORMAL":2},"indices":1})";
        GltfModel m;
        string warning;
        if (loadCase("position accessor without a buffer view", b, true, m, &warning)) {
            check("position accessor without a buffer view: skipped, the other one loaded",
                  m.getNodeCount() == 1 && m.getNode(0).mesh.getNumVertices() == 3);
            check("position accessor without a buffer view: warning logged",
                  warning.find("skipped 1 primitive") != string::npos);
        }
    }
    {
        // An index accessor without a buffer view has no data to bound its
        // count: the primitive is skipped before any index array is
        // allocated. The count does not wrap the size arithmetic, but no
        // allocation of it could succeed (2^62 - 1 on 64-bit, about 4 GB on
        // 32-bit)
        GltfBuilder b = triangle();
        b.addAccessor(accessorJson(-1, UINT, is64 ? "4611686018427387903" : "1073741823",
                                   "SCALAR"));
        b.primitive = R"({"attributes":{"POSITION":0},"indices":3},)"
                      R"({"attributes":{"POSITION":0,"NORMAL":2},"indices":1})";
        GltfModel m;
        string warning;
        if (loadCase("index accessor without a buffer view", b, true, m, &warning)) {
            check("index accessor without a buffer view: skipped, the other one loaded",
                  m.getNodeCount() == 1 && m.getNode(0).mesh.getNumIndices() == 3);
            check("index accessor without a buffer view: warning logged",
                  warning.find("skipped 1 primitive") != string::npos);
        }
    }
    {
        // POSITION and index accessors on a buffer without a uri: the views
        // lie inside the declared byteLength, but the buffer has no data
        GltfBuilder b = triangle();
        b.extraBuffers.push_back(R"({"byteLength":64})");
        b.views.push_back(R"({"buffer":1,"byteOffset":0,"byteLength":36})");
        int noDataView = (int)b.views.size() - 1;
        b.addAccessor(accessorJson(noDataView, FLOAT, "3", "VEC3"));   // 3
        b.addAccessor(accessorJson(noDataView, USHORT, "3", "SCALAR"));  // 4
        b.primitive = R"({"attributes":{"POSITION":3}},)"
                      R"({"attributes":{"POSITION":0},"indices":4},)"
                      R"({"attributes":{"POSITION":0,"NORMAL":2},"indices":1})";
        GltfModel m;
        string warning;
        if (loadCase("accessors on a buffer without data", b, true, m, &warning)) {
            check("accessors on a buffer without data: both skipped, the other one loaded",
                  m.getNodeCount() == 1 && m.getNode(0).mesh.getNumVertices() == 3 &&
                  m.getNode(0).mesh.getNumIndices() == 3);
            check("accessors on a buffer without data: one warning with the count",
                  warning.find("skipped 2 primitive") != string::npos);
        }
    }
    {
        // Four sparse values for an accessor of three elements. The sparse
        // indices stay below the count, so cgltf_validate() accepts them
        GltfBuilder b = triangle();
        const uint16_t sparseIdx[4] = { 0, 1, 2, 2 };
        int siv = b.addView(sparseIdx, sizeof(sparseIdx));
        const float sparseVal[12] = { 5, 5, 5,  6, 6, 6,  7, 7, 7,  8, 8, 8 };
        int svv = b.addView(sparseVal, sizeof(sparseVal));
        b.addAccessor(accessorJson(0, FLOAT, "3", "VEC3",
            ",\"sparse\":{\"count\":4,\"indices\":{\"bufferView\":" + to_string(siv) +
            ",\"componentType\":5123},\"values\":{\"bufferView\":" + to_string(svv) + "}}"));
        b.primitive = R"({"attributes":{"POSITION":3}})";
        GltfModel m;
        loadCase("more sparse values than elements", b, false, m);
    }

    // ----- textures -------------------------------------------------------------
    {
        // The image lives in a buffer without a uri, so the buffer has no
        // data. The texture is skipped; the mesh loads.
        GltfBuilder b = triangle();
        b.extraBuffers.push_back(R"({"byteLength":16})");
        b.views.push_back(R"({"buffer":1,"byteOffset":0,"byteLength":16})");
        int imgView = (int)b.views.size() - 1;
        b.extra = "\"images\":[{\"bufferView\":" + to_string(imgView) +
                  ",\"mimeType\":\"image/png\"}],"
                  "\"textures\":[{\"source\":0}],"
                  "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0}}}],";
        b.primitive = R"({"attributes":{"POSITION":0,"NORMAL":2},"indices":1,"material":0})";
        GltfModel m;
        if (loadCase("image in a buffer without data", b, true, m)) {
            check("image in a buffer without data: texture skipped",
                  m.getNodeCount() == 1 && !m.getNode(0).material.hasBaseColorTexture());
        }
    }

    // ----- scenes and node hierarchy ---------------------------------------------
    {
        // No scenes: every node without a parent is a root. Node 1 is a
        // child of node 0, so it is visited once, through node 0.
        GltfBuilder b = triangle();
        b.scenes = "";
        b.nodes = R"([{"children":[1]},{"mesh":0,"translation":[0,0,2]}])";
        GltfModel m;
        if (loadCase("no scene", b, true, m)) {
            check("no scene: loaded from the root node",
                  m.getNodeCount() == 1 && vecNear(m.getNode(0).mesh.getVertices()[1], 1, 0, 2));
        }
    }
    {
        GltfBuilder b = triangle();
        b.scenes = "";
        b.nodes = "";
        GltfModel m;
        loadCase("no scene and no nodes", b, false, m);
    }
    {
        // A chain 20000 nodes deep, each moved 1 along z: loads without
        // exhausting the call stack, with the transforms accumulated.
        // The depth is kept this low because cgltf_validate()'s parent-cycle
        // check is O(nodes * depth), so 100000 took about 30 s. 20000 still
        // overflows a recursive walk: one crashed at about 9000 levels in a
        // Release build with Linux's default 8 MB stack (under 2000 with
        // ASan), and Windows' default 1 MB stack gives out sooner still
        const int DEPTH = 20000;
        GltfBuilder b = triangle();
        string nodes = "[";
        for (int i = 0; i < DEPTH - 1; i++) {
            nodes += "{\"children\":[" + to_string(i + 1) + "],\"translation\":[0,0,1]},";
        }
        nodes += R"({"mesh":0,"translation":[0,0,1]}])";
        b.nodes = nodes;
        GltfModel m;
        if (loadCase("deep node hierarchy", b, true, m)) {
            check("deep node hierarchy: transforms accumulated",
                  m.getNodeCount() == 1 &&
                  vecNear(m.getNode(0).mesh.getVertices()[1], 1, 0, (float)DEPTH));
        }
    }
    {
        // Two nodes that are each other's child. No node is a root; the
        // cycle is refused by cgltf_validate()
        GltfBuilder b = triangle();
        b.scenes = "";
        b.nodes = R"([{"children":[1]},{"children":[0],"mesh":0}])";
        GltfModel m;
        string warning;
        loadCase("node cycle", b, false, m, &warning);
        check("node cycle: reported by validation",
              warning.find("failed validation") != string::npos);
    }
    {
        // A node that is its own child
        GltfBuilder b = triangle();
        b.scenes = "";
        b.nodes = R"([{"children":[0],"mesh":0}])";
        GltfModel m;
        loadCase("node that is its own child", b, false, m);
    }
    {
        // A scene that lists the same node twice (glTF requires unique
        // entries) is refused by the loader's walk
        GltfBuilder b = triangle();
        b.scenes = R"([{"nodes":[0,0]}])";
        GltfModel m;
        string warning;
        loadCase("scene lists a node twice", b, false, m, &warning);
        check("scene lists a node twice: reported by the loader",
              warning.find("more than once") != string::npos);
    }

    // ----- a failed load after a good one leaves the model empty ---------------
    {
        GltfModel m;
        bool first = m.load(writeGltf(triangle()).string());
        GltfBuilder bad = triangle();
        bad.accessors[0] = accessorJson(0, FLOAT, "4", "VEC3");
        bool second = m.load(writeGltf(bad).string());
        check("reload: good then rejected leaves the model empty",
              first && !second && !m.isLoaded() && m.getNodeCount() == 0);
    }

    error_code ec;
    fs::remove_all(g_dir, ec);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
