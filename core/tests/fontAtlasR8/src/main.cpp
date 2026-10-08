// =============================================================================
// fontAtlasR8 — TrueType glyph atlas pages are R8 (#293)
//
// Headless (run by CI):
// - a page holds one byte of glyph coverage per texel: getMemoryUsage() is
//   the sum of width * height over the pages, and each page's CPU copy is
//   exactly width * height bytes,
// - 300 glyphs loaded one after another grow the page through several
//   doubling steps, and every glyph is still in the page afterwards: the
//   texel at the centre of each glyph's UV rectangle holds full coverage
//   (the test font's glyph is a filled square), and texels outside the
//   glyph rectangles hold none.
//
// `fontAtlasR8 --gpu-check` (daily CI under Xvfb) opens a window
// and, in one frame, draws 150 glyphs that were never drawn before into an
// Fbo, one drawString() call each, so the page grows several times inside
// the frame. It checks that the page texture is SG_PIXELFORMAT_R8 and that
// every glyph is in the Fbo with the colour set by setColor() at full
// coverage (straight alpha over a transparent Fbo: rgb = colour, a = 1).
// Then it draws minified text, which gives the R8 page a mip chain, and
// checks that the glyphs are in the Fbo.
//
// The font is built here (a TrueType font whose codepoints U+4E00.. all map
// to one filled square glyph), so the test runs the same everywhere.
// =============================================================================

#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
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

// --- minimal TrueType builder ------------------------------------------------
using Bytes = vector<uint8_t>;

static void put16(Bytes& b, uint32_t v) {
    b.push_back((uint8_t)(v >> 8));
    b.push_back((uint8_t)v);
}
static void put32(Bytes& b, uint32_t v) {
    put16(b, v >> 16);
    put16(b, v & 0xffff);
}
static uint32_t tagOf(const char* t) {
    return (uint32_t(uint8_t(t[0])) << 24) | (uint32_t(uint8_t(t[1])) << 16) |
           (uint32_t(uint8_t(t[2])) << 8) | uint8_t(t[3]);
}

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
    while (out.size() % 4) out.push_back(0);
    return out;
}

static const uint32_t kFirstCodepoint = 0x4E00;
static const uint32_t kCodepointCount = 400;

// Glyph 0: empty .notdef, glyph 1: filled square (100..900 x 0..800), glyph 2:
// empty space. Codepoints U+4E00..U+4E00+kCodepointCount-1 map to glyph 1,
// ' ' to glyph 2. 1000 units per em, ascender 800, descender -200.
static Bytes makeSquareFont() {
    Bytes head;
    put32(head, 0x00010000);
    put32(head, 0x00010000);
    put32(head, 0);
    put32(head, 0x5F0F3CF5);
    put16(head, 0);
    put16(head, 1000);                          // unitsPerEm
    for (int i = 0; i < 16; i++) head.push_back(0);
    put16(head, 0); put16(head, 0); put16(head, 1000); put16(head, 800);
    put16(head, 0);
    put16(head, 8);
    put16(head, 2);
    put16(head, 1);                             // indexToLocFormat: long
    put16(head, 0);

    Bytes hhea;
    put32(hhea, 0x00010000);
    put16(hhea, 800);
    put16(hhea, (uint16_t)-200);
    put16(hhea, 0);
    put16(hhea, 1000);
    for (int i = 0; i < 11; i++) put16(hhea, 0);
    put16(hhea, 3);                             // numberOfHMetrics

    Bytes maxp;
    put32(maxp, 0x00005000);
    put16(maxp, 3);

    Bytes hmtx;
    for (int i = 0; i < 3; i++) { put16(hmtx, 1000); put16(hmtx, 0); }

    // cmap: one Microsoft / Unicode full subtable.
    Bytes cmap;
    put16(cmap, 0);
    put16(cmap, 1);
    put16(cmap, 3);
    put16(cmap, 10);
    put32(cmap, 12);
    {
        // Format 13 (many-to-one): every codepoint in a group maps to the
        // same glyph.
        Bytes s13;
        put16(s13, 13);
        put16(s13, 0);
        put32(s13, 16 + 12 * 2);
        put32(s13, 0);
        put32(s13, 2);
        put32(s13, 0x20); put32(s13, 0x20); put32(s13, 2);
        put32(s13, kFirstCodepoint); put32(s13, kFirstCodepoint + kCodepointCount - 1); put32(s13, 1);
        cmap.insert(cmap.end(), s13.begin(), s13.end());
    }

    // glyf: glyph 1 only (one contour, four on-curve points).
    Bytes square;
    put16(square, 1);
    put16(square, 100); put16(square, 0); put16(square, 900); put16(square, 800);
    put16(square, 3);                           // endPtsOfContours
    put16(square, 0);                           // instructionLength
    for (int i = 0; i < 4; i++) square.push_back(1);   // on-curve, int16 deltas
    put16(square, 100); put16(square, 800); put16(square, 0); put16(square, (uint16_t)-800);
    put16(square, 0); put16(square, 0); put16(square, 800); put16(square, 0);
    while (square.size() % 4) square.push_back(0);

    Bytes glyf = square;
    Bytes loca;
    put32(loca, 0);                             // glyph 0: empty
    put32(loca, 0);                             // glyph 1
    put32(loca, (uint32_t)square.size());       // glyph 2: empty
    put32(loca, (uint32_t)square.size());
    put32(loca, (uint32_t)square.size());

    return buildSfnt({{"cmap", cmap}, {"glyf", glyf}, {"head", head}, {"hhea", hhea},
                      {"hmtx", hmtx}, {"loca", loca}, {"maxp", maxp}});
}

static string utf8(uint32_t cp) {
    string s;
    s += char(0xE0 | (cp >> 12));
    s += char(0x80 | ((cp >> 6) & 0x3F));
    s += char(0x80 | (cp & 0x3F));
    return s;
}

// --- headless checks -----------------------------------------------------------
static void checkCpuPages() {
    const Bytes font = makeSquareFont();
    internal::FontAtlasManager mgr;
    check("the built font loads", mgr.setupFromMemory(font.data(), font.size(), 48));

    const size_t initialBytes = mgr.getMemoryUsage();
    check("a new font has one 256x256 page of 65536 bytes",
          mgr.getAtlasCount() == 1 && initialBytes == 256u * 256u,
          "bytes " + to_string(initialBytes));

    const uint32_t kGlyphs = 300;
    int invalid = 0;
    for (uint32_t i = 0; i < kGlyphs; i++) {
        const internal::GlyphInfo* g = mgr.getOrLoadGlyph(kFirstCodepoint + i);
        if (!g || !g->isValid() || g->getWidth() <= 0) invalid++;
    }
    check("300 glyphs load", invalid == 0, to_string(invalid) + " not loaded");

    size_t pageBytes = 0;
    bool sizesMatch = true;
    int maxSide = 0;
    for (size_t p = 0; p < mgr.getAtlasCount(); p++) {
        const auto& a = mgr.getAtlas(p);
        pageBytes += (size_t)a.getWidth() * a.getHeight();
        if (a.getPixels().size() != (size_t)a.getWidth() * a.getHeight()) sizesMatch = false;
        maxSide = max(maxSide, a.getWidth());
    }
    check("getMemoryUsage() is the sum of width * height over the pages",
          mgr.getMemoryUsage() == pageBytes,
          to_string(mgr.getMemoryUsage()) + " vs " + to_string(pageBytes));
    check("each page's CPU copy is width * height bytes", sizesMatch);
    check("the page grew through more than one doubling step (>= 1024)", maxSide >= 1024,
          "largest side " + to_string(maxSide));

    // Every glyph is still where its UVs point after the growth steps, and
    // the space between glyphs is empty.
    int missing = 0;
    vector<vector<uint8_t>> inside(mgr.getAtlasCount());
    for (size_t p = 0; p < mgr.getAtlasCount(); p++) {
        const auto& a = mgr.getAtlas(p);
        inside[p].assign((size_t)a.getWidth() * a.getHeight(), 0);
    }
    for (uint32_t i = 0; i < kGlyphs; i++) {
        const internal::GlyphInfo* g = mgr.getOrLoadGlyph(kFirstCodepoint + i);
        const auto& a = mgr.getAtlas(g->getAtlasIndex());
        const int w = a.getWidth(), h = a.getHeight();
        const int x0 = (int)lround(g->getU0() * w), x1 = (int)lround(g->getU1() * w);
        const int y0 = (int)lround(g->getV0() * h), y1 = (int)lround(g->getV1() * h);
        const int cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
        if (a.getPixels()[(size_t)cy * w + cx] != 255) missing++;
        auto& mask = inside[g->getAtlasIndex()];
        for (int y = max(0, y0 - 1); y < min(h, y1 + 1); y++)
            for (int x = max(0, x0 - 1); x < min(w, x1 + 1); x++) mask[(size_t)y * w + x] = 1;
    }
    check("every glyph's centre texel holds full coverage", missing == 0,
          to_string(missing) + " glyphs without coverage");
    size_t stray = 0;
    for (size_t p = 0; p < mgr.getAtlasCount(); p++) {
        const auto& px = mgr.getAtlas(p).getPixels();
        for (size_t t = 0; t < min(px.size(), inside[p].size()); t++)
            if (px[t] != 0 && !inside[p][t]) stray++;
    }
    check("texels outside the glyph rectangles hold no coverage", stray == 0,
          to_string(stray) + " texels");
}

// --- --gpu-check ---------------------------------------------------------------
static string g_fontPath;
static const int kCell = 48;
static const int kCols = 15;
static const int kRows = 10;

class GpuCheckApp : public App {
public:
    void setup() override {
        check("gpu: the built font loads from a file", (bool)font_.load(g_fontPath, 32));
        fbo_.allocate(kCols * kCell, kRows * kCell);
    }

    void draw() override {
        clear(0.0f);
        if (getFrameCount() < 3) return;

        // One frame: 150 glyphs never drawn before, one drawString() each.
        fbo_.begin(0.0f, 0.0f, 0.0f, 0.0f);
        setColor(1.0f, 0.5f, 0.25f);
        for (int i = 0; i < kCols * kRows; i++) {
            const int cx = (i % kCols) * kCell, cy = (i / kCols) * kCell;
            font_.drawString(utf8(kFirstCodepoint + (uint32_t)i), (float)cx + 8, (float)cy + 36);
        }
        fbo_.end();

        bool r8 = font_.getAtlasCount() > 0;
        size_t pageBytes = 0;
        for (size_t p = 0; p < font_.getAtlasCount(); p++) {
            const auto* a = font_.getAtlas(p);
            pageBytes += (size_t)a->getWidth() * a->getHeight();
            if (!a->isTextureValid() ||
                sg_query_image_desc(a->getTexture()).pixel_format != SG_PIXELFORMAT_R8) r8 = false;
        }
        check("gpu: every atlas page texture is SG_PIXELFORMAT_R8", r8);
        check("gpu: getMemoryUsage() is the sum of width * height over the pages",
              font_.getMemoryUsage() == pageBytes);
        check("gpu: the page grew within the frame (largest side >= 512)",
              font_.getAtlasCount() > 0 && font_.getAtlas(0)->getWidth() >= 512,
              "side " + to_string(font_.getAtlasCount() ? font_.getAtlas(0)->getWidth() : 0));

        const int W = kCols * kCell, H = kRows * kCell;
        vector<unsigned char> px((size_t)W * H * 4);
        check("gpu: Fbo readPixels", fbo_.readPixels(px.data()));

        // Fully covered texels must be exactly the colour (straight alpha over
        // transparent black: rgb = colour, a = 1). Counted per cell, so the
        // readback's row order does not matter (the grid fills the Fbo).
        int cellsWithGlyph = 0, wrongColour = 0;
        for (int i = 0; i < kCols * kRows; i++) {
            const int cx = (i % kCols) * kCell, cy = (i / kCols) * kCell;
            int full = 0;
            for (int y = cy; y < cy + kCell; y++) {
                for (int x = cx; x < cx + kCell; x++) {
                    const unsigned char* p = &px[((size_t)y * W + x) * 4];
                    if (p[3] != 255) continue;
                    full++;
                    if (abs(p[0] - 255) > 2 || abs(p[1] - 128) > 2 || abs(p[2] - 64) > 2) wrongColour++;
                }
            }
            if (full >= 200) cellsWithGlyph++;
        }
        check("gpu: all 150 glyphs drawn in the burst are in the Fbo",
              cellsWithGlyph == kCols * kRows, to_string(cellsWithGlyph) + " of 150");
        check("gpu: fully covered texels have the setColor() colour", wrongColour == 0,
              to_string(wrongColour) + " texels");

        // Minified draws: the first one asks for the mip chain, the next one
        // builds the R8 chain and samples it.
        fbo_.begin(0.0f, 0.0f, 0.0f, 0.0f);
        setColor(1.0f, 0.5f, 0.25f);
        pushMatrix();
        scale(0.25f);
        font_.drawString(utf8(kFirstCodepoint) + utf8(kFirstCodepoint + 1), 40.0f, 120.0f);
        font_.drawString(utf8(kFirstCodepoint + 2) + utf8(kFirstCodepoint + 3), 40.0f, 400.0f);
        popMatrix();
        fbo_.end();
        const auto* a0 = font_.getAtlas(0);
        check("gpu: a minified draw gives the R8 page a mip chain",
              a0 && sg_query_image_desc(a0->getTexture()).num_mipmaps > 1
                 && sg_query_image_desc(a0->getTexture()).pixel_format == SG_PIXELFORMAT_R8);
        check("gpu: Fbo readPixels after the minified draw", fbo_.readPixels(px.data()));
        int inked = 0;
        for (size_t t = 0; t < (size_t)W * H; t++) if (px[t * 4 + 3] > 0) inked++;
        check("gpu: the minified glyphs are in the Fbo", inked > 20, to_string(inked) + " texels");

        // Look at it on screen too (not checked).
        setColor(1.0f);
        fbo_.draw(0, 0);
        done_ = true;
        exitApp();
    }

    bool done_ = false;

private:
    Font font_;
    Fbo fbo_;
};

} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
    const bool gpuCheck = argc > 1 && strcmp(argv[1], "--gpu-check") == 0;
    if (!gpuCheck) {
        checkCpuPages();
        printf("fontAtlasR8: %s\n", g_fail == 0 ? "OK" : "FAIL");
        return g_fail == 0 ? 0 : 1;
    }

    const Bytes font = makeSquareFont();
    g_fontPath = (filesystem::temp_directory_path() / "fontAtlasR8_square.ttf").string();
    {
        ofstream f(g_fontPath, ios::binary);
        f.write((const char*)font.data(), (streamsize)font.size());
    }
    WindowSettings settings;
    settings.setSize(kCols * kCell, kRows * kCell);
    settings.setHighDpi(false);
    runApp<GpuCheckApp>(settings);
    filesystem::remove(g_fontPath);
    printf("fontAtlasR8 --gpu-check: %s\n", g_fail == 0 ? "OK" : "FAIL");
    return g_fail == 0 ? 0 : 1;
}
