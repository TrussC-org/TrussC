// =============================================================================
// fontAtlasLimit — glyphs larger than an atlas page (#404)
//
// FontAtlasManager checks each glyph against the largest atlas page before it
// creates or grows a page. A glyph whose box does not fit is rasterized at a
// lower resolution that fits (oversampling lowered for that glyph, below 1 if
// needed) and drawn at its size in final pixels. At load, the font's overall
// bounding box at this size and oversampling is checked against the page, and
// one warning per loaded font is logged when some glyphs will need that.
//
// Checked here, headless (no GPU, so the page limit is the 4096 default):
// - a font whose glyphs fit loads without a warning and keeps its
//   oversampling for every glyph,
// - a font at a size above the page limit logs exactly one warning, from
//   setup() through setOversample() and many lookups,
// - that glyph is valid, its texel box fits the page, its size and offset in
//   final pixels match the outline at that size, and the atlas page count and
//   memory stay the same over many simulated frames,
// - a lowered integer oversampling (box prefilter kept) and a raster scale
//   below 1 both give the right size,
// - clearAtlas() rasterizes the glyph again.
// The font is built here, so the test runs the same everywhere.
// =============================================================================

#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

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

// --- minimal TrueType font ---------------------------------------------------
// unitsPerEm 1000, ascender 800, descender -200 (so the pixel height scale is
// fontSize / 1000). Glyphs: 0 .notdef (empty), 1 'A' = the em box
// (0..1000 x -200..800), 2 'W' = 2000 x 1000 units, 3 'a' = 100 x 100 units.
using Bytes = vector<uint8_t>;
static void put16(Bytes& b, uint32_t v) { b.push_back((uint8_t)(v >> 8)); b.push_back((uint8_t)v); }
static void put32(Bytes& b, uint32_t v) { put16(b, v >> 16); put16(b, v & 0xffff); }

static Bytes boxGlyph(int x0, int y0, int x1, int y1) {
    Bytes b;
    put16(b, 1);                                   // one contour
    put16(b, (uint16_t)x0); put16(b, (uint16_t)y0);
    put16(b, (uint16_t)x1); put16(b, (uint16_t)y1);
    put16(b, 3);                                   // end point of the contour
    put16(b, 0);                                   // no instructions
    for (int i = 0; i < 4; i++) b.push_back(1);    // on-curve, int16 deltas
    const int xs[4] = {x0, x1, x1, x0}, ys[4] = {y0, y0, y1, y1};
    int prev = 0;
    for (int x : xs) { put16(b, (uint16_t)(x - prev)); prev = x; }
    prev = 0;
    for (int y : ys) { put16(b, (uint16_t)(y - prev)); prev = y; }
    while (b.size() % 4) b.push_back(0);
    return b;
}

static Bytes makeFont() {
    const vector<Bytes> glyphs = {Bytes{}, boxGlyph(0, -200, 1000, 800),
                                  boxGlyph(0, -200, 2000, 800), boxGlyph(0, 0, 100, 100)};
    Bytes glyf, loca;
    for (auto& g : glyphs) { put32(loca, (uint32_t)glyf.size()); glyf.insert(glyf.end(), g.begin(), g.end()); }
    put32(loca, (uint32_t)glyf.size());

    Bytes head;
    put32(head, 0x00010000); put32(head, 0x00010000); put32(head, 0); put32(head, 0x5F0F3CF5);
    put16(head, 0); put16(head, 1000);
    for (int i = 0; i < 16; i++) head.push_back(0);
    put16(head, 0); put16(head, (uint16_t)-200); put16(head, 2000); put16(head, 800);  // bbox
    put16(head, 0); put16(head, 8); put16(head, 2);
    put16(head, 1);  // long loca
    put16(head, 0);

    Bytes hhea;
    put32(hhea, 0x00010000); put16(hhea, 800); put16(hhea, (uint16_t)-200); put16(hhea, 0);
    put16(hhea, 2000);
    for (int i = 0; i < 11; i++) put16(hhea, 0);
    put16(hhea, 4);  // numberOfHMetrics

    Bytes maxp;
    put32(maxp, 0x00005000); put16(maxp, 4);

    Bytes hmtx;
    const int adv[4] = {500, 1000, 2000, 100};
    for (int a : adv) { put16(hmtx, (uint32_t)a); put16(hmtx, 0); }

    // cmap format 4: 'A' -> 1, 'W' -> 2, 'a' -> 3.
    struct Seg { uint16_t c, g; };
    const vector<Seg> segs = {{'A', 1}, {'W', 2}, {'a', 3}, {0xFFFF, 0}};
    const uint32_t n = (uint32_t)segs.size();
    Bytes sub;
    put16(sub, 4); put16(sub, 16 + 8 * n); put16(sub, 0);
    put16(sub, n * 2); put16(sub, 4); put16(sub, 1); put16(sub, n * 2 - 4);
    for (auto& s : segs) put16(sub, s.c);
    put16(sub, 0);
    for (auto& s : segs) put16(sub, s.c);
    for (auto& s : segs) put16(sub, (uint16_t)(s.c == 0xFFFF ? 1 : s.g - s.c));
    for (size_t i = 0; i < segs.size(); i++) put16(sub, 0);
    Bytes cmap;
    put16(cmap, 0); put16(cmap, 1); put16(cmap, 3); put16(cmap, 1); put32(cmap, 12);
    cmap.insert(cmap.end(), sub.begin(), sub.end());

    const vector<pair<const char*, Bytes>> tables = {
        {"cmap", cmap}, {"glyf", glyf}, {"head", head}, {"hhea", hhea},
        {"hmtx", hmtx}, {"loca", loca}, {"maxp", maxp}};
    Bytes out;
    put32(out, 0x00010000); put16(out, (uint32_t)tables.size());
    put16(out, 0); put16(out, 0); put16(out, 0);
    size_t offset = 12 + 16 * tables.size();
    vector<size_t> offsets;
    for (auto& t : tables) { offsets.push_back(offset); offset = (offset + t.second.size() + 3) & ~size_t(3); }
    for (size_t i = 0; i < tables.size(); i++) {
        const char* t = tables[i].first;
        put32(out, (uint32_t(uint8_t(t[0])) << 24) | (uint32_t(uint8_t(t[1])) << 16) |
                   (uint32_t(uint8_t(t[2])) << 8) | uint8_t(t[3]));
        put32(out, 0);
        put32(out, (uint32_t)offsets[i]);
        put32(out, (uint32_t)tables[i].second.size());
    }
    for (size_t i = 0; i < tables.size(); i++) {
        while (out.size() < offsets[i]) out.push_back(0);
        out.insert(out.end(), tables[i].second.begin(), tables[i].second.end());
    }
    return out;
}

// --- checks ------------------------------------------------------------------
static bool near(float a, float b, float tol) { return fabs(a - b) <= tol; }

// Texel size of a glyph in its atlas page.
static void texelSize(const internal::FontAtlasManager& m, const internal::GlyphInfo* g,
                      float& w, float& h) {
    const auto& atlas = m.getAtlas(g->getAtlasIndex());
    w = (g->getU1() - g->getU0()) * atlas.getWidth();
    h = (g->getV1() - g->getV0()) * atlas.getHeight();
}

// A box glyph of wEm x hEm em, top at 0.8 em above the baseline, at fontSize.
static void checkGlyphSize(const string& label, const internal::GlyphInfo* g, int fontSize,
                           float wEm, float hEm) {
    const float px = (float)fontSize;
    const float tol = 0.01f * px + 2.0f;
    check(label + ": size in final pixels matches the outline",
          g && near(g->getWidth(), wEm * px, tol) && near(g->getHeight(), hEm * px, tol),
          g ? "w=" + to_string(g->getWidth()) + " h=" + to_string(g->getHeight()) +
                  " expected " + to_string(wEm * px) + " x " + to_string(hEm * px)
            : "no glyph");
    check(label + ": offset in final pixels matches the outline",
          g && near(g->getXoff(), 0.0f, tol) && near(g->getYoff(), -0.8f * px, tol),
          g ? "xoff=" + to_string(g->getXoff()) + " yoff=" + to_string(g->getYoff()) : "no glyph");
}

static void checkFits(const Bytes& font) {
    internal::FontAtlasManager m;
    const int before = g_warnings;
    const bool ok = m.setupFromMemory(font.data(), font.size(), 100);
    m.setOversample(4);
    check("fits: loads without a warning", ok && g_warnings == before,
          "warnings=" + to_string(g_warnings - before) + " last=\"" + g_lastWarning + "\"");
    const internal::GlyphInfo* g = m.getOrLoadGlyph('A');
    check("fits: 'A' is valid", g && g->isValid());
    if (!g || !g->isValid()) return;
    float tw = 0, th = 0;
    texelSize(m, g, tw, th);
    // 100 px at oversampling 4, plus the 3-texel prefilter margin.
    check("fits: 'A' keeps oversampling 4", near(tw, 403.0f, 1.0f) && near(th, 403.0f, 1.0f),
          "texels " + to_string(tw) + " x " + to_string(th));
    checkGlyphSize("fits: 'A'", g, 100, 1.0f, 1.0f);
}

// Size above the page limit: one warning, a valid glyph, no atlas growth.
static void checkAbovePageLimit(const Bytes& font) {
    const int fontSize = 3000;
    internal::FontAtlasManager m;
    const int before = g_warnings;
    m.setOversample(2);  // before setup(), as SharedFontCache does
    const bool ok = m.setupFromMemory(font.data(), font.size(), fontSize);
    check("above limit: loads", ok);
    if (!ok) return;
    const int afterLoad = g_warnings - before;
    check("above limit: one warning at load", afterLoad == 1,
          "warnings=" + to_string(afterLoad));
    check("above limit: the warning names the size, oversampling and page limit",
          g_lastWarning.find("3000") != string::npos &&
              g_lastWarning.find("oversampling 2") != string::npos &&
              g_lastWarning.find(to_string(m.getMaxAtlasSize())) != string::npos,
          "\"" + g_lastWarning + "\"");

    const internal::GlyphInfo* g = m.getOrLoadGlyph('A');
    check("above limit: 'A' is valid", g && g->isValid());
    if (!g || !g->isValid()) return;
    const size_t pages = m.getAtlasCount();
    const size_t memory = m.getMemoryUsage();
    float tw = 0, th = 0;
    texelSize(m, g, tw, th);
    const int maxSize = m.getMaxAtlasSize();
    check("above limit: 'A' texel box fits the page",
          tw <= maxSize / 2.0f && th <= (float)maxSize && tw > maxSize / 4.0f,
          "texels " + to_string(tw) + " x " + to_string(th));
    checkGlyphSize("above limit: 'A'", g, fontSize, 1.0f, 1.0f);

    // 300 frames, three lookups per character per frame (as drawString does),
    // plus a glyph that fits.
    for (int frame = 0; frame < 300; frame++) {
        for (int i = 0; i < 3; i++) {
            m.getOrLoadGlyph('A');
            m.getOrLoadGlyph('W');
            m.getOrLoadGlyph('a');
        }
    }
    const internal::GlyphInfo* gw = m.getOrLoadGlyph('W');
    check("above limit: 'W' is valid", gw && gw->isValid());
    checkGlyphSize("above limit: 'W'", gw, fontSize, 2.0f, 1.0f);
    const size_t pagesAfter = m.getAtlasCount();
    const size_t memoryAfter = m.getMemoryUsage();
    check("above limit: at most one page per glyph, bounded", pagesAfter <= pages + 2,
          "pages " + to_string(pages) + " -> " + to_string(pagesAfter));
    // Same lookups again: nothing new is rasterized.
    for (int frame = 0; frame < 300; frame++) {
        for (int i = 0; i < 3; i++) {
            m.getOrLoadGlyph('A');
            m.getOrLoadGlyph('W');
            m.getOrLoadGlyph('a');
        }
    }
    check("above limit: page count stays the same over frames", m.getAtlasCount() == pagesAfter,
          to_string(pagesAfter) + " -> " + to_string(m.getAtlasCount()));
    check("above limit: memory stays the same over frames", m.getMemoryUsage() == memoryAfter,
          to_string(memoryAfter) + " -> " + to_string(m.getMemoryUsage()));
    check("above limit: memory stays within the pages in use",
          memoryAfter <= pagesAfter * (size_t)maxSize * maxSize * 4,
          to_string(memory) + " -> " + to_string(memoryAfter));
    check("above limit: still one warning after all lookups", g_warnings - before == 1,
          "warnings=" + to_string(g_warnings - before) + " last=\"" + g_lastWarning + "\"");
    check("above limit: one glyph entry per character", m.getLoadedGlyphCount() == 3,
          to_string(m.getLoadedGlyphCount()));

    // clearAtlas() drops the cache; the glyph is rasterized again.
    m.clearAtlas();
    const internal::GlyphInfo* g2 = m.getOrLoadGlyph('A');
    check("above limit: 'A' is valid after clearAtlas()", g2 && g2->isValid());
    checkGlyphSize("above limit after clearAtlas(): 'A'", g2, fontSize, 1.0f, 1.0f);
}

// 'W' at 300 px, oversampling 4: 2400 texels wide, lowered to an integer
// oversampling (3) that fits; 'A' at the same size keeps 4.
static void checkLoweredInteger(const Bytes& font) {
    const int fontSize = 300;
    internal::FontAtlasManager m;
    const int before = g_warnings;
    m.setupFromMemory(font.data(), font.size(), fontSize);
    m.setOversample(4);
    check("lowered oversampling: one warning at load", g_warnings - before == 1,
          "warnings=" + to_string(g_warnings - before));
    const internal::GlyphInfo* gw = m.getOrLoadGlyph('W');
    check("lowered oversampling: 'W' is valid", gw && gw->isValid());
    if (!gw || !gw->isValid()) return;
    float tw = 0, th = 0;
    texelSize(m, gw, tw, th);
    // 600 x 300 px at 3 texels per pixel, plus the 2-texel prefilter margin.
    check("lowered oversampling: 'W' rasterized at 3 texels per pixel",
          near(tw, 1802.0f, 1.0f) && near(th, 902.0f, 1.0f),
          "texels " + to_string(tw) + " x " + to_string(th));
    checkGlyphSize("lowered oversampling: 'W'", gw, fontSize, 2.0f, 1.0f);
    const internal::GlyphInfo* ga = m.getOrLoadGlyph('A');
    float aw = 0, ah = 0;
    if (ga) texelSize(m, ga, aw, ah);
    check("lowered oversampling: 'A' keeps 4 texels per pixel",
          ga && ga->isValid() && near(aw, 1203.0f, 1.0f),
          "texels " + to_string(aw));
    check("lowered oversampling: no further warning", g_warnings - before == 1);
}

// 20000 px at oversampling 1: raster scale below 1.
static void checkBelowOne(const Bytes& font) {
    const int fontSize = 20000;
    internal::FontAtlasManager m;
    m.setupFromMemory(font.data(), font.size(), fontSize);
    const internal::GlyphInfo* g = m.getOrLoadGlyph('A');
    check("below 1: 'A' is valid", g && g->isValid());
    if (!g || !g->isValid()) return;
    float tw = 0, th = 0;
    texelSize(m, g, tw, th);
    check("below 1: 'A' rasterized below 1 texel per pixel",
          tw < (float)fontSize && tw <= m.getMaxAtlasSize() / 2.0f,
          "texels " + to_string(tw) + " x " + to_string(th));
    checkGlyphSize("below 1: 'A'", g, fontSize, 1.0f, 1.0f);
}

} // namespace

TC_CORE_TEST_MAIN() {
    EventListener logSub = getLogger().onLog.listen([](LogEventArgs& e) {
        if (e.level == LogLevel::Warning) {
            ++g_warnings;
            g_lastWarning = e.message;
        }
    });

    const Bytes font = makeFont();
    checkFits(font);
    checkAbovePageLimit(font);
    checkLoweredInteger(font);
    checkBelowOne(font);

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
