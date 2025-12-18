#pragma once

// =============================================================================
// TrussC TrueType Font
// stb_truetype ベースの TrueType フォントレンダリング
//
// 設計: ofxTrueTypeFontLowRAM を参考
// - SharedFontCache: 同じフォント+サイズでアトラスを共有
// - FontAtlasManager: アトラス管理（複数アトラス対応、動的拡張）
// - Font: ユーザー向けクラス
//
// TODO: メモリ最適化
// - 現在 RGBA8 (4bytes/pixel) を使用
// - R8 (1byte/pixel) + カスタムシェーダーで 1/4 に削減可能
// - sokol_gfx を直接使用してシェーダーで swizzle する必要あり
// =============================================================================

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cstdint>
#include <fstream>
#include <functional>

// sokol ヘッダー
#include "sokol/sokol_app.h"
#include "sokol/sokol_gfx.h"
#include "sokol/sokol_gl.h"

// stb_truetype
#include "stb/stb_truetype.h"

#include "../utils/tcLog.h"
#include "../types/tcDirection.h"
#include "../types/tcRectangle.h"
#include "../../tcMath.h"  // Vec2

namespace trussc {

// ---------------------------------------------------------------------------
// フォントキャッシュキー（フォントパス + サイズ）
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
// グリフ情報
// ---------------------------------------------------------------------------
struct GlyphInfo {
    size_t atlasIndex;          // どのアトラスに入っているか
    float u0, v0, u1, v1;       // テクスチャ座標（正規化）
    float xoff, yoff;           // 描画オフセット
    float width, height;        // グリフサイズ（ピクセル）
    float advance;              // 次の文字への移動量
    bool valid = false;
};

// ---------------------------------------------------------------------------
// アトラス状態
// ---------------------------------------------------------------------------
struct AtlasState {
    int currentX = 0;           // 次のグリフを配置するX位置
    int currentY = 0;           // 次のグリフを配置するY位置
    int rowHeight = 0;          // 現在の行の高さ
    int width = 0;
    int height = 0;

    // GPU リソース
    sg_image texture = {};
    sg_view view = {};
    bool textureValid = false;
    bool textureDirty = false;
    uint64_t lastUpdateFrame = 0;  // 最後に更新したフレーム番号

    // CPU側ピクセルデータ（拡張・更新用）
    std::vector<uint8_t> pixels;  // RGBA
};

// ---------------------------------------------------------------------------
// フォントアトラス管理クラス
// 同じフォント+サイズで共有される
// ---------------------------------------------------------------------------
class FontAtlasManager {
public:
    FontAtlasManager() = default;
    ~FontAtlasManager() { cleanup(); }

    // コピー禁止
    FontAtlasManager(const FontAtlasManager&) = delete;
    FontAtlasManager& operator=(const FontAtlasManager&) = delete;

    // -------------------------------------------------------------------------
    // 初期化
    // -------------------------------------------------------------------------
    bool setup(const std::string& fontPath, int fontSize) {
        cleanup();

        // フォントファイルを読み込み
        std::ifstream file(fontPath, std::ios::binary | std::ios::ate);
        if (!file) {
            tcLogError() << "FontAtlasManager: failed to open " << fontPath;
            return false;
        }

        size_t fileSize = file.tellg();
        file.seekg(0, std::ios::beg);
        fontData_.resize(fileSize);
        if (!file.read(reinterpret_cast<char*>(fontData_.data()), fileSize)) {
            tcLogError() << "FontAtlasManager: failed to read " << fontPath;
            return false;
        }

        // stb_truetype で初期化
        if (!stbtt_InitFont(&fontInfo_, fontData_.data(), 0)) {
            tcLogError() << "FontAtlasManager: failed to init font " << fontPath;
            fontData_.clear();
            return false;
        }

        fontSize_ = fontSize;
        scale_ = stbtt_ScaleForPixelHeight(&fontInfo_, (float)fontSize);

        // フォントメトリクスを取得
        int ascent, descent, lineGap;
        stbtt_GetFontVMetrics(&fontInfo_, &ascent, &descent, &lineGap);
        ascent_ = ascent * scale_;
        descent_ = descent * scale_;
        lineGap_ = lineGap * scale_;

        // スペースの advance を取得
        int spaceIndex = stbtt_FindGlyphIndex(&fontInfo_, ' ');
        int advanceWidth, leftSideBearing;
        stbtt_GetGlyphHMetrics(&fontInfo_, spaceIndex, &advanceWidth, &leftSideBearing);
        spaceAdvance_ = advanceWidth * scale_;

        // 最初のアトラスを作成
        createNewAtlas();

        loaded_ = true;
        return true;
    }

    void cleanup() {
        // sokol がまだ有効な場合のみ GPU リソースを解放
        // （プログラム終了時は sokol がすでにシャットダウンしている可能性がある）
        if (sg_isvalid()) {
            for (auto& atlas : atlases_) {
                if (atlas.textureValid) {
                    sg_destroy_view(atlas.view);
                    sg_destroy_image(atlas.texture);
                }
            }
        }
        atlases_.clear();
        glyphs_.clear();
        fontData_.clear();
        loaded_ = false;
    }

    // -------------------------------------------------------------------------
    // グリフ取得（遅延ロード）
    // -------------------------------------------------------------------------
    const GlyphInfo* getOrLoadGlyph(uint32_t codepoint) {
        auto it = glyphs_.find(codepoint);
        if (it != glyphs_.end()) {
            return &it->second;
        }

        // グリフを追加
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
    // テクスチャ取得
    // -------------------------------------------------------------------------
    void ensureTexturesUpdated() {
        for (auto& atlas : atlases_) {
            if (atlas.textureDirty) {
                updateAtlasTexture(atlas);
            }
        }
    }

    size_t getAtlasCount() const { return atlases_.size(); }

    const AtlasState& getAtlas(size_t index) const { return atlases_[index]; }

    // -------------------------------------------------------------------------
    // メトリクス
    // -------------------------------------------------------------------------
    float getLineHeight() const { return ascent_ - descent_ + lineGap_; }
    float getAscent() const { return ascent_; }
    float getDescent() const { return descent_; }
    float getSpaceAdvance() const { return spaceAdvance_; }
    int getFontSize() const { return fontSize_; }

    // -------------------------------------------------------------------------
    // メモリ使用量
    // -------------------------------------------------------------------------
    size_t getMemoryUsage() const {
        size_t total = 0;
        for (const auto& atlas : atlases_) {
            total += atlas.pixels.size();
        }
        return total;
    }

    size_t getLoadedGlyphCount() const { return glyphs_.size(); }

private:
    static constexpr int INITIAL_ATLAS_SIZE = 256;
    static constexpr int MAX_ATLAS_SIZE = 4096;
    static constexpr int GLYPH_PADDING = 2;

    // フォントデータ
    std::vector<uint8_t> fontData_;
    stbtt_fontinfo fontInfo_ = {};
    int fontSize_ = 0;
    float scale_ = 0;
    float ascent_ = 0;
    float descent_ = 0;
    float lineGap_ = 0;
    float spaceAdvance_ = 0;

    // アトラス
    std::vector<AtlasState> atlases_;

    // グリフキャッシュ
    std::unordered_map<uint32_t, GlyphInfo> glyphs_;

    bool loaded_ = false;

    // -------------------------------------------------------------------------
    // アトラス管理
    // -------------------------------------------------------------------------
    size_t createNewAtlas() {
        AtlasState atlas;
        atlas.width = INITIAL_ATLAS_SIZE;
        atlas.height = INITIAL_ATLAS_SIZE;
        atlas.currentX = GLYPH_PADDING;
        atlas.currentY = GLYPH_PADDING;
        atlas.rowHeight = 0;
        atlas.pixels.resize(atlas.width * atlas.height * 4, 0);
        atlas.textureDirty = true;

        atlases_.push_back(std::move(atlas));
        return atlases_.size() - 1;
    }

    bool expandAtlas(size_t atlasIndex) {
        AtlasState& atlas = atlases_[atlasIndex];

        int newWidth = atlas.width * 2;
        int newHeight = atlas.height * 2;

        if (newWidth > MAX_ATLAS_SIZE || newHeight > MAX_ATLAS_SIZE) {
            return false;
        }

        tcLogVerbose() << "FontAtlasManager: expanding atlas " << atlasIndex
                       << " from " << atlas.width << "x" << atlas.height
                       << " to " << newWidth << "x" << newHeight;

        // 新しいバッファを作成
        std::vector<uint8_t> newPixels(newWidth * newHeight * 4, 0);

        // 古いデータをコピー
        for (int y = 0; y < atlas.height; y++) {
            memcpy(newPixels.data() + y * newWidth * 4,
                   atlas.pixels.data() + y * atlas.width * 4,
                   atlas.width * 4);
        }

        // UV座標を更新（このアトラスに属するグリフのみ）
        float scaleX = (float)atlas.width / newWidth;
        float scaleY = (float)atlas.height / newHeight;
        for (auto& pair : glyphs_) {
            GlyphInfo& g = pair.second;
            if (g.atlasIndex == atlasIndex) {
                g.u0 *= scaleX;
                g.u1 *= scaleX;
                g.v0 *= scaleY;
                g.v1 *= scaleY;
            }
        }

        atlas.pixels = std::move(newPixels);
        atlas.width = newWidth;
        atlas.height = newHeight;

        // GPUテクスチャを再作成
        if (atlas.textureValid) {
            sg_destroy_view(atlas.view);
            sg_destroy_image(atlas.texture);
            atlas.textureValid = false;
        }
        atlas.textureDirty = true;

        return true;
    }

    bool addGlyphToAtlas(uint32_t codepoint, GlyphInfo& outInfo) {
        // グリフをレンダリング
        int glyphIndex = stbtt_FindGlyphIndex(&fontInfo_, codepoint);

        // グリフのメトリクスを取得
        int advanceWidth, leftSideBearing;
        stbtt_GetGlyphHMetrics(&fontInfo_, glyphIndex, &advanceWidth, &leftSideBearing);

        int x0, y0, x1, y1;
        stbtt_GetGlyphBitmapBox(&fontInfo_, glyphIndex, scale_, scale_, &x0, &y0, &x1, &y1);

        int glyphWidth = x1 - x0;
        int glyphHeight = y1 - y0;

        // スペースなど幅0のグリフ
        if (glyphWidth <= 0 || glyphHeight <= 0) {
            outInfo.atlasIndex = 0;
            outInfo.u0 = outInfo.v0 = outInfo.u1 = outInfo.v1 = 0;
            outInfo.xoff = 0;
            outInfo.yoff = 0;
            outInfo.width = 0;
            outInfo.height = 0;
            outInfo.advance = advanceWidth * scale_;
            outInfo.valid = true;
            return true;
        }

        int paddedWidth = glyphWidth + GLYPH_PADDING;
        int paddedHeight = glyphHeight + GLYPH_PADDING;

        // 配置できるアトラスを探す
        size_t targetAtlas = atlases_.size();
        for (size_t i = 0; i < atlases_.size(); i++) {
            if (tryFitGlyph(i, paddedWidth, paddedHeight)) {
                targetAtlas = i;
                break;
            }
        }

        // どのアトラスにも入らない場合
        if (targetAtlas == atlases_.size()) {
            // 最後のアトラスを拡張してみる
            if (!atlases_.empty() && expandAtlas(atlases_.size() - 1)) {
                targetAtlas = atlases_.size() - 1;
            } else {
                // 新しいアトラスを作成
                targetAtlas = createNewAtlas();
            }

            // それでも入らなければ拡張を繰り返す
            while (!tryFitGlyph(targetAtlas, paddedWidth, paddedHeight)) {
                if (!expandAtlas(targetAtlas)) {
                    tcLogWarning() << "FontAtlasManager: cannot fit glyph for U+" << std::hex << codepoint << std::dec;
                    outInfo.valid = false;
                    return false;
                }
            }
        }

        AtlasState& atlas = atlases_[targetAtlas];

        // 行が溢れる場合は次の行へ
        if (atlas.currentX + paddedWidth > atlas.width) {
            atlas.currentX = GLYPH_PADDING;
            atlas.currentY += atlas.rowHeight + GLYPH_PADDING;
            atlas.rowHeight = 0;
        }

        int destX = atlas.currentX;
        int destY = atlas.currentY;

        // グリフをレンダリング（8bit grayscale）
        std::vector<uint8_t> glyphBitmap(glyphWidth * glyphHeight);
        stbtt_MakeGlyphBitmap(&fontInfo_,
                              glyphBitmap.data(),
                              glyphWidth, glyphHeight,
                              glyphWidth,  // stride
                              scale_, scale_,
                              glyphIndex);

        // アトラスにコピー（RGBA）
        for (int y = 0; y < glyphHeight; y++) {
            for (int x = 0; x < glyphWidth; x++) {
                int srcIdx = y * glyphWidth + x;
                int dstIdx = ((destY + y) * atlas.width + (destX + x)) * 4;
                uint8_t alpha = glyphBitmap[srcIdx];
                atlas.pixels[dstIdx + 0] = 255;    // R
                atlas.pixels[dstIdx + 1] = 255;    // G
                atlas.pixels[dstIdx + 2] = 255;    // B
                atlas.pixels[dstIdx + 3] = alpha;  // A
            }
        }

        // グリフ情報を設定
        outInfo.atlasIndex = targetAtlas;
        outInfo.u0 = (float)destX / atlas.width;
        outInfo.v0 = (float)destY / atlas.height;
        outInfo.u1 = (float)(destX + glyphWidth) / atlas.width;
        outInfo.v1 = (float)(destY + glyphHeight) / atlas.height;
        outInfo.xoff = (float)x0;
        outInfo.yoff = (float)y0;
        outInfo.width = (float)glyphWidth;
        outInfo.height = (float)glyphHeight;
        outInfo.advance = advanceWidth * scale_;
        outInfo.valid = true;

        // カーソルを進める
        atlas.currentX += paddedWidth;
        if (paddedHeight > atlas.rowHeight) {
            atlas.rowHeight = paddedHeight;
        }

        atlas.textureDirty = true;
        return true;
    }

    bool tryFitGlyph(size_t atlasIndex, int width, int height) {
        const AtlasState& atlas = atlases_[atlasIndex];

        // 現在の行に入るか
        if (atlas.currentX + width <= atlas.width) {
            if (atlas.currentY + height <= atlas.height) {
                return true;
            }
        }

        // 次の行に入るか
        int nextY = atlas.currentY + atlas.rowHeight + GLYPH_PADDING;
        if (nextY + height <= atlas.height) {
            return true;
        }

        return false;
    }

    void updateAtlasTexture(AtlasState& atlas) {
        // 同じフレームで既に更新済みならスキップ
        uint64_t currentFrame = sapp_frame_count();
        if (atlas.textureValid && atlas.lastUpdateFrame == currentFrame) {
            return;
        }

        // 既存のリソースを破棄
        if (atlas.textureValid) {
            sg_destroy_view(atlas.view);
            sg_destroy_image(atlas.texture);
            atlas.textureValid = false;
        }

        // immutable テクスチャとして新規作成（初期データ付き）
        // NOTE: 効率は良くないが、まず動作確認のため
        sg_image_desc img_desc = {};
        img_desc.width = atlas.width;
        img_desc.height = atlas.height;
        img_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
        // immutable (デフォルト) - 初期データを設定可能
        img_desc.data.mip_levels[0].ptr = atlas.pixels.data();
        img_desc.data.mip_levels[0].size = atlas.pixels.size();
        atlas.texture = sg_make_image(&img_desc);

        sg_view_desc view_desc = {};
        view_desc.texture.image = atlas.texture;
        atlas.view = sg_make_view(&view_desc);

        atlas.textureValid = true;
        atlas.lastUpdateFrame = currentFrame;
        atlas.textureDirty = false;
    }
};

// ---------------------------------------------------------------------------
// 共有フォントキャッシュ（シングルトン）
// ---------------------------------------------------------------------------
class SharedFontCache {
public:
    static SharedFontCache& getInstance() {
        static SharedFontCache instance;
        return instance;
    }

    // フォントアトラスを取得（なければ作成）
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

    // 特定のフォントを解放
    void release(const FontCacheKey& key) {
        cache_.erase(key);
    }

    // 全て解放
    void clear() {
        cache_.clear();
    }

    // 総メモリ使用量
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
// TrueType フォントクラス（ユーザー向け）
// 継承可能: 独自フォントシステムを実装する場合は継承してオーバーライド
// ---------------------------------------------------------------------------
class Font {
public:
    Font() = default;
    virtual ~Font() = default;

    // コピー・ムーブ可能（shared_ptr なので軽量）
    Font(const Font&) = default;
    Font& operator=(const Font&) = default;
    Font(Font&&) = default;
    Font& operator=(Font&&) = default;

    // -------------------------------------------------------------------------
    // フォント読み込み
    // -------------------------------------------------------------------------
    bool load(const std::string& path, int size) {
        cacheKey_.fontPath = path;
        cacheKey_.fontSize = size;

        atlasManager_ = SharedFontCache::getInstance().getOrCreate(cacheKey_);
        if (!atlasManager_) {
            return false;
        }

        // サンプラーとパイプラインを作成（まだなければ）
        if (!resourcesInitialized_) {
            initResources();
        }

        return true;
    }

    bool isLoaded() const { return atlasManager_ != nullptr; }

    // -------------------------------------------------------------------------
    // アラインメント設定
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
    // 行高さ設定（改行時の間隔）
    // -------------------------------------------------------------------------
    void setLineHeight(float pixels) {
        lineHeight_ = pixels;
    }

    // em単位で設定（1.0 = フォント本来の行高さ、1.5 = 1.5倍）
    void setLineHeightEm(float multiplier) {
        lineHeight_ = getDefaultLineHeight() * multiplier;
    }

    void resetLineHeight() {
        lineHeight_ = 0;  // 0 = フォント本来の行高さを使用
    }

    // -------------------------------------------------------------------------
    // 文字列描画（virtual - 継承先でカスタム可能）
    // -------------------------------------------------------------------------
    virtual void drawString(const std::string& text, float x, float y) const {
        drawStringInternal(text, x, y, alignH_, alignV_);
    }

    virtual void drawString(const std::string& text, float x, float y,
                            Direction h, Direction v) const {
        drawStringInternal(text, x, y, h, v);
    }

    // -------------------------------------------------------------------------
    // 内部描画実装
    // -------------------------------------------------------------------------
protected:
    void drawStringInternal(const std::string& text, float x, float y,
                            Direction h, Direction v) const {
        if (!atlasManager_ || text.empty()) return;

        // 必要なグリフをロード
        for (size_t i = 0; i < text.size(); ) {
            uint32_t codepoint = decodeUTF8(text, i);
            if (codepoint != '\n' && codepoint != '\t') {
                atlasManager_->getOrLoadGlyph(codepoint);
            }
        }

        // テクスチャを更新
        atlasManager_->ensureTexturesUpdated();

        // アラインメントオフセットを計算
        Vec2 offset = calcAlignOffset(text, h, v);

        // アトラスごとに描画
        size_t atlasCount = atlasManager_->getAtlasCount();
        for (size_t atlasIdx = 0; atlasIdx < atlasCount; atlasIdx++) {
            const AtlasState& atlas = atlasManager_->getAtlas(atlasIdx);
            if (!atlas.textureValid) continue;

            sgl_load_pipeline(pipeline_);
            sgl_enable_texture();
            sgl_texture(atlas.view, sampler_);

            // 現在の描画色を取得して設定
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
                if (!g || !g->valid || g->atlasIndex != atlasIdx) {
                    if (g) cursorX += g->advance;
                    continue;
                }

                if (g->width > 0 && g->height > 0) {
                    float gx = cursorX + g->xoff;
                    float gy = cursorY + g->yoff;

                    sgl_v2f_t2f(gx, gy, g->u0, g->v0);
                    sgl_v2f_t2f(gx + g->width, gy, g->u1, g->v0);
                    sgl_v2f_t2f(gx + g->width, gy + g->height, g->u1, g->v1);
                    sgl_v2f_t2f(gx, gy + g->height, g->u0, g->v1);
                }

                cursorX += g->advance;
            }

            sgl_end();
            sgl_disable_texture();
            sgl_load_default_pipeline();
        }
    }

public:
    // -------------------------------------------------------------------------
    // メトリクス（virtual - 継承先でカスタム可能）
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
            if (g && g->valid) {
                width += g->advance;
            }
        }

        return (width > maxWidth) ? width : maxWidth;
    }

    // 後方互換性のため残す
    float stringWidth(const std::string& text) const { return getWidth(text); }

    virtual float getHeight(const std::string& text) const {
        if (!atlasManager_) return 0;

        int lines = 1;
        for (char c : text) {
            if (c == '\n') lines++;
        }
        return getLineHeight() * lines;
    }

    // テキストの境界ボックスを取得（左上基準）
    virtual Rectangle getBBox(const std::string& text) const {
        return Rectangle(0, 0, getWidth(text), getHeight(text));
    }

    virtual float getLineHeight() const {
        if (lineHeight_ > 0) return lineHeight_;
        return atlasManager_ ? atlasManager_->getLineHeight() : 0;
    }

    // フォント本来の行高さを取得（setLineHeight の影響を受けない）
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
    // アラインメントオフセット計算（継承先で利用可能）
    // -------------------------------------------------------------------------
    Vec2 calcAlignOffset(const std::string& text, Direction h, Direction v) const {
        float offsetX = 0;
        float offsetY = 0;

        // 水平オフセット
        float w = getWidth(text);
        switch (h) {
            case Direction::Left:   offsetX = 0; break;
            case Direction::Center: offsetX = -w / 2; break;
            case Direction::Right:  offsetX = -w; break;
            default: break;
        }

        // 垂直オフセット
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

    // 設定（protected - 継承先でアクセス可能）
    Direction alignH_ = Direction::Left;
    Direction alignV_ = Direction::Top;
    float lineHeight_ = 0;  // 0 = フォント本来の行高さを使用

public:
    // -------------------------------------------------------------------------
    // メモリ情報
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

    // 共有GPUリソース
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

        // アルファブレンドパイプライン
        sg_pipeline_desc pip_desc = {};
        pip_desc.colors[0].blend.enabled = true;
        pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
        pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        pip_desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
        pip_desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ZERO;
        pipeline_ = sgl_make_pipeline(&pip_desc);

        resourcesInitialized_ = true;
    }

    // UTF-8デコード（簡易版）
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
