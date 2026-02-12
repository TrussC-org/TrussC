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
#include "sokol/sokol_gl.h"

// stb_truetype
#include "stb/stb_truetype.h"

#include "../utils/tcLog.h"
#include "../types/tcDirection.h"
#include "../types/tcRectangle.h"
#include "../../tcMath.h"  // Vec2

// ---------------------------------------------------------------------------
// System font paths - use these for cross-platform font loading
// ---------------------------------------------------------------------------
#ifdef __EMSCRIPTEN__
    // Web: Use Google Fonts via jsDelivr CDN (async load)
    #define TC_FONT_SANS  "https://cdn.jsdelivr.net/fontsource/fonts/noto-sans@latest/latin-400-normal.ttf"
    #define TC_FONT_SERIF "https://cdn.jsdelivr.net/fontsource/fonts/noto-serif@latest/latin-400-normal.ttf"
    #define TC_FONT_MONO  "https://cdn.jsdelivr.net/fontsource/fonts/noto-sans-mono@latest/latin-400-normal.ttf"
#elif defined(_WIN32)
    // Windows
    #define TC_FONT_SANS  "C:/Windows/Fonts/segoeui.ttf"
    #define TC_FONT_SERIF "C:/Windows/Fonts/times.ttf"
    #define TC_FONT_MONO  "C:/Windows/Fonts/consola.ttf"
#elif defined(__APPLE__)
    // macOS
    #define TC_FONT_SANS  "/System/Library/Fonts/Helvetica.ttc"
    #define TC_FONT_SERIF "/System/Library/Fonts/Times.ttc"
    #define TC_FONT_MONO  "/System/Library/Fonts/Menlo.ttc"
#else
    // Linux
    #define TC_FONT_SANS  "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
    #define TC_FONT_SERIF "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf"
    #define TC_FONT_MONO  "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
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
            for (auto& atlas : atlases_) {
                if (atlas.textureValid_) {
                    sg_destroy_view(atlas.view_);
                    sg_destroy_image(atlas.texture_);
                }
            }
        }
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

    // -------------------------------------------------------------------------
    // Get texture
    // -------------------------------------------------------------------------
    void ensureTexturesUpdated() {
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
    static constexpr int MAX_ATLAS_SIZE = 4096;
    static constexpr int GLYPH_PADDING = 2;

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

        if (newWidth > MAX_ATLAS_SIZE || newHeight > MAX_ATLAS_SIZE) {
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

        // Recreate GPU texture
        if (atlas.textureValid_) {
            sg_destroy_view(atlas.view_);
            sg_destroy_image(atlas.texture_);
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
            atlas.currentX_ = GLYPH_PADDING;
            atlas.currentY_ += atlas.rowHeight_ + GLYPH_PADDING;
            atlas.rowHeight_ = 0;
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
            return true;
        }

        return false;
    }

    void updateAtlasTexture(AtlasState& atlas) {
        // Skip if already updated this frame
        uint64_t currentFrame = sapp_frame_count();
        if (atlas.textureValid_ && atlas.lastUpdateFrame_ == currentFrame) {
            return;
        }

        // Destroy existing resources
        if (atlas.textureValid_) {
            sg_destroy_view(atlas.view_);
            sg_destroy_image(atlas.texture_);
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
    bool load(const std::string& path, int size) {
        cacheKey_.fontPath = path;
        cacheKey_.fontSize = size;

        // Create sampler and pipeline if not yet
        if (!resourcesInitialized_) {
            initResources();
        }

        // Check if URL
        if (isUrl(path)) {
#ifdef __EMSCRIPTEN__
            // Async load - returns immediately, font available after fetch completes
            loadFromUrlAsync(path, size);
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
        drawStringInternal(text, x, y, h, v);
    }

    virtual void drawString(const std::string& text, float x, float y,
                            Direction h, Direction v) const {
        drawStringInternal(text, x, y, h, v);
    }

    // -------------------------------------------------------------------------
    // Internal drawing implementation
    // -------------------------------------------------------------------------
protected:
    void drawStringInternal(const std::string& text, float x, float y,
                            Direction h, Direction v) const {
        if (!atlasManager_ || text.empty()) return;

        // Load required glyphs
        for (size_t i = 0; i < text.size(); ) {
            uint32_t codepoint = decodeUTF8(text, i);
            if (codepoint != '\n' && codepoint != '\t') {
                atlasManager_->getOrLoadGlyph(codepoint);
            }
        }

        // Update textures
        atlasManager_->ensureTexturesUpdated();

        // Calculate alignment offset
        Vec2 offset = calcAlignOffset(text, h, v);

        // Draw per atlas
        size_t atlasCount = atlasManager_->getAtlasCount();
        for (size_t atlasIdx = 0; atlasIdx < atlasCount; atlasIdx++) {
            const AtlasState& atlas = atlasManager_->getAtlas(atlasIdx);
            if (!atlas.isTextureValid()) continue;

            sgl_load_pipeline(pipeline_);
            sgl_enable_texture();
            sgl_texture(atlas.getView(), sampler_);

            // Get and set current draw color
            Color col = getDefaultContext().getColor();
            sgl_c4f(col.r, col.g, col.b, col.a);

            sgl_begin_quads();

            float cursorX = x + offset.x;
            float cursorY = y + offset.y + atlasManager_->getAscent();

            for (size_t i = 0; i < text.size(); ) {
                uint32_t codepoint = decodeUTF8(text, i);

                if (codepoint == '\n') {
                    cursorX = x;
                    cursorY += getLineHeight();
                    continue;
                }
                if (codepoint == '\t') {
                    cursorX += atlasManager_->getSpaceAdvance() * 4;
                    continue;
                }

                const GlyphInfo* g = atlasManager_->getOrLoadGlyph(codepoint);
                if (!g || !g->isValid() || g->getAtlasIndex() != atlasIdx) {
                    if (g) cursorX += g->getAdvance();
                    continue;
                }

                if (g->getWidth() > 0 && g->getHeight() > 0) {
                    float gx = cursorX + g->getXoff();
                    float gy = cursorY + g->getYoff();

                    sgl_v2f_t2f(gx, gy, g->getU0(), g->getV0());
                    sgl_v2f_t2f(gx + g->getWidth(), gy, g->getU1(), g->getV0());
                    sgl_v2f_t2f(gx + g->getWidth(), gy + g->getHeight(), g->getU1(), g->getV1());
                    sgl_v2f_t2f(gx, gy + g->getHeight(), g->getU0(), g->getV1());
                }

                cursorX += g->getAdvance();
            }

            sgl_end();
            sgl_disable_texture();
            internal::restoreCurrentPipeline();
        }
    }

public:
    // -------------------------------------------------------------------------
    // Metrics (virtual - customizable in subclass)
    // -------------------------------------------------------------------------
    virtual float getWidth(const std::string& text) const {
        if (!atlasManager_) return 0;

        float width = 0;
        float maxWidth = 0;

        for (size_t i = 0; i < text.size(); ) {
            uint32_t codepoint = decodeUTF8(text, i);

            if (codepoint == '\n') {
                if (width > maxWidth) maxWidth = width;
                width = 0;
                continue;
            }
            if (codepoint == '\t') {
                width += atlasManager_->getSpaceAdvance() * 4;
                continue;
            }

            const GlyphInfo* g = atlasManager_->getOrLoadGlyph(codepoint);
            if (g && g->isValid()) {
                width += g->getAdvance();
            }
        }

        return (width > maxWidth) ? width : maxWidth;
    }

    // Kept for backward compatibility
    float stringWidth(const std::string& text) const { return getWidth(text); }

    virtual float getHeight(const std::string& text) const {
        if (!atlasManager_) return 0;

        int lines = 1;
        for (char c : text) {
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
        return atlasManager_ ? atlasManager_->getLineHeight() : 0;
    }

    // Get font's default line height (unaffected by setLineHeight)
    float getDefaultLineHeight() const {
        return atlasManager_ ? atlasManager_->getLineHeight() : 0;
    }

    virtual float getAscent() const {
        return atlasManager_ ? atlasManager_->getAscent() : 0;
    }

    virtual float getDescent() const {
        return atlasManager_ ? atlasManager_->getDescent() : 0;
    }

    int getSize() const {
        return atlasManager_ ? atlasManager_->getFontSize() : 0;
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

public:
    // -------------------------------------------------------------------------
    // Memory info
    // -------------------------------------------------------------------------
    size_t getMemoryUsage() const {
        return atlasManager_ ? atlasManager_->getMemoryUsage() : 0;
    }

    size_t getLoadedGlyphCount() const {
        return atlasManager_ ? atlasManager_->getLoadedGlyphCount() : 0;
    }

    static size_t getTotalCacheMemoryUsage() {
        return SharedFontCache::getInstance().getTotalMemoryUsage();
    }

private:
    std::shared_ptr<FontAtlasManager> atlasManager_;
    FontCacheKey cacheKey_;

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
