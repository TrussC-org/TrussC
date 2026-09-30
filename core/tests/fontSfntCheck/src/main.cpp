// =============================================================================
// fontSfntCheck — font data is checked before stb_truetype reads it
//
// stb_truetype takes no buffer length and follows the offsets in the file.
// FontAtlasManager checks the sfnt skeleton against the real size first, and
// setupFromMemory() returns false with a warning when it does not hold:
// - fewer than 12 bytes (including 0 bytes with a null pointer),
// - a .ttc header whose font count or font offset points outside the data,
// - a table directory, or any table, that runs past the end of the data
//   (also when offset + length wraps in 32 bits),
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
// Plus:
// - a glyph index from the cmap past numGlyphs is treated as .notdef,
// - the CFF data is read with the CFF table's own length (a CharStrings
//   offset past the table no longer reads the bytes that follow it; runs only
//   with NDEBUG, since stb asserts on that offset otherwise),
// - a glyph whose last contour is a single off-curve point loads and
//   rasterizes (stb reads one vertex past its array there; the padded
//   STBTT_malloc keeps that read inside the block -- run under ASan),
// - valid fonts still load, with the metrics they are built with.
// The fonts are built here, so the test runs the same everywhere. Installed
// fonts found at the usual system paths are also loaded and cut short.
//
// `fontSfntCheck --dump <font files>` prints the metrics of a few glyphs, to
// compare two builds.
// =============================================================================

#include <TrussC.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
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

static int g_warnings = 0;
static string g_lastWarning;

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

// Advances: glyph 0 = 500, glyph 1 = 600, glyphs 2 and 3 repeat the last
// long entry (600).
static Bytes makeHmtx() {
    Bytes b;
    put16(b, 500); put16(b, 50);
    put16(b, 600); put16(b, 0);
    put16(b, 0);
    put16(b, 100);
    return b;  // 12 bytes, numberOfHMetrics = 2
}

// ' ' -> 2, 'A' -> 1, 'B' -> 4 (== numGlyphs: past the last glyph), 'C' -> 3
static Bytes makeCmap() {
    struct Seg { uint16_t start, end; uint16_t glyph; };
    const vector<Seg> segs = {{0x20, 0x20, 2}, {0x41, 0x41, 1}, {0x42, 0x42, 4},
                              {0x43, 0x43, 3}, {0xFFFF, 0xFFFF, 0}};
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
// 3: 'C', a triangle plus a last contour that is one off-curve point.
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

// hmtx goes last, so a read past it is a read past the data.
static Bytes makeTrueType(int locaFormat = 0) {
    Bytes glyf, loca;
    makeGlyfLoca(locaFormat, glyf, loca);
    return buildSfnt(0x00010000, {{"head", makeHead(locaFormat)},
                                  {"hhea", makeHhea(2)},
                                  {"maxp", makeMaxp(kNumGlyphs)},
                                  {"cmap", makeCmap()},
                                  {"loca", loca},
                                  {"glyf", glyf},
                                  {"hmtx", makeHmtx()}});
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
static Bytes makeCff(uint32_t charStringsAt, bool inside) {
    Bytes b = {1, 0, 4, 4};  // major, minor, hdrSize, offSize
    const Bytes name = cffIndex({Bytes{'T'}});
    Bytes dict = {29};
    put32(dict, charStringsAt);
    dict.push_back(17);  // CharStrings
    const Bytes top = cffIndex({dict});
    b.insert(b.end(), name.begin(), name.end());
    b.insert(b.end(), top.begin(), top.end());
    put16(b, 0);  // String INDEX
    put16(b, 0);  // Global Subr INDEX
    if (inside) {
        if (b.size() != charStringsAt) {
            printf("test bug: CharStrings at %zu, not %u\n", b.size(), charStringsAt);
            exit(2);
        }
        const Bytes cs = cffIndex(cffCharStrings());
        b.insert(b.end(), cs.begin(), cs.end());
    }
    return b;
}
static Bytes makeCffFont() {
    // The Top DICT has a fixed size, so the CharStrings INDEX starts where
    // the table without it ends.
    const uint32_t charStringsAt = (uint32_t)makeCff(0, false).size();
    return buildSfnt(tagOf("OTTO"), {{"head", makeHead(0)},
                                     {"hhea", makeHhea(2)},
                                     {"maxp", makeMaxp(kNumGlyphs)},
                                     {"cmap", makeCmap()},
                                     {"CFF ", makeCff(charStringsAt, true)},
                                     {"hmtx", makeHmtx()}});
}

// CharStrings offset points just past the CFF table; the next table holds a
// well-formed CharStrings INDEX, which only a read beyond the CFF table sees.
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

// setupFromMemory() copies into an exact-size heap block, so a read past the
// end is one ASan sees. `reason` is a part of the warning to expect (the
// guard that should catch it); empty accepts any.
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

// --- last contour is one off-curve point ---------------------------------------
static void checkSinglePointContour() {
    internal::FontAtlasManager m;
    if (!tryLoad(m, makeTrueType()).ok) {
        check("single off-curve contour: loads", false);
        return;
    }
    const Path pc = m.getGlyphPath('C');
    check("single off-curve contour: outline has two contours", pc.getNumSubpaths() == 2,
          "subpaths=" + to_string(pc.getNumSubpaths()));
    const internal::GlyphInfo* gc = m.getOrLoadGlyph('C');
    check("single off-curve contour: rasterizes", gc && gc->isValid() && gc->getWidth() > 0);
    // Oversampled rasterization goes through the same outline.
    internal::FontAtlasManager m2;
    const Bytes f = makeTrueType();
    m2.setupFromMemory(f.data(), f.size(), kFontSize);
    m2.setOversample(2);
    const internal::GlyphInfo* gc2 = m2.getOrLoadGlyph('C');
    check("single off-curve contour: rasterizes oversampled", gc2 && gc2->isValid());
}

// --- CFF length ----------------------------------------------------------------
static void checkCffLength() {
#ifndef NDEBUG
    // With the CFF table's own length, stb's STBTT_assert (plain assert)
    // stops the seek to a CharStrings offset past the table, which is what
    // this case sets up. Builds with NDEBUG (Release, as CI) run it.
    printf("%-72s SKIP (stb asserts in builds without NDEBUG)\n",
           "CFF: CharStrings past the CFF table are not read");
    fflush(stdout);
#else
    internal::FontAtlasManager m;
    const Bytes f = makeCffFontCharStringsOutside();
    const LoadOutcome r = tryLoad(m, f);
    // The skeleton is fine; the CharStrings offset is CFF data, which the
    // check does not look into. stb then finds no CharStrings in the table.
    if (!r.ok) {
        check("CFF: CharStrings past the CFF table are not read", true);
        return;
    }
    check("CFF: CharStrings past the CFF table are not read",
          m.getGlyphPath('A').empty(),
          "'A' outline has " + to_string(m.getGlyphPath('A').size()) +
              " vertices, read from the bytes after the CFF table");
#endif
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
        set32(f, l.entry + 12, 0x20);  // offset + length wraps to 0x10 in 32 bits
        expectRejected("table offset + length wraps in 32 bits", f, "table 'glyf' is outside");
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

    // A directory entry at offset 0 is "no table" to stb. For maxp, stb would
    // run with numGlyphs 0xffff while the check read a count from the header
    // bytes; for glyf, stb takes the CFF path.
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
        // Two maxp entries: stb stops at the first one (offset 0) and does not
        // look at the valid one after it.
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

int main(int argc, char** argv) {
    EventListener logSub = getLogger().onLog.listen([](LogEventArgs& e) {
        if (e.level == LogLevel::Warning) {
            ++g_warnings;
            g_lastWarning = e.message;
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
    checkGlyphIndexClamp("TrueType", makeTrueType());
    checkGlyphIndexClamp("CFF", makeCffFont());
    checkSinglePointContour();
    checkCffLength();
    checkMalformed();
    checkInstalledFonts(args);

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
