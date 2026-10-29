// =============================================================================
// tcxGltf tests - headless behavioral test (no window).
//
// Built and run by CI on every push/PR across macOS / Windows / Linux via
// examples/build_all.py --addon-tests-only (exit 0 = pass, non-zero = fail).
//
// Each case writes a small .gltf (buffer embedded as a base64 data URI) or
// .glb (JSON and BIN chunks) to a temp directory and loads it with GltfModel.
// Texture cases use the dummy GPU backend; no window or device is needed.
//
// It checks that GltfModel validates model data before reading it:
//   - valid models (indexed, non-indexed, node hierarchy, sparse, GLB) load
//     as before; sparse values are read tightly packed, also on a strided view
//   - an accessor or buffer view that runs past its buffer view / buffer (or
//     a GLB's BIN chunk), or a reference to an accessor / buffer view that
//     does not exist, fails to load
//   - a count too large to address fails to load, also on an accessor
//     without a buffer view
//   - a buffer view byteStride smaller than the accessor's element fails to
//     load
//   - attribute counts that differ within a primitive fail to load
//   - an index past the primitive's vertices fails to load (caught by
//     cgltf_validate() or, for vertices the loader cannot read, by the loader)
//   - a primitive without POSITION, or whose POSITION or index accessor has
//     no data in memory (no buffer view, or a buffer without data), is
//     skipped with one warning per load; no array is allocated from its count
//   - a sparse accessor with more values than elements, or whose indices
//     are not strictly increasing, fails to load
//   - an image in a buffer without data is skipped
//   - an external image that cannot be loaded, or whose uri is not valid
//     UTF-8, is skipped with a warning; the mesh loads
//   - an exception while the model is read (injected through the texture
//     load test hook) fails the load and leaves the model empty
//   - a component type glTF 2.0 does not allow fails validation
//   - a file with no scene loads from its root nodes; with no nodes it fails
//   - a 20000-deep node chain loads without recursion; node cycles and
//     repeated scene nodes fail to load
// Failed loads leave the model empty; allocation-failure diagnostics are best effort.
// =============================================================================

#include <tcxGltf.h>
#include "../../../../core/tests/common/tcModelAllocationProbe.h"

namespace tcx::gltf::internal {
bool parserCallocOverflowForTests();
void setAllocationHookForTests(bool (*hook)(size_t));
bool jsonOffsetsFit(size_t bytes);
bool jsonObjectMembersFit(size_t members);
bool jsonSkipFits(size_t tokens, size_t members);
size_t liveParserAllocationsForTests();
void setParserAllocatorForTests(void* (*allocate)(size_t), void (*release)(void*) noexcept);
size_t parserTableBytesForTests();
void setLargeParseTableNoticeBytesForTests(size_t bytes);
void setJsonPaddingForTests(size_t bytes);
void setJsonObjectMembersForTests(size_t members);
bool jsonTokenFits(size_t count, size_t length, bool stringToken);
}

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;
using namespace tc;
using tcx::gltf::GltfModel;
namespace gltf_internal = tcx::gltf::internal;

static size_t outstandingParserBytes = 0;
static int g_pass = 0, g_fail = 0;
static void check(const string& name, bool ok) {
    ok = ok && gltf_internal::liveParserAllocationsForTests() == 0 && outstandingParserBytes == 0;
    printf("%-60s %s\n", name.c_str(), ok ? "PASS" : "FAIL");
    fflush(stdout);  // flush each line so CI logs survive a later crash
    ok ? ++g_pass : ++g_fail;
}

// Counts warnings (and worse) logged while it is alive.
struct WarningCounter {
    int count = 0;
    int errors = 0;
    string last;  // text of the last warning
    EventListener listener;
    WarningCounter() {
        listener = getLogger().onLog.listen([this](LogEventArgs& e) {
            if (e.level >= LogLevel::Warning) {
                count++;
                if (e.level == LogLevel::Error) errors++;
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
    bool meshesLast = false;             // put mapping allocation last before fixup

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

    // The glTF JSON. With `embedUri` false, buffer 0 has no uri (a GLB's
    // BIN chunk supplies its data).
    string json(bool embedUri = true) const {
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
        const string meshTable = "\"meshes\":[{\"primitives\":[" + primitive + "]}]";
        if (!meshesLast) j += meshTable + ",";
        j += "\"accessors\":[" + join(accessors) + "],";
        j += "\"bufferViews\":[" + join(views) + "],";
        j += "\"buffers\":[{\"byteLength\":" + to_string(len);
        if (embedUri) j += ",\"uri\":\"data:application/octet-stream;base64," + base64(bin) + "\"";
        j += "}";
        for (const auto& eb : extraBuffers) j += "," + eb;
        j += "]";
        if (meshesLast) j += "," + meshTable;
        j += "}";
        return j;
    }

    // The same document as a GLB: header, JSON chunk, BIN chunk holding `bin`.
    vector<uint8_t> glb() const {
        string j = json(false);
        while (j.size() % 4) j += ' ';
        vector<uint8_t> chunkBin = bin;
        while (chunkBin.size() % 4) chunkBin.push_back(0);
        vector<uint8_t> out;
        auto u32 = [&out](uint32_t v) {
            for (int i = 0; i < 4; i++) out.push_back((uint8_t)(v >> (8 * i)));
        };
        u32(0x46546C67);  // "glTF"
        u32(2);
        u32((uint32_t)(12 + 8 + j.size() + 8 + chunkBin.size()));
        u32((uint32_t)j.size());
        u32(0x4E4F534A);  // "JSON"
        out.insert(out.end(), j.begin(), j.end());
        u32((uint32_t)chunkBin.size());
        u32(0x004E4942);  // "BIN\0"
        out.insert(out.end(), chunkBin.begin(), chunkBin.end());
        return out;
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

static const int CT_FLOAT = 5126, CT_USHORT = 5123, CT_UINT = 5125;
static const int CT_INT = 5124;  // not a glTF 2.0 component type

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
    b.addAccessor(accessorJson(pv, CT_FLOAT, "3", "VEC3"));
    b.addAccessor(accessorJson(iv, CT_USHORT, "3", "SCALAR"));
    b.addAccessor(accessorJson(nv, CT_FLOAT, "3", "VEC3"));
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

static fs::path writeGlb(const GltfBuilder& b) {
    fs::path p = g_dir / ("model" + to_string(g_fileNo++) + ".glb");
    vector<uint8_t> bytes = b.glb();
    ofstream(p, ios::binary).write((const char*)bytes.data(), (streamsize)bytes.size());
    return p;
}

// Load `b` and check the result. When the load should fail, also check that
// it logged exactly one Error and left the model empty.
// `lastWarning` (when given) receives the text of the last warning logged.
// With `asGlb`, `b` is written as a .glb instead of a .gltf.
static bool loadCase(const string& name, const GltfBuilder& b, bool expectOk, GltfModel& model,
                     string* lastWarning = nullptr, bool asGlb = false) {
    fs::path p = asGlb ? writeGlb(b) : writeGltf(b);
    WarningCounter warnings;
    auto result = model.load(p);
    bool ok = result.ok();
    if (expectOk) {
        check(name + ": loads", ok && model.isLoaded());
    } else {
        check(name + ": rejected", !ok && !model.isLoaded() && model.getNodeCount() == 0);
        check(name + ": DecodeFailed, exactly one Error",
              result.error == LoadError::DecodeFailed && warnings.errors == 1 &&
              warnings.last == "[GltfModel] " + result.message);
    }
    if (lastWarning) *lastWarning = warnings.last;
    return ok;
}

// A triangle with a material whose base color texture is `imageJson` (the
// JSON of image 0).
static GltfBuilder texturedTriangle(const string& imageJson) {
    GltfBuilder b = triangle();
    b.extra = "\"images\":[" + imageJson + "],"
              "\"textures\":[{\"source\":0}],"
              "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0}}}],";
    b.primitive = R"({"attributes":{"POSITION":0,"NORMAL":2},"indices":1,"material":0})";
    return b;
}

static size_t parserAllocationCount = 0, rejectParserAllocation = 0;
static bool parserFaultPersistent = false;
static int parserFaultKind = 0;
static bool observeParserAllocation(size_t bytes) {
    ++parserAllocationCount;
    if (!modelAllocationProbe::observe(bytes)) return false;
    if (!rejectParserAllocation || parserAllocationCount < rejectParserAllocation ||
        (!parserFaultPersistent && parserAllocationCount != rejectParserAllocation)) return true;
    if (parserFaultKind == 1) throw bad_alloc();
    if (parserFaultKind == 2) throw 42;
    return false;
}

// Observe actual malloc/free calls independently of registry bookkeeping.
struct ParserBlock { void* pointer; size_t bytes; };
// The deep-node fixture alone has 20000 child tables. This is observer
// capacity for these fixed tests, independent of production model admission.
static ParserBlock parserBlocks[65536]{};
static size_t actualParserAllocations = 0;
static size_t grantedParserBytes = 0, peakParserBytes = 0, largestParserBlock = 0;
static void* trackParserMalloc(size_t bytes) {
    void* pointer = std::malloc(bytes);
    if (!pointer) return nullptr;
    for (auto& block : parserBlocks) if (!block.pointer) {
        block = {pointer, bytes};
        outstandingParserBytes += bytes;
        ++actualParserAllocations;
        grantedParserBytes += bytes;
        peakParserBytes = (std::max)(peakParserBytes, outstandingParserBytes);
        largestParserBlock = (std::max)(largestParserBlock, bytes);
        return pointer;
    }
    std::abort(); // Test tracker capacity, never a model acceptance limit.
}
static void trackParserFree(void* pointer) noexcept {
    for (auto& block : parserBlocks) if (block.pointer == pointer) {
        outstandingParserBytes -= block.bytes;
        block = {};
        std::free(pointer);
        return;
    }
    std::abort(); // A double free or untracked pointer is a test failure.
}

static void throwRuntimeError() { throw runtime_error("injected failure"); }
static void throwInt() { throw 42; }

static bool vecNear(const Vec3& v, float x, float y, float z) {
    return fabs(v.x - x) < 1e-5f && fabs(v.y - y) < 1e-5f && fabs(v.z - z) < 1e-5f;
}

int main() {
    sg_desc graphics = {};
    sg_setup(&graphics);
    g_dir = fs::temp_directory_path() /
            utf8ToPath("tcxGltf-テスト-" + to_string(chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(g_dir);

    gltf_internal::setParserAllocatorForTests(trackParserMalloc, trackParserFree);

    {
        // Model fixtures exercise stb too. Fail the growing encoder output
        // buffer, after path preparation, without relying on filebuf storage.
        Pixels pixels;
        pixels.allocate(128, 128, 4);
        uint32_t noise = 1;
        for (size_t i = 0; i < 128 * 128 * 4; ++i) {
            noise = noise * 1664525u + 1013904223u;
            pixels.getData()[i] = static_cast<unsigned char>(noise >> 24);
        }
        for (const string& ext : {string("png"), string("bmp"), string("jpg")}) {
            auto path = g_dir / ("encoder-fault." + ext);
            for (bool persistent : {false, true}) {
                if (persistent && tcTest::skipPersistentAllocationFailure("stb encoder output")) continue;
                ofstream(path, ios::binary) << "previous";
                SaveResult result;
                modelAllocationProbe::begin(4096, persistent);
                bool escaped = false;
                try { result = pixels.save(path); } catch (...) { escaped = true; }
                modelAllocationProbe::end();
                check("stb " + ext + (persistent ? " persistent" : " single") + " callback failure returns after C",
                      modelAllocationProbe::failed && !escaped && !result && result.error == SaveError::WriteFailed);
                ifstream saved(path, ios::binary);
                string content((istreambuf_iterator<char>(saved)), {});
                check("stb failure preserves existing " + ext, content == "previous");
            }
        }
        for (const string& ext : {string("png"), string("bmp"), string("tga"), string("jpg")}) {
            const auto path = g_dir / "read-only-encoder-output";
            ofstream(path, ios::binary) << "previous";
            for (bool persistent : {false, true}) {
                if (persistent && tcTest::skipPersistentAllocationFailure("stb FILE error callback")) continue;
                // A diagnostic-only long path targets message payload growth,
                // after Sink/result proxy construction, on every STL. It is
                // never opened on disk. Do not assume an allocation index.
                const auto diagnosticPath = path / string(8192, 'x');
                FILE* file = tc::internal::openFile(path, "rb");
                SaveResult result;
                bool escaped = false;
                if (file) {
                    modelAllocationProbe::begin(diagnosticPath.native().size(), persistent);
                    try { result = tc::internal::writePixelsEncoded(file, pixels, ext, diagnosticPath); }
                    catch (...) { escaped = true; }
                    modelAllocationProbe::end();
                    fclose(file);
                }
                check("stb FILE " + ext + (persistent ? " persistent" : " single") + " error callback contains exception",
                      file && modelAllocationProbe::failed && !escaped && !result && result.error == SaveError::WriteFailed);
            }
        }
    }

    {
        fs::path oldRoot = getDataPathRoot();
        setDataPathRoot(g_dir);
        GltfModel model;
        WarningCounter logs;
        auto result = model.load(string("missing.gltf"));
        string expected = "file not found: " + pathToUtf8(g_dir / "missing.gltf");
        check("missing glTF: FileNotFound and exactly one shared Error",
              result.error == LoadError::FileNotFound && result.message == expected &&
              logs.errors == 1 && logs.count == 1 && logs.last == "[GltfModel] " + expected);
        logs.errors = logs.count = 0;
        ofstream(g_dir / "broken.gltf", ios::binary) << "not JSON";
        result = model.load("broken.gltf");
        check("broken glTF: DecodeFailed and exactly one Error",
              result.error == LoadError::DecodeFailed && !result.message.empty() &&
              logs.errors == 1 && logs.count == 1 && logs.last == "[GltfModel] " + result.message &&
              !model.isLoaded() && model.getNodeCount() == 0);
        setDataPathRoot(oldRoot);
    }

    {
        const size_t maxInt = static_cast<size_t>(INT_MAX);
        check("cgltf calloc overflow records failure without invoking allocator",
              gltf_internal::parserCallocOverflowForTests());
        check("JSON offsets use ptrdiff_t rather than int",
              gltf_internal::jsonOffsetsFit(static_cast<size_t>(PTRDIFF_MAX)) &&
              !gltf_internal::jsonOffsetsFit(static_cast<size_t>(PTRDIFF_MAX) + 1));
        if (PTRDIFF_MAX > INT_MAX)
            check("JSON bytes above INT_MAX are representable", gltf_internal::jsonOffsetsFit(maxInt + 4096));
        check("JSON token count fits int and sentinel allocation",
              gltf_internal::jsonTokenFits(1, 0, false) &&
              !gltf_internal::jsonTokenFits(maxInt + 1, 0, false));
        if (sizeof(size_t) > sizeof(int))
            check("JSON token count at int boundary fits on wide hosts",
                  gltf_internal::jsonTokenFits(maxInt, 0, false));
        check("JSON string length leaves room for int terminator addition",
              gltf_internal::jsonTokenFits(1, maxInt - 1, true) &&
              !gltf_internal::jsonTokenFits(1, maxInt, true));
        check("JSON primitive token length fits int",
              gltf_internal::jsonTokenFits(1, maxInt, false) &&
              !gltf_internal::jsonTokenFits(1, maxInt + 1, false));
    }

    {
        auto path = writeGltf(triangle());
        ofstream(path, ios::binary | ios::app).write("\0\0\0", 3);
        GltfModel model;
        check("glTF trailing NULs match parser end of input", model.load(path).ok() && model.getNodeCount() == 1);
        auto b = triangle();
        while (b.json(false).size() % 4 == 0) b.extra += ' ';
        auto bytes = b.glb();
        uint32_t length;
        memcpy(&length, bytes.data() + 12, 4);
        size_t padding = 0;
        for (size_t i = 20 + length; i > 20 && bytes[i - 1] == ' '; --i) {
            bytes[i - 1] = 0;
            ++padding;
        }
        path = g_dir / "nulpad.glb";
        ofstream(path, ios::binary).write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        check("GLB NUL JSON padding matches parser end of input",
              padding > 0 && model.load(path).ok() && model.getNodeCount() == 1);
        if (PTRDIFF_MAX > INT_MAX) {
            path = writeGltf(triangle());
            // The size gate sees virtual trailing whitespace, while scanning
            // and cgltf still receive the small physical document.
            gltf_internal::setJsonPaddingForTests(static_cast<size_t>(INT_MAX));
            auto result = model.load(path);
            gltf_internal::setJsonPaddingForTests(0);
            check("load admits JSON document size beyond INT_MAX", result.ok() && model.getNodeCount() == 1);
        }
        const size_t maxMembers = static_cast<size_t>(INT_MAX) / 2;
        check("JSON object member product fits signed skip arithmetic",
              gltf_internal::jsonObjectMembersFit(maxMembers) &&
              !gltf_internal::jsonObjectMembersFit(maxMembers + 1));
        check("JSON cumulative skip end includes start and member additions",
              gltf_internal::jsonSkipFits(maxMembers + 1, maxMembers) &&
              !gltf_internal::jsonSkipFits(maxMembers + 3, maxMembers) &&
              !gltf_internal::jsonSkipFits(3, SIZE_MAX));
        path = writeGltf(triangle());
        gltf_internal::setJsonObjectMembersForTests(maxMembers);
        auto result = model.load(path);
        gltf_internal::setJsonObjectMembersForTests(0);
        check("load rejects object member product overflow", !result && !model.isLoaded() && model.getNodeCount() == 0);
        // Each container is below the old product limit, but cumulative skip
        // accounting cannot be represented. No huge JSON/token fixture needed.
        gltf_internal::setJsonObjectMembersForTests(maxMembers - 100);
        result = model.load(path);
        gltf_internal::setJsonObjectMembersForTests(0);
        check("load rejects cumulative JSON skip end overflow",
              !result && !model.isLoaded() && model.getNodeCount() == 0);
    }
    for (int kind = 0; kind < 5; ++kind) {
        auto b = triangle();
        if (kind == 0) b.views[0] = R"({"buffer":0,"byteOffset":2,"byteLength":36})";
        if (kind == 1) b.views[0] = R"({"buffer":0,"byteOffset":0,"byteLength":36,"byteStride":14})";
        if (kind == 2) b.accessors[0] = accessorJson(0, CT_FLOAT, "2", "VEC3", R"(,"byteOffset":2)");
        if (kind >= 3) {
            const uint16_t index = 0;
            const float value[3] = {0, 0, 0};
            int iv = b.addView(&index, sizeof(index));
            int vv = b.addView(value, sizeof(value));
            b.accessors[0] = accessorJson(-1, CT_FLOAT, "3", "VEC3",
                ",\"sparse\":{\"count\":1,\"indices\":{\"bufferView\":" + to_string(iv) +
                ",\"componentType\":5123,\"byteOffset\":" + (kind == 3 ? "1" : "0") +
                "},\"values\":{\"bufferView\":" + to_string(vv) +
                ",\"byteOffset\":" + (kind == 4 ? "2" : "0") + "}}");
        }
        GltfModel model;
        auto result = model.load(writeGltf(b));
        check("misaligned accessor reports alignment cause " + to_string(kind),
              !result && result.message.find("align") != string::npos && model.getNodeCount() == 0);
    }
    {
        // Exercise both legal unpadded tails through the actual load path.
        for (size_t tail : {size_t(1), size_t(2)}) {
            auto b = triangle();
            while (b.bin.size() % 3 != tail) b.bin.push_back(0);
            string json = b.json();
            size_t end = json.find('"', json.find(";base64,") + 8);
            while (json[end - 1] == '=') { json.erase(end - 1, 1); --end; }
            auto path = g_dir / "unpadded.gltf";
            ofstream(path, ios::binary) << json;
            GltfModel model;
            check("unpadded base64 loads tail " + to_string(tail),
                  model.load(path).ok() && model.getNodeCount() == 1 &&
                  model.getNode(0).mesh.getNumVertices() == 3);
            // One residual character cannot supply a complete decoded byte.
            json.replace(json.find(";base64,") + 8, end - (json.find(";base64,") + 8), "A");
            ofstream(path, ios::binary) << json;
            check("one-character base64 tail rejects the whole model",
                  !model.load(path) && !model.isLoaded() && model.getNodeCount() == 0);
        }
    }
    {
        const auto valid = g_dir / "reentrant-valid.gltf";
        const auto invalid = g_dir / "reentrant-invalid.gltf";
        ofstream(valid) << R"({"asset":{"version":"2.0"},"nodes":[{}],"scenes":[{"nodes":[0]}],"scene":0})";
        ofstream(invalid) << R"({"asset":{"version":"2.0"},"nodes":[{}],"scenes":[{"nodes":[0]}],"scene":2147483648})";
        // Exercise the production Notice with tiny fixtures. This seam is
        // implementation-only; each context captures its own threshold.
        gltf_internal::setLargeParseTableNoticeBytesForTests(0);
        for (bool malformedOuter : {true, false}) {
            GltfModel outer;
            GltfModel inner;
            bool entered = false;
            bool innerOk = false;
            size_t outerNotices = 0;
            auto listener = getLogger().onLog.listen([&](LogEventArgs& event) {
                if (event.message.find("large local parse tables") == string::npos || entered) return;
                entered = true;
                ++outerNotices;
                innerOk = inner.load(malformedOuter ? valid : invalid).ok();
            });
            const auto result = outer.load(malformedOuter ? invalid : valid);
            check(malformedOuter ? "reentrant valid inner preserves outer rejection" :
                                   "reentrant invalid inner preserves outer success",
                  entered && outerNotices == 1 && innerOk == malformedOuter &&
                  inner.isLoaded() == malformedOuter && result.ok() == !malformedOuter &&
                  outer.isLoaded() == !malformedOuter &&
                  (malformedOuter ? result.error == LoadError::DecodeFailed : result.ok()));
        }
        gltf_internal::setLargeParseTableNoticeBytesForTests(size_t(256) * 1024 * 1024);
    }
    {
        constexpr size_t MiB = 1024 * 1024;
        auto materialJson = [](size_t count, size_t padding) {
            string json = R"({"asset":{"version":"2.0"},"nodes":[{}],"materials":[)";
            string material = "{}" + string(padding, ' ');
            json.reserve(count * (material.size() + 1) + 100);
            for (size_t i = 0; i < count; ++i) { if (i) json += ','; json += material; }
            return json + "]}";
        };
        auto path = g_dir / utf8ToPath("parse-memory-大.gltf");
        // Over 5 MB JSON, with enough real parse tables to exceed both the
        // Notice threshold and the former input-scaled rejection threshold.
        string json = materialJson(400000, 10);
        ofstream(path, ios::binary) << json;
        GltfModel model;
        WarningCounter logs;
        size_t notices = 0;
        string notice;
        const string prefix = "[GltfModel] large local parse tables for " + pathToUtf8(path) + ": ";
        auto listener = getLogger().onLog.listen([&](LogEventArgs& e) {
            if (e.level == LogLevel::Notice && e.message.find("large local parse tables") != string::npos) {
                ++notices;
                notice = e.message;
            }
        });
        for (int attempt = 0; attempt < 2; ++attempt) {
            notices = 0;
            notice.clear();
            actualParserAllocations = grantedParserBytes = peakParserBytes = largestParserBlock = 0;
            auto result = model.load(path);
            size_t tables = gltf_internal::parserTableBytesForTests();
            printf("large local parse tables: input=%zu tables=%zu malloc-peak=%zu\n",
                   json.size(), tables, peakParserBytes);
            check("large local parse tables load with one Notice per load " + to_string(attempt),
                  result.ok() && model.isLoaded() &&
                  logs.count == 0 && notices == 1 &&
                  notice == prefix + to_string(tables) + " bytes" && tables > 256 * MiB &&
                  json.size() >= 5000000 && tables > 64 * json.size());
            check("large parse tables obtain real memory and release every block",
                  actualParserAllocations > 1 && grantedParserBytes > tables &&
                  peakParserBytes > 256 * MiB && outstandingParserBytes == 0 &&
                  gltf_internal::liveParserAllocationsForTests() == 0);
        }
        json = materialJson(60000, 197); // ~12 MB JSON, ~83 MB tables.
        ofstream(path, ios::binary) << json;
        notices = 0;
        auto result = model.load(path);
        size_t tables = gltf_internal::parserTableBytesForTests();
        check("ordinary parse loads without a large-table Notice",
              result.ok() && model.isLoaded() && logs.count == 0 && notices == 0 &&
              json.size() >= 12000000 && tables > 60 * MiB && tables < 90 * MiB);
        check("parse table loads free parser storage",
              gltf_internal::liveParserAllocationsForTests() == 0 && outstandingParserBytes == 0);
    }
    gltf_internal::setAllocationHookForTests(&observeParserAllocation);
    {
        GltfBuilder b = triangle();
        b.bufferLength = 1000000000;
        auto path = writeGltf(b);
        GltfModel model;
        modelAllocationProbe::begin();
        auto result = model.load(path);
        modelAllocationProbe::end();
        check("inline buffer length checked before allocation", !result && !model.isLoaded() &&
              model.getNodeCount() == 0 && modelAllocationProbe::largest < 65536);
    }
    {
        GltfBuilder b = triangle();
        b.extra = "\"unused\":" + string(65, '[') + "0" + string(65, ']') + ",";
        GltfModel model;
        loadCase("nested JSON exceeds parser depth", b, false, model);
        b.extra = "\"unused\":" + string(63, '[') + "0" + string(63, ']') + ",";
        loadCase("JSON at parser depth boundary", b, true, model);
    }
    {
        GltfBuilder b = triangle();
        b.accessors[0] = accessorJson(0, CT_FLOAT, "18446744073709551616", "VEC3");
        GltfModel model;
        loadCase("integer count outside parser width", b, false, model);
        b = triangle();
        b.primitive = R"({"attributes":{"POSITION":4294967296},"indices":1})";
        loadCase("integer reference outside parser width", b, false, model);
        b = triangle();
        b.views[0] = R"({"buffer":0,"byteOffset":-1,"byteLength":36})";
        loadCase("negative buffer offset", b, false, model);
    }
    for (bool missingIndices : {false, true}) {
        GltfBuilder b = triangle();
        const uint16_t sparseIndex = 1;
        const float sparseValue[3] = {5, 5, 5};
        int storedIndex = b.addView(&sparseIndex, sizeof(sparseIndex));
        int storedValue = b.addView(sparseValue, sizeof(sparseValue));
        int absentView = static_cast<int>(b.views.size());
        b.extraBuffers.push_back(R"({"byteLength":12})");
        b.views.push_back(R"({"buffer":1,"byteLength":12})");
        b.addAccessor(accessorJson(0, CT_FLOAT, "3", "VEC3",
            ",\"sparse\":{\"count\":1,\"indices\":{\"bufferView\":" +
            to_string(missingIndices ? absentView : storedIndex) +
            ",\"componentType\":5123},\"values\":{\"bufferView\":" +
            to_string(missingIndices ? storedValue : absentView) + "}}"));
        b.primitive = R"({"attributes":{"POSITION":3},"indices":1})";
        GltfModel model;
        loadCase(missingIndices ? "sparse index storage absent" : "sparse value storage absent", b, false, model);
    }
    {
        auto bytes = triangle().glb();
        bytes[8] = 20; bytes[9] = bytes[10] = bytes[11] = 0;
        auto path = g_dir / "length.glb";
        ofstream(path, ios::binary).write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        GltfModel model;
        auto result = model.load(path);
        check("GLB chunks must fit the declared file length", !result && !model.isLoaded());
    }
    {
        // Faults in both the C parser allocator and STL output allocations
        // must release partial state and allow the same model to load again.
        auto path = writeGltf(triangle());
        GltfModel model;
        for (bool persistent : {false, true}) {
            if (persistent && tcTest::skipPersistentAllocationFailure("glTF load entry")) continue;
            check("glTF starts allocation test with a loaded model", model.load(path).ok());
            LoadResult result;
            bool escaped = false;
            // Target path payload growth inside recovery, allowing setup
            // proxies. The threshold comes from the fixture, not an index.
            modelAllocationProbe::begin(path.native().size(), persistent);
            try { result = model.load(path); }
            catch (const bad_alloc&) { escaped = true; }
            modelAllocationProbe::end();
            check(persistent ? "persistent allocation failure returns empty failure" :
                               "STL allocation failure returns empty failure",
                  modelAllocationProbe::failed && !escaped && !result &&
                  result.error == LoadError::DecodeFailed && !model.isLoaded() && model.getNodeCount() == 0 &&
                  gltf_internal::liveParserAllocationsForTests() == 0 && outstandingParserBytes == 0);
            check("retry after parser allocation failure succeeds", model.load(path).ok());
        }
    }
    {
        // Sweep NULL, bad_alloc, and non-standard exceptions at the actual C
        // allocator seam, independently of platform STL/filebuf allocation.
        // Extra tables reproduce cgltf's count-before-NULL-check cleanup sites.
        // Mappings come last: their NULL allocation can still return a positive
        // root index, so the failure guard must precede pointer fixup too.
        auto b = triangle();
        b.meshesLast = true;
        b.primitive = R"({"attributes":{"POSITION":0},"targets":[{"POSITION":0}],"extras":{"x":1},"extensions":{"unknown":{"x":1},"KHR_materials_variants":{"mappings":[{"material":0,"variants":[0]}]}}})";
        b.extra = R"("materials":[{}],"extensionsUsed":["unknown","KHR_materials_variants"],"extensionsRequired":["unknown"],"extensions":{"unknown":{"x":1},"KHR_materials_variants":{"variants":[{"name":"sample"}]}},)";
        for (int shape = 0; shape < 3; ++shape) {
            auto path = shape == 1 ? writeGlb(b) : writeGltf(b);
            if (shape == 2) {
                string document = b.json();
                size_t uri = document.find("data:application/octet-stream;base64,");
                document.replace(uri, document.find('"', uri) - uri, "fault.bin");
                ofstream(g_dir / "fault.bin", ios::binary).write(reinterpret_cast<const char*>(b.bin.data()), b.bin.size());
                ofstream(path, ios::binary) << document;
            }
            GltfModel model;
            parserAllocationCount = 0;
            bool ok = model.load(path).ok();
            size_t allocations = parserAllocationCount;
            printf("parser seam shape=%d requests=%zu (mappings last in parse)\n", shape, allocations);
            for (bool persistent : {false, true}) {
                for (int fault = 0; fault < 3; ++fault) {
                    bool clean = ok && allocations > 10;
                    for (size_t nth = 1; nth <= allocations; ++nth) {
                        parserAllocationCount = 0;
                        rejectParserAllocation = nth;
                        parserFaultPersistent = persistent;
                        parserFaultKind = fault;
                        auto result = model.load(path);
                        clean = clean && parserAllocationCount >= nth && !result &&
                            result.error == LoadError::DecodeFailed &&
                            result.message.find("not enough memory") != string::npos &&
                            !model.isLoaded() && model.getNodeCount() == 0 &&
                            gltf_internal::liveParserAllocationsForTests() == 0 && outstandingParserBytes == 0;
                        rejectParserAllocation = 0;
                        clean = clean && model.load(path).ok() &&
                            gltf_internal::liveParserAllocationsForTests() == 0 && outstandingParserBytes == 0;
                    }
                    check("parser seam shape=" + to_string(shape) + " fault=" + to_string(fault) +
                          (persistent ? " persistent cleanup and retry" : " single cleanup and retry"), clean);
                }
            }
        }
    }
    {
        Pixels pixels;
        pixels.allocate(2, 2, 4);
        pixels.save(g_dir / "sweep.png");
        for (int shape = 0; shape < 3; ++shape) {
            auto b = shape == 2 ? texturedTriangle(R"({"uri":"sweep.png"})") : triangle();
            auto path = shape == 1 ? writeGlb(b) : writeGltf(b);
            if (shape == 2) {
                Json document = Json::parse(b.json());
                document["buffers"][0]["uri"] = "sweep.bin";
                ofstream(g_dir / "sweep.bin", ios::binary).write(reinterpret_cast<const char*>(b.bin.data()), b.bin.size());
                ofstream(path, ios::binary) << document.dump();
            }
            { GltfModel warm; warm.load(path); }
            size_t requests = 0;
            { GltfModel model;
              modelAllocationProbe::begin();
              model.load(path);
              modelAllocationProbe::end();
              requests = modelAllocationProbe::requests; }
            for (bool persistent : {false, true}) {
                if (persistent && tcTest::skipPersistentAllocationFailure("glTF every allocation")) continue;
                if (!persistent && tcTest::skipProxyAllocationSweep("glTF every allocation: single recovery and retry")) continue;
                bool clean = requests > 0;
                for (size_t nth = 1; nth <= requests; ++nth) {
                    // Retire prior dummy resources between cases, as an app
                    // does after a frame, before enabling fault injection.
                    sg_commit();
                    tc::internal::drainPendingGpuDestroys();
                    GltfModel model;
                    LoadResult result;
                    bool escaped = false;
                    modelAllocationProbe::beginNth(nth, persistent);
                    try { result = model.load(path); } catch (...) { escaped = true; }
                    modelAllocationProbe::end();
                    clean = clean && modelAllocationProbe::failed && !escaped &&
                            (result.ok() || (result.error == LoadError::DecodeFailed && !model.isLoaded() && model.getNodeCount() == 0));
                    clean = clean && gltf_internal::liveParserAllocationsForTests() == 0 && outstandingParserBytes == 0;
                    clean = clean && model.load(path).ok() &&
                            gltf_internal::liveParserAllocationsForTests() == 0 && outstandingParserBytes == 0;
                }
                check(persistent ? "glTF every allocation: persistent recovery and retry" :
                                   "glTF every allocation: single recovery and retry", clean);
            }
        }
    }
    gltf_internal::setAllocationHookForTests(nullptr);

    // Decode each URI once, including escaped '%' and UTF-8 bytes, while
    // leaving the literal '%' in the model's parent directory untouched.
    for (const string& imageUri : {string("a%20b.png"), string("a%2520b.png"),
                                   string("%E7%94%BB%E5%83%8F.png")}) {
        fs::path oldRoot = getDataPathRoot();
        fs::path folder = g_dir / utf8ToPath("読み込み%20");
        fs::create_directories(folder);
        setDataPathRoot(g_dir);
        Pixels pixels;
        pixels.allocate(2, 2, 4);
        pixels.setColor(0, 0, Color(1, 0, 0, 1));
        string imageFile = imageUri == "a%20b.png" ? "a b.png" :
                           imageUri == "a%2520b.png" ? "a%20b.png" : "画像.png";
        check("URI fixture: PNG saved", pixels.save(folder / utf8ToPath(imageFile)).ok());
        GltfBuilder b = texturedTriangle(Json{{"uri", imageUri}}.dump());
        Json document = Json::parse(b.json());
        document["buffers"][0]["uri"] = "%E9%A0%82%E7%82%B9%20%2520.bin";
        ofstream(folder / utf8ToPath("頂点 %20.bin"), ios::binary)
            .write(reinterpret_cast<const char*>(b.bin.data()), (streamsize)b.bin.size());
        ofstream(folder / utf8ToPath("モデル.gltf"), ios::binary) << document.dump();
        GltfModel model;
        WarningCounter logs;
        auto result = model.load(utf8ToPath("読み込み%20/モデル.gltf"));
        check("encoded URI: relative model, external buffer and texture load",
              result.ok() && logs.count == 0 && model.getNodeCount() == 1 &&
              model.getNode(0).mesh.getNumIndices() == 3 &&
              model.getNode(0).mesh.getVertices()[1].x == 1 &&
              model.getNode(0).material.hasBaseColorTexture() &&
              model.getNode(0).material.getBaseColorTexture()->getWidth() == 2);
        fs::remove(folder / utf8ToPath("頂点 %20.bin"));
        result = model.load(utf8ToPath("読み込み%20/モデル.gltf"));
        check("missing external buffer: DecodeFailed, exactly one Error",
              result.error == LoadError::DecodeFailed && logs.errors == 1 &&
              logs.last == "[GltfModel] " + result.message && !model.isLoaded());
        setDataPathRoot(oldRoot);
    }

    // ----- UTF-8 paths, with embedded and external buffers --------------------
    {
        fs::path oldRoot = getDataPathRoot();
        setDataPathRoot(g_dir);
        Pixels pixels;
        pixels.allocate(2, 2, 4);
        for (int y = 0; y < 2; ++y) {
            for (int x = 0; x < 2; ++x) pixels.setColor(x, y, Color(1, 0, 0, 1));
        }
        check("UTF-8 glTF fixture: PNG saved",
              bool(pixels.save(g_dir / utf8ToPath("テクスチャ.png"))));
        GltfBuilder b = texturedTriangle(R"({"uri":"テクスチャ.png"})");
        for (bool external : {false, true}) {
            string json = b.json();
            if (external) {
                string embedded = "data:application/octet-stream;base64," + base64(b.bin);
                json.replace(json.find(embedded), embedded.size(), "頂点.bin");
                ofstream(g_dir / utf8ToPath("頂点.bin"), ios::binary)
                    .write(reinterpret_cast<const char*>(b.bin.data()), (streamsize)b.bin.size());
            }
            fs::path fileName = utf8ToPath(external ? "外部.gltf" : "埋込.gltf");
            ofstream(g_dir / fileName, ios::binary) << json;
            GltfModel m;
            WarningCounter warnings;
            bool loaded = m.load(fileName).ok();
            string name = external ? "UTF-8 glTF external buffer" : "UTF-8 glTF data URI buffer";
            check(name + ": geometry loads without warnings",
                  loaded && warnings.count == 0 && m.getNodeCount() == 1 &&
                  m.getNode(0).mesh.getNumVertices() == 3 && m.getNode(0).mesh.getNumIndices() == 3);
            check(name + ": external texture loads",
                  loaded && m.getNodeCount() == 1 && m.getNode(0).material.hasBaseColorTexture());
        }
        setDataPathRoot(oldRoot);
    }

    // A count too large to address on 64-bit
    const string HUGE_COUNT = "4611686018427387905";
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
        b.addAccessor(accessorJson(0, CT_FLOAT, "3", "VEC3",
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

    // ----- GLB: data in the BIN chunk -------------------------------------------
    {
        // Positions and indices read from the BIN chunk
        GltfModel m;
        if (loadCase("valid GLB", triangle(), true, m, nullptr, true)) {
            const auto& mesh = m.getNode(0).mesh;
            check("valid GLB: 3 vertices, 3 indices",
                  m.getNodeCount() == 1 && mesh.getNumVertices() == 3 && mesh.getNumIndices() == 3);
            check("valid GLB: vertex and index data",
                  mesh.getNumVertices() == 3 && mesh.getNumIndices() == 3 &&
                  vecNear(mesh.getVertices()[1], 1, 0, 0) && vecNear(mesh.getVertices()[2], 0, 1, 0) &&
                  vecNear(mesh.getNormals()[0], 0, 0, 1) &&
                  mesh.getIndices()[0] == 0 && mesh.getIndices()[1] == 1 && mesh.getIndices()[2] == 2);
        }
    }
    {
        // The POSITION buffer view runs 8 bytes past the end of the BIN chunk
        GltfBuilder b = triangle();
        b.views[0] = "{\"buffer\":0,\"byteOffset\":" + to_string(b.bin.size() - 28) +
                     ",\"byteLength\":36}";
        GltfModel m;
        loadCase("GLB buffer view past the BIN chunk", b, false, m, nullptr, true);
    }
    {
        // The buffer declares more bytes than the BIN chunk holds
        GltfBuilder b = triangle();
        b.bufferLength = (long long)b.bin.size() + 16;
        GltfModel m;
        loadCase("GLB buffer longer than the BIN chunk", b, false, m, nullptr, true);
    }

    // ----- ranges and references ----------------------------------------------
    {
        GltfBuilder b = triangle();
        b.accessors[0] = accessorJson(0, CT_FLOAT, "4", "VEC3");
        b.accessors[2] = accessorJson(2, CT_FLOAT, "4", "VEC3");
        GltfModel m;
        loadCase("accessor count past its buffer view", b, false, m);
    }
    {
        GltfBuilder b = triangle();
        b.accessors[0] = accessorJson(0, CT_FLOAT, "3", "VEC3", ",\"byteOffset\":4");
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
        b.accessors[1] = accessorJson(7, CT_USHORT, "3", "SCALAR");
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
        // POSITION count too large to address
        GltfBuilder b = triangle();
        b.accessors[0] = accessorJson(0, CT_FLOAT, HUGE_COUNT, "VEC3");
        b.primitive = R"({"attributes":{"POSITION":0}})";
        GltfModel m;
        loadCase("position count too large to address", b, false, m);
    }
    if (is64) {
        // Index count too large to address
        GltfBuilder b = triangle();
        const uint32_t idx32[1] = { 0 };
        int v = b.addView(idx32, sizeof(idx32));
        b.addAccessor(accessorJson(v, CT_UINT, HUGE_COUNT, "SCALAR"));
        b.primitive = R"({"attributes":{"POSITION":0},"indices":3})";
        GltfModel m;
        loadCase("index count too large to address", b, false, m);
    }
    if (is64) {
        // Sparse count too large to address
        GltfBuilder b = triangle();
        const uint32_t sparseIdx[1] = { 0 };
        const float sparseVal[3] = { 5, 5, 5 };
        int siv = b.addView(sparseIdx, sizeof(sparseIdx));
        int svv = b.addView(sparseVal, sizeof(sparseVal));
        b.addAccessor(accessorJson(-1, CT_FLOAT, "3", "VEC3",
            ",\"sparse\":{\"count\":" + HUGE_COUNT + ",\"indices\":{\"bufferView\":" + to_string(siv) +
            ",\"componentType\":5125},\"values\":{\"bufferView\":" + to_string(svv) + "}}"));
        b.primitive = R"({"attributes":{"POSITION":3}})";
        GltfModel m;
        loadCase("sparse count too large to address", b, false, m);
    }
    if (is64) {
        // Sparse accessor without a buffer view whose own count is too large
        // to address: refused
        GltfBuilder b = triangle();
        const uint16_t sparseIdx[1] = { 0 };
        const float sparseVal[3] = { 5, 5, 5 };
        int siv = b.addView(sparseIdx, sizeof(sparseIdx));
        int svv = b.addView(sparseVal, sizeof(sparseVal));
        b.addAccessor(accessorJson(-1, CT_FLOAT, "6148914691236517206", "VEC3",
            ",\"sparse\":{\"count\":1,\"indices\":{\"bufferView\":" + to_string(siv) +
            ",\"componentType\":5123},\"values\":{\"bufferView\":" + to_string(svv) + "}}"));
        b.primitive = R"({"attributes":{"POSITION":3}})";
        GltfModel m;
        string warning;
        loadCase("accessor without a buffer view, count too large", b, false, m, &warning);
        check("accessor without a buffer view, count too large: reported as such",
              warning.find("too large to address") != string::npos);
    }
    {
        // Sparse accessor on a view with byteStride 16: the sparse values are
        // packed tightly (12 bytes apart), and are read that way. Other data
        // follows the values view, so a value read from the wrong place
        // shows up in the result.
        GltfBuilder b = triangle();
        const float strided[12] = { 0, 0, 0, 0,  1, 0, 0, 0,  0, 1, 0, 0 };
        int basev = b.addView(strided, sizeof(strided), 16);
        const uint16_t sparseIdx[2] = { 0, 2 };
        int siv = b.addView(sparseIdx, sizeof(sparseIdx));
        const float sparseVal[6] = { 5, 5, 5,  6, 7, 8 };
        int svv = b.addView(sparseVal, sizeof(sparseVal));
        const float after[4] = { 9, 9, 9, 9 };
        b.addView(after, sizeof(after));
        b.addAccessor(accessorJson(basev, CT_FLOAT, "3", "VEC3",
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
        b.addAccessor(accessorJson(0, CT_FLOAT, "3", "VEC3",
            ",\"sparse\":{\"count\":2,\"indices\":{\"bufferView\":" + to_string(siv) +
            ",\"componentType\":5123},\"values\":{\"bufferView\":" + to_string(svv) + "}}"));
        b.primitive = R"({"attributes":{"POSITION":3}})";
        GltfModel m;
        loadCase("sparse values past their buffer view", b, false, m);
    }
    {
        // POSITION on a view with byteStride 4, smaller than its 12-byte
        // VEC3 element: refused
        GltfBuilder b = triangle();
        b.views[0] = "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36,\"byteStride\":4}";
        GltfModel m;
        string warning;
        loadCase("byteStride smaller than the element", b, false, m, &warning);
        check("byteStride smaller than the element: reported as such",
              warning.find("byteStride smaller") != string::npos);
    }
    {
        // A component type glTF 2.0 does not allow is reported as a
        // validation failure, not as a range problem
        GltfBuilder b = triangle();
        b.addAccessor(accessorJson(2, CT_INT, "3", "VEC3"));
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
        b.addAccessor(accessorJson(0, CT_FLOAT, "3", "VEC3",
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
        b.accessors[2] = accessorJson(2, CT_FLOAT, "2", "VEC3");  // 2 normals, 3 positions
        GltfModel m;
        loadCase("attribute counts differ", b, false, m);
    }
    {
        GltfBuilder b = triangle();
        const uint16_t idx[3] = { 0, 1, 5 };
        int v = b.addView(idx, sizeof(idx));
        b.addAccessor(accessorJson(v, CT_USHORT, "3", "SCALAR"));
        b.primitive = R"({"attributes":{"POSITION":0},"indices":3})";
        GltfModel m;
        loadCase("index past the vertex count", b, false, m);
    }
    {
        // POSITION declared VEC2 over 6 floats: 2 whole vertices are read,
        // and the loader refuses index 2, which is past them
        GltfBuilder b = triangle();
        b.addAccessor(accessorJson(0, CT_FLOAT, "3", "VEC2"));
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
        bool ok = m.load(writeGltf(b)).ok();
        check("two primitives without positions: loads, one node",
              ok && m.isLoaded() && m.getNodeCount() == 1);
        check("two primitives without positions: one warning with the count",
              warnings.count == 1 && warnings.last.find("skipped 2 primitive") != string::npos);
    }
    {
        // POSITION accessor without a buffer view has no vertex data of its
        // own: the primitive is skipped like one without POSITION
        GltfBuilder b = triangle();
        b.addAccessor(accessorJson(-1, CT_FLOAT, "3", "VEC3"));
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
        // An index accessor without a buffer view, with a count far larger
        // than memory: the primitive is skipped without allocating an array
        // from that count, and the other one loads
        GltfBuilder b = triangle();
        b.addAccessor(accessorJson(-1, CT_UINT, is64 ? "4611686018427387903" : "1073741823",
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
        b.addAccessor(accessorJson(noDataView, CT_FLOAT, "3", "VEC3"));   // 3
        b.addAccessor(accessorJson(noDataView, CT_USHORT, "3", "SCALAR"));  // 4
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
        // Four sparse values for an accessor of three elements: refused
        GltfBuilder b = triangle();
        const uint16_t sparseIdx[4] = { 0, 1, 2, 2 };
        int siv = b.addView(sparseIdx, sizeof(sparseIdx));
        const float sparseVal[12] = { 5, 5, 5,  6, 6, 6,  7, 7, 7,  8, 8, 8 };
        int svv = b.addView(sparseVal, sizeof(sparseVal));
        b.addAccessor(accessorJson(0, CT_FLOAT, "3", "VEC3",
            ",\"sparse\":{\"count\":4,\"indices\":{\"bufferView\":" + to_string(siv) +
            ",\"componentType\":5123},\"values\":{\"bufferView\":" + to_string(svv) + "}}"));
        b.primitive = R"({"attributes":{"POSITION":3}})";
        GltfModel m;
        loadCase("more sparse values than elements", b, false, m);
    }
    {
        // Sparse indices must be strictly increasing: a repeated index and a
        // decreasing one are refused
        const uint16_t repeated[2] = { 1, 1 };
        const uint16_t decreasing[2] = { 2, 1 };
        const struct { const char* name; const uint16_t* idx; } cases[] = {
            { "repeated sparse index", repeated },
            { "decreasing sparse index", decreasing },
        };
        for (const auto& c : cases) {
            GltfBuilder b = triangle();
            int siv = b.addView(c.idx, 2 * sizeof(uint16_t));
            const float sparseVal[6] = { 5, 5, 5,  6, 6, 6 };
            int svv = b.addView(sparseVal, sizeof(sparseVal));
            b.addAccessor(accessorJson(0, CT_FLOAT, "3", "VEC3",
                ",\"sparse\":{\"count\":2,\"indices\":{\"bufferView\":" + to_string(siv) +
                ",\"componentType\":5123},\"values\":{\"bufferView\":" + to_string(svv) + "}}"));
            b.primitive = R"({"attributes":{"POSITION":3}})";
            GltfModel m;
            string warning;
            loadCase(c.name, b, false, m, &warning);
            check(string(c.name) + ": reported as not increasing",
                  warning.find("strictly increasing") != string::npos);
        }
    }

    // ----- textures -------------------------------------------------------------
    {
        // Put a valid image outside the model folder and a different-sized
        // image at the appended path, so selecting the outside file is visible.
        fs::path modelDir = g_dir / "absolute-uri-model";
        fs::create_directories(modelDir);
        fs::path outside = fs::absolute(g_dir / utf8ToPath("外部テクスチャ.png"));
        check("absolute image uri: fixture path is absolute", outside.is_absolute());
        Pixels pixels;
        pixels.allocate(2, 2, 4);
        for (int y = 0; y < 2; ++y) {
            for (int x = 0; x < 2; ++x) pixels.setColor(x, y, Color(1, 0, 0, 1));
        }
        check("absolute image uri: outside PNG saved", bool(pixels.save(outside)));
        if (!outside.has_root_name()) {
            fs::path inside = modelDir / outside.relative_path();
            fs::create_directories(inside.parent_path());
            pixels.allocate(1, 1, 4);
            pixels.setColor(0, 0, Color(0, 1, 0, 1));
            check("absolute image uri: model-folder PNG saved", bool(pixels.save(inside)));
        }
        GltfBuilder b = texturedTriangle(Json{{"uri", pathToUtf8(outside)}}.dump());
        fs::path modelPath = modelDir / "model.gltf";
        ofstream(modelPath, ios::binary) << b.json();
        GltfModel m;
        WarningCounter warnings;
        bool loaded = m.load(modelPath).ok();
        check("absolute image uri: geometry loads",
              loaded && m.getNodeCount() == 1 && m.getNode(0).mesh.getNumVertices() == 3);
        const Texture* texture = loaded && m.getNodeCount() == 1
                               ? m.getNode(0).material.getBaseColorTexture() : nullptr;
        if (!outside.has_root_name()) {
            check("absolute image uri: uses image under model folder",
                  texture && texture->getWidth() == 1 && texture->getHeight() == 1 &&
                  warnings.count == 0);
        } else {
            // On Windows, appending a drive-qualified URI puts its colon in
            // a path component, so the image is skipped rather than loaded.
            check("absolute image uri: appended drive path is skipped",
                  !texture && warnings.count > 0);
        }
        check("absolute image uri: outside image is not selected",
              !texture || texture->getWidth() != 2 || texture->getHeight() != 2);
    }
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

    {
        // An external image that does not exist: skipped with a warning
        GltfModel m;
        string warning;
        if (loadCase("missing image file", texturedTriangle(R"({"uri":"missing.png"})"), true, m,
                     &warning)) {
            check("missing image file: texture skipped",
                  m.getNodeCount() == 1 && !m.getNode(0).material.hasBaseColorTexture());
            check("missing image file: warning logged",
                  warning.find("could not be loaded") != string::npos);
        }
    }
    {
        // An image uri with a byte that is not valid UTF-8. The loader builds
        // the path with utf8ToPath(): on Windows that throws for such bytes,
        // and the loader skips the image with a warning. On POSIX the bytes
        // are a valid file name and the conversion does not throw, so here
        // the image is skipped because no such file exists (the same
        // outcome, through the missing-file warning). Either way load()
        // succeeds, without that texture, and nothing escapes.
        GltfModel m;
        string warning;
        bool threw = false;
        bool ok = false;
        try {
            ok = loadCase("image uri not valid UTF-8",
                          texturedTriangle("{\"uri\":\"bad\xff" "name.png\"}"), true, m, &warning);
        } catch (...) {
            threw = true;
        }
        check("image uri not valid UTF-8: no exception", !threw);
        if (ok) {
            check("image uri not valid UTF-8: texture skipped",
                  m.getNodeCount() == 1 && !m.getNode(0).material.hasBaseColorTexture());
            check("image uri not valid UTF-8: warning logged",
                  warning.find("skipped an image") != string::npos);
        }
    }

    // ----- an exception while reading the model -----------------------------------
    {
        // The test hook throws when the texture of the second primitive is
        // about to be read. The first primitive (no material) has been added
        // by then; the failed load must not keep it.
        GltfBuilder b = texturedTriangle(R"({"uri":"missing.png"})");
        b.primitive = R"({"attributes":{"POSITION":0,"NORMAL":2},"indices":1},)"
                      R"({"attributes":{"POSITION":0,"NORMAL":2},"indices":1,"material":0})";
        GltfModel m;
        gltf_internal::setTextureLoadHookForTests(&throwRuntimeError);
        string warning;
        bool threw = false;
        try {
            loadCase("exception while reading", b, false, m, &warning);
        } catch (...) {
            threw = true;
        }
        gltf_internal::setTextureLoadHookForTests(nullptr);
        check("exception while reading: does not escape load()", !threw);
        check("exception while reading: primitive read before it not kept",
              m.getNodeCount() == 0 && !m.isLoaded());
        check("exception while reading: warning names the error",
              warning.find("injected failure") != string::npos);

        // An exception that is not a std::exception
        gltf_internal::setTextureLoadHookForTests(&throwInt);
        threw = false;
        try {
            loadCase("non-standard exception while reading", b, false, m);
        } catch (...) {
            threw = true;
        }
        gltf_internal::setTextureLoadHookForTests(nullptr);
        check("non-standard exception while reading: does not escape load()", !threw);

        // With the hook off, the same file loads (the texture is skipped)
        loadCase("same file without the hook", b, true, m);
        check("same file without the hook: both primitives", m.getNodeCount() == 2);
    }

    {
        GltfModel model;
        string warning;
        gltf_internal::setTextureLoadHookForTests(+[] { throw length_error("mesh vertex count exceeds the Mesh index width"); });
        loadCase("mesh width failure", texturedTriangle(R"({"uri":"missing.png"})"), false, model, &warning);
        gltf_internal::setTextureLoadHookForTests(nullptr);
        check("mesh width failure preserves the cause",
              warning.find("Mesh index width") != string::npos && warning.find("not enough memory") == string::npos);
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
        // A chain 20000 nodes deep, each moved 1 along z: a deep chain loads
        // without recursion, with the transforms accumulated. The depth is
        // kept at 20000 so the test stays fast
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
        bool first = m.load(writeGltf(triangle())).ok();
        GltfBuilder bad = triangle();
        bad.accessors[0] = accessorJson(0, CT_FLOAT, "4", "VEC3");
        bool second = m.load(writeGltf(bad)).ok();
        check("reload: good then rejected leaves the model empty",
              first && !second && !m.isLoaded() && m.getNodeCount() == 0);
    }

    error_code ec;
    fs::remove_all(g_dir, ec);

    sg_shutdown();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
