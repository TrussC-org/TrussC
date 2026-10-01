// =============================================================================
// fontSfntCheck — font data is checked before it is given to stb_truetype
//
// FontAtlasManager checks the sfnt skeleton against the data size before the
// data is given to stb_truetype, and setupFromMemory() returns false with a
// warning when it does not hold:
// - fewer than 12 bytes (including 0 bytes with a null pointer),
// - a .ttc header whose font count or font offset points outside the data,
// - a table directory, or any table, that runs past the end of the data
//   (offset + length is checked in 64 bits),
// - a missing cmap / head / hhea / hmtx / maxp, or loca (TrueType) / CFF,
//   where a directory entry at offset 0 counts as missing (as in stb),
// - head / hhea / maxp / cmap shorter than the fields stb reads, cmap
//   encoding records past cmap, a used cmap subtable offset past cmap,
// - hhea numberOfHMetrics outside 1..numGlyphs, hmtx shorter than
//   4*numLong + 2*(numGlyphs-numLong),
// - loca shorter than numGlyphs+1 entries, an unknown loca format, a loca
//   entry past the end of glyf,
// - truncated copies of a TrueType, a CFF and a collection font (mid header,
//   mid table directory, mid table body).
// - loca entries that decrease,
// - a CFF CharStrings INDEX whose count or offset array cannot be read
//   within the CFF table (an INDEX past the table, an empty one, a count
//   past the table),
// - a CFF table of 0 to 3 bytes, a CFF INDEX with an offset size outside
//   1..4, and an empty Top DICT INDEX or Top DICT (refused by stbtt_InitFont,
//   with an error).
// Plus:
// - a glyph index from the cmap past numGlyphs, or for CFF past the number
//   of CharStrings, is treated as .notdef,
// - a codepoint above U+10FFFF is answered as missing (.notdef),
// - CFF data is read within the CFF table's length (a CharStrings offset
//   past the table finds no CharStrings INDEX),
// - a glyph with a one-point contour loads and rasterizes, plain and
//   oversampled,
// - CFF vertex counting stops within the range stb handles, and a vertex
//   array that cannot be allocated leaves the glyph empty (with limits
//   lowered through internal::setStbttLimitsForTests()): a glyph over the
//   limit, one whose closing vertex is the one over it, one far over any
//   limit through nested subroutines, and one whose array allocation fails
//   all come back empty,
// - the flattened point count of a glyph stops within the range stb handles
//   (with the limit lowered): a glyph with more points than the limit is not
//   drawn, one at the limit is, and FontAtlasManager still gives it a glyph
//   entry,
// - valid fonts still load, with the metrics they are built with, including
//   numberOfHMetrics == numGlyphs, a cmap format 12 subtable, a table of
//   length 0 and tables that share bytes with another.
// The fonts are built here, so the test runs the same everywhere. Installed
// fonts found at the usual system paths are also loaded and cut short.
//
// Run the core tests under AddressSanitizer after changing stb_truetype or
// core/include/impl/stb_impl.cpp; CI does not build with ASan. For this test,
// from the repository root:
//   tools/bin/trusscli update -p core/tests/fontSfntCheck --tc-root "$PWD" --ide cmake
//   cd core/tests/fontSfntCheck
//   cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Release \
//     -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g" \
//     -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address
//   cmake --build build-asan -j4
//   ./bin/fontSfntCheck
// (prefix the last line with `setarch -R` where ASan fails to start because
// of the kernel's address randomization).
//
// `fontSfntCheck --dump <font files>` prints the metrics of a few glyphs, to
// compare two builds.
// =============================================================================

#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#ifdef __linux__
#include <sys/resource.h>
#endif

using namespace std;
using namespace tc;

namespace {

static int g_fail = 0;
static void check(const string& name, bool ok, const string& detail = "") {
    printf("%-72s %s%s\n", name.c_str(), ok ? "PASS" : "FAIL",
           ok || detail.empty() ? "" : ("  -- " + detail).c_str());
    fflush(stdout);
    if (!ok) ++g_fail;
}

static int g_warnings = 0;
static string g_lastWarning;
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
static void set16(Bytes& b, size_t at, uint32_t v) {
    b[at] = (uint8_t)(v >> 8);
    b[at + 1] = (uint8_t)v;
}
static void set32(Bytes& b, size_t at, uint32_t v) {
    set16(b, at, v >> 16);
    set16(b, at + 2, v & 0xffff);
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

// Directory in the given order, tables laid out in the same order, 4-byte
// aligned. No padding after the last table: the data ends where it ends.
static Bytes buildSfnt(uint32_t version, const vector<Table>& tables) {
    Bytes out;
    put32(out, version);
    put16(out, (uint32_t)tables.size());
    put16(out, 0);  // searchRange etc.: stb does not read them
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
        put32(out, 0);  // checksum
        put32(out, (uint32_t)offsets[i]);
        put32(out, (uint32_t)tables[i].data.size());
    }
    for (size_t i = 0; i < tables.size(); i++) {
        while (out.size() < offsets[i]) out.push_back(0);
        out.insert(out.end(), tables[i].data.begin(), tables[i].data.end());
    }
    return out;
}

struct Loc {
    size_t entry = 0;   // directory entry
    size_t offset = 0;  // table data
    size_t length = 0;
};
static Loc findTable(const Bytes& f, const char* tag, size_t fontStart = 0) {
    const size_t n = get16(f, fontStart + 4);
    for (size_t i = 0; i < n; i++) {
        const size_t rec = fontStart + 12 + 16 * i;
        if (get32(f, rec) == tagOf(tag)) return {rec, get32(f, rec + 8), get32(f, rec + 12)};
    }
    printf("test bug: no table %s\n", tag);
    exit(2);
}

// --- tables ------------------------------------------------------------------
static const int kUnitsPerEm = 1000;
static const int kNumGlyphs = 4;

static Bytes makeHead(int locaFormat) {
    Bytes b;
    put32(b, 0x00010000);  // version
    put32(b, 0x00010000);  // fontRevision
    put32(b, 0);           // checkSumAdjustment
    put32(b, 0x5F0F3CF5);  // magicNumber
    put16(b, 0);           // flags
    put16(b, kUnitsPerEm);
    for (int i = 0; i < 16; i++) b.push_back(0);  // created, modified
    put16(b, 0);           // xMin
    put16(b, 0);           // yMin
    put16(b, 500);         // xMax
    put16(b, 700);         // yMax
    put16(b, 0);           // macStyle
    put16(b, 8);           // lowestRecPPEM
    put16(b, 2);           // fontDirectionHint
    put16(b, (uint32_t)locaFormat);
    put16(b, 0);           // glyphDataFormat
    return b;              // 54 bytes
}

static Bytes makeHhea(int numLong) {
    Bytes b;
    put32(b, 0x00010000);
    put16(b, 800);                 // ascender
    put16(b, (uint16_t)-200);      // descender
    put16(b, 0);                   // lineGap
    put16(b, 600);                 // advanceWidthMax
    for (int i = 0; i < 11; i++) put16(b, 0);
    put16(b, (uint32_t)numLong);   // numberOfHMetrics at +34
    return b;                      // 36 bytes
}

static Bytes makeMaxp(int numGlyphs) {
    Bytes b;
    put32(b, 0x00005000);  // version 0.5
    put16(b, (uint32_t)numGlyphs);
    return b;
}

// Advances: glyph 0 = 500, every other glyph 600 (with numLong = 2, glyphs
// 2 and up repeat the last long entry). The default is 12 bytes: two long
// entries and two short ones.
static Bytes makeHmtx(int numGlyphs = kNumGlyphs, int numLong = 2) {
    Bytes b;
    put16(b, 500); put16(b, 50);
    for (int i = 1; i < numLong; i++) { put16(b, 600); put16(b, 0); }
    for (int i = numLong; i < numGlyphs; i++) put16(b, i == 3 ? 100 : 0);
    return b;
}

// Format 4 subtable (Microsoft, Unicode BMP). By default ' ' -> 2, 'A' -> 1,
// 'B' -> 4 (== numGlyphs: past the last glyph), 'C' -> 3. Segments sorted,
// the closing 0xFFFF segment is added here.
struct Seg { uint16_t start, end; uint16_t glyph; };
static Bytes makeCmap(vector<Seg> segs = {{0x20, 0x20, 2}, {0x41, 0x41, 1}, {0x42, 0x42, 4},
                                          {0x43, 0x43, 3}}) {
    segs.push_back({0xFFFF, 0xFFFF, 0});
    const uint32_t segCount = (uint32_t)segs.size();
    uint32_t searchRange = 2, entrySelector = 0;
    while (searchRange * 2 <= segCount * 2) { searchRange *= 2; entrySelector++; }
    Bytes sub;
    put16(sub, 4);
    put16(sub, 16 + 8 * segCount);
    put16(sub, 0);
    put16(sub, segCount * 2);
    put16(sub, searchRange);
    put16(sub, entrySelector);
    put16(sub, segCount * 2 - searchRange);
    for (auto& s : segs) put16(sub, s.end);
    put16(sub, 0);
    for (auto& s : segs) put16(sub, s.start);
    for (auto& s : segs) put16(sub, (uint16_t)(s.start == 0xFFFF ? 1 : s.glyph - s.start));
    for (size_t i = 0; i < segs.size(); i++) put16(sub, 0);
    Bytes b;
    put16(b, 0);           // version
    put16(b, 1);           // numTables
    put16(b, 3);           // Microsoft
    put16(b, 1);           // Unicode BMP
    put32(b, 12);          // subtable offset
    b.insert(b.end(), sub.begin(), sub.end());
    return b;
}

// Format 12 subtable (Microsoft, Unicode full). Groups of one codepoint each,
// sorted by codepoint.
static Bytes makeCmap12(const vector<pair<uint32_t, uint32_t>>& codepointToGlyph) {
    Bytes sub;
    put16(sub, 12);
    put16(sub, 0);
    put32(sub, 16 + 12 * (uint32_t)codepointToGlyph.size());
    put32(sub, 0);  // language
    put32(sub, (uint32_t)codepointToGlyph.size());
    for (auto& g : codepointToGlyph) {
        put32(sub, g.first);
        put32(sub, g.first);
        put32(sub, g.second);
    }
    Bytes b;
    put16(b, 0);           // version
    put16(b, 1);           // numTables
    put16(b, 3);           // Microsoft
    put16(b, 10);          // Unicode full
    put32(b, 12);          // subtable offset
    b.insert(b.end(), sub.begin(), sub.end());
    return b;
}

struct Pt { int x, y; bool on; };
static Bytes simpleGlyph(const vector<vector<Pt>>& contours) {
    int xMin = 1 << 30, yMin = 1 << 30, xMax = -(1 << 30), yMax = -(1 << 30);
    vector<Pt> pts;
    for (auto& c : contours) {
        for (auto& p : c) {
            pts.push_back(p);
            xMin = min(xMin, p.x); yMin = min(yMin, p.y);
            xMax = max(xMax, p.x); yMax = max(yMax, p.y);
        }
    }
    Bytes b;
    put16(b, (uint32_t)contours.size());
    put16(b, (uint16_t)xMin); put16(b, (uint16_t)yMin);
    put16(b, (uint16_t)xMax); put16(b, (uint16_t)yMax);
    int end = -1;
    for (auto& c : contours) { end += (int)c.size(); put16(b, (uint32_t)end); }
    put16(b, 0);  // instructionLength
    for (auto& p : pts) b.push_back(p.on ? 1 : 0);  // int16 x and y deltas
    int prev = 0;
    for (auto& p : pts) { put16(b, (uint16_t)(p.x - prev)); prev = p.x; }
    prev = 0;
    for (auto& p : pts) { put16(b, (uint16_t)(p.y - prev)); prev = p.y; }
    pad4(b);
    return b;
}

// glyph 0: .notdef box, 1: 'A' with a curve, 2: space (empty),
// 3: 'C', a triangle plus a one-point contour (one off-curve point).
static void makeGlyfLoca(int locaFormat, Bytes& glyf, Bytes& loca) {
    const vector<Bytes> glyphs = {
        simpleGlyph({{{50, 0, true}, {450, 0, true}, {450, 700, true}, {50, 700, true}}}),
        simpleGlyph({{{0, 0, true}, {250, 700, false}, {500, 0, true}}}),
        Bytes{},
        simpleGlyph({{{100, 0, true}, {300, 500, true}, {500, 0, true}},
                     {{300, 200, false}}}),
    };
    glyf.clear();
    loca.clear();
    for (auto& g : glyphs) {
        if (locaFormat == 0) put16(loca, (uint32_t)(glyf.size() / 2));
        else put32(loca, (uint32_t)glyf.size());
        glyf.insert(glyf.end(), g.begin(), g.end());
    }
    if (locaFormat == 0) put16(loca, (uint32_t)(glyf.size() / 2));
    else put32(loca, (uint32_t)glyf.size());
}

// hmtx goes last, so it ends where the data ends. `extra` tables go between
// cmap and loca.
static Bytes makeTrueType(int locaFormat = 0, int numLong = 2, const Bytes& cmap = makeCmap(),
                          const vector<Table>& extra = {}) {
    Bytes glyf, loca;
    makeGlyfLoca(locaFormat, glyf, loca);
    vector<Table> tables = {{"head", makeHead(locaFormat)},
                            {"hhea", makeHhea(numLong)},
                            {"maxp", makeMaxp(kNumGlyphs)},
                            {"cmap", cmap}};
    tables.insert(tables.end(), extra.begin(), extra.end());
    tables.push_back({"loca", loca});
    tables.push_back({"glyf", glyf});
    tables.push_back({"hmtx", makeHmtx(kNumGlyphs, numLong)});
    return buildSfnt(0x00010000, tables);
}

// --- CFF ---------------------------------------------------------------------
static void csNum(Bytes& b, int v) {
    if (v >= -107 && v <= 107) {
        b.push_back((uint8_t)(v + 139));
    } else {
        b.push_back(28);
        put16(b, (uint16_t)v);
    }
}
static Bytes cffIndex(const vector<Bytes>& items) {
    Bytes b;
    put16(b, (uint32_t)items.size());
    if (items.empty()) return b;
    b.push_back(4);  // offSize
    uint32_t off = 1;
    put32(b, off);
    for (auto& it : items) { off += (uint32_t)it.size(); put32(b, off); }
    for (auto& it : items) b.insert(b.end(), it.begin(), it.end());
    return b;
}
static vector<Bytes> cffCharStrings() {
    Bytes notdef;         // box
    csNum(notdef, 50); csNum(notdef, 0); notdef.push_back(21);    // rmoveto
    csNum(notdef, 400); csNum(notdef, 0); notdef.push_back(5);    // rlineto
    csNum(notdef, 0); csNum(notdef, 700); notdef.push_back(5);
    csNum(notdef, -400); csNum(notdef, 0); notdef.push_back(5);
    notdef.push_back(14);                                         // endchar
    Bytes a;              // box
    csNum(a, 100); csNum(a, 0); a.push_back(21);          // rmoveto
    csNum(a, 400); csNum(a, 0); a.push_back(5);           // rlineto
    csNum(a, 0); csNum(a, 500); a.push_back(5);
    csNum(a, -400); csNum(a, 0); a.push_back(5);
    a.push_back(14);
    Bytes space = {14};
    Bytes c;              // triangle
    csNum(c, 100); csNum(c, 0); c.push_back(21);
    csNum(c, 200); csNum(c, 500); c.push_back(5);
    csNum(c, 200); csNum(c, -500); c.push_back(5);
    c.push_back(14);
    return {notdef, a, space, c};
}
// CFF with header, Name, Top DICT (CharStrings offset only), String and
// Global Subr INDEXes. The CharStrings INDEX sits at `charStringsAt`, inside
// the table when `inside`, otherwise the caller puts it after the table.
static Bytes makeCff(uint32_t charStringsAt, bool inside,
                     const vector<Bytes>& charStrings = cffCharStrings(),
                     const vector<Bytes>& gsubrs = {}) {
    Bytes b = {1, 0, 4, 4};  // major, minor, hdrSize, offSize
    const Bytes name = cffIndex({Bytes{'T'}});
    Bytes dict = {29};
    put32(dict, charStringsAt);
    dict.push_back(17);  // CharStrings
    const Bytes top = cffIndex({dict});
    b.insert(b.end(), name.begin(), name.end());
    b.insert(b.end(), top.begin(), top.end());
    put16(b, 0);  // String INDEX
    const Bytes gs = cffIndex(gsubrs);
    b.insert(b.end(), gs.begin(), gs.end());
    if (inside) {
        if (b.size() != charStringsAt) {
            printf("test bug: CharStrings at %zu, not %u\n", b.size(), charStringsAt);
            exit(2);
        }
        const Bytes cs = cffIndex(charStrings);
        b.insert(b.end(), cs.begin(), cs.end());
    }
    return b;
}
// A CFF font around the given CFF table.
static Bytes makeCffFontWithTable(const Bytes& cffTable, int numGlyphs = kNumGlyphs,
                                  const Bytes& cmap = makeCmap()) {
    return buildSfnt(tagOf("OTTO"), {{"head", makeHead(0)},
                                     {"hhea", makeHhea(2)},
                                     {"maxp", makeMaxp(numGlyphs)},
                                     {"cmap", cmap},
                                     {"CFF ", cffTable},
                                     {"hmtx", makeHmtx(numGlyphs)}});
}
static Bytes makeCffFont(const vector<Bytes>& charStrings = cffCharStrings(),
                         const vector<Bytes>& gsubrs = {}, int numGlyphs = kNumGlyphs,
                         const Bytes& cmap = makeCmap()) {
    // The Top DICT has a fixed size, so the CharStrings INDEX starts where
    // the table without it ends.
    const uint32_t charStringsAt = (uint32_t)makeCff(0, false, charStrings, gsubrs).size();
    return makeCffFontWithTable(makeCff(charStringsAt, true, charStrings, gsubrs), numGlyphs,
                                cmap);
}

// CharStrings offset points just past the CFF table; the next table holds a
// well-formed CharStrings INDEX, outside the CFF table.
static Bytes makeCffFontCharStringsOutside() {
    const Bytes probe = makeCff(0, false);
    const uint32_t after = (uint32_t)((probe.size() + 3) & ~size_t(3));
    return buildSfnt(tagOf("OTTO"), {{"head", makeHead(0)},
                                     {"hhea", makeHhea(2)},
                                     {"maxp", makeMaxp(kNumGlyphs)},
                                     {"cmap", makeCmap()},
                                     {"CFF ", makeCff(after, false)},
                                     {"XCHR", cffIndex(cffCharStrings())},
                                     {"hmtx", makeHmtx()}});
}

// Wrap one sfnt into a .ttc with one font.
static Bytes makeCollection(const Bytes& font) {
    Bytes b;
    put32(b, tagOf("ttcf"));
    put32(b, 0x00010000);
    put32(b, 1);
    put32(b, 16);
    // Table offsets inside a collection are from the start of the file.
    Bytes f = font;
    const size_t n = get16(f, 4);
    for (size_t i = 0; i < n; i++) {
        const size_t rec = 12 + 16 * i;
        set32(f, rec + 8, get32(f, rec + 8) + 16);
    }
    b.insert(b.end(), f.begin(), f.end());
    return b;
}

// --- loading -----------------------------------------------------------------
static const int kFontSize = 100;

struct LoadOutcome {
    bool ok = false;
    int warnings = 0;
};
static LoadOutcome tryLoad(internal::FontAtlasManager& m, const uint8_t* data, size_t size) {
    const int before = g_warnings;
    LoadOutcome r;
    r.ok = m.setupFromMemory(data, size, kFontSize);
    r.warnings = g_warnings - before;
    return r;
}
static LoadOutcome tryLoad(internal::FontAtlasManager& m, const Bytes& f) {
    return tryLoad(m, f.data(), f.size());
}

// `reason` is a part of the warning to expect (the check that should reject
// the font); empty accepts any.
static void expectRejected(const string& name, const Bytes& f, const string& reason = "") {
    internal::FontAtlasManager m;
    g_lastWarning.clear();
    const LoadOutcome r = tryLoad(m, f);
    check("rejected: " + name,
          !r.ok && r.warnings >= 1 && g_lastWarning.find(reason) != string::npos,
          string("ok=") + (r.ok ? "true" : "false") + " warnings=" + to_string(r.warnings) +
              " last=\"" + g_lastWarning + "\"");
}

static bool closeTo(float a, float b) { return fabs(a - b) < 1e-4f; }

// --- valid fonts -------------------------------------------------------------
static void checkValid(const string& label, const Bytes& f) {
    internal::FontAtlasManager m;
    const LoadOutcome r = tryLoad(m, f);
    check(label + ": loads without a warning", r.ok && r.warnings == 0,
          "warnings=" + to_string(r.warnings));
    if (!r.ok) return;
    const float scale = (float)kFontSize / 1000.0f;  // ascender - descender = 1000
    check(label + ": ascent / descent from hhea",
          closeTo(m.getAscent(), 800 * scale) && closeTo(m.getDescent(), -200 * scale));
    check(label + ": space advance from hmtx", closeTo(m.getSpaceAdvance(), 600 * scale));
    check(label + ": 'A' advance 0.6 em", closeTo(m.getGlyphAdvanceEm('A'), 0.6f));
    check(label + ": 'A' has a glyph", m.fontHasGlyph('A'));
    const Path pa = m.getGlyphPath('A');
    check(label + ": 'A' outline has one contour", pa.getNumSubpaths() == 1,
          "subpaths=" + to_string(pa.getNumSubpaths()));
    const internal::GlyphInfo* ga = m.getOrLoadGlyph('A');
    check(label + ": 'A' rasterizes", ga && ga->isValid() && ga->getWidth() > 0 &&
          closeTo(ga->getAdvance(), 600 * scale));
    const internal::GlyphInfo* gs = m.getOrLoadGlyph(' ');
    check(label + ": space rasterizes empty", gs && gs->isValid() && gs->getWidth() == 0);
}

// --- glyph index past numGlyphs ----------------------------------------------
static void checkGlyphIndexClamp(const string& label, const Bytes& f) {
    internal::FontAtlasManager m;
    if (!tryLoad(m, f).ok) {
        check(label + ": loads", false);
        return;
    }
    const float scale = (float)kFontSize / 1000.0f;
    // 'B' maps to glyph 4 in a 4-glyph font.
    check(label + ": 'B' (glyph past numGlyphs) is not a glyph", !m.fontHasGlyph('B'));
    check(label + ": 'B' advance is 0", m.getGlyphAdvanceEm('B') == 0.0f,
          to_string(m.getGlyphAdvanceEm('B')));
    const int before = g_warnings;
    const Path pb = m.getGlyphPath('B');
    check(label + ": 'B' outline is empty (with the no-glyph warning)",
          pb.empty() && g_warnings == before + 1,
          "vertices=" + to_string(pb.size()) + " warnings=" + to_string(g_warnings - before));
    const internal::GlyphInfo* gb = m.getOrLoadGlyph('B');
    check(label + ": 'B' draws as .notdef",
          gb && gb->isValid() && closeTo(gb->getAdvance(), 500 * scale) && gb->getWidth() > 0,
          gb ? "advance=" + to_string(gb->getAdvance()) : "null");
}

// --- CFF: fewer CharStrings than glyphs -----------------------------------------
static void checkCffCharStringsCount() {
    const float scale = (float)kFontSize / 1000.0f;
    // Three CharStrings (.notdef, 'A', space) for four glyphs: 'C' maps to
    // glyph 3, which maxp allows and the CharStrings INDEX does not have.
    vector<Bytes> cs = cffCharStrings();
    cs.pop_back();
    const string label = "CFF, 3 CharStrings for 4 glyphs";
    internal::FontAtlasManager m;
    if (!tryLoad(m, makeCffFont(cs)).ok) {
        check(label + ": loads", false);
        return;
    }
    check(label + ": 'A' has a glyph", m.fontHasGlyph('A'));
    check(label + ": 'C' (glyph past the CharStrings) is not a glyph", !m.fontHasGlyph('C'));
    check(label + ": 'C' advance is 0", m.getGlyphAdvanceEm('C') == 0.0f,
          to_string(m.getGlyphAdvanceEm('C')));
    const int before = g_warnings;
    const Path pc = m.getGlyphPath('C');
    check(label + ": 'C' outline is empty (with the no-glyph warning)",
          pc.empty() && g_warnings == before + 1,
          "vertices=" + to_string(pc.size()) + " warnings=" + to_string(g_warnings - before));
    const internal::GlyphInfo* gc = m.getOrLoadGlyph('C');
    check(label + ": 'C' draws as .notdef",
          gc && gc->isValid() && closeTo(gc->getAdvance(), 500 * scale) && gc->getWidth() > 0,
          gc ? "advance=" + to_string(gc->getAdvance()) : "null");

    // No CharStrings at all: the count is 0, and the font is refused.
    expectRejected("CFF with an empty CharStrings INDEX", makeCffFont({}),
                   "CFF CharStrings INDEX cannot be read");
    // A count whose offset array does not fit in the CFF table.
    {
        Bytes f = makeCffFont();
        const Loc cff = findTable(f, "CFF ");
        const size_t charStringsAt = makeCff(0, false).size();
        set16(f, cff.offset + charStringsAt, 0xFFFF);
        expectRejected("CFF CharStrings count past the CFF table", f,
                       "CFF CharStrings INDEX cannot be read");
    }
}

// --- malformed CFF data ----------------------------------------------------------
// Refused either by the checks (a warning) or by stbtt_InitFont (an error).
static void expectNotLoaded(const string& name, const Bytes& f) {
    internal::FontAtlasManager m;
    const int errorsBefore = g_errors;
    const LoadOutcome r = tryLoad(m, f);
    const int logged = r.warnings + (g_errors - errorsBefore);
    check("rejected: " + name, !r.ok && logged >= 1,
          string("ok=") + (r.ok ? "true" : "false") + " logged=" + to_string(logged));
}

static void checkCffMalformed() {
    const Bytes cff = makeCffFont();
    const Loc table = findTable(cff, "CFF ");
    // A CFF table shorter than its 4-byte header.
    for (uint32_t len = 0; len <= 3; len++) {
        Bytes f = cff;
        set32(f, table.entry + 12, len);
        expectNotLoaded("CFF table of length " + to_string(len), f);
    }
    // Offset size outside 1..4 in the Name, Top DICT and CharStrings INDEX.
    // makeCff() lays out the header (4 bytes), then the Name INDEX (12 bytes),
    // then the Top DICT INDEX.
    const size_t charStringsAt = makeCff(0, false).size();
    const struct { const char* name; size_t offSizeAt; } indexes[] = {
        {"Name", 4 + 2}, {"Top DICT", 16 + 2}, {"CharStrings", charStringsAt + 2}};
    for (const auto& index : indexes) {
        for (uint8_t offSize : {uint8_t(0), uint8_t(5), uint8_t(255)}) {
            Bytes f = cff;
            f[table.offset + index.offSizeAt] = offSize;
            expectNotLoaded(string("CFF ") + index.name + " INDEX with offset size " +
                                to_string(offSize), f);
        }
    }
    // An empty Top DICT INDEX, and a Top DICT INDEX whose one DICT is empty.
    for (const bool emptyIndex : {true, false}) {
        Bytes b = {1, 0, 4, 4};
        const Bytes name = cffIndex({Bytes{'T'}});
        const Bytes top = emptyIndex ? cffIndex({}) : cffIndex({Bytes{}});
        const Bytes cs = cffIndex(cffCharStrings());
        b.insert(b.end(), name.begin(), name.end());
        b.insert(b.end(), top.begin(), top.end());
        put16(b, 0);  // String INDEX
        put16(b, 0);  // Global Subr INDEX
        b.insert(b.end(), cs.begin(), cs.end());
        expectNotLoaded(emptyIndex ? "CFF with an empty Top DICT INDEX"
                                   : "CFF with an empty Top DICT",
                        makeCffFontWithTable(b));
    }
}

// --- codepoints above U+10FFFF -----------------------------------------------------
static void checkCodepointRange() {
    // A format 12 cmap can map any 32-bit value.
    const Bytes f = makeTrueType(0, 2, makeCmap12({{0x20, 2}, {0x41, 1}, {0x10FFFF, 1},
                                                   {0x110041, 1}, {0xFFFFFFFF, 1}}));
    checkValid("TrueType, cmap format 12", f);
    internal::FontAtlasManager m;
    if (!tryLoad(m, f).ok) return;
    const float scale = (float)kFontSize / 1000.0f;
    check("codepoints: U+10FFFF has a glyph", m.fontHasGlyph(0x10FFFF));
    check("codepoints: 0x110041 is not a glyph", !m.fontHasGlyph(0x110041));
    check("codepoints: 0xFFFFFFFF is not a glyph", !m.fontHasGlyph(0xFFFFFFFFu));
    check("codepoints: 0x110041 advance is 0", m.getGlyphAdvanceEm(0x110041) == 0.0f);
    const Path p = m.getGlyphPath(0xFFFFFFFFu);
    check("codepoints: 0xFFFFFFFF outline is empty", p.empty(), "vertices=" + to_string(p.size()));
    const internal::GlyphInfo* g = m.getOrLoadGlyph(0x110041);
    check("codepoints: 0x110041 draws as .notdef",
          g && g->isValid() && closeTo(g->getAdvance(), 500 * scale),
          g ? "advance=" + to_string(g->getAdvance()) : "null");
}

// --- CFF vertex count --------------------------------------------------------------
// Pushes the operand for global subroutine `index` (bias 107 for fewer than
// 1240 subroutines) and calls it.
static void csCallGsubr(Bytes& b, int index) {
    csNum(b, index - 107);
    b.push_back(29);  // callgsubr
}

// Global subroutines: G0 draws 256 one-unit lines that end where they
// started; G1, G2 and G3 each call the one before 256 times, so G<n> draws
// 256^(n+1) lines.
static vector<Bytes> vertexCountGsubrs() {
    vector<Bytes> g(4);
    for (int k = 0; k < 16; k++) {
        for (int i = 0; i < 4; i++) {
            csNum(g[0], 1); csNum(g[0], 1); csNum(g[0], -1); csNum(g[0], -1);
        }
        g[0].push_back(6);  // hlineto, 16 arguments: 16 lines
    }
    g[0].push_back(11);  // return
    for (int n = 1; n < 4; n++) {
        for (int k = 0; k < 256; k++) csCallGsubr(g[n], n - 1);
        g[n].push_back(11);
    }
    return g;
}

// Glyphs 0-3 as in cffCharStrings() ('C' is a triangle: 4 vertices with the
// closing line), 4 ('D'): G3, far over any vertex limit, 5 ('E'): 2^18 + 1
// vertices.
static Bytes makeVertexCountFont() {
    vector<Bytes> cs = cffCharStrings();
    Bytes d;
    csNum(d, 0); csNum(d, 0); d.push_back(21);  // rmoveto
    csCallGsubr(d, 3);
    d.push_back(14);
    Bytes e;
    csNum(e, 0); csNum(e, 0); e.push_back(21);
    for (int k = 0; k < 4; k++) csCallGsubr(e, 1);
    e.push_back(14);
    cs.push_back(d);
    cs.push_back(e);
    return makeCffFont(cs, vertexCountGsubrs(), 6,
                       makeCmap({{0x20, 0x20, 2}, {0x41, 0x41, 1}, {0x43, 0x43, 3},
                                 {0x44, 0x44, 4}, {0x45, 0x45, 5}}));
}

static void checkVertexCount() {
    const Bytes f = makeVertexCountFont();
    // INT_MAX is clamped to the default limit.
    const int defaultMaxVertices = INT_MAX;
    auto outlineSize = [&](uint32_t cp, int maxVertices, size_t mallocMax) {
        internal::setStbttLimitsForTests(maxVertices, INT_MAX, mallocMax);
        internal::FontAtlasManager m;
        int n = -1;
        if (tryLoad(m, f).ok) n = m.getGlyphPath(cp).size();
        internal::resetStbttLimitsForTests();
        return n;
    };
    int n = outlineSize('C', 4, SIZE_MAX);
    check("vertex count: 'C' (4 vertices) with a limit of 4 has an outline", n > 0,
          "vertices=" + to_string(n));
    n = outlineSize('C', 3, SIZE_MAX);
    check("vertex count: 'C' (4 vertices) with a limit of 3 is empty", n == 0,
          "vertices=" + to_string(n));
    n = outlineSize('E', 1 << 10, SIZE_MAX);
    check("vertex count: 'E' (2^18 + 1 vertices) with a limit of 2^10 is empty", n == 0,
          "vertices=" + to_string(n));
    n = outlineSize('E', defaultMaxVertices, 1024);
    check("vertex count: 'E' is empty when its vertex array cannot be allocated", n == 0,
          "vertices=" + to_string(n));
    n = outlineSize('E', defaultMaxVertices, SIZE_MAX);
    check("vertex count: 'E' (2^18 + 1 vertices) has an outline", n > 0,
          "vertices=" + to_string(n));

    // 'D' is far over any limit.
    internal::FontAtlasManager m;
    if (!tryLoad(m, f).ok) {
        check("vertex count: font loads", false);
        return;
    }
    check("vertex count: 'A' has one contour", m.getGlyphPath('A').getNumSubpaths() == 1);
    const Path pd = m.getGlyphPath('D');
    check("vertex count: 'D' (far over the limit) is empty", pd.empty(),
          "vertices=" + to_string(pd.size()));
    const internal::GlyphInfo* gd = m.getOrLoadGlyph('D');
    check("vertex count: 'D' draws empty", gd && gd->isValid() && gd->getWidth() == 0);
}

// --- flattened point count ----------------------------------------------------------
// Coverage of a glyph rasterized by stb_truetype into a zeroed bitmap, with
// the flattened point limit at maxPoints (INT_MAX: the default); -1 when the
// font does not load or the glyph box is empty.
static long rasterCoverage(const Bytes& f, int glyph, float scale, int maxPoints) {
    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, f.data(), 0)) return -1;
    int x0, y0, x1, y1;
    stbtt_GetGlyphBitmapBox(&info, glyph, scale, scale, &x0, &y0, &x1, &y1);
    const int w = x1 - x0, h = y1 - y0;
    if (w <= 0 || h <= 0) return -1;
    vector<uint8_t> pixels((size_t)w * h, 0);
    internal::setStbttLimitsForTests(INT_MAX, maxPoints, SIZE_MAX);
    stbtt_MakeGlyphBitmap(&info, pixels.data(), w, h, w, scale, scale, glyph);
    internal::resetStbttLimitsForTests();
    long sum = 0;
    for (uint8_t v : pixels) sum += v;
    return sum;
}

static void checkPointCount() {
    const Bytes f = makeTrueType();
    // Glyph 0 is a box: 5 points (the move, three lines and the closing line).
    long c = rasterCoverage(f, 0, 0.1f, 5);
    check("point count: box (5 points) with a limit of 5 is drawn", c > 0,
          "coverage=" + to_string(c));
    c = rasterCoverage(f, 0, 0.1f, 4);
    check("point count: box (5 points) with a limit of 4 is not drawn", c == 0,
          "coverage=" + to_string(c));
    // Glyph 1 ('A') has a curve, which is split into many points at scale 1.
    c = rasterCoverage(f, 1, 1.0f, INT_MAX);
    check("point count: 'A' (a curve) is drawn", c > 0, "coverage=" + to_string(c));
    c = rasterCoverage(f, 1, 1.0f, 8);
    check("point count: 'A' (a curve) with a limit of 8 is not drawn", c == 0,
          "coverage=" + to_string(c));

    // Through FontAtlasManager, plain and oversampled.
    for (int os : {1, 2}) {
        internal::FontAtlasManager m;
        if (!tryLoad(m, f).ok) {
            check("point count: font loads", false);
            return;
        }
        m.setOversample(os);
        internal::setStbttLimitsForTests(INT_MAX, 4, SIZE_MAX);
        const internal::GlyphInfo* ga = m.getOrLoadGlyph('A');
        internal::resetStbttLimitsForTests();
        check("point count: 'A' over the limit gets a glyph entry (oversample " +
                  to_string(os) + ")",
              ga && ga->isValid() && closeTo(ga->getAdvance(), 60.0f));
    }
}

// --- one-point contour -----------------------------------------------------------
static void checkOnePointContour() {
    internal::FontAtlasManager m;
    if (!tryLoad(m, makeTrueType()).ok) {
        check("one-point contour: loads", false);
        return;
    }
    const Path pc = m.getGlyphPath('C');
    check("one-point contour: outline has two contours", pc.getNumSubpaths() == 2,
          "subpaths=" + to_string(pc.getNumSubpaths()));
    const internal::GlyphInfo* gc = m.getOrLoadGlyph('C');
    check("one-point contour: rasterizes", gc && gc->isValid() && gc->getWidth() > 0);
    // Oversampled rasterization goes through the same outline.
    internal::FontAtlasManager m2;
    const Bytes f = makeTrueType();
    m2.setupFromMemory(f.data(), f.size(), kFontSize);
    m2.setOversample(2);
    const internal::GlyphInfo* gc2 = m2.getOrLoadGlyph('C');
    check("one-point contour: rasterizes oversampled", gc2 && gc2->isValid());
}

// --- CFF length ----------------------------------------------------------------
static void checkCffLength() {
    // The skeleton is fine; the CharStrings offset is CFF data, which the
    // skeleton check does not look into. stb finds no CharStrings INDEX in
    // the CFF table, so there is no count to read and the font is refused.
    expectRejected("CFF: CharStrings past the CFF table are not read",
                   makeCffFontCharStringsOutside(), "CFF CharStrings INDEX cannot be read");
}

// --- malformed fonts -------------------------------------------------------------
static void checkMalformed() {
    const Bytes tt = makeTrueType();
    const Bytes ttLong = makeTrueType(1);
    const Bytes cff = makeCffFont();
    const Bytes ttc = makeCollection(tt);

    // Empty data
    {
        internal::FontAtlasManager m;
        const LoadOutcome r = tryLoad(m, nullptr, 0);
        check("rejected: 0 bytes, null pointer",
              !r.ok && r.warnings >= 1 && g_lastWarning.find("shorter than a font header") != string::npos);
        const uint8_t one = 0;
        const LoadOutcome r2 = tryLoad(m, &one, 0);
        check("rejected: 0 bytes, non-null pointer",
              !r2.ok && r2.warnings >= 1 && g_lastWarning.find("shorter than a font header") != string::npos);
        const LoadOutcome r3 = tryLoad(m, nullptr, 16);
        check("rejected: null pointer with a size",
              !r3.ok && r3.warnings >= 1 && g_lastWarning.find("pointer is null") != string::npos);
    }
    expectRejected("not a font (12 bytes of PNG signature)",
                   Bytes{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 13},
                   "not a TrueType");

    // Truncated: header, directory, every table body, for each font kind.
    struct Kind { string name; const Bytes* f; size_t fontStart; };
    for (const Kind& k : {Kind{"TrueType", &tt, 0}, Kind{"TrueType long loca", &ttLong, 0},
                          Kind{"CFF", &cff, 0}, Kind{"collection", &ttc, 16}}) {
        const Bytes& f = *k.f;
        for (size_t cut : {size_t(4), size_t(11), k.fontStart + 6, k.fontStart + 11}) {
            if (cut >= f.size()) continue;
            expectRejected(k.name + " cut to " + to_string(cut) + " bytes (header)",
                           Bytes(f.begin(), f.begin() + cut));
        }
        const size_t numTables = get16(f, k.fontStart + 4);
        const size_t dirEnd = k.fontStart + 12 + 16 * numTables;
        for (size_t cut : {k.fontStart + 12 + 5, k.fontStart + 12 + 16 * (numTables / 2) + 9,
                           dirEnd - 1}) {
            expectRejected(k.name + " cut to " + to_string(cut) + " bytes (table directory)",
                           Bytes(f.begin(), f.begin() + cut));
        }
        for (size_t i = 0; i < numTables; i++) {
            const size_t rec = k.fontStart + 12 + 16 * i;
            const size_t off = get32(f, rec + 8), len = get32(f, rec + 12);
            const string tag = f.size() >= rec + 4 ? string((const char*)&f[rec], 4) : "?";
            for (size_t cut : {off + len / 2, off + len - 1}) {
                expectRejected(k.name + " cut to " + to_string(cut) + " bytes (in '" + tag + "')",
                               Bytes(f.begin(), f.begin() + cut));
            }
        }
    }

    // Collection header
    {
        Bytes f = ttc;
        set32(f, 8, 0);
        expectRejected("collection: 0 fonts", f, "collection header is truncated");
        f = ttc;
        set32(f, 8, 0xFFFFFFFF);
        expectRejected("collection: font count past the data", f, "collection header is truncated");
        f = ttc;
        set32(f, 12, 0x7FFFFFF0);
        expectRejected("collection: font offset outside the data", f, "font offset in the collection");
        f = ttc;
        set32(f, 12, (uint32_t)f.size() - 8);
        expectRejected("collection: font offset + 12 past the data", f, "font offset in the collection");
        f = ttc;
        set32(f, 4, 0x00030000);
        expectRejected("collection: unknown version", f, "collection version");
        f = ttc;
        set32(f, 16, 0x12345678);
        expectRejected("collection: font is not an sfnt", f, "not a TrueType");
        checkValid("collection", ttc);
    }

    // Table directory and table bounds
    {
        Bytes f = tt;
        set16(f, 4, 0xFFFF);
        expectRejected("numTables past the data", f, "table directory is truncated");
        f = tt;
        Loc l = findTable(f, "glyf");
        set32(f, l.entry + 8, 0xFFFFFFF0);
        set32(f, l.entry + 12, 0x20);  // offset + length does not fit in 32 bits
        expectRejected("table offset + length over 32 bits", f, "table 'glyf' is outside");
        f = tt;
        l = findTable(f, "hmtx");
        set32(f, l.entry + 12, l.length + 1);
        expectRejected("last table one byte longer than the data", f, "table 'hmtx' is outside");
        // An unused table outside the data is rejected too.
        Bytes g = buildSfnt(0x00010000, {{"head", makeHead(0)}, {"zzzz", Bytes(8, 0)}});
        l = findTable(g, "zzzz");
        set32(g, l.entry + 8, 0x10000);
        expectRejected("unused table outside the data", g, "table 'zzzz' is outside");
    }

    // Required tables
    for (const char* tag : {"cmap", "head", "hhea", "hmtx", "maxp", "loca", "glyf"}) {
        Bytes f = tt;
        const Loc l = findTable(f, tag);
        set32(f, l.entry, tagOf("zzzz"));
        expectRejected(string("TrueType without '") + tag + "'", f,
                       string("missing required table '") +
                           (string(tag) == "glyf" ? "CFF " : tag) + "'");
    }
    {
        Bytes f = cff;
        const Loc l = findTable(f, "CFF ");
        set32(f, l.entry, tagOf("zzzz"));
        expectRejected("CFF font without 'CFF '", f, "missing required table 'CFF '");
    }

    // A directory entry at offset 0 counts as missing, as in stbtt_InitFont
    // (without glyf, the font is taken as CFF).
    for (const char* tag : {"cmap", "head", "hhea", "hmtx", "maxp", "loca", "glyf"}) {
        Bytes f = tt;
        const Loc l = findTable(f, tag);
        set32(f, l.entry + 8, 0);
        // maxp: a length that fits in the data from offset 0 and covers the
        // fields the check reads, so only the offset makes it missing.
        if (string(tag) == "maxp") set32(f, l.entry + 12, 6);
        expectRejected(string("TrueType with '") + tag + "' at offset 0", f,
                       string("missing required table '") +
                           (string(tag) == "glyf" ? "CFF " : tag) + "'");
    }
    {
        Bytes f = cff;
        Loc l = findTable(f, "maxp");
        set32(f, l.entry + 8, 0);
        set32(f, l.entry + 12, 6);
        expectRejected("CFF font with 'maxp' at offset 0", f, "missing required table 'maxp'");
        f = cff;
        l = findTable(f, "CFF ");
        set32(f, l.entry + 8, 0);
        set32(f, l.entry + 12, 12);
        expectRejected("CFF font with 'CFF ' at offset 0", f, "missing required table 'CFF '");
    }
    {
        // Two maxp entries: the first one (offset 0) is the one used, so the
        // font counts as without maxp.
        Bytes glyf, loca;
        makeGlyfLoca(0, glyf, loca);
        Bytes f = buildSfnt(0x00010000, {{"head", makeHead(0)},
                                         {"hhea", makeHhea(2)},
                                         {"maxp", makeMaxp(kNumGlyphs)},
                                         {"maxp", makeMaxp(kNumGlyphs)},
                                         {"cmap", makeCmap()},
                                         {"loca", loca},
                                         {"glyf", glyf},
                                         {"hmtx", makeHmtx()}});
        const Loc l = findTable(f, "maxp");
        set32(f, l.entry + 8, 0);
        set32(f, l.entry + 12, 6);
        expectRejected("first of two 'maxp' entries at offset 0", f,
                       "missing required table 'maxp'");
        // Control: the same font with the first entry intact loads.
        Bytes g = buildSfnt(0x00010000, {{"head", makeHead(0)},
                                         {"hhea", makeHhea(2)},
                                         {"maxp", makeMaxp(kNumGlyphs)},
                                         {"maxp", makeMaxp(kNumGlyphs)},
                                         {"cmap", makeCmap()},
                                         {"loca", loca},
                                         {"glyf", glyf},
                                         {"hmtx", makeHmtx()}});
        internal::FontAtlasManager m;
        check("two 'maxp' entries, both valid: loads", tryLoad(m, g).ok);
    }

    // Fixed fields
    for (auto spec : {pair<const char*, uint32_t>{"head", 53}, {"hhea", 35}, {"maxp", 5}, {"cmap", 3}}) {
        Bytes f = tt;
        const Loc l = findTable(f, spec.first);
        set32(f, l.entry + 12, spec.second);
        expectRejected(string("'") + spec.first + "' shorter than its fixed fields", f,
                       "head / hhea / maxp / cmap table is too short");
    }
    {
        Bytes f = tt;
        Loc l = findTable(f, "cmap");
        set16(f, l.offset + 2, 0x1000);
        expectRejected("cmap encoding records past cmap", f, "cmap encoding records");
        f = tt;
        set32(f, l.offset + 8, (uint32_t)l.length - 1);
        expectRejected("cmap subtable offset past cmap", f, "cmap subtable offset");
    }

    // hmtx
    {
        Bytes f = tt;
        Loc l = findTable(f, "hmtx");
        set32(f, l.entry + 12, l.length - 2);
        expectRejected("hmtx too short", f, "hmtx table is too short");
        f = tt;
        l = findTable(f, "hhea");
        set16(f, l.offset + 34, 0);
        expectRejected("numberOfHMetrics 0", f, "numberOfHMetrics");
        f = tt;
        set16(f, l.offset + 34, kNumGlyphs + 1);
        expectRejected("numberOfHMetrics > numGlyphs", f, "numberOfHMetrics");
        f = cff;
        l = findTable(f, "hmtx");
        set32(f, l.entry + 12, l.length - 2);
        expectRejected("CFF font: hmtx too short", f, "hmtx table is too short");
        f = tt;
        l = findTable(f, "maxp");
        set16(f, l.offset + 4, 0);
        expectRejected("numGlyphs 0", f, "numberOfHMetrics");
    }

    // loca
    {
        Bytes f = tt;
        Loc head = findTable(f, "head");
        set16(f, head.offset + 50, 2);
        expectRejected("unknown loca format", f, "unsupported loca format");
        f = tt;
        Loc l = findTable(f, "loca");
        set32(f, l.entry + 12, 2 * kNumGlyphs);
        expectRejected("loca one entry short", f, "loca table is too short");
        f = tt;
        const size_t glyfLen = findTable(f, "glyf").length;
        set16(f, l.offset + 2 * kNumGlyphs, (uint32_t)(glyfLen / 2 + 1));
        expectRejected("last loca entry past glyf", f, "loca entry points past");
        f = tt;
        set16(f, l.offset + 2, (uint32_t)(glyfLen / 2 + 1));
        expectRejected("middle loca entry past glyf", f, "loca entry points past");
        f = ttLong;
        l = findTable(f, "loca");
        set32(f, l.offset + 4 * kNumGlyphs, (uint32_t)findTable(f, "glyf").length + 1);
        expectRejected("long loca: last entry past glyf", f, "loca entry points past");
        f = ttLong;
        set32(f, l.offset + 4, 0x80000000u);
        expectRejected("long loca: entry far past glyf", f, "loca entry points past");
        f = ttLong;
        set32(f, l.offset + 8, get32(f, l.offset + 4) - 1);
        expectRejected("long loca: entry below the one before it", f,
                       "loca entries are not in increasing order");
        f = tt;
        l = findTable(f, "loca");
        set16(f, l.offset + 4, get16(f, l.offset + 2) - 1);
        expectRejected("loca entry below the one before it", f,
                       "loca entries are not in increasing order");
        f = tt;
        // Pointing the loca format at long (4-byte) entries doubles the size
        // the table needs.
        head = findTable(f, "head");
        set16(f, head.offset + 50, 1);
        expectRejected("loca too short for the long format", f, "loca table is too short");
    }
}

// --- installed fonts -----------------------------------------------------------
static bool readFile(const string& path, Bytes& out) {
    ifstream in(path, ios::binary);
    if (!in) return false;
    out.assign(istreambuf_iterator<char>(in), istreambuf_iterator<char>());
    return !out.empty();
}

// End of the last table of the font at fontStart (or 0 when the directory
// itself is not readable).
static size_t tablesEnd(const Bytes& f, size_t fontStart) {
    if (f.size() < fontStart + 12) return 0;
    const size_t n = get16(f, fontStart + 4);
    if (f.size() < fontStart + 12 + 16 * n) return 0;
    size_t end = fontStart + 12 + 16 * n;
    for (size_t i = 0; i < n; i++) {
        const size_t rec = fontStart + 12 + 16 * i;
        end = max(end, (size_t)get32(f, rec + 8) + get32(f, rec + 12));
    }
    return end;
}

static void checkInstalledFonts(const vector<string>& extra) {
    vector<string> candidates = {
        // macOS
        "/System/Library/Fonts/Helvetica.ttc",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/ヒラギノ角ゴシック W3.ttc",
        // Windows
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/msgothic.ttc",
        // Linux
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/opentype/urw-base35/NimbusSans-Regular.otf",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    };
    candidates.insert(candidates.end(), extra.begin(), extra.end());

    int found = 0;
    for (const string& path : candidates) {
        Bytes f;
        if (!readFile(path, f)) continue;
        found++;
        internal::FontAtlasManager m;
        const LoadOutcome r = tryLoad(m, f);
        check("installed: " + path + " loads", r.ok && r.warnings == 0);
        if (r.ok) {
            const internal::GlyphInfo* g = m.getOrLoadGlyph('A');
            check("installed: " + path + " rasterizes 'A'", g && g->isValid());
        }
        // Cut the data short at points spread over the font's tables. Every
        // cut before the end of the last table must be rejected. Big fonts
        // get fewer cuts, since each load copies the whole prefix.
        const size_t fontStart = (get32(f, 0) == tagOf("ttcf")) ? get32(f, 12) : 0;
        const size_t end = min(tablesEnd(f, fontStart), f.size());
        if (end == 0) continue;
        const size_t cuts = f.size() > (4u << 20) ? 24 : 96;
        int rejected = 0, total = 0;
        string firstMiss;
        for (size_t k = 0; k < cuts; k++) {
            const size_t cut = (size_t)((uint64_t)end * k / cuts) + (k * 7919) % 97;
            if (cut >= end) continue;
            total++;
            internal::FontAtlasManager mc;
            const LoadOutcome rc = tryLoad(mc, f.data(), cut);
            if (!rc.ok) rejected++;
            else if (firstMiss.empty()) firstMiss = to_string(cut);
        }
        check("installed: " + path + " cut short is rejected (" + to_string(total) + " cuts)",
              rejected == total, "first accepted cut at " + firstMiss);
    }
    if (found == 0) printf("installed fonts: none found at the usual paths, skipped\n");
}

// --- setup() from a path -----------------------------------------------------------
// setup() opens only regular files, and refuses a file of 1 GiB or more
// before its buffer is allocated.
#ifdef __linux__
static long maxRssKb() {
    rusage ru{};
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_maxrss;
}
#endif

static void checkSetupPath() {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec) /
                         ("fontSfntCheck-" + to_string((unsigned long long)getElapsedTimeMicros()));
    if (ec || !fs::create_directories(dir, ec)) {
        check("setup(): temp directory can be created", false, dir.string());
        return;
    }
    {
        internal::FontAtlasManager m;
        g_lastError.clear();
        const int before = g_errors;
        const bool ok = m.setup(dir.string(), kFontSize);
        check("setup(): a directory is refused as not a regular file",
              !ok && g_errors > before && g_lastError.find("not a regular file") != string::npos,
              string("ok=") + (ok ? "true" : "false") + " last=\"" + g_lastError + "\"");
    }
#ifdef __linux__
    {
        // A sparse file: no disk blocks are written.
        const fs::path big = dir / "big.ttf";
        { ofstream(big, ios::binary); }
        fs::resize_file(big, (uintmax_t)0x40000000u + 1, ec);
        if (ec) {
            printf("setup(): sparse 1 GiB + 1 file could not be created, skipped\n");
        } else {
            internal::FontAtlasManager m;
            g_lastWarning.clear();
            const int before = g_warnings;
            const long rssBefore = maxRssKb();
            const bool ok = m.setup(big.string(), kFontSize);
            const long grownKb = maxRssKb() - rssBefore;
            check("setup(): a 1 GiB + 1 file is refused",
                  !ok && g_warnings > before && g_lastWarning.find("1 GiB") != string::npos,
                  string("ok=") + (ok ? "true" : "false") + " last=\"" + g_lastWarning + "\"");
            // Peak RSS would grow by about 1 GiB had the file been read.
            check("setup(): the 1 GiB + 1 file is refused before it is read",
                  grownKb < 64 * 1024, "peak RSS grew by " + to_string(grownKb) + " KiB");
        }
    }
#else
    printf("setup(): 1 GiB + 1 file check runs only on Linux (sparse file), skipped\n");
#endif
    fs::remove_all(dir, ec);
}

// --- metrics dump (compare two builds) ---------------------------------------------
static void dumpMetrics(const vector<string>& paths) {
    const uint32_t cps[] = {'A', 'g', 'W', 'j', '0', '?', 0x00E9, 0x3042, 0x6F22, 0xFF01};
    for (const string& path : paths) {
        Bytes f;
        if (!readFile(path, f)) { printf("%s: unreadable\n", path.c_str()); continue; }
        for (int os : {1, 2}) {
            internal::FontAtlasManager m;
            if (!m.setupFromMemory(f.data(), f.size(), 48)) {
                printf("%s: load failed\n", path.c_str());
                break;
            }
            m.setOversample(os);
            printf("%s os=%d ascent=%.4f descent=%.4f lineHeight=%.4f space=%.4f\n",
                   path.c_str(), os, m.getAscent(), m.getDescent(), m.getLineHeight(),
                   m.getSpaceAdvance());
            for (uint32_t cp : cps) {
                const internal::GlyphInfo* g = m.getOrLoadGlyph(cp);
                const Path p = m.getGlyphPath(cp);
                double sum = 0;
                for (auto& v : p.getVertices()) sum += v.x * 3.0 + v.y;
                printf("  U+%04X has=%d advEm=%.5f", cp, m.fontHasGlyph(cp) ? 1 : 0,
                       m.getGlyphAdvanceEm(cp));
                if (g) {
                    printf(" xoff=%.4f yoff=%.4f w=%.4f h=%.4f adv=%.4f", g->getXoff(),
                           g->getYoff(), g->getWidth(), g->getHeight(), g->getAdvance());
                }
                printf(" subpaths=%zu verts=%d sum=%.4f\n", p.getNumSubpaths(), p.size(), sum);
            }
        }
    }
}

} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
    EventListener logSub = getLogger().onLog.listen([](LogEventArgs& e) {
        if (e.level == LogLevel::Warning) {
            ++g_warnings;
            g_lastWarning = e.message;
        } else if (e.level == LogLevel::Error) {
            ++g_errors;
            g_lastError = e.message;
        }
    });

    vector<string> args(argv + 1, argv + argc);
    if (!args.empty() && args[0] == "--dump") {
        dumpMetrics(vector<string>(args.begin() + 1, args.end()));
        return 0;
    }

    checkValid("TrueType", makeTrueType(0));
    checkValid("TrueType long loca", makeTrueType(1));
    checkValid("CFF", makeCffFont());
    checkValid("TrueType, numberOfHMetrics == numGlyphs", makeTrueType(0, kNumGlyphs));
    // Legal in sfnt: a table of length 0, and tables that share bytes.
    checkValid("TrueType with a zero-length table",
               makeTrueType(0, 2, makeCmap(), {{"zzzz", Bytes{}}}));
    {
        Bytes f = makeTrueType(0, 2, makeCmap(), {{"zzzz", Bytes(4, 0)}, {"yyyy", Bytes(4, 0)}});
        const Loc glyf = findTable(f, "glyf");
        const Loc z = findTable(f, "zzzz");
        set32(f, z.entry + 8, (uint32_t)glyf.offset);
        set32(f, z.entry + 12, (uint32_t)glyf.length);
        const Loc y = findTable(f, "yyyy");
        set32(f, y.entry + 8, (uint32_t)glyf.offset + 4);
        set32(f, y.entry + 12, (uint32_t)glyf.length - 4);
        checkValid("TrueType with tables in the same bytes as glyf", f);
    }
    checkGlyphIndexClamp("TrueType", makeTrueType());
    checkGlyphIndexClamp("CFF", makeCffFont());
    checkCffCharStringsCount();
    checkCffMalformed();
    checkCodepointRange();
    checkVertexCount();
    checkPointCount();
    checkOnePointContour();
    checkCffLength();
    checkMalformed();
    checkSetupPath();
    checkInstalledFonts(args);

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
