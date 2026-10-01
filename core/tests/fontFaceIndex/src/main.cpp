// =============================================================================
// fontFaceIndex — the face index picks the face inside a font collection (#294)
//
// - A two-face .ttc built here: face 0 and face 1 give different glyph
//   metrics, through FontAtlasManager::setupFromMemory(), setup() from a file
//   and the shared font cache (the face index is part of FontCacheKey, so the
//   two faces get separate atlases).
// - The default face index is 0, so a call without one is unchanged.
// - A face index below 0, or at or past the number of faces, fails the load
//   with an error log (for a collection and for a single font).
// - internal::findFaceByPostScriptName() / findFaceInFileByPostScriptName()
//   (the macOS / iOS face lookup) find a face by its PostScript name (name
//   ID 6) in Windows and Mac name records, and stop at data that ends early.
// - Linux: a system font name resolves to the face fontconfig reports
//   (FC_INDEX). With Noto Sans CJK installed, "Noto Sans CJK SC" and
//   "Noto Sans CJK JP" give the faces of NotoSansCJK*.ttc whose PostScript
//   names are NotoSansCJKsc-Regular / NotoSansCJKjp-Regular, and their outlines
//   of U+9AA8 differ. SKIP when the font is not installed.
//
// The fonts are built here, so the test runs the same everywhere.
// =============================================================================

#include <TrussC.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace std;
using namespace tc;

static int g_fail = 0;
static void check(const string& name, bool ok, const string& detail = "") {
    printf("%-72s %s%s\n", name.c_str(), ok ? "PASS" : "FAIL",
           ok || detail.empty() ? "" : ("  -- " + detail).c_str());
    fflush(stdout);
    if (!ok) ++g_fail;
}

static int g_errors = 0;
static string g_lastError;

// --- big-endian helpers ------------------------------------------------------
using Bytes = vector<uint8_t>;

static void put16(Bytes& b, uint32_t v) {
    b.push_back((uint8_t)(v >> 8));
    b.push_back((uint8_t)v);
}
static void put32(Bytes& b, uint32_t v) {
    put16(b, v >> 16);
    put16(b, v & 0xffff);
}
static void set32(Bytes& b, size_t at, uint32_t v) {
    b[at] = (uint8_t)(v >> 24);
    b[at + 1] = (uint8_t)(v >> 16);
    b[at + 2] = (uint8_t)(v >> 8);
    b[at + 3] = (uint8_t)v;
}
static uint32_t get16(const Bytes& b, size_t at) { return (uint32_t(b[at]) << 8) | b[at + 1]; }
static uint32_t get32(const Bytes& b, size_t at) { return (get16(b, at) << 16) | get16(b, at + 2); }
static void pad4(Bytes& b) {
    while (b.size() % 4) b.push_back(0);
}
static uint32_t tagOf(const char* t) {
    return (uint32_t(uint8_t(t[0])) << 24) | (uint32_t(uint8_t(t[1])) << 16) |
           (uint32_t(uint8_t(t[2])) << 8) | uint8_t(t[3]);
}

// --- sfnt builder ------------------------------------------------------------
struct Table {
    string tag;
    Bytes data;
};

static Bytes buildSfnt(const vector<Table>& tables) {
    Bytes out;
    put32(out, 0x00010000);
    put16(out, (uint32_t)tables.size());
    put16(out, 0);
    put16(out, 0);
    put16(out, 0);
    size_t offset = 12 + 16 * tables.size();
    vector<size_t> offsets;
    for (auto& t : tables) {
        offsets.push_back(offset);
        offset += t.data.size();
        offset = (offset + 3) & ~size_t(3);
    }
    for (size_t i = 0; i < tables.size(); i++) {
        put32(out, tagOf(tables[i].tag.c_str()));
        put32(out, 0);
        put32(out, (uint32_t)offsets[i]);
        put32(out, (uint32_t)tables[i].data.size());
    }
    for (size_t i = 0; i < tables.size(); i++) {
        while (out.size() < offsets[i]) out.push_back(0);
        out.insert(out.end(), tables[i].data.begin(), tables[i].data.end());
    }
    pad4(out);
    return out;
}

static const int kUnitsPerEm = 1000;
static const int kNumGlyphs = 2;  // 0: .notdef, 1: 'A'

static Bytes makeHead() {
    Bytes b;
    put32(b, 0x00010000);
    put32(b, 0x00010000);
    put32(b, 0);
    put32(b, 0x5F0F3CF5);
    put16(b, 0);
    put16(b, kUnitsPerEm);
    for (int i = 0; i < 16; i++) b.push_back(0);
    put16(b, 0);
    put16(b, 0);
    put16(b, 900);
    put16(b, 700);
    put16(b, 0);
    put16(b, 8);
    put16(b, 2);
    put16(b, 0);  // short loca
    put16(b, 0);
    return b;
}

static Bytes makeHhea() {
    Bytes b;
    put32(b, 0x00010000);
    put16(b, 800);
    put16(b, (uint16_t)-200);
    put16(b, 0);
    put16(b, 900);
    for (int i = 0; i < 11; i++) put16(b, 0);
    put16(b, kNumGlyphs);
    return b;
}

static Bytes makeMaxp() {
    Bytes b;
    put32(b, 0x00005000);
    put16(b, kNumGlyphs);
    return b;
}

static Bytes makeHmtx(int advanceA) {
    Bytes b;
    put16(b, 500); put16(b, 50);
    put16(b, (uint32_t)advanceA); put16(b, 0);
    return b;
}

// Format 4: 'A' -> glyph 1.
static Bytes makeCmap() {
    Bytes sub;
    put16(sub, 4);
    put16(sub, 16 + 8 * 2);
    put16(sub, 0);
    put16(sub, 4);   // segCountX2
    put16(sub, 4);   // searchRange
    put16(sub, 1);   // entrySelector
    put16(sub, 0);   // rangeShift
    put16(sub, 0x41); put16(sub, 0xFFFF);  // endCode
    put16(sub, 0);
    put16(sub, 0x41); put16(sub, 0xFFFF);  // startCode
    put16(sub, (uint16_t)(1 - 0x41)); put16(sub, 1);  // idDelta
    put16(sub, 0); put16(sub, 0);          // idRangeOffset
    Bytes b;
    put16(b, 0);
    put16(b, 1);
    put16(b, 3);
    put16(b, 1);
    put32(b, 12);
    b.insert(b.end(), sub.begin(), sub.end());
    return b;
}

// A triangle `width` units wide.
static Bytes triangle(int width) {
    Bytes b;
    put16(b, 1);
    put16(b, 0); put16(b, 0); put16(b, (uint32_t)width); put16(b, 700);
    put16(b, 2);  // end point of the contour
    put16(b, 0);  // instructionLength
    for (int i = 0; i < 3; i++) b.push_back(1);  // on-curve, int16 deltas
    put16(b, 0); put16(b, (uint32_t)(width / 2)); put16(b, (uint32_t)(width - width / 2));
    put16(b, 0); put16(b, 700); put16(b, (uint16_t)-700);
    pad4(b);
    return b;
}

struct NameRecord {
    int platformID;  // 1: Mac (Roman bytes), 3: Windows (UTF-16BE)
    int nameID;
    string text;
};

static Bytes makeName(const vector<NameRecord>& records) {
    Bytes strings;
    Bytes b;
    put16(b, 0);
    put16(b, (uint32_t)records.size());
    put16(b, (uint32_t)(6 + 12 * records.size()));
    for (auto& r : records) {
        Bytes s;
        for (char c : r.text) {
            if (r.platformID == 3) s.push_back(0);
            s.push_back((uint8_t)c);
        }
        put16(b, (uint32_t)r.platformID);
        put16(b, r.platformID == 3 ? 1 : 0);
        put16(b, r.platformID == 3 ? 0x409 : 0);
        put16(b, (uint32_t)r.nameID);
        put16(b, (uint32_t)s.size());
        put16(b, (uint32_t)strings.size());
        strings.insert(strings.end(), s.begin(), s.end());
    }
    b.insert(b.end(), strings.begin(), strings.end());
    return b;
}

// One TrueType face whose 'A' advances `advanceA` units and is `advanceA`
// units wide.
static Bytes makeFace(int advanceA, const vector<NameRecord>& names) {
    Bytes glyf = triangle(400);  // .notdef
    Bytes glyphA = triangle(advanceA);
    Bytes loca;
    put16(loca, 0);
    put16(loca, (uint32_t)(glyf.size() / 2));
    glyf.insert(glyf.end(), glyphA.begin(), glyphA.end());
    put16(loca, (uint32_t)(glyf.size() / 2));
    return buildSfnt({{"head", makeHead()},
                      {"hhea", makeHhea()},
                      {"maxp", makeMaxp()},
                      {"cmap", makeCmap()},
                      {"name", makeName(names)},
                      {"loca", loca},
                      {"glyf", glyf},
                      {"hmtx", makeHmtx(advanceA)}});
}

// A .ttc holding the given faces in order. Table offsets in a collection
// are from the start of the file.
static Bytes makeCollection(const vector<Bytes>& faces) {
    Bytes b;
    put32(b, tagOf("ttcf"));
    put32(b, 0x00010000);
    put32(b, (uint32_t)faces.size());
    size_t start = 12 + 4 * faces.size();
    vector<size_t> starts;
    for (auto& f : faces) {
        starts.push_back(start);
        put32(b, (uint32_t)start);
        start += f.size();
    }
    for (size_t i = 0; i < faces.size(); i++) {
        Bytes f = faces[i];
        const size_t n = get16(f, 4);
        for (size_t t = 0; t < n; t++) {
            const size_t rec = 12 + 16 * t;
            set32(f, rec + 8, get32(f, rec + 8) + (uint32_t)starts[i]);
        }
        b.insert(b.end(), f.begin(), f.end());
    }
    return b;
}

static const int kAdvance0 = 600;
static const int kAdvance1 = 900;

static Bytes face0() {
    return makeFace(kAdvance0, {{3, 1, "TcFaceFamily"}, {3, 6, "TcFace-Zero"}});
}
static Bytes face1() {
    return makeFace(kAdvance1, {{1, 1, "TcFaceFamily"}, {1, 6, "TcFace-One"}});
}

static bool closeTo(float a, float b) { return fabs(a - b) < 1e-4f; }

static bool writeFile(const fs::path& path, const Bytes& data) {
    ofstream out(path, ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()), (streamsize)data.size());
    return (bool)out;
}

// --- face index through FontAtlasManager --------------------------------------
static void checkMemory(const Bytes& ttc) {
    {
        internal::FontAtlasManager m;
        const bool ok = m.setupFromMemory(ttc.data(), ttc.size(), 100);
        check("setupFromMemory(): default face is face 0",
              ok && closeTo(m.getGlyphAdvanceEm('A'), kAdvance0 / 1000.0f),
              ok ? "advEm=" + to_string(m.getGlyphAdvanceEm('A')) : "load failed");
    }
    for (int face : {0, 1}) {
        internal::FontAtlasManager m;
        const bool ok = m.setupFromMemory(ttc.data(), ttc.size(), 100, face);
        const float want = (face == 0 ? kAdvance0 : kAdvance1) / 1000.0f;
        const internal::GlyphInfo* g = ok ? m.getOrLoadGlyph('A') : nullptr;
        check("setupFromMemory(): face " + to_string(face) + " has its own 'A' advance and width",
              ok && closeTo(m.getGlyphAdvanceEm('A'), want) && g && g->isValid() &&
                  fabs(g->getWidth() - want * 100) <= 2.0f,
              ok ? "advEm=" + to_string(m.getGlyphAdvanceEm('A')) +
                       " width=" + to_string(g ? g->getWidth() : -1)
                 : "load failed");
    }
    for (int face : {2, -1, 1000}) {
        internal::FontAtlasManager m;
        const int before = g_errors;
        g_lastError.clear();
        const bool ok = m.setupFromMemory(ttc.data(), ttc.size(), 100, face);
        check("setupFromMemory(): face " + to_string(face) + " of 2 fails with an error",
              !ok && g_errors > before && g_lastError.find("out of range") != string::npos,
              "ok=" + to_string(ok) + " last=\"" + g_lastError + "\"");
    }
    {
        const Bytes single = face1();
        internal::FontAtlasManager m;
        const bool ok0 = m.setupFromMemory(single.data(), single.size(), 100);
        check("setupFromMemory(): a single font loads as face 0",
              ok0 && closeTo(m.getGlyphAdvanceEm('A'), kAdvance1 / 1000.0f));
        internal::FontAtlasManager m1;
        const int before = g_errors;
        g_lastError.clear();
        const bool ok1 = m1.setupFromMemory(single.data(), single.size(), 100, 1);
        check("setupFromMemory(): face 1 of a single font fails with an error",
              !ok1 && g_errors > before && g_lastError.find("out of range") != string::npos,
              "last=\"" + g_lastError + "\"");
    }
}

static void checkFile(const fs::path& path) {
    for (int face : {0, 1}) {
        internal::FontAtlasManager m;
        const bool ok = m.setup(path.string(), 100, face);
        const float want = (face == 0 ? kAdvance0 : kAdvance1) / 1000.0f;
        check("setup(): face " + to_string(face) + " from a .ttc file",
              ok && closeTo(m.getGlyphAdvanceEm('A'), want));
    }
    {
        internal::FontAtlasManager m;
        const int before = g_errors;
        const bool ok = m.setup(path.string(), 100, 2);
        check("setup(): face 2 of a 2-face file fails with an error", !ok && g_errors > before);
    }

    // The face index is part of the cache key: two faces of one file get
    // separate atlases, and each key gives its own face.
    auto& cache = internal::SharedFontCache::getInstance();
    internal::FontCacheKey k0{path.string(), 100};
    internal::FontCacheKey k1 = k0;
    k1.faceIndex = 1;
    check("FontCacheKey: keys differ by face index", !(k0 == k1));
    auto m0 = cache.getOrCreate(k0);
    auto m1 = cache.getOrCreate(k1);
    check("SharedFontCache: face 0 and face 1 get separate atlases",
          m0 && m1 && m0 != m1 && closeTo(m0->getGlyphAdvanceEm('A'), kAdvance0 / 1000.0f) &&
              closeTo(m1->getGlyphAdvanceEm('A'), kAdvance1 / 1000.0f));
    check("SharedFontCache: the same key gives the same atlas", cache.getOrCreate(k1) == m1);
    m0.reset();
    m1.reset();
    cache.release(k0);
    cache.release(k1);
}

// --- PostScript name lookup (macOS / iOS) --------------------------------------
static void checkPostScriptNames(const Bytes& ttc, const fs::path& ttcPath,
                                 const fs::path& singlePath) {
    using internal::findFaceByPostScriptName;
    check("findFaceByPostScriptName(): Windows record finds face 0",
          findFaceByPostScriptName(ttc.data(), ttc.size(), "TcFace-Zero") == 0);
    check("findFaceByPostScriptName(): Mac record finds face 1",
          findFaceByPostScriptName(ttc.data(), ttc.size(), "TcFace-One") == 1);
    check("findFaceByPostScriptName(): only name ID 6 counts",
          findFaceByPostScriptName(ttc.data(), ttc.size(), "TcFaceFamily") == -1);
    check("findFaceByPostScriptName(): unknown name gives -1",
          findFaceByPostScriptName(ttc.data(), ttc.size(), "TcFace-Two") == -1);
    check("findFaceByPostScriptName(): names are compared exactly",
          findFaceByPostScriptName(ttc.data(), ttc.size(), "tcface-one") == -1 &&
              findFaceByPostScriptName(ttc.data(), ttc.size(), "TcFace-On") == -1);
    const Bytes single = face1();
    check("findFaceByPostScriptName(): a single font is face 0",
          findFaceByPostScriptName(single.data(), single.size(), "TcFace-One") == 0);

    // Every shorter copy of the collection: nothing past the end is read
    // (run under ASan to see it), and a face whose name table is cut off is
    // not found.
    bool allInRange = true;
    for (size_t n = 0; n < ttc.size(); n++) {
        Bytes cut(ttc.begin(), ttc.begin() + (ptrdiff_t)n);
        const int r = findFaceByPostScriptName(cut.data(), cut.size(), "TcFace-One");
        if (r != -1 && r != 1) allInRange = false;
    }
    check("findFaceByPostScriptName(): truncated copies give -1 or the face", allInRange);
    check("findFaceByPostScriptName(): null data gives -1",
          findFaceByPostScriptName(nullptr, 0, "TcFace-One") == -1);

    check("findFaceInFileByPostScriptName(): face 1 of a .ttc file",
          internal::findFaceInFileByPostScriptName(ttcPath, "TcFace-One") == 1);
    check("findFaceInFileByPostScriptName(): unknown name in a .ttc file gives -1",
          internal::findFaceInFileByPostScriptName(ttcPath, "TcFace-Two") == -1);
    check("findFaceInFileByPostScriptName(): a single font file is face 0",
          internal::findFaceInFileByPostScriptName(singlePath, "Anything") == 0);
    check("findFaceInFileByPostScriptName(): a missing file gives -1",
          internal::findFaceInFileByPostScriptName(ttcPath.parent_path() / "missing.ttc",
                                                   "TcFace-One") == -1);
}

// --- system font names (Linux, fontconfig) -------------------------------------
#ifdef __linux__
// Sum of the outline vertices of a codepoint: differs when the outlines do.
static double outlineSum(internal::FontAtlasManager& m, uint32_t cp) {
    double sum = 0;
    const Path p = m.getGlyphPath(cp);
    for (auto& v : p.getVertices()) sum += v.x * 3.0 + v.y;
    return sum;
}

static void checkSystemFaces() {
    const internal::SystemFontFace sc = internal::systemFontFace("Noto Sans CJK SC");
    const internal::SystemFontFace jp = internal::systemFontFace("Noto Sans CJK JP");
    const string scFile = sc.path.filename().string();
    if (sc.path.empty() || jp.path.empty() || scFile.find("NotoSansCJK") == string::npos ||
        sc.path.extension() != ".ttc" || sc.path != jp.path) {
        printf("SKIP: Noto Sans CJK (.ttc) not installed; system face check not run\n");
        return;
    }
    printf("Noto Sans CJK SC -> %s face %d, JP -> face %d\n", sc.path.string().c_str(),
           sc.index, jp.index);
    check("systemFontPath() gives the path of systemFontFace()",
          systemFontPath("Noto Sans CJK SC") == sc.path);
    check("Linux: \"Noto Sans CJK SC\" gives the face named NotoSansCJKsc-Regular",
          sc.index > 0 &&
              sc.index == internal::findFaceInFileByPostScriptName(sc.path, "NotoSansCJKsc-Regular"));
    check("Linux: \"Noto Sans CJK JP\" gives the face named NotoSansCJKjp-Regular",
          jp.index == internal::findFaceInFileByPostScriptName(jp.path, "NotoSansCJKjp-Regular"));

    internal::FontAtlasManager mSc, mJp;
    const bool okSc = mSc.setup(sc.path.string(), 48, sc.index);
    const bool okJp = mJp.setup(jp.path.string(), 48, jp.index);
    const double sSc = okSc ? outlineSum(mSc, 0x9AA8) : 0;
    const double sJp = okJp ? outlineSum(mJp, 0x9AA8) : 0;
    check("Linux: SC and JP faces give different outlines for U+9AA8",
          okSc && okJp && sSc != 0 && sJp != 0 && sSc != sJp,
          "sc=" + to_string(sSc) + " jp=" + to_string(sJp));
}
#endif

int main() {
    EventListener logSub = getLogger().onLog.listen([](LogEventArgs& e) {
        if (e.level == LogLevel::Error) {
            ++g_errors;
            g_lastError = e.message;
        }
    });

    const Bytes ttc = makeCollection({face0(), face1()});

    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec) / ("tc_fontFaceIndex_" + to_string(chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir, ec);
    const fs::path ttcPath = dir / "two-faces.ttc";
    const fs::path singlePath = dir / "single.ttf";
    if (!writeFile(ttcPath, ttc) || !writeFile(singlePath, face1())) {
        printf("test bug: cannot write to %s\n", dir.string().c_str());
        return 2;
    }

    checkMemory(ttc);
    checkFile(ttcPath);
    checkPostScriptNames(ttc, ttcPath, singlePath);
#ifdef __linux__
    checkSystemFaces();
#else
    printf("SKIP: system font face check runs on Linux; check MS PGothic / Hiragino by hand\n");
#endif

    fs::remove_all(dir, ec);
    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail,
           g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
