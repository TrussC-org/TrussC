#pragma once

// =============================================================================
// TrussC TrueType Font
// TrueType font rendering based on stb_truetype
//
// Design: Inspired by ofxTrueTypeFontLowRAM
// - SharedFontCache: Shares atlas for same font+size
// - FontAtlasManager: Atlas management (multi-atlas, dynamic expansion)
// - Font: User-facing class
//
// Atlas pages are R8: one byte of glyph coverage per texel, on the CPU and on
// the GPU. They are drawn with internal::activeCoverage2D(), whose shader
// (core/shaders/sglCoverage.glsl) uses R as alpha. A page starts at 256x256 and
// doubles up to the GPU's max 2D image size, capped at 8192x8192 (4096x4096
// when the limit cannot be queried); after that a new page is added.
// =============================================================================

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <functional>
#include <cstring>
#include <cmath>

#ifdef __EMSCRIPTEN__
#include <emscripten/fetch.h>
#include <emscripten.h>
#endif

// sokol headers
#include "sokol/sokol_app_tc.h"
#include "sokol/sokol_gfx.h"
#include "sokol/util/sokol_gl_tc.h"

// stb_truetype
#include "stb/stb_truetype.h"

#include "../utils/tcLog.h"
#include "../utils/tcOnceGate.h"
#include "tc/utils/tcLoadResult.h"
#include "../utils/tcSystemFont.h"
#include "../types/tcDirection.h"
#include "../types/tcRectangle.h"
#include "../../tcMath.h"  // Vec2
#include "tcFontVertical.h"
#include "tcPath.h"

// ---------------------------------------------------------------------------
// System font paths - use these for cross-platform font loading
// ---------------------------------------------------------------------------
// Names are resolved at load time via tc::systemFontPath (CoreText / DirectWrite
// / fontconfig per platform). Web keeps URLs since browsers don't expose system
// fonts — Font::load handles URL inputs directly via emscripten_fetch.
#ifdef __EMSCRIPTEN__
    // Web: Use Google Fonts via jsDelivr CDN (async load)
    #define TC_FONT_SANS     "https://cdn.jsdelivr.net/fontsource/fonts/noto-sans@latest/latin-400-normal.ttf"
    #define TC_FONT_SERIF    "https://cdn.jsdelivr.net/fontsource/fonts/noto-serif@latest/latin-400-normal.ttf"
    #define TC_FONT_MONO     "https://cdn.jsdelivr.net/fontsource/fonts/noto-sans-mono@latest/latin-400-normal.ttf"
    #define TC_FONT_SANS_JA  "https://cdn.jsdelivr.net/fontsource/fonts/noto-sans-jp@latest/japanese-400-normal.ttf"
    #define TC_FONT_SERIF_JA "https://cdn.jsdelivr.net/fontsource/fonts/noto-serif-jp@latest/japanese-400-normal.ttf"
#elif defined(_WIN32)
    // Windows — resolved via DirectWrite (not yet implemented; falls back to path)
    #define TC_FONT_SANS     "Segoe UI"
    #define TC_FONT_SERIF    "Times New Roman"
    #define TC_FONT_MONO     "Consolas"
    #define TC_FONT_SANS_JA  "Yu Gothic"
    #define TC_FONT_SERIF_JA "Yu Mincho"
#elif defined(__APPLE__)
    // macOS — resolved via CoreText (PostScript / family names)
    #define TC_FONT_SANS     "Helvetica"
    #define TC_FONT_SERIF    "Times New Roman"
    #define TC_FONT_MONO     "Menlo"
    #define TC_FONT_SANS_JA  "HiraginoSans-W3"
    #define TC_FONT_SERIF_JA "HiraMinProN-W3"
#elif defined(__ANDROID__)
    // Android — name resolution not yet implemented; system_fonts path-based lookup planned
    #define TC_FONT_SANS     "Roboto"
    #define TC_FONT_SERIF    "Noto Serif"
    #define TC_FONT_MONO     "Droid Sans Mono"
    #define TC_FONT_SANS_JA  "Noto Sans CJK JP"
    #define TC_FONT_SERIF_JA "Noto Serif CJK JP"
#else
    // Linux — resolved via fontconfig (not yet implemented; falls back to path)
    #define TC_FONT_SANS     "DejaVu Sans"
    #define TC_FONT_SERIF    "DejaVu Serif"
    #define TC_FONT_MONO     "DejaVu Sans Mono"
    #define TC_FONT_SANS_JA  "Noto Sans CJK JP"
    #define TC_FONT_SERIF_JA "Noto Serif CJK JP"
#endif

namespace trussc {

// Atlas / cache internals — not part of the public Font API. The user-facing
// Font class below wraps all of this.
namespace internal {

// Test hooks, not user settings (core/tests/fontSfntCheck): lower the vertex
// limit of stb_truetype's CFF counting pass and its flattened point limit,
// and cap the size STBTT_malloc allocates, so a test reaches these limits
// with small fonts. Values above the defaults are clamped to them. The reset
// restores the defaults (stb's vertex and point limits, no size cap). State
// lives in core/include/impl/stb_impl.cpp.
void setStbttLimitsForTests(int maxVertices, int maxPoints, size_t mallocMax);
void resetStbttLimitsForTests();

// ---------------------------------------------------------------------------
// Font cache key (font path + size)
// ---------------------------------------------------------------------------
struct FontCacheKey {
    std::string fontPath;
    int fontSize;
    bool mipmaps = true;    // allowed to build a mip chain (built lazily on first minified draw)
    int oversample = 1;     // rasterize NxN finer, then box-prefilter back down

    // Grid fit is deliberately absent: it is a draw-time placement decision
    // (see Font::fitY) and does not touch the atlas, so two fonts that differ
    // only in gridFit can and should share one.

    bool operator==(const FontCacheKey& other) const {
        return fontPath == other.fontPath && fontSize == other.fontSize
            && mipmaps == other.mipmaps && oversample == other.oversample;
    }
};

struct FontCacheKeyHash {
    size_t operator()(const FontCacheKey& key) const {
        size_t h1 = std::hash<std::string>()(key.fontPath);
        size_t h2 = std::hash<int>()(key.fontSize);
        size_t h3 = std::hash<bool>()(key.mipmaps);
        size_t h4 = std::hash<int>()(key.oversample);
        return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3);
    }
};

// ---------------------------------------------------------------------------
// Glyph information
// ---------------------------------------------------------------------------
class GlyphInfo {
public:
    size_t getAtlasIndex() const { return atlasIndex_; }
    float getU0() const { return u0_; }
    float getV0() const { return v0_; }
    float getU1() const { return u1_; }
    float getV1() const { return v1_; }
    float getXoff() const { return xoff_; }
    float getYoff() const { return yoff_; }
    float getWidth() const { return width_; }
    float getHeight() const { return height_; }
    float getAdvance() const { return advance_; }
    bool isValid() const { return valid_; }

private:
    friend class FontAtlasManager;

    size_t atlasIndex_;          // Which atlas contains this glyph
    float u0_, v0_, u1_, v1_;    // Texture coordinates (normalized)
    float xoff_, yoff_;          // Drawing offset
    float width_, height_;       // Glyph size (pixels)
    float advance_;              // Advance width to next character
    bool valid_ = false;
};

// ---------------------------------------------------------------------------
// Atlas state
// ---------------------------------------------------------------------------
class AtlasState {
public:
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }
    sg_image getTexture() const { return texture_; }
    sg_view getView() const { return view_; }
    bool isTextureValid() const { return textureValid_; }
    // CPU copy of the page: width * height bytes, glyph coverage per texel.
    const std::vector<uint8_t>& getPixels() const { return pixels_; }

private:
    friend class FontAtlasManager;

    int currentX_ = 0;           // X position for next glyph
    int currentY_ = 0;           // Y position for next glyph
    int rowHeight_ = 0;          // Current row height
    int width_ = 0;
    int height_ = 0;

    // GPU resources
    sg_image texture_ = {};
    sg_view view_ = {};
    bool textureValid_ = false;
    bool textureDirty_ = false;

    // CPU-side pixel data (for expansion/update)
    std::vector<uint8_t> pixels_;  // R8: glyph coverage, one byte per texel
};

// ---------------------------------------------------------------------------
// Font atlas manager class
// Shared for same font+size combination
// ---------------------------------------------------------------------------
class FontAtlasManager {
public:
    FontAtlasManager() = default;
    ~FontAtlasManager() { cleanup(); }

    // Non-copyable
    FontAtlasManager(const FontAtlasManager&) = delete;
    FontAtlasManager& operator=(const FontAtlasManager&) = delete;

    // -------------------------------------------------------------------------
    // Initialization
    // -------------------------------------------------------------------------
    bool setup(const std::string& fontPath, int fontSize) {
        cleanup();

        // Load font file (fontPath is UTF-8 — convert so non-ASCII paths
        // survive on Windows)
        const auto path = internal::utf8ToPath(fontPath);
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec)) {
            logError() << "FontAtlasManager: not a regular file: " << fontPath;
            return false;
        }
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) {
            logError() << "FontAtlasManager: failed to open " << fontPath;
            return false;
        }

        const std::streamoff end = file.tellg();
        if (end < 0) {
            logError() << "FontAtlasManager: cannot get the size of " << fontPath;
            return false;
        }
        // Same size limit as checkSfntSkeleton, checked before the buffer
        // is allocated.
        if ((uint64_t)end >= kMaxFontDataSize) {
            logWarning() << "FontAtlasManager: not a usable font (" << kTooLargeReason << ")";
            return false;
        }
        const size_t fileSize = (size_t)end;
        file.seekg(0, std::ios::beg);
        fontData_.resize(fileSize);
        if (!file.read(reinterpret_cast<char*>(fontData_.data()), fileSize)) {
            logError() << "FontAtlasManager: failed to read " << fontPath;
            return false;
        }

        fontName_ = fontPath;
        return initFromFontData(fontSize);
    }

    bool setupFromMemory(const uint8_t* data, size_t size, int fontSize) {
        cleanup();

        if (!data && size > 0) {
            logWarning() << "FontAtlasManager: font data pointer is null";
            return false;
        }
        // Empty data is rejected by the sfnt check in initFromFontData.
        fontData_.assign(data, data + size);

        fontName_ = "font data in memory";
        return initFromFontData(fontSize);
    }

    // Opt-in mipmapping. Must be set before glyphs are uploaded (the atlas
    // texture is (re)built lazily in updateAtlasTexture, which reads this).
    void setMipmaps(bool enabled) { wantMipmaps_ = enabled; }

    // Called from the draw path the first time this atlas is sampled below the
    // bilinear-safe rate. Building the chain eagerly would tax every app that
    // never minifies: the atlas texture is destroyed and recreated on every new
    // glyph (see updateAtlasTexture), so a mip chain means re-walking ~1.33x
    // the atlas on the CPU each time a fresh glyph shows up -- which for CJK is
    // most frames during warm-up. Deferring it makes the cost land only on apps
    // that actually draw small text, and only once.
    void requestMipmaps() {
        if (!wantMipmaps_ || mipsBuilt_) return;
        mipsBuilt_ = true;
        for (auto& atlas : atlases_) atlas.textureDirty_ = true;
        // Fires at most once per atlas, and marks a real one-off cost (~33%
        // more atlas texture, plus a rebuild), so it is worth saying out loud.
        logNotice("Font") << "text drawn minified — building glyph atlas mipmaps";
    }
    bool hasMipmaps() const { return mipsBuilt_; }

    // Must be set before any glyph is rasterized (glyphs are lazy, so setting
    // it right after setup() is early enough). Clamped to at least 1.
    void setOversample(int n) {
        oversample_ = (n < 1) ? 1 : n;
        if (loaded_) warnIfGlyphsExceedPage();
    }
    int getOversample() const { return oversample_; }

    // Test hook (core/tests/fontAtlasLimit), not part of the Font API:
    // largest atlas page side in texels (the GPU limit, capped at 8192).
    int getMaxAtlasSizeForTests() const { return maxAtlasSize_; }

private:
    // A glyph fits the largest page when its padded box fits the region the
    // packer fills first on a page grown to maxAtlasSize_: the right half,
    // starting GLYPH_PADDING in from the top and from the middle.
    bool fitsLargestPage(int paddedWidth, int paddedHeight) const {
        return paddedWidth <= maxAtlasSize_ / 2 - GLYPH_PADDING &&
               paddedHeight <= maxAtlasSize_ - GLYPH_PADDING;
    }

    // Load-time check: the font's overall bounding box at this size and
    // oversampling against the largest page. When some glyphs can be larger
    // than a page, those glyphs are rasterized at a lower resolution (see
    // addGlyphToAtlas); one warning per loaded font says so.
    void warnIfGlyphsExceedPage() {
        int bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
        stbtt_GetFontBoundingBox(&fontInfo_, &bx0, &by0, &bx1, &by1);
        const double s = (double)scale_ * oversample_;
        const double w = std::ceil(bx1 * s) - std::floor(bx0 * s) + (oversample_ - 1);
        const double h = std::ceil(by1 * s) - std::floor(by0 * s) + (oversample_ - 1);
        const int pw = (int)std::min(w + GLYPH_PADDING, 1e9);
        const int ph = (int)std::min(h + GLYPH_PADDING, 1e9);
        if (fitsLargestPage(pw, ph)) return;
        if (!pageLimitWarned_.isFirstTime()) return;
        logWarning("Font") << fontName_ << " at size " << fontSize_
                           << " (oversampling " << oversample_
                           << "): some glyphs are larger than an atlas page ("
                           << maxAtlasSize_ << "x" << maxAtlasSize_
                           << " texels); those glyphs are drawn at a lower resolution";
    }

    bool initFromFontData(int fontSize, int fontIndex = 0) {
        // The sfnt structure is checked against the data size before the data
        // is given to stb_truetype.
        std::string reason;
        if (!checkSfntSkeleton(fontData_.data(), fontData_.size(), fontIndex, reason)) {
            logWarning() << "FontAtlasManager: not a usable font (" << reason << ")";
            fontData_.clear();
            return false;
        }

        // Get font offset (required for .ttc files with multiple fonts)
        int offset = stbtt_GetFontOffsetForIndex(fontData_.data(), fontIndex);
        if (offset < 0) {
            logError() << "FontAtlasManager: invalid font index " << fontIndex;
            fontData_.clear();
            return false;
        }

        // Initialize with stb_truetype
        if (!stbtt_InitFont(&fontInfo_, fontData_.data(), offset)) {
            logError() << "FontAtlasManager: failed to init font";
            fontData_.clear();
            return false;
        }

        // Usable glyph indices are below maxp numGlyphs, and for a CFF font
        // also below the number of CharStrings.
        glyphLimit_ = fontInfo_.numGlyphs;
        if (!fontInfo_.glyf) {
            const int count = cffCharStringsCount(fontInfo_.charstrings);
            if (count < 1) {
                logWarning() << "FontAtlasManager: not a usable font "
                                "(CFF CharStrings INDEX cannot be read)";
                fontData_.clear();
                return false;
            }
            glyphLimit_ = std::min(glyphLimit_, count);
        }

        fontSize_ = fontSize;
        scale_ = stbtt_ScaleForPixelHeight(&fontInfo_, (float)fontSize);

        // Get font metrics
        int ascent, descent, lineGap;
        stbtt_GetFontVMetrics(&fontInfo_, &ascent, &descent, &lineGap);

        ascent_ = ascent * scale_;
        descent_ = descent * scale_;
        lineGap_ = lineGap * scale_;

        // Note: the metrics reported here are the font's own, always. Grid fit
        // does not live at load time -- it is a draw-time decision made per
        // baseline against the live transform. See Font::fitY().

        // Get space advance
        int spaceIndex = findGlyphIndex(' ');
        int advanceWidth, leftSideBearing;
        stbtt_GetGlyphHMetrics(&fontInfo_, spaceIndex, &advanceWidth, &leftSideBearing);
        spaceAdvance_ = advanceWidth * scale_;

        // Query GPU max texture size (cap at 8192 for sanity)
        if (sg_isvalid()) {
            int gpuMax = sg_query_limits().max_image_size_2d;
            maxAtlasSize_ = (gpuMax > 0) ? std::min(gpuMax, 8192) : 4096;
        }

        // Create first atlas
        createNewAtlas();

        loaded_ = true;
        warnIfGlyphsExceedPage();
        return true;
    }

    // Glyph index for a codepoint. A glyph index from the cmap past numGlyphs
    // (for CFF, past the CharStrings count) maps to .notdef (0), and so does a
    // codepoint above U+10FFFF.
    int findGlyphIndex(uint32_t codepoint) const {
        if (codepoint > 0x10FFFF) return 0;
        const int glyph = stbtt_FindGlyphIndex(&fontInfo_, (int)codepoint);
        if (glyph < 0 || glyph >= glyphLimit_) return 0;
        return glyph;
    }

    // Number of entries in a CFF INDEX (stb's view of the CharStrings INDEX,
    // which lies within the CFF table), or 0 when the count and its offset
    // array do not fit in the buffer.
    static int cffCharStringsCount(const stbtt__buf& index) {
        if (!index.data || index.size < 3) return 0;
        const uint64_t count = ((uint64_t)index.data[0] << 8) | index.data[1];
        const uint64_t offSize = index.data[2];
        if (count == 0 || offSize < 1 || offSize > 4) return 0;
        if (3 + (count + 1) * offSize > (uint64_t)index.size) return 0;
        return (int)count;
    }

    // -------------------------------------------------------------------------
    // sfnt skeleton check (stb_truetype backend)
    //
    // Checks the collection header, the table directory, the fixed fields of
    // head / hhea / maxp / cmap, loca and hmtx against the data size before
    // the data is given to stb_truetype. It does not look inside cmap
    // subtables, glyph outlines or CFF data. It belongs to the stb backend:
    // drop it together with stb if the backend is replaced. Offsets and
    // lengths are added in 64 bits.
    // -------------------------------------------------------------------------
    static constexpr uint64_t kMaxFontDataSize = 0x40000000u;
    static constexpr const char* kTooLargeReason =
        "the bundled font engine (stb_truetype) cannot handle fonts larger than 1 GiB";

    static bool checkSfntSkeleton(const uint8_t* data, size_t size, int fontIndex,
                                  std::string& reason) {
        auto u16 = [data](uint64_t o) -> uint64_t {
            return ((uint64_t)data[o] << 8) | data[o + 1];
        };
        auto u32 = [data](uint64_t o) -> uint64_t {
            return ((uint64_t)data[o] << 24) | ((uint64_t)data[o + 1] << 16) |
                   ((uint64_t)data[o + 2] << 8) | data[o + 3];
        };
        auto tag = [](const char* t) -> uint64_t {
            return ((uint64_t)(uint8_t)t[0] << 24) | ((uint64_t)(uint8_t)t[1] << 16) |
                   ((uint64_t)(uint8_t)t[2] << 8) | (uint8_t)t[3];
        };
        auto tagName = [](uint64_t t) {
            std::string s(4, ' ');
            for (int i = 0; i < 4; i++) {
                const char c = (char)((t >> (24 - 8 * i)) & 0xff);
                s[i] = (c >= 0x20 && c < 0x7f) ? c : '?';
            }
            return s;
        };

        const uint64_t n = size;
        if (n < 12) {
            reason = "data is " + std::to_string(size) + " bytes, shorter than a font header";
            return false;
        }
        // The stb backend handles fonts below 1 GiB.
        if (n >= kMaxFontDataSize) {
            reason = kTooLargeReason;
            return false;
        }

        // Font collection (.ttc): pick the font at fontIndex.
        uint64_t fontStart = 0;
        if (u32(0) == tag("ttcf")) {
            const uint64_t version = u32(4);
            if (version != 0x00010000u && version != 0x00020000u) {
                reason = "unsupported font collection version";
                return false;
            }
            const uint64_t numFonts = u32(8);
            if (numFonts == 0 || 12 + 4 * numFonts > n) {
                reason = "font collection header is truncated";
                return false;
            }
            if (fontIndex < 0 || (uint64_t)fontIndex >= numFonts) {
                reason = "font index " + std::to_string(fontIndex) + " is out of range";
                return false;
            }
            fontStart = u32(12 + 4 * (uint64_t)fontIndex);
            if (fontStart + 12 > n) {
                reason = "font offset in the collection is outside the data";
                return false;
            }
        } else if (fontIndex != 0) {
            reason = "font index " + std::to_string(fontIndex) + " is out of range";
            return false;
        }

        // The sfnt versions stb_truetype accepts.
        const uint64_t sfntVersion = u32(fontStart);
        if (sfntVersion != 0x00010000u && sfntVersion != tag("1\0\0\0") &&
            sfntVersion != tag("true") && sfntVersion != tag("typ1") &&
            sfntVersion != tag("OTTO")) {
            reason = "not a TrueType / OpenType font";
            return false;
        }

        // Table directory. As in stbtt_InitFont, the first entry with a given
        // tag is used, and an entry at offset 0 counts as missing.
        const uint64_t numTables = u16(fontStart + 4);
        const uint64_t dirStart = fontStart + 12;
        if (dirStart + 16 * numTables > n) {
            reason = "table directory is truncated";
            return false;
        }
        struct Table { uint64_t offset = 0, length = 0; bool seen = false, found = false; };
        Table cmap, head, hhea, hmtx, maxp, glyf, loca, cff;
        const std::pair<uint64_t, Table*> wanted[] = {
            {tag("cmap"), &cmap}, {tag("head"), &head}, {tag("hhea"), &hhea},
            {tag("hmtx"), &hmtx}, {tag("maxp"), &maxp}, {tag("glyf"), &glyf},
            {tag("loca"), &loca}, {tag("CFF "), &cff},
        };
        for (uint64_t i = 0; i < numTables; i++) {
            const uint64_t rec = dirStart + 16 * i;
            const uint64_t t = u32(rec);
            const uint64_t offset = u32(rec + 8);
            const uint64_t length = u32(rec + 12);
            if (offset + length > n) {
                reason = "table '" + tagName(t) + "' is outside the data";
                return false;
            }
            for (const auto& w : wanted) {
                if (w.first == t && !w.second->seen) {
                    *w.second = Table{offset, length, true, offset != 0};
                }
            }
        }

        // Required tables, and the fixed fields stb reads from them.
        const bool trueType = glyf.found;  // same rule as stbtt_InitFont
        const std::pair<const char*, const Table*> required[] = {
            {"cmap", &cmap}, {"head", &head}, {"hhea", &hhea}, {"hmtx", &hmtx},
            {"maxp", &maxp}, {trueType ? "loca" : "CFF ", trueType ? &loca : &cff},
        };
        for (const auto& r : required) {
            if (!r.second->found) {
                reason = std::string("missing required table '") + r.first + "'";
                return false;
            }
        }
        if (head.length < 54 || hhea.length < 36 || maxp.length < 6 || cmap.length < 4) {
            reason = "head / hhea / maxp / cmap table is too short";
            return false;
        }

        // cmap encoding records, and the offsets of the subtables stb may pick
        // (Microsoft Unicode BMP / full, or any Unicode platform record).
        const uint64_t numSubtables = u16(cmap.offset + 2);
        if (4 + 8 * numSubtables > cmap.length) {
            reason = "cmap encoding records are truncated";
            return false;
        }
        for (uint64_t i = 0; i < numSubtables; i++) {
            const uint64_t rec = cmap.offset + 4 + 8 * i;
            const uint64_t platform = u16(rec);
            const uint64_t encoding = u16(rec + 2);
            if (platform == 0 || (platform == 3 && (encoding == 1 || encoding == 10))) {
                if (u32(rec + 4) + 2 > cmap.length) {
                    reason = "cmap subtable offset is outside the cmap table";
                    return false;
                }
            }
        }

        // hmtx: numberOfHMetrics long entries, then one short entry per
        // remaining glyph.
        const uint64_t numGlyphs = u16(maxp.offset + 4);
        const uint64_t numLong = u16(hhea.offset + 34);
        if (numLong < 1 || numLong > numGlyphs) {
            reason = "hhea numberOfHMetrics is out of range";
            return false;
        }
        if (4 * numLong + 2 * (numGlyphs - numLong) > hmtx.length) {
            reason = "hmtx table is too short";
            return false;
        }

        // loca: numGlyphs + 1 entries, in non-decreasing order, each within
        // glyf (a glyph's data runs from its entry to the next one).
        if (trueType) {
            const uint64_t locaFormat = u16(head.offset + 50);
            if (locaFormat > 1) {
                reason = "unsupported loca format";
                return false;
            }
            const uint64_t entrySize = (locaFormat == 0) ? 2 : 4;
            if ((numGlyphs + 1) * entrySize > loca.length) {
                reason = "loca table is too short";
                return false;
            }
            uint64_t previous = 0;
            for (uint64_t i = 0; i <= numGlyphs; i++) {
                const uint64_t at = loca.offset + i * entrySize;
                const uint64_t glyphOffset = (locaFormat == 0) ? u16(at) * 2 : u32(at);
                if (glyphOffset > glyf.length) {
                    reason = "loca entry points past the end of glyf";
                    return false;
                }
                if (glyphOffset < previous) {
                    reason = "loca entries are not in increasing order";
                    return false;
                }
                previous = glyphOffset;
            }
        }
        return true;
    }

public:

    void cleanup() {
        // Only release GPU resources if sokol is still valid
        // (may have already shut down at program exit). Destruction is
        // deferred: cleanup() also runs mid-frame when a font is re-loaded
        // via setup(), and draws recorded earlier that frame may still
        // reference the old atlas views.
        if (sg_isvalid()) {
            for (auto& pd : pendingDestroys_) {
                internal::deferGpuDestroy(pd.view);
                internal::deferGpuDestroy(pd.image);
            }
            for (auto& atlas : atlases_) {
                if (atlas.textureValid_) {
                    internal::deferGpuDestroy(atlas.view_);
                    internal::deferGpuDestroy(atlas.texture_);
                }
            }
        }
        pendingDestroys_.clear();
        atlases_.clear();
        glyphs_.clear();
        fontData_.clear();
        glyphLimit_ = 0;
        loaded_ = false;
    }

    // -------------------------------------------------------------------------
    // Get glyph (lazy loading)
    // -------------------------------------------------------------------------
    const GlyphInfo* getOrLoadGlyph(uint32_t codepoint) {
        auto it = glyphs_.find(codepoint);
        if (it != glyphs_.end()) {
            return &it->second;
        }

        // Add glyph. A glyph that cannot be placed is kept with valid_ = false,
        // so it is not tried again until clearAtlas().
        GlyphInfo info;
        if (!addGlyphToAtlas(codepoint, info)) info.valid_ = false;
        return &(glyphs_[codepoint] = info);
    }

    bool hasGlyph(uint32_t codepoint) const {
        return glyphs_.find(codepoint) != glyphs_.end();
    }

    // Check whether the underlying font file actually contains a glyph for
    // this codepoint (vs. .notdef). Used by vertical writing to decide whether
    // a CJK Compatibility Forms variant (U+FE10–FE4F) is usable.
    bool fontHasGlyph(uint32_t codepoint) const {
        if (!loaded_) return false;
        return findGlyphIndex(codepoint) != 0;
    }

    // -------------------------------------------------------------------------
    // Vector glyph outline (em-normalized, screen Y-down).
    //
    // Returns a single Path with one subpath per contour (use moveTo to walk
    // them). Coordinates are in em units (1.0 = em), so a glyph above the
    // baseline has Y < 0. Each subpath is closed. Glyphs with no outline
    // (e.g. the space character) return an empty Path without logging.
    // Subpath layout matches stbtt's contour order — for TrueType fonts the
    // outer ring comes first (CCW in font Y-up, CW in our screen Y-down),
    // followed by holes (opposite winding). Path::drawFill() detects this
    // via spatial containment so callers don't need to think about it.
    // -------------------------------------------------------------------------
    Path getGlyphPath(uint32_t codepoint) const {
        Path result;
        if (!loaded_) return result;

        int glyphIndex = findGlyphIndex(codepoint);
        if (glyphIndex == 0) {
            logWarning() << "FontAtlasManager: no glyph for U+" << std::hex
                         << codepoint << std::dec;
            return result;
        }

        stbtt_vertex* vertices = nullptr;
        const int numVerts = stbtt_GetGlyphShape(&fontInfo_, glyphIndex, &vertices);
        if (numVerts <= 0 || !vertices) {
            if (vertices) stbtt_FreeShape(&fontInfo_, vertices);
            return result;  // valid but empty outline (space etc.)
        }

        // stbtt returns vertex coords in font design units, Y-up.
        // Multiply by emScale to get em units (1.0 = em); negate Y for screen Y-down.
        const float emScale = stbtt_ScaleForMappingEmToPixels(&fontInfo_, 1.0f);

        bool subpathOpen = false;
        for (int i = 0; i < numVerts; i++) {
            const stbtt_vertex& v = vertices[i];
            const float x = (float)v.x * emScale;
            const float y = -(float)v.y * emScale;
            switch (v.type) {
                case STBTT_vmove:
                    if (subpathOpen) result.close();
                    result.moveTo(x, y);
                    subpathOpen = true;
                    break;
                case STBTT_vline:
                    result.lineTo(x, y);
                    break;
                case STBTT_vcurve: {
                    // Explicit resolution: Path's adaptive tessellation uses an
                    // absolute tolerance, but our coords are em-normalized
                    // (~1.0 max), so without an explicit count curves collapse
                    // to straight chords. 12 segments is plenty for glyph quads.
                    const float cx = (float)v.cx * emScale;
                    const float cy = -(float)v.cy * emScale;
                    result.quadBezierTo(Vec2{cx, cy}, Vec2{x, y}, 12);
                    break;
                }
                case STBTT_vcubic: {
                    const float cx  = (float)v.cx  * emScale;
                    const float cy  = -(float)v.cy * emScale;
                    const float cx1 = (float)v.cx1 * emScale;
                    const float cy1 = -(float)v.cy1 * emScale;
                    result.bezierTo(Vec3{cx, cy, 0.f}, Vec3{cx1, cy1, 0.f}, Vec3{x, y, 0.f}, 16);
                    break;
                }
            }
        }
        if (subpathOpen) result.close();

        stbtt_FreeShape(&fontInfo_, vertices);
        return result;
    }

    // Em-normalized horizontal advance for this codepoint (1.0 = em).
    float getGlyphAdvanceEm(uint32_t codepoint) const {
        if (!loaded_) return 0.f;
        int glyphIndex = findGlyphIndex(codepoint);
        if (glyphIndex == 0) return 0.f;
        int advanceWidth = 0, lsb = 0;
        stbtt_GetGlyphHMetrics(&fontInfo_, glyphIndex, &advanceWidth, &lsb);
        const float emScale = stbtt_ScaleForMappingEmToPixels(&fontInfo_, 1.0f);
        return (float)advanceWidth * emScale;
    }

    // -------------------------------------------------------------------------
    // Get texture
    // -------------------------------------------------------------------------
    void ensureTexturesUpdated() {
        // Destroy GPU resources from previous frame (safe: sgl commands already consumed)
        flushPendingDestroys();

        for (auto& atlas : atlases_) {
            if (atlas.textureDirty_) {
                updateAtlasTexture(atlas);
            }
        }
    }

    size_t getAtlasCount() const { return atlases_.size(); }

    const AtlasState& getAtlas(size_t index) const { return atlases_[index]; }

    // -------------------------------------------------------------------------
    // Metrics
    // -------------------------------------------------------------------------
    float getLineHeight() const { return ascent_ - descent_ + lineGap_; }
    float getAscent() const { return ascent_; }
    float getDescent() const { return descent_; }

    float getSpaceAdvance() const { return spaceAdvance_; }
    int getFontSize() const { return fontSize_; }

    // -------------------------------------------------------------------------
    // Atlas management
    // -------------------------------------------------------------------------

    // Clear all atlas pages and cached glyphs.
    // Font data and metrics are preserved, so glyphs are re-rasterized on demand.
    void clearAtlas() {
        // Deferred destroy: text drawn earlier this frame may still have
        // sokol_gl commands referencing the old atlas views (drained in
        // present(); skipped there if sokol has already shut down).
        for (auto& pd : pendingDestroys_) {
            internal::deferGpuDestroy(pd.view);
            internal::deferGpuDestroy(pd.image);
        }
        for (auto& atlas : atlases_) {
            if (atlas.textureValid_) {
                internal::deferGpuDestroy(atlas.view_);
                internal::deferGpuDestroy(atlas.texture_);
            }
        }
        pendingDestroys_.clear();
        atlases_.clear();
        glyphs_.clear();
        if (loaded_) {
            createNewAtlas();
        }
    }

    // -------------------------------------------------------------------------
    // Memory usage
    // -------------------------------------------------------------------------
    size_t getMemoryUsage() const {
        size_t total = 0;
        for (const auto& atlas : atlases_) {
            total += atlas.pixels_.size();
        }
        return total;
    }

    size_t getLoadedGlyphCount() const { return glyphs_.size(); }

private:
    static constexpr int INITIAL_ATLAS_SIZE = 256;
    static constexpr int GLYPH_PADDING = 2;

    // Queried at runtime from GPU limits (capped to 8192 for sanity)
    int maxAtlasSize_ = 4096;

    // Font data
    std::vector<uint8_t> fontData_;
    stbtt_fontinfo fontInfo_ = {};
    int glyphLimit_ = 0;         // glyph indices below this are usable
    int fontSize_ = 0;
    float scale_ = 0;
    float ascent_ = 0;
    float descent_ = 0;
    float lineGap_ = 0;
    float spaceAdvance_ = 0;

    // Atlases
    std::vector<AtlasState> atlases_;
    bool wantMipmaps_ = true;    // mip chain allowed (opt out via Font::setMipmaps)
    bool mipsBuilt_ = false;     // ...and actually needed, i.e. something minified
    int oversample_ = 1;         // NxN supersampling of the rasterized glyph
    std::string fontName_;       // path, for log messages
    OnceGate pageLimitWarned_;   // load-time page-limit warning, once per font

    // Glyph cache
    std::unordered_map<uint32_t, GlyphInfo> glyphs_;

    bool loaded_ = false;

    // Deferred GPU resource destruction (views/images may still be referenced
    // by queued sgl commands from earlier draw calls in the same frame)
    struct PendingDestroy {
        sg_view view;
        sg_image image;
    };
    std::vector<PendingDestroy> pendingDestroys_;
    uint64_t lastDestroyFlushFrame_ = 0;

    // -------------------------------------------------------------------------
    // Atlas management
    // -------------------------------------------------------------------------
    size_t createNewAtlas() {
        AtlasState atlas;
        atlas.width_ = INITIAL_ATLAS_SIZE;
        atlas.height_ = INITIAL_ATLAS_SIZE;
        atlas.currentX_ = GLYPH_PADDING;
        atlas.currentY_ = GLYPH_PADDING;
        atlas.rowHeight_ = 0;
        atlas.pixels_.resize((size_t)atlas.width_ * atlas.height_, 0);
        atlas.textureDirty_ = true;

        atlases_.push_back(std::move(atlas));
        return atlases_.size() - 1;
    }

    bool expandAtlas(size_t atlasIndex) {
        AtlasState& atlas = atlases_[atlasIndex];

        int newWidth = atlas.width_ * 2;
        int newHeight = atlas.height_ * 2;

        if (newWidth > maxAtlasSize_ || newHeight > maxAtlasSize_) {
            return false;
        }

        logVerbose() << "FontAtlasManager: expanding atlas " << atlasIndex
                       << " from " << atlas.width_ << "x" << atlas.height_
                       << " to " << newWidth << "x" << newHeight;

        // Create new buffer
        std::vector<uint8_t> newPixels((size_t)newWidth * newHeight, 0);

        // Copy old data
        for (int y = 0; y < atlas.height_; y++) {
            memcpy(newPixels.data() + (size_t)y * newWidth,
                   atlas.pixels_.data() + (size_t)y * atlas.width_,
                   atlas.width_);
        }

        // Update UV coordinates (only for glyphs in this atlas)
        float scaleX = (float)atlas.width_ / newWidth;
        float scaleY = (float)atlas.height_ / newHeight;
        for (auto& pair : glyphs_) {
            GlyphInfo& g = pair.second;
            if (g.atlasIndex_ == atlasIndex) {
                g.u0_ *= scaleX;
                g.u1_ *= scaleX;
                g.v0_ *= scaleY;
                g.v1_ *= scaleY;
            }
        }

        atlas.pixels_ = std::move(newPixels);
        atlas.width_ = newWidth;
        atlas.height_ = newHeight;

        // Start filling from top-right corner of new space
        // Old content is in top-left quadrant (newWidth/2 x newHeight/2)
        atlas.currentX_ = newWidth / 2 + GLYPH_PADDING;
        atlas.currentY_ = GLYPH_PADDING;
        atlas.rowHeight_ = 0;

        // Defer GPU resource destruction (old view may still be in sgl command queue)
        if (atlas.textureValid_) {
            pendingDestroys_.push_back({atlas.view_, atlas.texture_});
            atlas.view_ = {};
            atlas.texture_ = {};
            atlas.textureValid_ = false;
        }
        atlas.textureDirty_ = true;

        return true;
    }

    bool addGlyphToAtlas(uint32_t codepoint, GlyphInfo& outInfo) {
        // Render glyph
        int glyphIndex = findGlyphIndex(codepoint);

        // Get glyph metrics
        int advanceWidth, leftSideBearing;
        stbtt_GetGlyphHMetrics(&fontInfo_, glyphIndex, &advanceWidth, &leftSideBearing);

        // Oversampling: rasterize at oversample_ times the target resolution and
        // box-prefilter back down, so the bilinear fetch at draw time has real
        // sub-pixel detail to interpolate instead of one hard-edged coverage
        // sample. Unlike snapping the quad this survives rotation and scale --
        // there is simply more information in the atlas, whatever the transform.
        // Layout below is in OVERSAMPLED texels; the metrics handed back to the
        // draw path are converted to final pixels at the end.
        //
        // `rs` is the raster scale (texels per final pixel) and `os` the
        // prefilter width. Both start at oversample_; for a glyph larger than
        // the largest page they are lowered below (rs below 1 if needed).
        int   os      = oversample_;
        float rs      = (float)os;
        float osScale = scale_ * rs;

        int x0, y0, x1, y1;
        stbtt_GetGlyphBitmapBox(&fontInfo_, glyphIndex, osScale, osScale, &x0, &y0, &x1, &y1);

        // Zero-width glyphs (like space). Tested on the raw box, before the
        // prefilter margin below -- that margin is nonzero for os > 1 and would
        // make an empty glyph look like it had area.
        if ((x1 - x0) <= 0 || (y1 - y0) <= 0) {
            outInfo.atlasIndex_ = 0;
            outInfo.u0_ = outInfo.v0_ = outInfo.u1_ = outInfo.v1_ = 0;
            outInfo.xoff_ = 0;
            outInfo.yoff_ = 0;
            outInfo.width_ = 0;
            outInfo.height_ = 0;
            outInfo.advance_ = advanceWidth * scale_;
            outInfo.valid_ = true;
            return true;
        }

        // The prefilter is a box of width `os`, which needs os-1 texels of extra
        // room to run out into (stb's own packer reserves exactly the same).
        int glyphWidth  = (x1 - x0) + (os - 1);
        int glyphHeight = (y1 - y0) + (os - 1);

        int paddedWidth = glyphWidth + GLYPH_PADDING;
        int paddedHeight = glyphHeight + GLYPH_PADDING;

        // Checked before any page is created or grown: a glyph whose box does
        // not fit the largest page is rasterized at a lower resolution that
        // fits, and drawn scaled up to its size in final pixels. Integer scales
        // keep the box prefilter; below 1 the glyph is rasterized directly.
        for (int attempt = 0; attempt < 32 && !fitsLargestPage(paddedWidth, paddedHeight); ++attempt) {
            const float fitW = (float)(maxAtlasSize_ / 2 - 2 * GLYPH_PADDING) / (float)glyphWidth;
            const float fitH = (float)(maxAtlasSize_ - 2 * GLYPH_PADDING) / (float)glyphHeight;
            const float next = rs * std::min(fitW, fitH) * 0.99f;
            if (next >= 1.0f) {
                os = (int)next;
                rs = (float)os;
            } else {
                os = 1;
                rs = next;
            }
            if (!(rs > 0.0f)) break;
            osScale = scale_ * rs;
            stbtt_GetGlyphBitmapBox(&fontInfo_, glyphIndex, osScale, osScale, &x0, &y0, &x1, &y1);
            glyphWidth  = std::max(x1 - x0, 1) + (os - 1);
            glyphHeight = std::max(y1 - y0, 1) + (os - 1);
            paddedWidth = glyphWidth + GLYPH_PADDING;
            paddedHeight = glyphHeight + GLYPH_PADDING;
        }
        if (!fitsLargestPage(paddedWidth, paddedHeight)) {
            logWarning("Font") << "glyph U+" << std::hex << codepoint << std::dec
                               << " does not fit an atlas page of " << maxAtlasSize_
                               << "x" << maxAtlasSize_ << " texels; it is not drawn";
            outInfo.advance_ = advanceWidth * scale_;
            outInfo.valid_ = false;
            return false;
        }
        if (rs < (float)oversample_) {
            logVerbose("Font") << "glyph U+" << std::hex << codepoint << std::dec
                               << " rasterized at " << rs << " texels per pixel"
                               << " (oversampling " << oversample_ << ")";
        }

        // Find atlas that can fit glyph
        size_t targetAtlas = atlases_.size();
        for (size_t i = 0; i < atlases_.size(); i++) {
            if (tryFitGlyph(i, paddedWidth, paddedHeight)) {
                targetAtlas = i;
                break;
            }
        }

        // If no atlas can fit
        if (targetAtlas == atlases_.size()) {
            // Try expanding last atlas
            if (!atlases_.empty() && expandAtlas(atlases_.size() - 1)) {
                targetAtlas = atlases_.size() - 1;
            } else {
                // Create new atlas
                targetAtlas = createNewAtlas();
            }

            // Expand until it fits
            while (!tryFitGlyph(targetAtlas, paddedWidth, paddedHeight)) {
                if (!expandAtlas(targetAtlas)) {
                    logWarning() << "FontAtlasManager: cannot fit glyph for U+" << std::hex << codepoint << std::dec;
                    outInfo.valid_ = false;
                    return false;
                }
            }
        }

        AtlasState& atlas = atlases_[targetAtlas];

        // Move to next row if current row overflows
        if (atlas.currentX_ + paddedWidth > atlas.width_) {
            atlas.currentY_ += atlas.rowHeight_ + GLYPH_PADDING;
            atlas.rowHeight_ = 0;

            // After expand, old content occupies top-left quadrant (width/2 x height/2).
            // While in that vertical range, start rows from the right half.
            if (atlas.width_ > INITIAL_ATLAS_SIZE && atlas.currentY_ < atlas.height_ / 2) {
                atlas.currentX_ = atlas.width_ / 2 + GLYPH_PADDING;
            } else {
                atlas.currentX_ = GLYPH_PADDING;
            }
        }

        int destX = atlas.currentX_;
        int destY = atlas.currentY_;

        // Render glyph (8bit grayscale). Zero-filled because the prefilter runs
        // out past the rasterized box into the os-1 margin and expects to read
        // background there.
        std::vector<uint8_t> glyphBitmap((size_t)glyphWidth * glyphHeight, 0);

        // Box prefiltering shifts the image by (os-1)/2 oversampled texels;
        // stb reports the compensating offset in final pixels via sub*, which
        // has to be folded into the glyph origin below.
        //
        // That leaves the origin at a non-integer position -- 1/(2*os) of a
        // pixel, so a quarter pixel at os = 2 -- which means the atlas texel
        // grid never quite lands on the screen pixel grid, however carefully
        // grid fit places the baseline. Cancelling it (rasterize with the
        // opposite shift, snap the box outward to whole pixels, render into the
        // interior of a larger bitmap so the margin does not move the glyph)
        // was built and measured: with the phase pinned and stepped 0.0..0.9 it
        // was worth +0.5% mean concentration and roughly halved the spread
        // across phases. Not enough to justify the code, which went through
        // three separate sign/offset bugs on the way -- two of which no
        // sharpness metric could see, because a glyph rendered crisply in the
        // wrong place still scores as crisp. Left alone deliberately.
        float subX = 0.0f, subY = 0.0f;
        if (os > 1) {
            stbtt_MakeGlyphBitmapSubpixelPrefilter(&fontInfo_,
                                                   glyphBitmap.data(),
                                                   glyphWidth, glyphHeight,
                                                   glyphWidth,  // stride
                                                   osScale, osScale,
                                                   0.0f, 0.0f,  // no subpixel shift
                                                   os, os,
                                                   &subX, &subY,
                                                   glyphIndex);
        } else {
            stbtt_MakeGlyphBitmap(&fontInfo_,
                                  glyphBitmap.data(),
                                  glyphWidth, glyphHeight,
                                  glyphWidth,  // stride
                                  osScale, osScale,
                                  glyphIndex);
        }

        // Copy to atlas (R8 coverage)
        for (int y = 0; y < glyphHeight; y++) {
            memcpy(atlas.pixels_.data() + (size_t)(destY + y) * atlas.width_ + destX,
                   glyphBitmap.data() + (size_t)y * glyphWidth,
                   glyphWidth);
        }

        // Set glyph info
        outInfo.atlasIndex_ = targetAtlas;
        outInfo.u0_ = (float)destX / atlas.width_;
        outInfo.v0_ = (float)destY / atlas.height_;
        outInfo.u1_ = (float)(destX + glyphWidth) / atlas.width_;
        outInfo.v1_ = (float)(destY + glyphHeight) / atlas.height_;
        // UVs above address oversampled TEXELS; everything the draw path uses is
        // in FINAL pixels, so divide out the oversampling here. This is the only
        // place the two spaces meet -- emitPlacedGlyphsToAtlas needs no changes.
        const float inv = 1.0f / rs;
        outInfo.xoff_ = (float)x0 * inv + subX;
        outInfo.yoff_ = (float)y0 * inv + subY;
        outInfo.width_ = (float)glyphWidth * inv;
        outInfo.height_ = (float)glyphHeight * inv;
        outInfo.advance_ = advanceWidth * scale_;
        outInfo.valid_ = true;

        // Advance cursor
        atlas.currentX_ += paddedWidth;
        if (paddedHeight > atlas.rowHeight_) {
            atlas.rowHeight_ = paddedHeight;
        }

        atlas.textureDirty_ = true;
        return true;
    }

    bool tryFitGlyph(size_t atlasIndex, int width, int height) {
        const AtlasState& atlas = atlases_[atlasIndex];

        // Fits in current row?
        if (atlas.currentX_ + width <= atlas.width_) {
            if (atlas.currentY_ + height <= atlas.height_) {
                return true;
            }
        }

        // Fits in next row?
        int nextY = atlas.currentY_ + atlas.rowHeight_ + GLYPH_PADDING;
        if (nextY + height <= atlas.height_) {
            // Check if the glyph fits horizontally in the next row
            // (right-half rows are narrower)
            int nextX = GLYPH_PADDING;
            if (atlas.width_ > INITIAL_ATLAS_SIZE && nextY < atlas.height_ / 2) {
                nextX = atlas.width_ / 2 + GLYPH_PADDING;
            }
            if (nextX + width <= atlas.width_) {
                return true;
            }
        }

        return false;
    }

    // Destroy old GPU resources that are safe to release (previous frame's sgl
    // commands have already been consumed by _sgl_draw)
    void flushPendingDestroys() {
        uint64_t currentFrame = sapp_frame_count();
        if (lastDestroyFlushFrame_ == currentFrame) return;
        lastDestroyFlushFrame_ = currentFrame;

        for (auto& pd : pendingDestroys_) {
            sg_destroy_view(pd.view);
            sg_destroy_image(pd.image);
        }
        pendingDestroys_.clear();
    }

    void updateAtlasTexture(AtlasState& atlas) {
        // Re-upload whenever the atlas is dirty (only reached via
        // ensureTexturesUpdated, which already gates on textureDirty_). No
        // once-per-frame throttle: glyphs may be added across several draw calls
        // in one frame (e.g. text rendered into multiple FBOs at setup), and
        // each needs its new glyphs on the GPU before its draw. Deferred
        // destruction (pendingDestroys_, freed next frame after the commands
        // referencing the old view are consumed) keeps mid-frame recreate safe.

        // Defer destruction of existing resources
        if (atlas.textureValid_) {
            pendingDestroys_.push_back({atlas.view_, atlas.texture_});
            atlas.view_ = {};
            atlas.texture_ = {};
            atlas.textureValid_ = false;
        }

        // Create as immutable texture (with initial data)
        // NOTE: Not most efficient, but works for now
        sg_image_desc img_desc = {};
        img_desc.width = atlas.width_;
        img_desc.height = atlas.height_;
        img_desc.pixel_format = SG_PIXELFORMAT_R8;   // coverage, drawn by activeCoverage2D()
        // immutable (default) - can set initial data
        img_desc.data.mip_levels[0].ptr = atlas.pixels_.data();
        img_desc.data.mip_levels[0].size = atlas.pixels_.size();

        // Optional mip chain: without it, glyphs minified on screen (far/small
        // text, non-HiDPI displays) alias and shimmer under motion — MSAA can't
        // fix in-texture minification. sokol never auto-generates mipmaps, so we
        // build the chain on the CPU here. Texels hold coverage only (the colour
        // comes from the vertex colour in the coverage shader), so each mip
        // box-averages coverage; there is no colour channel to darken at glyph
        // edges, and minified glyphs stay clean. (GLYPH_PADDING=2 means very
        // coarse mips bleed slightly between neighbours, but that range is
        // sub-pixel on screen and far preferable to shimmer.)
        std::vector<std::vector<uint8_t>> lowerMips;   // levels 1..N (level 0 = atlas.pixels_)
        // Oversampling and mipmapping compose: oversampling owns 1:1 and above,
        // the mip chain owns minification -- which is exactly where a denser
        // atlas would otherwise make aliasing worse. pickSampler() keeps the
        // GPU on mip 0 while the modelview is not minifying, so the mips below
        // are only ever reached when they are the right answer. On an NxN
        // atlas the chain also lands better than on a 1x one: drawing at 1/N
        // scale reads mip log2(N), whose resolution matches the target exactly.
        if (wantMipmaps_ && mipsBuilt_) {
            int numMips = 1;
            for (int mw = atlas.width_, mh = atlas.height_; mw > 1 || mh > 1; ) {
                mw = std::max(1, mw / 2);
                mh = std::max(1, mh / 2);
                ++numMips;
            }
            lowerMips.reserve(numMips - 1);   // reserve so .data() pointers stay valid

            const uint8_t* prev = atlas.pixels_.data();
            int pw = atlas.width_, ph = atlas.height_;
            img_desc.num_mipmaps = numMips;
            for (int level = 1; level < numMips; ++level) {
                int cw = std::max(1, pw / 2), ch = std::max(1, ph / 2);
                std::vector<uint8_t> dst(static_cast<size_t>(cw) * ch);
                for (int y = 0; y < ch; ++y) {
                    for (int x = 0; x < cw; ++x) {
                        int x0 = x * 2, y0 = y * 2;
                        int x1 = std::min(x0 + 1, pw - 1), y1 = std::min(y0 + 1, ph - 1);
                        int a = (prev[y0 * pw + x0] + prev[y0 * pw + x1]
                               + prev[y1 * pw + x0] + prev[y1 * pw + x1] + 2) / 4;
                        dst[static_cast<size_t>(y) * cw + x] = static_cast<uint8_t>(a);
                    }
                }
                lowerMips.push_back(std::move(dst));
                img_desc.data.mip_levels[level].ptr  = lowerMips.back().data();
                img_desc.data.mip_levels[level].size = lowerMips.back().size();
                prev = lowerMips.back().data();
                pw = cw; ph = ch;
            }
        }
        atlas.texture_ = sg_make_image(&img_desc);

        sg_view_desc view_desc = {};
        view_desc.texture.image = atlas.texture_;
        atlas.view_ = sg_make_view(&view_desc);

        atlas.textureValid_ = true;
        atlas.textureDirty_ = false;
    }
};

// ---------------------------------------------------------------------------
// Shared font cache (singleton)
// ---------------------------------------------------------------------------
class SharedFontCache {
public:
    // One cache per process, defined in tcGlobal.cpp. It keeps every atlas it
    // holds until a Font reloads at another key. Header-inline, each hot reload
    // guest generation filled a cache of its own, and the atlases of every old
    // generation stayed resident, since a guest library is never unloaded (#249).
    static SharedFontCache& getInstance();

    // Get cached font (returns nullptr if not cached)
    std::shared_ptr<FontAtlasManager> get(const FontCacheKey& key) {
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            return it->second;
        }
        return nullptr;
    }

    // Get or create font atlas from file
    std::shared_ptr<FontAtlasManager> getOrCreate(const FontCacheKey& key) {
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            return it->second;
        }

        auto manager = std::make_shared<FontAtlasManager>();
        // Oversampling first, so the load-time page check uses it.
        manager->setOversample(key.oversample);
        manager->setMipmaps(key.mipmaps);
        if (!manager->setup(key.fontPath, key.fontSize)) {
            return nullptr;
        }

        cache_[key] = manager;
        return manager;
    }

    // Get or create font atlas from memory (for URL fetched fonts)
    std::shared_ptr<FontAtlasManager> getOrCreateFromMemory(
            const FontCacheKey& key, const uint8_t* data, size_t size) {
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            return it->second;
        }

        auto manager = std::make_shared<FontAtlasManager>();
        // Oversampling first, so the load-time page check uses it.
        manager->setOversample(key.oversample);
        manager->setMipmaps(key.mipmaps);
        if (!manager->setupFromMemory(data, size, key.fontSize)) {
            return nullptr;
        }

        cache_[key] = manager;
        return manager;
    }

    // Release specific font
    void release(const FontCacheKey& key) {
        cache_.erase(key);
    }

    // Drop a cached atlas, but only when this cache holds the last reference.
    // Called when a Font reloads at a different key: the cache owns a strong
    // ref and nothing else ever evicts, so without this every size a Font
    // visits stays resident for the process lifetime (a size slider walking
    // 16..40 leaves 25 atlases behind). use_count() == 1 means "only the cache
    // is holding it", so a Font still using this atlas is never freed from
    // under it.
    void releaseIfUnused(const FontCacheKey& key) {
        auto it = cache_.find(key);
        if (it != cache_.end() && it->second.use_count() == 1) {
            cache_.erase(it);
        }
    }

    // Release all
    void clear() {
        cache_.clear();
    }

    // Total memory usage
    size_t getTotalMemoryUsage() const {
        size_t total = 0;
        for (const auto& pair : cache_) {
            total += pair.second->getMemoryUsage();
        }
        return total;
    }

private:
    SharedFontCache() = default;
    std::unordered_map<FontCacheKey, std::shared_ptr<FontAtlasManager>, FontCacheKeyHash> cache_;
};

// The samplers every Font draws with (see Font::initResources). One pair per
// process, defined in tcGlobal.cpp, like the cache above: nothing destroys
// them, so a copy per module made a new pair for each hot reload generation.
struct FontSamplers {
    sg_sampler sharp = {};    // max_lod 0 (1:1 and above)
    sg_sampler mipped = {};   // full chain (minified)
    bool initialized = false;
};
FontSamplers& fontSamplers();

} // namespace internal

// ---------------------------------------------------------------------------
// TrueType font class (user-facing)
// Inheritable: Override to implement custom font system
// ---------------------------------------------------------------------------
class Font {
public:
    Font() = default;
    virtual ~Font() = default;

    // Copyable/movable (lightweight, uses shared_ptr)
    Font(const Font&) = default;
    Font& operator=(const Font&) = default;
    Font(Font&&) = default;
    Font& operator=(Font&&) = default;

    // -------------------------------------------------------------------------
    // Load font
    // -------------------------------------------------------------------------
    // Load a font. `nameOrPath` can be one of:
    //   - A URL (Emscripten only, async load)
    //   - A filesystem path
    //   - A system font name (PostScript or family name) — resolved via
    //     tc::systemFontPath when the path doesn't exist on disk. Lets users
    //     write `font.load("HiraginoSans-W3", 24)` cross-platform.
    // -------------------------------------------------------------------------
    // Oversampling (supersampled glyph rasterization)
    // -------------------------------------------------------------------------
    // Rasterize each glyph N times finer than the target size and box-prefilter
    // it back down, so the bilinear fetch at draw time interpolates real detail.
    // Costs N^2 atlas memory. Unlike rounding glyph positions onto the pixel
    // grid, this keeps working under rotation and scale -- the atlas simply
    // holds more information, whatever the transform does with it.
    //
    // Order-independent by design: calling it after load() re-resolves the
    // atlas rather than silently doing nothing.
    static void setDefaultOversampling(int n) {
        defaultOversample_ = clampOversample(n);
    }
    static int getDefaultOversampling() { return defaultOversample_; }

    Font& setOversampling(int n) {
        n = clampOversample(n);
        if (n == oversample_) return *this;      // nothing to rebuild
        oversample_ = n;
        reresolveAtlas([n](internal::FontCacheKey& k) { k.oversample = n; },
                       "setOversampling");
        return *this;
    }
    int getOversampling() const { return oversample_; }

    // Round each baseline to a whole pixel at draw time. With the default
    // Direction::Top the baseline lands at y + ascent, and a fractional ascent
    // drags every glyph bitmap on the line off the pixel grid -- the horizontal
    // strokes then smear no matter how good the atlas is. Snapping the baseline
    // is what lets the atlas's own alignment reach the screen.
    //
    // Costs nothing: no extra memory, no reload, one round() per line. It also
    // cannot misfire under a transform, because it stands down when the
    // transform is not 1:1 (see fitY).
    //
    // The trade is that a line sits up to half a pixel off the baseline the
    // font declares. Nothing else moves: glyph bitmaps, advances, line widths
    // and the reported metrics are all identical to an unfitted font, so the
    // text occupies the same box and drifts no further than that half pixel no
    // matter how many lines it runs to.
    Font& setGridFit(bool enabled) {
        gridFit_ = enabled;
        return *this;
    }
    bool getGridFit() const { return gridFit_; }

    // Allow a mip chain for this atlas. On by default and built lazily: nothing
    // is generated until a draw actually samples below the bilinear-safe rate,
    // so apps that never minify text pay nothing. Turn it off to trade shimmer
    // on small/distant text for ~33% less atlas memory and no rebuild cost.
    Font& setMipmaps(bool enabled) {
        if (enabled == mipmaps_) return *this;
        mipmaps_ = enabled;
        reresolveAtlas([enabled](internal::FontCacheKey& k) { k.mipmaps = enabled; },
                       "setMipmaps");
        return *this;
    }
    bool getMipmaps() const { return mipmaps_; }

private:
    // Apply an atlas-option change to the cache key and swap in the atlas it
    // now names. Called from the option setters so they are order-independent:
    // a setter that only takes effect when it precedes load() is the kind of
    // trap that leaves a feature quietly doing nothing.
    template <class ApplyToKey>
    void reresolveAtlas(ApplyToKey apply, const char* who) {
        if (!atlasManager_) {                    // picked up by the next load()
            apply(cacheKey_);
            return;
        }
        if (isUrl(cacheKey_.fontPath)) {
            // The bytes live in the async-fetch cache entry, not on disk, so we
            // cannot re-rasterize from here. Record it for the next load().
            logWarning("Font") << who << " on a URL-loaded font takes effect on "
                                         "the next load()";
            apply(cacheKey_);
            return;
        }
        const internal::FontCacheKey previousKey = cacheKey_;
        apply(cacheKey_);
        atlasManager_ = internal::SharedFontCache::getInstance().getOrCreate(cacheKey_);
        internal::SharedFontCache::getInstance().releaseIfUnused(previousKey);
    }

public:
    LoadResult load(const fs::path& nameOrPath, int size) {
        // Render glyphs at physical pixel size for sharp text on HiDPI displays.
        // All metrics/drawing are scaled back to logical coordinates.
        dpiScale_ = sapp_dpi_scale();
        int physicalSize = (int)(size * dpiScale_ + 0.5f);
        logicalSize_ = size;

        // Create the shared samplers if not yet
        initResources();

        // Resolve input to a concrete path (file / URL). A font NAME
        // ("HiraginoSans-W3") is a valid relative fs::path, so both spellings
        // arrive here; the UTF-8 string form is what cache keys and the
        // system-font lookup use. A relative file path resolves via
        // getDataPath, like Image::load; when a file in the data folder and a
        // system font share a name, the data file wins.
        std::string nameStr = internal::pathToUtf8(nameOrPath);
        std::string actualPath = nameStr;
        if (!isUrl(nameStr)) {
            const fs::path filePath = getDataPath(nameOrPath);   // absolute paths pass through
            actualPath = internal::pathToUtf8(filePath);
            std::ifstream test(filePath, std::ios::binary);
            if (!test.good()) {
                // Not a usable file path — try as a system font name.
                fs::path resolved = systemFontPath(nameStr);
                if (!resolved.empty()) {
                    actualPath = internal::pathToUtf8(resolved);
                    logNotice("Font") << "Resolved \"" << nameStr
                                      << "\" → " << actualPath;
                }
                // If resolution failed, keep the data-folder path: the load
                // error then names the input and where it was looked for.
            }
        }

        // What this Font was holding before, so a reload at a different key can
        // hand the previous atlas back to the cache. Note this is deliberately
        // NOT done in ~Font(): a Font constructed and destroyed every frame
        // would then re-rasterize its whole atlas every frame, turning a merely
        // wasteful pattern into a stall. Keeping unreferenced atlases cached is
        // what a cache is for; only a *reload* has a known-dead predecessor.
        const internal::FontCacheKey previousKey = cacheKey_;
        const bool hadAtlas = (atlasManager_ != nullptr);

        cacheKey_.fontPath = actualPath;
        cacheKey_.fontSize = physicalSize;
        cacheKey_.oversample = oversample_;
        cacheKey_.mipmaps = mipmaps_;

        if (isUrl(actualPath)) {
#ifdef __EMSCRIPTEN__
            // Async load - returns immediately, font available after fetch completes
            loadFromUrlAsync(actualPath, physicalSize);
            return LoadResult::success();  // Will be loaded asynchronously
#else
            logError() << "Font: URL loading only supported in WebAssembly";
            return LoadResult::fail(LoadError::UnsupportedFormat,
                                    "URL loading only supported in WebAssembly: " + actualPath);
#endif
        } else {
            atlasManager_ = internal::SharedFontCache::getInstance().getOrCreate(cacheKey_);
        }

        // The assignment above dropped this Font's reference to its previous
        // atlas. If nothing else holds it, let the cache go too.
        if (hadAtlas && !(previousKey == cacheKey_)) {
            internal::SharedFontCache::getInstance().releaseIfUnused(previousKey);
        }

        if (!atlasManager_) {
            // Classify: the resolved path doesn't exist on disk -> the font
            // was never found (bad path or unresolvable system-font name);
            // otherwise the file opened but stb_truetype rejected it.
            std::error_code ec;
            if (!fs::exists(internal::utf8ToPath(actualPath), ec)) {
                std::string msg = "font not found: \"" + nameStr + "\"";
                if (actualPath != nameStr) msg += " (resolved to \"" + actualPath + "\")";
                return LoadResult::fail(LoadError::FileNotFound, msg);
            }
            return LoadResult::fail(LoadError::DecodeFailed,
                                    "failed to init font: " + actualPath);
        }
        return LoadResult::success();
    }

private:
    static bool isUrl(const std::string& path) {
        return path.find("http://") == 0 || path.find("https://") == 0;
    }

#ifdef __EMSCRIPTEN__
    // Context for async font loading
    struct FontLoadContext {
        Font* font;
        internal::FontCacheKey key;
    };

    static void onFetchSuccess(emscripten_fetch_t* fetch) {
        FontLoadContext* ctx = reinterpret_cast<FontLoadContext*>(fetch->userData);

        // A response larger than SIZE_MAX (size_t is 32-bit on wasm32) is
        // refused. setupFromMemory() rejects an empty response with a warning.
        if (fetch->numBytes > (uint64_t)SIZE_MAX) {
            logWarning() << "Font: response too large for " << ctx->key.fontPath;
            delete ctx;
            emscripten_fetch_close(fetch);
            return;
        }

        ctx->font->atlasManager_ = internal::SharedFontCache::getInstance().getOrCreateFromMemory(
            ctx->key,
            reinterpret_cast<const uint8_t*>(fetch->data),
            (size_t)fetch->numBytes
        );

        if (ctx->font->atlasManager_) {
            logNotice("Font") << "Loaded from URL: " << ctx->key.fontPath;
        }

        delete ctx;
        emscripten_fetch_close(fetch);
    }

    static void onFetchError(emscripten_fetch_t* fetch) {
        FontLoadContext* ctx = reinterpret_cast<FontLoadContext*>(fetch->userData);
        logError() << "Font: failed to fetch " << ctx->key.fontPath
                     << " (status: " << fetch->status << ")";
        delete ctx;
        emscripten_fetch_close(fetch);
    }

    void loadFromUrlAsync(const std::string& url, int size) {
        // Check cache first (don't try to load from file)
        internal::FontCacheKey key{url, size};
        auto cached = internal::SharedFontCache::getInstance().get(key);
        if (cached) {
            atlasManager_ = cached;
            return;
        }

        // Start async fetch
        FontLoadContext* ctx = new FontLoadContext{this, key};

        emscripten_fetch_attr_t attr;
        emscripten_fetch_attr_init(&attr);
        std::strcpy(attr.requestMethod, "GET");
        attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
        attr.onsuccess = onFetchSuccess;
        attr.onerror = onFetchError;
        attr.userData = ctx;

        emscripten_fetch(&attr, url.c_str());
    }
#endif

public:

    bool isLoaded() const { return atlasManager_ != nullptr; }

    // -------------------------------------------------------------------------
    // Alignment settings
    // -------------------------------------------------------------------------
    void setAlign(Direction h, Direction v) {
        alignH_ = h;
        alignV_ = v;
    }

    void setAlign(Direction h) {
        alignH_ = h;
    }

    Direction getAlignH() const { return alignH_; }
    Direction getAlignV() const { return alignV_; }

    // -------------------------------------------------------------------------
    // Line height settings (spacing for newlines)
    // -------------------------------------------------------------------------
    void setLineHeight(float pixels) {
        lineHeight_ = pixels;
    }

    // Set in em units (1.0 = font's default line height, 1.5 = 1.5x)
    void setLineHeightEm(float multiplier) {
        lineHeight_ = getDefaultLineHeight() * multiplier;
    }

    void resetLineHeight() {
        lineHeight_ = 0;  // 0 = use font's default line height
    }

    // -------------------------------------------------------------------------
    // Draw string (virtual - customizable in subclass)
    // Uses alignment set by global setTextAlign()
    // -------------------------------------------------------------------------
    virtual void drawString(const std::string& text, float x, float y) const {
        Direction h = getDefaultContext().getTextAlignH();
        Direction v = getDefaultContext().getTextAlignV();
        const std::string wrapped = wrapTextIfEnabled(text);
        if (writingMode_ == WritingMode::VerticalRL) {
            drawStringVerticalInternal(wrapped, x, y, h, v);
        } else {
            drawStringInternal(wrapped, x, y, h, v);
        }
    }

    virtual void drawString(const std::string& text, float x, float y,
                            Direction h, Direction v) const {
        const std::string wrapped = wrapTextIfEnabled(text);
        if (writingMode_ == WritingMode::VerticalRL) {
            drawStringVerticalInternal(wrapped, x, y, h, v);
        } else {
            drawStringInternal(wrapped, x, y, h, v);
        }
    }

    // -------------------------------------------------------------------------
    // Vector glyph outlines (rotation / scaling / animation use cases).
    //
    // getGlyphPath returns a single Path with one subpath per contour
    // (em-normalized, 1.0 = em, screen Y-down, baseline at y=0, pen at x=0).
    // Glyphs with holes like 'O' / '日' / 'あ' have multiple subpaths and
    // render correctly with `path.drawFill()`.
    //
    // getStringPath returns a single Path containing every glyph's contours
    // positioned with the same layout pipeline as drawString — writing mode,
    // alignment, wrap, kinsoku, TCY all apply. Coordinates are in logical
    // pixels relative to (x, y).
    // -------------------------------------------------------------------------
    Path getGlyphPath(uint32_t codepoint) const {
        if (!atlasManager_) return {};
        return atlasManager_->getGlyphPath(codepoint);
    }

    Path getStringPath(const std::string& text, float x, float y,
                       Direction h, Direction v) const {
        Path result;
        if (!atlasManager_ || text.empty()) return result;

        const float em = (float)logicalSize_;

        forEachGlyph(text, x, y, h, v, [&](const PlacedGlyph& pg) {
            Path glyph = atlasManager_->getGlyphPath(pg.codepoint);
            if (glyph.empty()) return;

            const float c  = std::cos(pg.rotationCw);
            const float si = std::sin(pg.rotationCw);
            const bool  rotated = (pg.rotationCw != 0.f);

            // Transform the glyph's vertices in place and append each subpath
            // to result as its own subpath (moveTo at every contour start so
            // glyph boundaries and holes survive the merge).
            const size_t nSub = glyph.getNumSubpaths();
            for (size_t si2 = 0; si2 < nSub; ++si2) {
                auto [gs, ge] = glyph.getSubpathRange(si2);
                if (ge - gs == 0) continue;
                bool first = true;
                for (size_t k = gs; k < ge; ++k) {
                    Vec3& vert = glyph.getVertices()[k];
                    float fx = vert.x * em * pg.scaleX + pg.drawX;
                    float fy = vert.y * em            + pg.baselineY;
                    if (rotated) {
                        const float dx = fx - pg.pivotX;
                        const float dy = fy - pg.pivotY;
                        fx = pg.pivotX + c * dx - si * dy;
                        fy = pg.pivotY + si * dx + c * dy;
                    }
                    if (first) { result.moveTo(fx, fy); first = false; }
                    else       { result.lineTo(fx, fy); }
                }
                if (glyph.isSubpathClosed(si2)) result.close();
            }
        });

        return result;
    }

    Path getStringPath(const std::string& text, float x, float y) const {
        Direction h = getDefaultContext().getTextAlignH();
        Direction v = getDefaultContext().getTextAlignV();
        return getStringPath(text, x, y, h, v);
    }

    // -------------------------------------------------------------------------
    // Writing mode (default: Horizontal — existing behavior unchanged)
    // -------------------------------------------------------------------------
    void setWritingMode(WritingMode mode) { writingMode_ = mode; }
    WritingMode getWritingMode() const { return writingMode_; }

    // Tate-chu-yoko (縦中横) for runs of consecutive ASCII digits.
    //   maxDigits == 0   → disabled, every run uses overflowMode
    //   N digits ≤ max   → inMode (e.g. Combine to squeeze digits into one cell)
    //   N digits  > max  → overflowMode (typically Rotate)
    void setTcyDigits(int maxDigits, TcyMode inMode, TcyMode overflowMode) {
        tcyDigitMax_ = maxDigits;
        tcyDigitInMode_ = inMode;
        tcyDigitOverflowMode_ = overflowMode;
    }
    // Tate-chu-yoko for runs of Latin letters (single mode, default Rotate).
    void setTcyLatin(TcyMode mode) { tcyLatinMode_ = mode; }

    TcyMode getTcyLatinMode() const { return tcyLatinMode_; }
    int     getTcyDigitMax() const { return tcyDigitMax_; }

    // -------------------------------------------------------------------------
    // Line wrapping (default: off — existing single-line behavior unchanged).
    // Both writing modes use the same API:
    //   horizontal → maxLineLength is the line width before wrapping
    //   vertical   → maxLineLength is the column height before column-break
    // -------------------------------------------------------------------------
    void  enableWrap(bool enabled)       { wrapEnabled_ = enabled; }
    bool  isWrapEnabled() const          { return wrapEnabled_; }
    void  setMaxLineLength(float length) { maxLineLength_ = length; }
    float getMaxLineLength() const       { return maxLineLength_; }

    // When wrapping a Latin run with no break opportunity inside, insert '-'
    // before the forced break instead of cutting silently. Default: off.
    void setLatinHyphenation(bool enabled) { latinHyphenation_ = enabled; }
    bool getLatinHyphenation() const       { return latinHyphenation_; }

    // CJK kinsoku: when a 行頭禁則 character (、。」』）etc.) would otherwise
    // start a new line, let it hang past the edge of the current line instead.
    // Default: off (plain greedy wrap).
    void setHangingPunctuation(bool enabled) { hangingPunctuation_ = enabled; }
    bool getHangingPunctuation() const       { return hangingPunctuation_; }

    // Which subset of the CJK kinsoku tables to consult during wrap.
    //   Off (default)    — every CJK boundary is breakable, no kinsoku at all
    //   PunctuationOnly  — only 、。, . : ; ? ! ， ． ： ； ？ ！ … ‥ block line-start
    //   Standard         — full table (closing brackets + small kana + sound
    //                       marks + iteration marks + punctuation)
    // Affects both break-opportunity decisions and the hanging-punctuation
    // overflow path (see setHangingPunctuation).
    void         setKinsoku(KinsokuLevel level) { kinsokuLevel_ = level; }
    KinsokuLevel getKinsoku() const             { return kinsokuLevel_; }

    // -------------------------------------------------------------------------
    // Layout abstraction: PlacedGlyph + forEachGlyph*
    //
    // forEachGlyph computes glyph positions for the current writingMode/wrap/
    // kinsoku/TCY settings and invokes visitor once per glyph. The visitor is
    // backend-agnostic — atlas-quad emit, vector Path emit, hit testing, etc.
    // share the same layout pass.
    //
    // PlacedGlyph fields:
    //   codepoint  — final codepoint after vertical-form mapping (e.g. FE10-FE4F)
    //   drawX/Y    — pen position; glyph xoff/yoff are added on top
    //   rotationCw — 0 (upright) or TAU/4 (90° CW) in radians
    //   pivotX/Y   — rotation center (used only when rotationCw != 0)
    //   scaleX     — horizontal scale (1.0 normally, <1 for TCY Combine)
    // -------------------------------------------------------------------------
public:
    struct PlacedGlyph {
        uint32_t codepoint;
        float    drawX;
        float    baselineY;
        float    rotationCw;
        float    pivotX;
        float    pivotY;
        float    scaleX;
    };
    using GlyphVisitor = std::function<void(const PlacedGlyph&)>;

    // Dispatches to horizontal/vertical layout based on writingMode_.
    // Applies wrap preprocessing internally.
    void forEachGlyph(const std::string& text, float x, float y,
                      Direction h, Direction v,
                      const GlyphVisitor& visitor) const {
        const std::string wrapped = wrapTextIfEnabled(text);
        if (writingMode_ == WritingMode::VerticalRL) {
            forEachGlyphVertical(wrapped, x, y, h, v, visitor);
        } else {
            forEachGlyphHorizontal(wrapped, x, y, h, v, visitor);
        }
    }

    // Convenience overload using current alignment settings.
    void forEachGlyph(const std::string& text, float x, float y,
                      const GlyphVisitor& visitor) const {
        Direction h = getDefaultContext().getTextAlignH();
        Direction v = getDefaultContext().getTextAlignV();
        forEachGlyph(text, x, y, h, v, visitor);
    }

    // -------------------------------------------------------------------------
    // Internal drawing implementation
    // -------------------------------------------------------------------------
protected:
    // Layout pass for horizontal writing mode. Text is assumed pre-wrapped.
    void forEachGlyphHorizontal(const std::string& text, float x, float y,
                                Direction h, Direction v,
                                const GlyphVisitor& visitor) const {
        if (!atlasManager_ || text.empty()) return;

        // Preload required glyphs (atlas-side); cheap if already cached.
        for (size_t i = 0; i < text.size(); ) {
            uint32_t cp = decodeUTF8(text, i);
            if (cp != '\n' && cp != '\t') {
                atlasManager_->getOrLoadGlyph(cp);
            }
        }

        const float s = 1.0f / dpiScale_;

        // Pre-compute per-line widths for horizontal alignment.
        std::vector<float> lineWidths;
        {
            float w = 0;
            for (size_t i = 0; i < text.size(); ) {
                uint32_t cp = decodeUTF8(text, i);
                if (cp == '\n') {
                    lineWidths.push_back(w);
                    w = 0;
                } else if (cp == '\t') {
                    w += atlasManager_->getSpaceAdvance() * s * 4;
                } else {
                    const internal::GlyphInfo* g = atlasManager_->getOrLoadGlyph(cp);
                    if (g && g->isValid()) w += g->getAdvance() * s;
                }
            }
            lineWidths.push_back(w);
        }

        auto lineOffsetX = [&](int lineIdx) -> float {
            float lw = (lineIdx < (int)lineWidths.size()) ? lineWidths[lineIdx] : 0;
            switch (h) {
                case Direction::Center: return -lw / 2;
                case Direction::Right:  return -lw;
                default: return 0;
            }
        };

        float offsetY = 0;
        const float lineH = getLineHeight();
        float totalTextH = lineH * lineWidths.size();
        float ascent = getAscent();
        switch (v) {
            case Direction::Top:      offsetY = 0; break;
            case Direction::Baseline: offsetY = -ascent; break;
            case Direction::Center:   offsetY = -totalTextH / 2; break;
            case Direction::Bottom:   offsetY = -totalTextH; break;
            default: break;
        }

        // Baseline of line n. Accumulated from the anchor, then snapped once --
        // see fitY() for why stepping by a snapped line height is wrong.
        auto baselineOf = [&](int lineIdx) -> float {
            return y + fitY(offsetY + ascent + lineH * (float)lineIdx);
        };

        int currentLine = 0;
        float cursorX = x + lineOffsetX(0);
        float cursorY = baselineOf(0);

        for (size_t i = 0; i < text.size(); ) {
            uint32_t cp = decodeUTF8(text, i);
            if (cp == '\n') {
                currentLine++;
                cursorX = x + lineOffsetX(currentLine);
                cursorY = baselineOf(currentLine);
                continue;
            }
            if (cp == '\t') {
                cursorX += atlasManager_->getSpaceAdvance() * s * 4;
                continue;
            }
            const internal::GlyphInfo* g = atlasManager_->getOrLoadGlyph(cp);
            if (!g || !g->isValid()) continue;

            visitor(PlacedGlyph{cp, cursorX, cursorY, 0.f, 0.f, 0.f, 1.f});

            cursorX += g->getAdvance() * s;
        }
    }

    // Unified atlas-quad emit for both horizontal and vertical layouts.
    // Consumes PlacedGlyph stream produced by forEachGlyph*.
    void emitPlacedGlyphsToAtlas(const std::vector<PlacedGlyph>& placed) const {
        if (!atlasManager_ || placed.empty()) return;

        atlasManager_->ensureTexturesUpdated();

        const float s = 1.0f / dpiScale_;
        const size_t atlasCount = atlasManager_->getAtlasCount();

        for (size_t atlasIdx = 0; atlasIdx < atlasCount; ++atlasIdx) {
            const internal::AtlasState& atlas = atlasManager_->getAtlas(atlasIdx);
            if (!atlas.isTextureValid()) continue;

            // Coverage pipeline for the active target (swapchain or FBO): the
            // Alpha blend of activeFill2D() (dst alpha accumulates) with a shader
            // that reads the R8 atlas's R channel as alpha.
            internal::loadPipeline(internal::activeCoverage2D());
            sgl_enable_texture();
            sgl_texture(atlas.getView(), pickSampler());

            Color col = getColor();
            sgl_c4f(col.r, col.g, col.b, col.a);

            sgl_begin_quads();

            for (const PlacedGlyph& pg : placed) {
                const internal::GlyphInfo* g = atlasManager_->getOrLoadGlyph(pg.codepoint);
                if (!g || !g->isValid() || g->getAtlasIndex() != atlasIdx) continue;
                if (g->getWidth() <= 0 || g->getHeight() <= 0) continue;

                float gx = pg.drawX + g->getXoff() * s * pg.scaleX;
                float gy = pg.baselineY + g->getYoff() * s;
                float gw = g->getWidth() * s * pg.scaleX;
                float gh = g->getHeight() * s;

                if (pg.rotationCw == 0.f) {
                    sgl_v2f_t2f(gx,      gy,      g->getU0(), g->getV0());
                    sgl_v2f_t2f(gx + gw, gy,      g->getU1(), g->getV0());
                    sgl_v2f_t2f(gx + gw, gy + gh, g->getU1(), g->getV1());
                    sgl_v2f_t2f(gx,      gy + gh, g->getU0(), g->getV1());
                } else {
                    // Rotate around pivot. Screen Y-down: positive rotationCw
                    // rotates clockwise visually. Specialized for θ=π/2 this
                    // is (px,py) → (cx-(py-cy), cy+(px-cx)) — matches the
                    // original vertical-text emitRotated path.
                    const float c = std::cos(pg.rotationCw);
                    const float si = std::sin(pg.rotationCw);
                    auto rot = [&](float px, float py, float& ox, float& oy) {
                        float dx = px - pg.pivotX;
                        float dy = py - pg.pivotY;
                        ox = pg.pivotX + c * dx - si * dy;
                        oy = pg.pivotY + si * dx + c * dy;
                    };
                    float v0x, v0y, v1x, v1y, v2x, v2y, v3x, v3y;
                    rot(gx,      gy,      v0x, v0y);
                    rot(gx + gw, gy,      v1x, v1y);
                    rot(gx + gw, gy + gh, v2x, v2y);
                    rot(gx,      gy + gh, v3x, v3y);
                    sgl_v2f_t2f(v0x, v0y, g->getU0(), g->getV0());
                    sgl_v2f_t2f(v1x, v1y, g->getU1(), g->getV0());
                    sgl_v2f_t2f(v2x, v2y, g->getU1(), g->getV1());
                    sgl_v2f_t2f(v3x, v3y, g->getU0(), g->getV1());
                }
            }

            sgl_end();
            sgl_disable_texture();
            internal::restoreCurrentPipeline();
        }
    }

    void drawStringInternal(const std::string& text, float x, float y,
                            Direction h, Direction v) const {
        if (!atlasManager_ || text.empty()) return;

        std::vector<PlacedGlyph> placed;
        placed.reserve(text.size());
        forEachGlyphHorizontal(text, x, y, h, v,
            [&](const PlacedGlyph& pg) { placed.push_back(pg); });

        emitPlacedGlyphsToAtlas(placed);
    }

    // -------------------------------------------------------------------------
    // Vertical text drawing (writingMode_ == VerticalRL).
    //
    // Coordinate convention with default alignment (Right, Top):
    //   (x, y) = right-top corner of the first column.
    // Columns flow right→left (Japanese books). Newlines start a new column.
    // ---------------------------------------------------------------------
    enum class VTokKind_ { Single, LatinRun, DigitRun, Newline };
    struct VTok_ {
        VTokKind_ kind;
        std::vector<uint32_t> cps;  // for LatinRun / DigitRun
        uint32_t cp = 0;            // for Single
    };

    // Layout pass for vertical writing mode. Text is assumed pre-wrapped.
    void forEachGlyphVertical(const std::string& text, float x, float y,
                              Direction h, Direction v,
                              const GlyphVisitor& visitor) const {
        if (!atlasManager_ || text.empty()) return;

        const float s   = 1.0f / dpiScale_;
        const float em  = (float)logicalSize_;
        const float asc = getAscent();
        const float colSpacing = getLineHeight();
        const float cellH = em;  // CJK vertical advance per cell

        // -------- Tokenize --------
        std::vector<VTok_> toks;
        toks.reserve(text.size());
        for (size_t i = 0; i < text.size(); ) {
            uint32_t cp = decodeUTF8(text, i);
            if (cp == '\n') { toks.push_back({VTokKind_::Newline, {}, 0}); continue; }
            if (cp == '\t') continue;  // ignored in vertical for now
            if (internal::isAsciiDigit(cp)) {
                if (toks.empty() || toks.back().kind != VTokKind_::DigitRun)
                    toks.push_back({VTokKind_::DigitRun, {}, 0});
                toks.back().cps.push_back(cp);
            } else if (internal::isAsciiLetter(cp)) {
                if (toks.empty() || toks.back().kind != VTokKind_::LatinRun)
                    toks.push_back({VTokKind_::LatinRun, {}, 0});
                toks.back().cps.push_back(cp);
            } else {
                toks.push_back({VTokKind_::Single, {}, cp});
            }
        }

        // -------- Preload glyphs (incl. vertical-form variants) --------
        for (auto& t : toks) {
            if (t.kind == VTokKind_::Single) {
                atlasManager_->getOrLoadGlyph(t.cp);
                uint32_t vcp = internal::getVerticalCodepoint(t.cp);
                if (vcp != 0 && atlasManager_->fontHasGlyph(vcp)) {
                    atlasManager_->getOrLoadGlyph(vcp);
                }
            } else if (t.kind == VTokKind_::LatinRun || t.kind == VTokKind_::DigitRun) {
                for (uint32_t cp : t.cps) atlasManager_->getOrLoadGlyph(cp);
            }
        }

        // -------- TCY mode selection per token --------
        auto pickTcy = [&](const VTok_& t) -> TcyMode {
            if (t.kind == VTokKind_::DigitRun) {
                return ((int)t.cps.size() <= tcyDigitMax_) ? tcyDigitInMode_
                                                           : tcyDigitOverflowMode_;
            }
            return tcyLatinMode_;
        };

        // Horizontal layout width of a run (sum of advances).
        auto runHorizWidth = [&](const VTok_& t) -> float {
            float w = 0;
            for (uint32_t cp : t.cps) {
                const internal::GlyphInfo* g = atlasManager_->getOrLoadGlyph(cp);
                if (g && g->isValid()) w += g->getAdvance() * s;
            }
            return w;
        };

        // Token vertical advance in its column.
        auto tokAdvance = [&](const VTok_& t) -> float {
            if (t.kind == VTokKind_::Single)   return cellH;
            if (t.kind == VTokKind_::Newline)  return 0;
            const TcyMode m = pickTcy(t);
            switch (m) {
                case TcyMode::Rotate:  return runHorizWidth(t);
                case TcyMode::Upright: return cellH * (float)t.cps.size();
                case TcyMode::Combine: return cellH;
            }
            return cellH;
        };

        // -------- Column-height pass (for vertical alignment) --------
        std::vector<float> colHeights;
        {
            float ch = 0;
            for (const auto& t : toks) {
                if (t.kind == VTokKind_::Newline) {
                    colHeights.push_back(ch);
                    ch = 0;
                } else {
                    ch += tokAdvance(t);
                }
            }
            colHeights.push_back(ch);
        }
        const size_t numCols = colHeights.size();

        // -------- Horizontal alignment of column block --------
        // Columns are placed right→left; column 0 has its right edge at colX0.
        // Total block horizontal extent ≈ numCols * colSpacing.
        const float totalW = (float)numCols * colSpacing;
        float colX0;  // right edge x of column 0
        switch (h) {
            case Direction::Right:  colX0 = x; break;
            case Direction::Center: colX0 = x + totalW / 2.f - colSpacing / 2.f; break;
            case Direction::Left:   colX0 = x + totalW - colSpacing; break;
            default:                colX0 = x; break;
        }

        auto colYStart = [&](size_t i) -> float {
            const float ch = (i < colHeights.size()) ? colHeights[i] : 0.f;
            switch (v) {
                case Direction::Top:      return y;
                case Direction::Center:   return y - ch / 2.f;
                case Direction::Bottom:   return y - ch;
                case Direction::Baseline: return y;  // treat like Top
                default:                  return y;
            }
        };

        // -------- Layout pass: emit PlacedGlyph per glyph via visitor --------
        size_t colIdx = 0;
        float  colX        = colX0;
        float  colCenterX  = colX - em / 2.f;
        float  colY        = colYStart(0);
        float  colTop      = colY;   // anchor fitY() accumulates from

        // Baseline for an upright glyph whose cell starts at cellTop. Snapped
        // once, from the top of the column -- see fitY().
        //
        // Rotated glyphs are left alone: their baseline runs vertically after
        // the rotation, so moving baselineY moves them ACROSS the column rather
        // than along it, and grid fit has nothing to say about that axis.
        auto uprightBaseline = [&](float cellTop) -> float {
            return colTop + fitY(cellTop - colTop + asc);
        };

        for (const auto& t : toks) {
            if (t.kind == VTokKind_::Newline) {
                colIdx++;
                colX       -= colSpacing;
                colCenterX  = colX - em / 2.f;
                colY        = colYStart(colIdx);
                colTop      = colY;
                continue;
            }

            if (t.kind == VTokKind_::Single) {
                const uint32_t cp = t.cp;
                const internal::VertOrient vo = internal::getVerticalOrientation(cp);
                const uint32_t vcp = internal::getVerticalCodepoint(cp);
                const bool hasVert = (vcp != 0 && atlasManager_->fontHasGlyph(vcp));

                if (vo == internal::VertOrient::U
                    || (vo == internal::VertOrient::Tu)
                    || (vo == internal::VertOrient::Tr && hasVert)) {
                    // Upright path.
                    const uint32_t useCp = hasVert ? vcp : cp;
                    const internal::GlyphInfo* g = atlasManager_->getOrLoadGlyph(useCp);
                    if (g && g->isValid()) {
                        float drawX = colCenterX - g->getAdvance() * s / 2.f;
                        float baselineY = uprightBaseline(colY);
                        // Fallback offset for Tu without vertical form.
                        if (vo == internal::VertOrient::Tu && !hasVert) {
                            internal::VertOffset off = internal::getVerticalPunctOffset(cp);
                            drawX     += off.dx * em;
                            baselineY += off.dy * em;
                        }
                        visitor(PlacedGlyph{useCp, drawX, baselineY, 0.f, 0.f, 0.f, 1.f});
                    }
                } else {
                    // Rotate 90° CW around cell center.
                    const internal::GlyphInfo* g = atlasManager_->getOrLoadGlyph(cp);
                    if (g && g->isValid()) {
                        float cx = colCenterX;
                        float cy = colY + cellH / 2.f;
                        float drawX = cx - g->getAdvance() * s / 2.f;
                        float baselineY = cy - em / 2.f + asc;
                        visitor(PlacedGlyph{cp, drawX, baselineY, QUARTER_TAU, cx, cy, 1.f});
                    }
                }
                colY += cellH;
                continue;
            }

            // ---- LatinRun / DigitRun ----
            const TcyMode m = pickTcy(t);
            const float rw = runHorizWidth(t);
            switch (m) {
                case TcyMode::Rotate: {
                    float cx = colCenterX;
                    float cy = colY + rw / 2.f;
                    float cursorX   = cx - rw / 2.f;
                    float baselineY = cy - em / 2.f + asc;
                    for (uint32_t cp : t.cps) {
                        const internal::GlyphInfo* g = atlasManager_->getOrLoadGlyph(cp);
                        if (!g || !g->isValid()) continue;
                        visitor(PlacedGlyph{cp, cursorX, baselineY, QUARTER_TAU, cx, cy, 1.f});
                        cursorX += g->getAdvance() * s;
                    }
                    colY += rw;
                    break;
                }
                case TcyMode::Upright: {
                    for (uint32_t cp : t.cps) {
                        const internal::GlyphInfo* g = atlasManager_->getOrLoadGlyph(cp);
                        if (g && g->isValid()) {
                            float drawX = colCenterX - g->getAdvance() * s / 2.f;
                            float baselineY = uprightBaseline(colY);
                            visitor(PlacedGlyph{cp, drawX, baselineY, 0.f, 0.f, 0.f, 1.f});
                        }
                        colY += cellH;
                    }
                    break;
                }
                case TcyMode::Combine: {
                    // Fit run horizontally into one em cell with x-scale only.
                    const float xscale = (rw > em) ? em / rw : 1.f;
                    const float scaledW = rw * xscale;
                    const float cellLeft = colCenterX - em / 2.f;
                    float cursorX = cellLeft + (em - scaledW) / 2.f;
                    float baselineY = uprightBaseline(colY);
                    for (uint32_t cp : t.cps) {
                        const internal::GlyphInfo* g = atlasManager_->getOrLoadGlyph(cp);
                        if (!g || !g->isValid()) continue;
                        visitor(PlacedGlyph{cp, cursorX, baselineY, 0.f, 0.f, 0.f, xscale});
                        cursorX += g->getAdvance() * s * xscale;
                    }
                    colY += cellH;
                    break;
                }
            }
        }
    }

    void drawStringVerticalInternal(const std::string& text, float x, float y,
                                    Direction h, Direction v) const {
        if (!atlasManager_ || text.empty()) return;

        std::vector<PlacedGlyph> placed;
        placed.reserve(text.size());
        forEachGlyphVertical(text, x, y, h, v,
            [&](const PlacedGlyph& pg) { placed.push_back(pg); });

        emitPlacedGlyphsToAtlas(placed);
    }

    // -------------------------------------------------------------------------
    // Kinsoku-level-gated table lookups. When kinsokuLevel_ == Off, both return
    // false unconditionally (Latin word integrity in isBreakable still applies,
    // because that's a separate Latin-wrap rule, not kinsoku).
    // -------------------------------------------------------------------------
    bool kinsokuLineStart(uint32_t cp) const {
        switch (kinsokuLevel_) {
            case KinsokuLevel::Off:             return false;
            case KinsokuLevel::PunctuationOnly: return internal::isPunctuationOnlyLineStart(cp);
            case KinsokuLevel::Standard:        return internal::isLineStartProhibited(cp);
        }
        return false;
    }
    bool kinsokuLineEnd(uint32_t cp) const {
        switch (kinsokuLevel_) {
            case KinsokuLevel::Off:             return false;
            case KinsokuLevel::PunctuationOnly: return false;  // no opening bracket subset for puncts-only
            case KinsokuLevel::Standard:        return internal::isLineEndProhibited(cp);
        }
        return false;
    }

    // -------------------------------------------------------------------------
    // Line wrap preprocessing — inserts '\n' (and '-' for hyphenation) into the
    // input string so the rest of the pipeline keeps using the simple hard-newline
    // logic it already has. Returns the input unchanged when wrap is disabled
    // or maxLineLength <= 0.
    // -------------------------------------------------------------------------
    std::string wrapTextHorizontal(const std::string& text) const {
        if (!wrapEnabled_ || maxLineLength_ <= 0 || !atlasManager_) return text;

        const float s = 1.0f / dpiScale_;
        const float spaceAdv = atlasManager_->getSpaceAdvance() * s;

        // Decode all codepoints with byte ranges and per-glyph advances.
        struct Cp { uint32_t cp; size_t byteStart; size_t byteEnd; float advance; };
        std::vector<Cp> cps;
        cps.reserve(text.size());
        for (size_t i = 0; i < text.size(); ) {
            size_t before = i;
            uint32_t cp = decodeUTF8(text, i);
            float adv = 0;
            if (cp != '\n') {
                if (cp == '\t') adv = spaceAdv * 4;
                else {
                    const internal::GlyphInfo* g = atlasManager_->getOrLoadGlyph(cp);
                    adv = (g && g->isValid()) ? g->getAdvance() * s : 0;
                }
            }
            cps.push_back({cp, before, i, adv});
        }

        auto emitRange = [&](size_t from, size_t to, std::string& out) {
            if (from >= to || from >= cps.size()) return;
            size_t lastIdx = (to <= cps.size()) ? to - 1 : cps.size() - 1;
            size_t bs = cps[from].byteStart;
            size_t be = cps[lastIdx].byteEnd;
            out.append(text, bs, be - bs);
        };

        // "Can we break the line immediately AFTER cps[i]?"
        auto isBreakable = [&](size_t i) -> bool {
            if (i + 1 >= cps.size()) return false;
            uint32_t a = cps[i].cp;
            uint32_t b = cps[i + 1].cp;
            if (b == '\n') return false;            // hard newline handled separately
            if (a == ' ' || a == '\t') return true; // after whitespace
            if (a == '-') return true;              // after hyphen
            const bool aCjk = internal::isCjkChar(a);
            const bool bCjk = internal::isCjkChar(b);
            if (aCjk || bCjk) {
                if (kinsokuLineEnd(a)) return false;   // can't end on opener (when kinsoku active)
                if (kinsokuLineStart(b)) return false; // can't start on kinsoku
                return true;
            }
            return false;
        };

        std::string out;
        out.reserve(text.size() + text.size() / 8);
        size_t lineStart = 0;
        while (lineStart < cps.size()) {
            // Pass-through leading hard newline
            if (cps[lineStart].cp == '\n') {
                out += '\n';
                ++lineStart;
                continue;
            }

            float  width = 0;
            size_t lastBreak = std::string::npos;
            size_t i = lineStart;
            bool   wrapped = false;

            while (i < cps.size()) {
                if (cps[i].cp == '\n') {
                    emitRange(lineStart, i, out);
                    out += '\n';
                    lineStart = i + 1;
                    wrapped = true;
                    break;
                }
                const float nextW = width + cps[i].advance;
                if (nextW > maxLineLength_ && i > lineStart) {
                    // Kinsoku hanging: 行頭禁則 chars at the break point ride on
                    // the current line even though they overflow. Sweep across
                    // runs (e.g. ）。 or 」」) so consecutive prohibited chars
                    // hang together — otherwise the second one wraps alone.
                    if (hangingPunctuation_ && kinsokuLineStart(cps[i].cp)) {
                        size_t hangEnd = i + 1;
                        while (hangEnd < cps.size()
                               && cps[hangEnd].cp != '\n'
                               && kinsokuLineStart(cps[hangEnd].cp)) {
                            ++hangEnd;
                        }
                        emitRange(lineStart, hangEnd, out);
                        if (hangEnd < cps.size()) out += '\n';
                        lineStart = hangEnd;
                        wrapped = true;
                        break;
                    }
                    if (lastBreak != std::string::npos) {
                        const size_t breakAt = lastBreak + 1;
                        emitRange(lineStart, breakAt, out);
                        out += '\n';
                        lineStart = breakAt;
                    } else {
                        // No break opportunity — forced break before cps[i].
                        emitRange(lineStart, i, out);
                        if (latinHyphenation_) out += '-';
                        out += '\n';
                        lineStart = i;
                    }
                    wrapped = true;
                    break;
                }
                width = nextW;
                if (isBreakable(i)) lastBreak = i;
                ++i;
            }

            if (!wrapped) {
                emitRange(lineStart, cps.size(), out);
                lineStart = cps.size();
            }
        }
        return out;
    }

    // Vertical wrap: maxLineLength is the column-height budget. Tokenize the
    // same way drawStringVerticalInternal does (so per-token vertical advance
    // matches the actual layout: CJK = em, Latin/Digit runs depend on TcyMode),
    // then greedy-wrap by inserting '\n' between tokens.
    std::string wrapTextVertical(const std::string& text) const {
        if (!wrapEnabled_ || maxLineLength_ <= 0 || !atlasManager_) return text;

        const float s = 1.0f / dpiScale_;
        const float em = (float)logicalSize_;
        const float cellH = em;

        enum class TK { Single, Latin, Digit, NL };
        struct Tok { TK kind; size_t byteStart; size_t byteEnd; float advance; };
        std::vector<Tok> toks;
        toks.reserve(text.size());

        // ---- Tokenize ----
        for (size_t i = 0; i < text.size(); ) {
            size_t before = i;
            uint32_t cp = decodeUTF8(text, i);
            if (cp == '\n') { toks.push_back({TK::NL, before, i, 0}); continue; }
            if (cp == '\t') continue;
            if (internal::isAsciiDigit(cp)) {
                if (!toks.empty() && toks.back().kind == TK::Digit)
                    toks.back().byteEnd = i;
                else
                    toks.push_back({TK::Digit, before, i, 0});
                continue;
            }
            if (internal::isAsciiLetter(cp)) {
                if (!toks.empty() && toks.back().kind == TK::Latin)
                    toks.back().byteEnd = i;
                else
                    toks.push_back({TK::Latin, before, i, 0});
                continue;
            }
            toks.push_back({TK::Single, before, i, 0});
        }

        auto runHorizWidth = [&](const Tok& t) -> float {
            float w = 0;
            for (size_t bi = t.byteStart; bi < t.byteEnd; ) {
                uint32_t cp = decodeUTF8(text, bi);
                const internal::GlyphInfo* g = atlasManager_->getOrLoadGlyph(cp);
                if (g && g->isValid()) w += g->getAdvance() * s;
            }
            return w;
        };
        auto runCount = [&](const Tok& t) -> int {
            int n = 0;
            for (size_t bi = t.byteStart; bi < t.byteEnd; ) {
                decodeUTF8(text, bi);
                ++n;
            }
            return n;
        };
        auto firstCp = [&](const Tok& t) -> uint32_t {
            size_t bi = t.byteStart;
            if (bi >= text.size()) return 0;
            return decodeUTF8(text, bi);
        };

        // ---- Per-token vertical advance ----
        for (auto& t : toks) {
            switch (t.kind) {
                case TK::NL:     t.advance = 0;     break;
                case TK::Single: t.advance = cellH; break;
                case TK::Latin: {
                    const int n = runCount(t);
                    switch (tcyLatinMode_) {
                        case TcyMode::Rotate:  t.advance = runHorizWidth(t); break;
                        case TcyMode::Upright: t.advance = cellH * (float)n; break;
                        case TcyMode::Combine: t.advance = cellH;            break;
                    }
                    break;
                }
                case TK::Digit: {
                    const int n = runCount(t);
                    const TcyMode m = (n <= tcyDigitMax_) ? tcyDigitInMode_
                                                         : tcyDigitOverflowMode_;
                    switch (m) {
                        case TcyMode::Rotate:  t.advance = runHorizWidth(t); break;
                        case TcyMode::Upright: t.advance = cellH * (float)n; break;
                        case TcyMode::Combine: t.advance = cellH;            break;
                    }
                    break;
                }
            }
        }

        auto isBreakableAfter = [&](size_t i) -> bool {
            if (i + 1 >= toks.size()) return false;
            const Tok& a = toks[i];
            const Tok& b = toks[i + 1];
            if (a.kind == TK::NL || b.kind == TK::NL) return false;
            if (a.kind == TK::Single && kinsokuLineEnd(firstCp(a))) return false;
            if (b.kind == TK::Single && kinsokuLineStart(firstCp(b))) return false;
            return true;
        };

        auto emitTokRange = [&](size_t from, size_t to, std::string& out) {
            if (from >= to || from >= toks.size()) return;
            if (to > toks.size()) to = toks.size();
            size_t bs = toks[from].byteStart;
            size_t be = toks[to - 1].byteEnd;
            if (bs < be) out.append(text, bs, be - bs);
        };

        // ---- Greedy column wrap ----
        std::string out;
        out.reserve(text.size() + text.size() / 8);
        size_t colStart = 0;
        while (colStart < toks.size()) {
            if (toks[colStart].kind == TK::NL) {
                out += '\n';
                ++colStart;
                continue;
            }
            float  height = 0;
            size_t lastBreak = std::string::npos;
            size_t i = colStart;
            bool   wrapped = false;
            while (i < toks.size()) {
                if (toks[i].kind == TK::NL) {
                    emitTokRange(colStart, i, out);
                    out += '\n';
                    colStart = i + 1;
                    wrapped = true;
                    break;
                }
                const float nextH = height + toks[i].advance;
                if (nextH > maxLineLength_ && i > colStart) {
                    if (hangingPunctuation_
                        && toks[i].kind == TK::Single
                        && kinsokuLineStart(firstCp(toks[i]))) {
                        // Sweep over consecutive 行頭禁則 chars so e.g. ）。
                        // stays together at the column edge.
                        size_t hangEnd = i + 1;
                        while (hangEnd < toks.size()
                               && toks[hangEnd].kind == TK::Single
                               && kinsokuLineStart(firstCp(toks[hangEnd]))) {
                            ++hangEnd;
                        }
                        emitTokRange(colStart, hangEnd, out);
                        if (hangEnd < toks.size()) out += '\n';
                        colStart = hangEnd;
                        wrapped = true;
                        break;
                    }
                    if (lastBreak != std::string::npos) {
                        const size_t breakAt = lastBreak + 1;
                        emitTokRange(colStart, breakAt, out);
                        out += '\n';
                        colStart = breakAt;
                    } else {
                        emitTokRange(colStart, i, out);
                        out += '\n';
                        colStart = i;
                    }
                    wrapped = true;
                    break;
                }
                height = nextH;
                if (isBreakableAfter(i)) lastBreak = i;
                ++i;
            }
            if (!wrapped) {
                emitTokRange(colStart, toks.size(), out);
                colStart = toks.size();
            }
        }
        return out;
    }

    std::string wrapTextIfEnabled(const std::string& text) const {
        if (!wrapEnabled_) return text;
        return (writingMode_ == WritingMode::VerticalRL)
            ? wrapTextVertical(text)
            : wrapTextHorizontal(text);
    }

public:
    // -------------------------------------------------------------------------
    // Metrics (virtual - customizable in subclass)
    // -------------------------------------------------------------------------
    virtual float getWidth(const std::string& text) const {
        if (!atlasManager_) return 0;

        const std::string src = wrapTextIfEnabled(text);

        float width = 0;
        float maxWidth = 0;
        const float s = 1.0f / dpiScale_;

        for (size_t i = 0; i < src.size(); ) {
            uint32_t codepoint = decodeUTF8(src, i);

            if (codepoint == '\n') {
                if (width > maxWidth) maxWidth = width;
                width = 0;
                continue;
            }
            if (codepoint == '\t') {
                width += atlasManager_->getSpaceAdvance() * s * 4;
                continue;
            }

            const internal::GlyphInfo* g = atlasManager_->getOrLoadGlyph(codepoint);
            if (g && g->isValid()) {
                width += g->getAdvance() * s;
            }
        }

        return (width > maxWidth) ? width : maxWidth;
    }

    // Kept for backward compatibility
    float stringWidth(const std::string& text) const { return getWidth(text); }

    virtual float getHeight(const std::string& text) const {
        if (!atlasManager_) return 0;

        const std::string src = wrapTextIfEnabled(text);
        int lines = 1;
        for (char c : src) {
            if (c == '\n') lines++;
        }
        return getLineHeight() * lines;
    }

    // Get text bounding box (top-left origin)
    virtual Rect getBBox(const std::string& text) const {
        return Rect(0, 0, getWidth(text), getHeight(text));
    }

    virtual float getLineHeight() const {
        if (lineHeight_ > 0) return lineHeight_;
        return atlasManager_ ? atlasManager_->getLineHeight() / dpiScale_ : 0;
    }

    // Get font's default line height (unaffected by setLineHeight)
    float getDefaultLineHeight() const {
        return atlasManager_ ? atlasManager_->getLineHeight() / dpiScale_ : 0;
    }

    virtual float getAscent() const {
        return atlasManager_ ? atlasManager_->getAscent() / dpiScale_ : 0;
    }

    virtual float getDescent() const {
        return atlasManager_ ? atlasManager_->getDescent() / dpiScale_ : 0;
    }

    int getSize() const {
        return logicalSize_;
    }

protected:
    // ---- Grid fit (draw path) -----------------------------------------------
    // Whether snapping a baseline to a whole model unit actually puts it on the
    // pixel grid. Rounding happens in MODEL space, so it only lands while one
    // model unit is one device pixel. Under any other scale it does the
    // opposite of its job: a snapped baseline at, say, y = 13 sits at 6.5
    // device pixels when drawn at scale 0.5 -- the worst possible phase, on
    // every line, in every frame, instead of the uniformly distributed phase an
    // unsnapped baseline gives. That is not a rounding-error-level effect;
    // measured on Helvetica at scale 0.5 it costs up to 13% of the energy
    // concentration it wins back at 1:1. So grid fit stands down off 1:1.
    bool gridFitLands() const {
        if (!gridFit_) return false;
        return std::fabs(getDefaultContext().getScale() - 1.0f) < 0.01f;
    }

    // Snap a baseline offset to the pixel grid.
    //
    // Callers must pass the offset ACCUMULATED from the anchor (ascent +
    // n * lineHeight), not a per-line delta, and must round once. Rounding the
    // line height instead and stepping by it would compound: at lineHeight
    // 11.66 the block would run 0, 12, 24, 36 and end a whole pixel below where
    // the text is supposed to be. Accumulating first gives 0, 12, 23, 35 --
    // every baseline on the grid AND the block never more than half a pixel
    // from its true height, however many lines it runs to.
    //
    // The offset is snapped rather than the final position so that a caller
    // animating y still gets smooth sub-pixel motion instead of whole-pixel
    // judder; what is fixed is the spacing within the block.
    float fitY(float offsetFromAnchor) const {
        return gridFitLands() ? std::round(offsetFromAnchor) : offsetFromAnchor;
    }

    // -------------------------------------------------------------------------
    // Alignment offset calculation (available to subclasses)
    // -------------------------------------------------------------------------
    Vec2 calcAlignOffset(const std::string& text, Direction h, Direction v) const {
        float offsetX = 0;
        float offsetY = 0;

        // Horizontal offset
        float w = getWidth(text);
        switch (h) {
            case Direction::Left:   offsetX = 0; break;
            case Direction::Center: offsetX = -w / 2; break;
            case Direction::Right:  offsetX = -w; break;
            default: break;
        }

        // Vertical offset
        float ascent = getAscent();
        float descent = getDescent();
        float totalHeight = ascent - descent;

        switch (v) {
            case Direction::Top:      offsetY = 0; break;
            case Direction::Baseline: offsetY = -ascent; break;
            case Direction::Center:   offsetY = -totalHeight / 2; break;
            case Direction::Bottom:   offsetY = -totalHeight; break;
            default: break;
        }

        return Vec2(offsetX, offsetY);
    }

    // Settings (protected - accessible to subclasses)
    Direction alignH_ = Direction::Left;
    Direction alignV_ = Direction::Top;
    float lineHeight_ = 0;  // 0 = use font's default line height

    // Vertical writing settings (default = horizontal, no behavior change)
    WritingMode writingMode_         = WritingMode::Horizontal;
    int         tcyDigitMax_         = 0;                // 0 → disabled
    TcyMode     tcyDigitInMode_      = TcyMode::Combine; // digit count ≤ max
    TcyMode     tcyDigitOverflowMode_= TcyMode::Rotate;  // digit count >  max
    TcyMode     tcyLatinMode_        = TcyMode::Rotate;  // Latin letter runs

    // Line wrap settings (default = off, no behavior change)
    bool         wrapEnabled_        = false;            // master toggle
    float        maxLineLength_      = 0;                // pixels; horizontal: line width, vertical: column height
    bool         latinHyphenation_   = false;            // insert '-' on forced Latin mid-word break
    bool         hangingPunctuation_ = false;            // CJK kinsoku — let prohibited line-start chars hang past edge
    KinsokuLevel kinsokuLevel_       = KinsokuLevel::Off;// which subset of kinsoku tables to consult

public:
    // -------------------------------------------------------------------------
    // Memory info
    // -------------------------------------------------------------------------
    size_t getMemoryUsage() const {
        return atlasManager_ ? atlasManager_->getMemoryUsage() : 0;
    }

    // Alias for clarity
    size_t getAtlasMemoryUsage() const { return getMemoryUsage(); }

    // Clear atlas pages (glyphs re-rasterized on next draw)
    void clearAtlas() {
        if (atlasManager_) atlasManager_->clearAtlas();
    }

    // Atlas page access (for debug visualization)
    size_t getAtlasCount() const {
        return atlasManager_ ? atlasManager_->getAtlasCount() : 0;
    }

    const internal::AtlasState* getAtlas(size_t index) const {
        return atlasManager_ ? &atlasManager_->getAtlas(index) : nullptr;
    }

    // Get shared sampler (for debug atlas rendering). Returns the mip-0-pinned
    // one: a debug view of the atlas wants to show the texels as stored.
    sg_sampler getSampler() { initResources(); return internal::fontSamplers().sharp; }

    size_t getLoadedGlyphCount() const {
        return atlasManager_ ? atlasManager_->getLoadedGlyphCount() : 0;
    }

    static size_t getTotalCacheMemoryUsage() {
        return internal::SharedFontCache::getInstance().getTotalMemoryUsage();
    }

private:
    std::shared_ptr<internal::FontAtlasManager> atlasManager_;
    internal::FontCacheKey cacheKey_;
    float dpiScale_ = 1.0f;    // DPI scale at load time (physical/logical ratio)
    int oversample_ = defaultOversample_;   // desired; stamped into cacheKey_ on load
    bool mipmaps_ = true;                   // desired; stamped into cacheKey_ on load
    // On by default: it costs no memory and one round() per line, is positive
    // at 1:1 on every face and size measured, and stands down automatically
    // under any other transform (see gridFitLands()). Not part of cacheKey_ --
    // it is a placement decision and never reaches the atlas.
    bool gridFit_ = true;

    // 4x4 already costs 16x the atlas; beyond that the prefilter gains nothing
    // a bigger font size would not give more cheaply.
    static int clampOversample(int n) { return (n < 1) ? 1 : (n > 4 ? 4 : n); }
    // Set and read by the code that loads fonts (app code): harmless per module.
    static inline int defaultOversample_ = 1;
    int logicalSize_ = 0;      // User-requested font size (logical pixels)

    // Shared GPU resources. The TTF draw path loads the active per-target
    // coverage pipeline (internal::activeCoverage2D()) at draw time, so the font class
    // only needs its own samplers: internal::fontSamplers(), one pair per
    // process, lazily created by initResources().

    // Minified text wants the mip chain; everything sharper wants the
    // oversampled mip 0. The quantity that decides it is texels per screen
    // pixel: the quad covers width*oversample texels and width/dpiScale *
    // getScale() * dpiScale screen pixels, so the dpiScale cancels and it is
    // just oversample/getScale().
    //
    // The threshold is 2, not 1, because a bilinear fetch reads a 2x2
    // neighbourhood and copes with 2:1 on its own. Measured on 16px text
    // (phase-sweep shimmer, un-oversampled atlas): at 2 texels/px mip 0 is
    // actually BETTER (1.05% vs 1.16% — the mip chain only adds blur where
    // nothing was broken), while at 4 it is not close (9.79% vs 2.95%) and it
    // keeps growing from there (16 texels/px: 28.4% vs 13.6%).
    //
    // getScale() takes the same shortcuts the adaptive curve tessellator
    // already accepts (tcRenderContext.h): column lengths approximate rotation
    // and shear, and perspective is not considered. Choosing a mip level is a
    // far more forgiving use of that estimate than choosing a segment count.
    sg_sampler pickSampler() const {
        const int   oversample = atlasManager_->getOversample();
        const float scale = getDefaultContext().getScale();
        const float texelsPerPixel = (scale > 0.0f) ? (oversample / scale)
                                                    : (float)oversample;
        if (texelsPerPixel <= 2.0f) return internal::fontSamplers().sharp;

        // First draw below the bilinear-safe rate is what pays for the chain.
        atlasManager_->requestMipmaps();
        return internal::fontSamplers().mipped;
    }

    void initResources() {
        internal::FontSamplers& samplers = internal::fontSamplers();
        if (samplers.initialized) return;

        // Two samplers, picked per draw by the effective scale (see
        // pickSampler). Trilinear on both: without mipmap_filter sokol defaults
        // to NEAREST, which snaps to the nearest level and makes a 0.6x draw
        // jump to the half-resolution mip and magnify it back up -- visibly
        // soft, with a pop at the boundary. Texture already sets LINEAR here
        // (tcTexture.h); the font path was the outlier.
        sg_sampler_desc smp_desc = {};
        smp_desc.min_filter = SG_FILTER_LINEAR;
        smp_desc.mag_filter = SG_FILTER_LINEAR;
        smp_desc.mipmap_filter = SG_FILTER_LINEAR;
        smp_desc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
        smp_desc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
        samplers.mipped = sg_make_sampler(&smp_desc);

        // Pinned to mip 0. An NxN atlas is N times denser than the screen, so
        // even a 1:1 draw computes LOD log2(N) and would read a mip that throws
        // the oversampling away. Clamping the LOD is how the denser atlas gets
        // to be denser. Harmless on a mip-less atlas -- there is only level 0.
        //
        // NOT 0.0f: sokol treats a zero max_lod as "unset" and substitutes
        // FLT_MAX (_sg_sampler_desc_defaults), so asking for exactly mip 0
        // silently asks for the whole chain. A small epsilon reads as an
        // explicit value and still floors to level 0 under trilinear.
        smp_desc.max_lod = 0.01f;
        samplers.sharp = sg_make_sampler(&smp_desc);

        samplers.initialized = true;
    }

    // UTF-8 decode (simple version)
    static uint32_t decodeUTF8(const std::string& str, size_t& i) {
        uint8_t c = str[i++];
        if ((c & 0x80) == 0) {
            return c;
        } else if ((c & 0xE0) == 0xC0) {
            uint32_t cp = (c & 0x1F) << 6;
            if (i < str.size()) cp |= (str[i++] & 0x3F);
            return cp;
        } else if ((c & 0xF0) == 0xE0) {
            uint32_t cp = (c & 0x0F) << 12;
            if (i < str.size()) cp |= (str[i++] & 0x3F) << 6;
            if (i < str.size()) cp |= (str[i++] & 0x3F);
            return cp;
        } else if ((c & 0xF8) == 0xF0) {
            uint32_t cp = (c & 0x07) << 18;
            if (i < str.size()) cp |= (str[i++] & 0x3F) << 12;
            if (i < str.size()) cp |= (str[i++] & 0x3F) << 6;
            if (i < str.size()) cp |= (str[i++] & 0x3F);
            return cp;
        }
        return '?';
    }
};

} // namespace trussc

namespace tc = trussc;
