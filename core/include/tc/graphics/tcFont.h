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
// TODO: Memory optimization
// - Currently uses RGBA8 (4bytes/pixel)
// - Could reduce to 1/4 with R8 (1byte/pixel) + custom shader
// - Requires direct sokol_gfx usage with shader swizzle
// =============================================================================

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cstdint>
#include <fstream>
#include <functional>
#include <cstring>

#ifdef __EMSCRIPTEN__
#include <emscripten/fetch.h>
#include <emscripten.h>
#endif

// sokol headers
#include "sokol/sokol_app.h"
#include "sokol/sokol_gfx.h"
#include "sokol/util/sokol_gl_tc.h"

// stb_truetype
#include "stb/stb_truetype.h"

#include "../utils/tcLog.h"
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

// ---------------------------------------------------------------------------
// Font cache key (font path + size)
// ---------------------------------------------------------------------------
struct FontCacheKey {
    std::string fontPath;
    int fontSize;

    bool operator==(const FontCacheKey& other) const {
        return fontPath == other.fontPath && fontSize == other.fontSize;
    }
};

struct FontCacheKeyHash {
    size_t operator()(const FontCacheKey& key) const {
        size_t h1 = std::hash<std::string>()(key.fontPath);
        size_t h2 = std::hash<int>()(key.fontSize);
        return h1 ^ (h2 << 1);
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
    uint64_t lastUpdateFrame_ = 0;  // Last update frame number

    // CPU-side pixel data (for expansion/update)
    std::vector<uint8_t> pixels_;  // RGBA
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

        // Load font file
        std::ifstream file(fontPath, std::ios::binary | std::ios::ate);
        if (!file) {
            logError() << "FontAtlasManager: failed to open " << fontPath;
            return false;
        }

        size_t fileSize = file.tellg();
        file.seekg(0, std::ios::beg);
        fontData_.resize(fileSize);
        if (!file.read(reinterpret_cast<char*>(fontData_.data()), fileSize)) {
            logError() << "FontAtlasManager: failed to read " << fontPath;
            return false;
        }

        return initFromFontData(fontSize);
    }

    bool setupFromMemory(const uint8_t* data, size_t size, int fontSize) {
        cleanup();

        fontData_.resize(size);
        std::memcpy(fontData_.data(), data, size);

        return initFromFontData(fontSize);
    }

private:
    bool initFromFontData(int fontSize, int fontIndex = 0) {
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

        fontSize_ = fontSize;
        scale_ = stbtt_ScaleForPixelHeight(&fontInfo_, (float)fontSize);

        // Get font metrics
        int ascent, descent, lineGap;
        stbtt_GetFontVMetrics(&fontInfo_, &ascent, &descent, &lineGap);
        ascent_ = ascent * scale_;
        descent_ = descent * scale_;
        lineGap_ = lineGap * scale_;

        // Get space advance
        int spaceIndex = stbtt_FindGlyphIndex(&fontInfo_, ' ');
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
        return true;
    }

public:

    void cleanup() {
        // Only release GPU resources if sokol is still valid
        // (may have already shut down at program exit)
        if (sg_isvalid()) {
            for (auto& pd : pendingDestroys_) {
                sg_destroy_view(pd.view);
                sg_destroy_image(pd.image);
            }
            for (auto& atlas : atlases_) {
                if (atlas.textureValid_) {
                    sg_destroy_view(atlas.view_);
                    sg_destroy_image(atlas.texture_);
                }
            }
        }
        pendingDestroys_.clear();
        atlases_.clear();
        glyphs_.clear();
        fontData_.clear();
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

        // Add glyph
        GlyphInfo info;
        if (addGlyphToAtlas(codepoint, info)) {
            glyphs_[codepoint] = info;
            return &glyphs_[codepoint];
        }

        return nullptr;
    }

    bool hasGlyph(uint32_t codepoint) const {
        return glyphs_.find(codepoint) != glyphs_.end();
    }

    // Check whether the underlying font file actually contains a glyph for
    // this codepoint (vs. .notdef). Used by vertical writing to decide whether
    // a CJK Compatibility Forms variant (U+FE10–FE4F) is usable.
    bool fontHasGlyph(uint32_t codepoint) const {
        if (!loaded_) return false;
        return stbtt_FindGlyphIndex(&fontInfo_, (int)codepoint) != 0;
    }

    // -------------------------------------------------------------------------
    // Vector glyph outline (em-normalized, screen Y-down).
    //
    // Returns one Path per contour. Coordinates are in em units (1.0 = em),
    // so a glyph above the baseline has Y < 0. Each Path is closed (last
    // segment returns to first vertex). For glyphs with no outline (e.g. the
    // space character) returns an empty vector without logging.
    // -------------------------------------------------------------------------
    std::vector<Path> getGlyphPath(uint32_t codepoint) const {
        std::vector<Path> result;
        if (!loaded_) return result;

        int glyphIndex = stbtt_FindGlyphIndex(&fontInfo_, (int)codepoint);
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

        Path current;
        auto flush = [&]() {
            if (!current.empty()) {
                current.close();
                result.push_back(std::move(current));
                current = Path{};
            }
        };

        for (int i = 0; i < numVerts; i++) {
            const stbtt_vertex& v = vertices[i];
            const float x = (float)v.x * emScale;
            const float y = -(float)v.y * emScale;
            switch (v.type) {
                case STBTT_vmove:
                    flush();
                    current.lineTo(x, y);  // first vertex of new contour
                    break;
                case STBTT_vline:
                    current.lineTo(x, y);
                    break;
                case STBTT_vcurve: {
                    // Explicit resolution: Path's adaptive tessellation uses an
                    // absolute tolerance, but our coords are em-normalized
                    // (~1.0 max), so without an explicit count curves collapse
                    // to straight chords. 12 segments is plenty for glyph quads.
                    const float cx = (float)v.cx * emScale;
                    const float cy = -(float)v.cy * emScale;
                    current.quadBezierTo(Vec2{cx, cy}, Vec2{x, y}, 12);
                    break;
                }
                case STBTT_vcubic: {
                    const float cx  = (float)v.cx  * emScale;
                    const float cy  = -(float)v.cy * emScale;
                    const float cx1 = (float)v.cx1 * emScale;
                    const float cy1 = -(float)v.cy1 * emScale;
                    current.bezierTo(Vec3{cx, cy, 0.f}, Vec3{cx1, cy1, 0.f}, Vec3{x, y, 0.f}, 16);
                    break;
                }
            }
        }
        flush();

        stbtt_FreeShape(&fontInfo_, vertices);
        return result;
    }

    // Em-normalized horizontal advance for this codepoint (1.0 = em).
    float getGlyphAdvanceEm(uint32_t codepoint) const {
        if (!loaded_) return 0.f;
        int glyphIndex = stbtt_FindGlyphIndex(&fontInfo_, (int)codepoint);
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
        if (sg_isvalid()) {
            for (auto& pd : pendingDestroys_) {
                sg_destroy_view(pd.view);
                sg_destroy_image(pd.image);
            }
            for (auto& atlas : atlases_) {
                if (atlas.textureValid_) {
                    sg_destroy_view(atlas.view_);
                    sg_destroy_image(atlas.texture_);
                }
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
    int fontSize_ = 0;
    float scale_ = 0;
    float ascent_ = 0;
    float descent_ = 0;
    float lineGap_ = 0;
    float spaceAdvance_ = 0;

    // Atlases
    std::vector<AtlasState> atlases_;

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
        atlas.pixels_.resize(atlas.width_ * atlas.height_ * 4, 0);
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
        std::vector<uint8_t> newPixels(newWidth * newHeight * 4, 0);

        // Copy old data
        for (int y = 0; y < atlas.height_; y++) {
            memcpy(newPixels.data() + y * newWidth * 4,
                   atlas.pixels_.data() + y * atlas.width_ * 4,
                   atlas.width_ * 4);
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
        int glyphIndex = stbtt_FindGlyphIndex(&fontInfo_, codepoint);

        // Get glyph metrics
        int advanceWidth, leftSideBearing;
        stbtt_GetGlyphHMetrics(&fontInfo_, glyphIndex, &advanceWidth, &leftSideBearing);

        int x0, y0, x1, y1;
        stbtt_GetGlyphBitmapBox(&fontInfo_, glyphIndex, scale_, scale_, &x0, &y0, &x1, &y1);

        int glyphWidth = x1 - x0;
        int glyphHeight = y1 - y0;

        // Zero-width glyphs (like space)
        if (glyphWidth <= 0 || glyphHeight <= 0) {
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

        int paddedWidth = glyphWidth + GLYPH_PADDING;
        int paddedHeight = glyphHeight + GLYPH_PADDING;

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

        // Render glyph (8bit grayscale)
        std::vector<uint8_t> glyphBitmap(glyphWidth * glyphHeight);
        stbtt_MakeGlyphBitmap(&fontInfo_,
                              glyphBitmap.data(),
                              glyphWidth, glyphHeight,
                              glyphWidth,  // stride
                              scale_, scale_,
                              glyphIndex);

        // Copy to atlas (RGBA)
        for (int y = 0; y < glyphHeight; y++) {
            for (int x = 0; x < glyphWidth; x++) {
                int srcIdx = y * glyphWidth + x;
                int dstIdx = ((destY + y) * atlas.width_ + (destX + x)) * 4;
                uint8_t alpha = glyphBitmap[srcIdx];
                atlas.pixels_[dstIdx + 0] = 255;    // R
                atlas.pixels_[dstIdx + 1] = 255;    // G
                atlas.pixels_[dstIdx + 2] = 255;    // B
                atlas.pixels_[dstIdx + 3] = alpha;  // A
            }
        }

        // Set glyph info
        outInfo.atlasIndex_ = targetAtlas;
        outInfo.u0_ = (float)destX / atlas.width_;
        outInfo.v0_ = (float)destY / atlas.height_;
        outInfo.u1_ = (float)(destX + glyphWidth) / atlas.width_;
        outInfo.v1_ = (float)(destY + glyphHeight) / atlas.height_;
        outInfo.xoff_ = (float)x0;
        outInfo.yoff_ = (float)y0;
        outInfo.width_ = (float)glyphWidth;
        outInfo.height_ = (float)glyphHeight;
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
        // Skip if already updated this frame
        uint64_t currentFrame = sapp_frame_count();
        if (atlas.textureValid_ && atlas.lastUpdateFrame_ == currentFrame) {
            return;
        }

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
        img_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
        // immutable (default) - can set initial data
        img_desc.data.mip_levels[0].ptr = atlas.pixels_.data();
        img_desc.data.mip_levels[0].size = atlas.pixels_.size();
        atlas.texture_ = sg_make_image(&img_desc);

        sg_view_desc view_desc = {};
        view_desc.texture.image = atlas.texture_;
        atlas.view_ = sg_make_view(&view_desc);

        atlas.textureValid_ = true;
        atlas.lastUpdateFrame_ = currentFrame;
        atlas.textureDirty_ = false;
    }
};

// ---------------------------------------------------------------------------
// Shared font cache (singleton)
// ---------------------------------------------------------------------------
class SharedFontCache {
public:
    static SharedFontCache& getInstance() {
        static SharedFontCache instance;
        return instance;
    }

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
    bool load(const std::string& nameOrPath, int size) {
        // Render glyphs at physical pixel size for sharp text on HiDPI displays.
        // All metrics/drawing are scaled back to logical coordinates.
        dpiScale_ = sapp_dpi_scale();
        int physicalSize = (int)(size * dpiScale_ + 0.5f);
        logicalSize_ = size;

        // Create sampler and pipeline if not yet
        if (!resourcesInitialized_) {
            initResources();
        }

        // Resolve input to a concrete path (file / URL).
        std::string actualPath = nameOrPath;
        if (!isUrl(nameOrPath)) {
            std::ifstream test(nameOrPath, std::ios::binary);
            if (!test.good()) {
                // Not a usable file path — try as a system font name.
                std::string resolved = systemFontPath(nameOrPath);
                if (!resolved.empty()) {
                    actualPath = resolved;
                    logNotice("Font") << "Resolved \"" << nameOrPath
                                      << "\" → " << resolved;
                }
                // If resolution failed, fall through with the original input so
                // the eventual load error mentions what the user actually asked for.
            }
        }

        cacheKey_.fontPath = actualPath;
        cacheKey_.fontSize = physicalSize;

        if (isUrl(actualPath)) {
#ifdef __EMSCRIPTEN__
            // Async load - returns immediately, font available after fetch completes
            loadFromUrlAsync(actualPath, physicalSize);
            return true;  // Will be loaded asynchronously
#else
            logError() << "Font: URL loading only supported in WebAssembly";
            return false;
#endif
        } else {
            atlasManager_ = SharedFontCache::getInstance().getOrCreate(cacheKey_);
        }

        return atlasManager_ != nullptr;
    }

private:
    static bool isUrl(const std::string& path) {
        return path.find("http://") == 0 || path.find("https://") == 0;
    }

#ifdef __EMSCRIPTEN__
    // Context for async font loading
    struct FontLoadContext {
        Font* font;
        FontCacheKey key;
    };

    static void onFetchSuccess(emscripten_fetch_t* fetch) {
        FontLoadContext* ctx = reinterpret_cast<FontLoadContext*>(fetch->userData);

        ctx->font->atlasManager_ = SharedFontCache::getInstance().getOrCreateFromMemory(
            ctx->key,
            reinterpret_cast<const uint8_t*>(fetch->data),
            fetch->numBytes
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
        FontCacheKey key{url, size};
        auto cached = SharedFontCache::getInstance().get(key);
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
    // getGlyphPath returns the contours of a single glyph in em-normalized
    // coordinates (1.0 = em, screen Y-down, baseline at y=0, pen at x=0).
    // One Path per contour — glyphs like 'O' or '日' produce multiple.
    //
    // getStringPath returns contours for the whole string, positioned with
    // the same layout pipeline as drawString — writing mode, alignment, wrap,
    // kinsoku, TCY all apply. Each Path is in logical pixel coordinates
    // relative to (x, y).
    // -------------------------------------------------------------------------
    std::vector<Path> getGlyphPath(uint32_t codepoint) const {
        if (!atlasManager_) return {};
        return atlasManager_->getGlyphPath(codepoint);
    }

    std::vector<Path> getStringPath(const std::string& text, float x, float y,
                                    Direction h, Direction v) const {
        std::vector<Path> result;
        if (!atlasManager_ || text.empty()) return result;

        const float em = (float)logicalSize_;

        forEachGlyph(text, x, y, h, v, [&](const PlacedGlyph& pg) {
            std::vector<Path> contours = atlasManager_->getGlyphPath(pg.codepoint);
            if (contours.empty()) return;

            const float c  = std::cos(pg.rotationCw);
            const float si = std::sin(pg.rotationCw);
            const bool  rotated = (pg.rotationCw != 0.f);

            for (Path& p : contours) {
                for (Vec3& vert : p.getVertices()) {
                    // em-normalized → logical pixels, with horizontal scale.
                    float fx = vert.x * em * pg.scaleX;
                    float fy = vert.y * em;
                    // Translate to pen position.
                    fx += pg.drawX;
                    fy += pg.baselineY;
                    // Rotate around pivot (matches emitPlacedGlyphsToAtlas math).
                    if (rotated) {
                        const float dx = fx - pg.pivotX;
                        const float dy = fy - pg.pivotY;
                        fx = pg.pivotX + c * dx - si * dy;
                        fy = pg.pivotY + si * dx + c * dy;
                    }
                    vert.x = fx;
                    vert.y = fy;
                }
                result.push_back(std::move(p));
            }
        });

        return result;
    }

    std::vector<Path> getStringPath(const std::string& text, float x, float y) const {
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
                    const GlyphInfo* g = atlasManager_->getOrLoadGlyph(cp);
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
        float totalTextH = getLineHeight() * lineWidths.size();
        float ascent = atlasManager_->getAscent() * s;
        switch (v) {
            case Direction::Top:      offsetY = 0; break;
            case Direction::Baseline: offsetY = -ascent; break;
            case Direction::Center:   offsetY = -totalTextH / 2; break;
            case Direction::Bottom:   offsetY = -totalTextH; break;
            default: break;
        }

        int currentLine = 0;
        float cursorX = x + lineOffsetX(0);
        float cursorY = y + offsetY + ascent;

        for (size_t i = 0; i < text.size(); ) {
            uint32_t cp = decodeUTF8(text, i);
            if (cp == '\n') {
                currentLine++;
                cursorX = x + lineOffsetX(currentLine);
                cursorY += getLineHeight();
                continue;
            }
            if (cp == '\t') {
                cursorX += atlasManager_->getSpaceAdvance() * s * 4;
                continue;
            }
            const GlyphInfo* g = atlasManager_->getOrLoadGlyph(cp);
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
            const AtlasState& atlas = atlasManager_->getAtlas(atlasIdx);
            if (!atlas.isTextureValid()) continue;

            // Use FBO blend pipeline when inside FBO pass (font pipeline has
            // dst_factor_alpha=ZERO which destroys background alpha).
            if (internal::inFboPass && internal::currentFboBlendPipeline.id != 0) {
                sgl_load_pipeline(internal::currentFboBlendPipeline);
            } else {
                sgl_load_pipeline(pipeline_);
            }
            sgl_enable_texture();
            sgl_texture(atlas.getView(), sampler_);

            Color col = getDefaultContext().getColor();
            sgl_c4f(col.r, col.g, col.b, col.a);

            sgl_begin_quads();

            for (const PlacedGlyph& pg : placed) {
                const GlyphInfo* g = atlasManager_->getOrLoadGlyph(pg.codepoint);
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
        const float asc = atlasManager_->getAscent() * s;
        const float colSpacing = getLineHeight();
        const float cellH = em;  // CJK vertical advance per cell

        // -------- Tokenize --------
        std::vector<VTok_> toks;
        toks.reserve(text.size());
        for (size_t i = 0; i < text.size(); ) {
            uint32_t cp = decodeUTF8(text, i);
            if (cp == '\n') { toks.push_back({VTokKind_::Newline, {}, 0}); continue; }
            if (cp == '\t') continue;  // ignored in vertical for now
            if (isAsciiDigit(cp)) {
                if (toks.empty() || toks.back().kind != VTokKind_::DigitRun)
                    toks.push_back({VTokKind_::DigitRun, {}, 0});
                toks.back().cps.push_back(cp);
            } else if (isAsciiLetter(cp)) {
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
                uint32_t vcp = getVerticalCodepoint(t.cp);
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
                const GlyphInfo* g = atlasManager_->getOrLoadGlyph(cp);
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

        for (const auto& t : toks) {
            if (t.kind == VTokKind_::Newline) {
                colIdx++;
                colX       -= colSpacing;
                colCenterX  = colX - em / 2.f;
                colY        = colYStart(colIdx);
                continue;
            }

            if (t.kind == VTokKind_::Single) {
                const uint32_t cp = t.cp;
                const VertOrient vo = getVerticalOrientation(cp);
                const uint32_t vcp = getVerticalCodepoint(cp);
                const bool hasVert = (vcp != 0 && atlasManager_->fontHasGlyph(vcp));

                if (vo == VertOrient::U
                    || (vo == VertOrient::Tu)
                    || (vo == VertOrient::Tr && hasVert)) {
                    // Upright path.
                    const uint32_t useCp = hasVert ? vcp : cp;
                    const GlyphInfo* g = atlasManager_->getOrLoadGlyph(useCp);
                    if (g && g->isValid()) {
                        float drawX = colCenterX - g->getAdvance() * s / 2.f;
                        float baselineY = colY + asc;
                        // Fallback offset for Tu without vertical form.
                        if (vo == VertOrient::Tu && !hasVert) {
                            VertOffset off = getVerticalPunctOffset(cp);
                            drawX     += off.dx * em;
                            baselineY += off.dy * em;
                        }
                        visitor(PlacedGlyph{useCp, drawX, baselineY, 0.f, 0.f, 0.f, 1.f});
                    }
                } else {
                    // Rotate 90° CW around cell center.
                    const GlyphInfo* g = atlasManager_->getOrLoadGlyph(cp);
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
                        const GlyphInfo* g = atlasManager_->getOrLoadGlyph(cp);
                        if (!g || !g->isValid()) continue;
                        visitor(PlacedGlyph{cp, cursorX, baselineY, QUARTER_TAU, cx, cy, 1.f});
                        cursorX += g->getAdvance() * s;
                    }
                    colY += rw;
                    break;
                }
                case TcyMode::Upright: {
                    for (uint32_t cp : t.cps) {
                        const GlyphInfo* g = atlasManager_->getOrLoadGlyph(cp);
                        if (g && g->isValid()) {
                            float drawX = colCenterX - g->getAdvance() * s / 2.f;
                            float baselineY = colY + asc;
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
                    float baselineY = colY + asc;
                    for (uint32_t cp : t.cps) {
                        const GlyphInfo* g = atlasManager_->getOrLoadGlyph(cp);
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
            case KinsokuLevel::PunctuationOnly: return isPunctuationOnlyLineStart(cp);
            case KinsokuLevel::Standard:        return isLineStartProhibited(cp);
        }
        return false;
    }
    bool kinsokuLineEnd(uint32_t cp) const {
        switch (kinsokuLevel_) {
            case KinsokuLevel::Off:             return false;
            case KinsokuLevel::PunctuationOnly: return false;  // no opening bracket subset for puncts-only
            case KinsokuLevel::Standard:        return isLineEndProhibited(cp);
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
                    const GlyphInfo* g = atlasManager_->getOrLoadGlyph(cp);
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
            const bool aCjk = isCjkChar(a);
            const bool bCjk = isCjkChar(b);
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
            if (isAsciiDigit(cp)) {
                if (!toks.empty() && toks.back().kind == TK::Digit)
                    toks.back().byteEnd = i;
                else
                    toks.push_back({TK::Digit, before, i, 0});
                continue;
            }
            if (isAsciiLetter(cp)) {
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
                const GlyphInfo* g = atlasManager_->getOrLoadGlyph(cp);
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

            const GlyphInfo* g = atlasManager_->getOrLoadGlyph(codepoint);
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

    const AtlasState* getAtlas(size_t index) const {
        return atlasManager_ ? &atlasManager_->getAtlas(index) : nullptr;
    }

    // Get shared sampler (for debug atlas rendering)
    sg_sampler getSampler() { initResources(); return sampler_; }

    size_t getLoadedGlyphCount() const {
        return atlasManager_ ? atlasManager_->getLoadedGlyphCount() : 0;
    }

    static size_t getTotalCacheMemoryUsage() {
        return SharedFontCache::getInstance().getTotalMemoryUsage();
    }

private:
    std::shared_ptr<FontAtlasManager> atlasManager_;
    FontCacheKey cacheKey_;
    float dpiScale_ = 1.0f;    // DPI scale at load time (physical/logical ratio)
    int logicalSize_ = 0;      // User-requested font size (logical pixels)

    // Shared GPU resources
    static inline sg_sampler sampler_ = {};
    static inline sgl_pipeline pipeline_ = {};
    static inline bool resourcesInitialized_ = false;

    void initResources() {
        if (resourcesInitialized_) return;

        sg_sampler_desc smp_desc = {};
        smp_desc.min_filter = SG_FILTER_LINEAR;
        smp_desc.mag_filter = SG_FILTER_LINEAR;
        smp_desc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
        smp_desc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
        sampler_ = sg_make_sampler(&smp_desc);

        // Alpha blend pipeline
        sg_pipeline_desc pip_desc = {};
        pip_desc.colors[0].blend.enabled = true;
        pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
        pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        pip_desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
        pip_desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ZERO;
        pipeline_ = sgl_make_pipeline(&pip_desc);

        resourcesInitialized_ = true;
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
