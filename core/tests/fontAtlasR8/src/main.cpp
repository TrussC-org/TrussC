// =============================================================================
// fontAtlasR8 — TrueType glyph atlas pages are R8 (#293)
//
// Headless (run by CI):
// - a page holds one byte of glyph coverage per texel: getMemoryUsage() is
//   the sum of width * height over the pages, and each page's CPU copy is
//   exactly width * height bytes,
// - 300 glyphs loaded one after another grow an unrecorded page,
//   and every glyph is still in the page afterwards: the
//   texel at the center of each glyph's UV rectangle holds full coverage
//   (the test font's glyph is a filled square), and texels outside the
//   glyph rectangles hold none.
//
// `fontAtlasR8 --gpu-check` (daily CI under Xvfb) opens a window
// and, in one frame, draws 150 glyphs that were never drawn before into an
// Fbo, one drawString() call each, using pages whose sizes stay fixed inside
// the frame. It checks that the page texture is SG_PIXELFORMAT_R8 and that
// every glyph is in the Fbo with the color set by setColor() at full
// coverage (straight alpha over a transparent Fbo: rgb = color, a = 1).
// Then it checks real-metric page sizing, aligned placement and identical
// minified pixels for identical glyph rasters at different atlas offsets.
//
// The font is built here with 600 distinct glyph indices and identical square
// outlines, so the test runs the same everywhere.
// =============================================================================

#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"

#include <chrono>
#include <map>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
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
static const uint32_t kCodepointCount = 600;

// Glyph 0: empty .notdef; 600 squares (100..900 x 0..800); empty space.
// Codepoints U+4E00..U+4E00+kCodepointCount-1 have distinct glyph indices,
// ' ' maps to the final glyph. 1000 units per em, ascender 800, descender -200.
// The large-cell variant uses descender 0 (388 padded texels at 128px/3x)
// and maps U+4E00+7*i to glyph i+1 to match the reported per-call workload.
static Bytes makeSquareFont(bool withCounter = false, bool largeCells = false) {
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
    put16(hhea, largeCells ? 0 : (uint16_t)-200);
    put16(hhea, 0);
    put16(hhea, 1000);
    for (int i = 0; i < 11; i++) put16(hhea, 0);
    put16(hhea, kCodepointCount + 2);          // numberOfHMetrics

    Bytes maxp;
    put32(maxp, 0x00005000);
    put16(maxp, kCodepointCount + 2);

    Bytes hmtx;
    for (uint32_t i = 0; i < kCodepointCount + 2; i++) { put16(hmtx, 1000); put16(hmtx, 0); }

    // cmap: one Microsoft / Unicode full subtable.
    Bytes cmap;
    put16(cmap, 0);
    put16(cmap, 1);
    put16(cmap, 3);
    put16(cmap, 10);
    put32(cmap, 12);
    {
        // Format 12 gives every CJK codepoint its own glyph index.
        Bytes s12;
        put16(s12, 12);
        put16(s12, 0);
        put32(s12, 16 + 12 * (largeCells ? kCodepointCount + 1 : 2));
        put32(s12, 0);
        put32(s12, largeCells ? kCodepointCount + 1 : 2);
        put32(s12, 0x20); put32(s12, 0x20); put32(s12, kCodepointCount + 1);
        if (largeCells) {
            for (uint32_t i = 0; i < kCodepointCount; ++i) {
                put32(s12, kFirstCodepoint + 7 * i);
                put32(s12, kFirstCodepoint + 7 * i);
                put32(s12, i + 1);
            }
        } else {
            put32(s12, kFirstCodepoint); put32(s12, kFirstCodepoint + kCodepointCount - 1); put32(s12, 1);
        }
        cmap.insert(cmap.end(), s12.begin(), s12.end());
    }

    // The mid-pass fixture adds a reverse-wound inner contour. A transparent
    // counter inside the quad distinguishes actual glyph coverage from a
    // constant-white texture or an opaque rectangle.
    Bytes square;
    put16(square, withCounter ? 2 : 1);
    put16(square, 100); put16(square, 0); put16(square, 900); put16(square, 800);
    put16(square, 3);                           // endPtsOfContours
    if (withCounter) put16(square, 7);
    put16(square, 0);                           // instructionLength
    for (int i = 0; i < (withCounter ? 8 : 4); i++) square.push_back(1);
    put16(square, 100); put16(square, 800); put16(square, 0); put16(square, (uint16_t)-800);
    if (withCounter) {
        put16(square, 200); put16(square, 0); put16(square, 400); put16(square, 0);
    }
    put16(square, 0); put16(square, 0); put16(square, 800); put16(square, 0);
    if (withCounter) {
        put16(square, (uint16_t)-600); put16(square, 400); put16(square, 0); put16(square, (uint16_t)-400);
    }
    while (square.size() % 4) square.push_back(0);

    Bytes glyf;
    Bytes loca;
    put32(loca, 0); // empty .notdef
    for (uint32_t i = 0; i < kCodepointCount; ++i) {
        put32(loca, (uint32_t)glyf.size());
        glyf.insert(glyf.end(), square.begin(), square.end());
    }
    put32(loca, (uint32_t)glyf.size()); // empty space
    put32(loca, (uint32_t)glyf.size()); // end

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
    check("the built small font loads", mgr.setupFromMemory(font.data(), font.size(), 16));

    const size_t initialBytes = mgr.getMemoryUsage();
    check("a new font has one 256x256 page of 65536 bytes",
          mgr.getAtlasCount() == 1 && initialBytes == 256u * 256u,
          "bytes " + to_string(initialBytes));
    check("the built font loads", mgr.setupFromMemory(font.data(), font.size(), 48));
    check("a larger font reserves its recording size before packing",
          mgr.getAtlas(0).getWidth() == 512 && mgr.getAtlas(0).getHeight() == 512);

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
    check("the unrecorded page grew (>= 1024)", maxSide >= 1024,
          "largest side " + to_string(maxSide));

    // Every glyph is still where its UVs point after the growth steps, and
    // the space between glyphs is empty.
    int missing = 0;
    bool aligned = true;
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
    check("every glyph's center texel holds full coverage", missing == 0,
          to_string(missing) + " glyphs without coverage");
    size_t stray = 0;
    for (size_t p = 0; p < mgr.getAtlasCount(); p++) {
        const auto& px = mgr.getAtlas(p).getPixels();
        for (size_t t = 0; t < min(px.size(), inside[p].size()); t++)
            if (px[t] != 0 && !inside[p][t]) stray++;
    }
    for (uint32_t i = 0; i < kGlyphs; ++i) {
        const auto* g = mgr.getOrLoadGlyph(kFirstCodepoint + i);
        const auto& a = mgr.getAtlas(g->getAtlasIndex());
        aligned &= int(lround(g->getU0() * a.getWidth())) % 16 == 0 &&
                   int(lround(g->getV0() * a.getHeight())) % 16 == 0;
    }
    check("every glyph raster/UV origin stays 16-aligned after CPU growth", aligned);
    check("texels outside the glyph rectangles hold no coverage", stray == 0,
          to_string(stray) + " texels");
}

// --- --gpu-check ---------------------------------------------------------------
static string g_fontPath;
static const int kCell = 48;
static const int kCols = 15;
static const int kRows = 10;

// The fixture uses ascent 800/descent 0 and a 1000x800 font box.
// Font passes rounded logical-size * DPI to the manager; pass 256 here to
// exercise 128px at DPI 2 independently of the Xvfb display's actual scale.
static void checkPageSizing() {
    const Bytes bytes = makeSquareFont(false, true);
    internal::FontAtlasManager mgr;
    mgr.setOversample(3);
    check("gpu: 128px at DPI 2 loads through Font's physical-size input",
          mgr.setupFromMemory(bytes.data(), bytes.size(), 256));
    const int gpuLimit = min(sg_query_limits().max_image_size_2d, 8192);
    int expected = 256;
    // Raster scale = 256/800 * 3; aligned cells include filter + padding.
    while (expected * 2 <= gpuLimit &&
           ((expected - 16) / 976) * ((expected - 16) / 784) < 32) expected *= 2;
    printf("FONT_SIZING physical_size=256 oversample=3 gpu_max=%d expected=%d page=%dx%d\n",
           sg_query_limits().max_image_size_2d, expected,
           mgr.getAtlas(0).getWidth(), mgr.getAtlas(0).getHeight());
    check("gpu: big font reserves 32 real cells up to the GPU ceiling",
          mgr.getAtlas(0).getWidth() == expected,
          "expected " + to_string(expected) + ", got " + to_string(mgr.getAtlas(0).getWidth()));
    internal::FontAtlasManager plain;
    check("gpu: small font at 1x loads", plain.setupFromMemory(bytes.data(), bytes.size(), 16));
    check("gpu: small font uses a 256x256 page", plain.getAtlas(0).getWidth() == 256);
}

static void checkAlignedMove() {
    Font font;
    font.setOversampling(1);
    check("aligned move: font loads", bool(font.load(g_fontPath, 37)));
    auto mgr = internal::SharedFontCache::getInstance().get(
        internal::FontCacheKey{g_fontPath, 37, 0, true, 1});
    if (!mgr) { check("aligned move: manager available", false); return; }
    const auto* first = mgr->getOrLoadGlyph(kFirstCodepoint);
    // Fill past a shelf boundary so both x and y phases are exercised.
    for (uint32_t i = 1; i <= 16; ++i) mgr->getOrLoadGlyph(kFirstCodepoint + i);
    const auto* second = mgr->getOrLoadGlyph(kFirstCodepoint + 16);
    const auto& page = mgr->getAtlas(0);
    const int x0 = int(lround(first->getU0() * page.getWidth()));
    const int x1 = int(lround(second->getU0() * page.getWidth()));
    const int y0 = int(lround(first->getV0() * page.getHeight()));
    const int y1 = int(lround(second->getV0() * page.getHeight()));
    check("aligned move: identical rasters occupy different 16-aligned offsets",
          y0 != y1 && x0 != x1 && x0 % 16 == 0 && x1 % 16 == 0 &&
          y0 % 16 == 0 && y1 % 16 == 0);
    Fbo target;
    target.allocate(64, 64);
    vector<unsigned char> pixels[2];
    for (int i = 0; i < 2; ++i) {
        target.begin(0, 0, 0, 0);
        setColor(1);
        pushMatrix();
        scale(.25f); // Font chooses the trilinear mip sampler.
        font.drawString(utf8(kFirstCodepoint + (i == 0 ? 0 : 16)), 48, 160);
        popMatrix();
        target.end();
        pixels[i].resize(64 * 64 * 4);
        check("aligned move: readback", target.readPixels(pixels[i].data()));
    }
    check("aligned move: production page has mipmaps",
          sg_query_image_desc(mgr->getAtlas(0).getTexture()).num_mipmaps > 1);
    bool hasInk = false;
    for (size_t i = 3; i < pixels[0].size(); i += 4) hasInk |= pixels[0][i] != 0;
    check("aligned move: 0.25x trilinear pixels are identical and contain ink",
          hasInk && pixels[0] == pixels[1]);
}

class GpuCheckApp : public App {
public:
    void setup() override {
        checkPageSizing();
        check("gpu: the built font loads from a file", (bool)font_.load(g_fontPath, 32));
        fbo_.allocate(kCols * kCell, kRows * kCell);
    }

    void draw() override {
        clear(0.0f);
        if (getDrawCount() < 3) return;

        // One frame: 150 glyphs never drawn before, one drawString() each.
        fbo_.begin(0.0f, 0.0f, 0.0f, 0.0f);
        setColor(1.0f, 0.5f, 0.25f);
        int recordedWidth = 0;
        for (int i = 0; i < kCols * kRows; i++) {
            const int cx = (i % kCols) * kCell, cy = (i / kCols) * kCell;
            font_.drawString(utf8(kFirstCodepoint + (uint32_t)i), (float)cx + 8, (float)cy + 36);
            if (i == 0) recordedWidth = font_.getAtlas(0)->getWidth();
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
        check("gpu: recorded page dimensions stay fixed during the burst",
              font_.getAtlas(0)->getWidth() == recordedWidth);

        const int W = kCols * kCell, H = kRows * kCell;
        vector<unsigned char> px((size_t)W * H * 4);
        check("gpu: Fbo readPixels", fbo_.readPixels(px.data()));

        // Fully covered texels must be exactly the color (straight alpha over
        // transparent black: rgb = color, a = 1). Counted per cell, so the
        // readback's row order does not matter (the grid fills the Fbo).
        int cellsWithGlyph = 0, wrongColor = 0;
        for (int i = 0; i < kCols * kRows; i++) {
            const int cx = (i % kCols) * kCell, cy = (i / kCols) * kCell;
            int full = 0;
            for (int y = cy; y < cy + kCell; y++) {
                for (int x = cx; x < cx + kCell; x++) {
                    const unsigned char* p = &px[((size_t)y * W + x) * 4];
                    if (p[3] != 255) continue;
                    full++;
                    if (abs(p[0] - 255) > 2 || abs(p[1] - 128) > 2 || abs(p[2] - 64) > 2) wrongColor++;
                }
            }
            if (full >= 200) cellsWithGlyph++;
        }
        check("gpu: all 150 glyphs drawn in the burst are in the Fbo",
              cellsWithGlyph == kCols * kRows, to_string(cellsWithGlyph) + " of 150");
        check("gpu: fully covered texels have the setColor() color", wrongColor == 0,
              to_string(wrongColor) + " texels");

        // Minified draws build and sample the R8 chain on the first draw.
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

        checkAlignedMove();

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

#include "midpassChecks.h"
bool g_burstPerGlyph = false;
#include "burstChecks.h"

} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
    const bool midpassCheck = argc > 1 && strcmp(argv[1], "--midpass-check") == 0;
    const bool burstCheck = argc > 1 && strcmp(argv[1], "--burst-check") == 0;
    const bool gpuCheck = burstCheck || midpassCheck || (argc > 1 && strcmp(argv[1], "--gpu-check") == 0);
    if (!gpuCheck) {
        checkCpuPages();
        printf("fontAtlasR8: %s\n", g_fail == 0 ? "OK" : "FAIL");
        return g_fail == 0 ? 0 : 1;
    }

    g_burstPerGlyph = burstCheck && argc > 2 && strcmp(argv[2], "--per-glyph") == 0;
    const Bytes font = makeSquareFont(midpassCheck || burstCheck, g_burstPerGlyph);
    g_fontPath = (filesystem::temp_directory_path() / "fontAtlasR8_square.ttf").string();
    {
        ofstream f(g_fontPath, ios::binary);
        f.write((const char*)font.data(), (streamsize)font.size());
    }
    WindowSettings settings;
    settings.setSize(kCols * kCell, kRows * kCell);
    settings.setHighDpi(false);
    if (burstCheck) {
        settings.setSize(256, 192);
        return tcCoreTest::runDisplayTest<BurstCheckApp>(settings, g_fail, [] {
            filesystem::remove(g_fontPath);
            check("burst checks completed", burstCompleted);
        });
    }
    if (midpassCheck) {
        settings.setSize(256, 192);
        return tcCoreTest::runDisplayTest<MidpassCheckApp>(settings, g_fail, [] {
            filesystem::remove(g_fontPath);
            check("one-shot and later-frame checks completed", midpassCompleted);
        });
    }
    return tcCoreTest::runDisplayTest<GpuCheckApp>(settings, g_fail, [] {
        filesystem::remove(g_fontPath);
        printf("fontAtlasR8 --gpu-check: %s\n", g_fail == 0 ? "OK" : "FAIL");
    });
}
