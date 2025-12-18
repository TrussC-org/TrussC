#pragma once

// =============================================================================
// tcHasTexture.h - テクスチャを持つオブジェクトのインターフェース
// =============================================================================

// このファイルは TrussC.h からインクルードされる
// Texture クラスが先にインクルードされている必要がある

#include <filesystem>

namespace trussc {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// HasTexture - テクスチャを持つオブジェクトの基底クラス
// ---------------------------------------------------------------------------
class HasTexture {
public:
    virtual ~HasTexture() = default;

    // === テクスチャアクセス（純粋仮想） ===

    virtual Texture& getTexture() = 0;
    virtual const Texture& getTexture() const = 0;

    // === 状態 ===

    virtual bool hasTexture() const {
        return getTexture().isAllocated();
    }

    // === 描画（デフォルト実装） ===

    virtual void draw(float x, float y) const {
        if (hasTexture()) {
            getTexture().draw(x, y);
        }
    }

    virtual void draw(float x, float y, float w, float h) const {
        if (hasTexture()) {
            getTexture().draw(x, y, w, h);
        }
    }

    // === テクスチャ設定の委譲 ===

    void setMinFilter(TextureFilter filter) {
        getTexture().setMinFilter(filter);
    }

    void setMagFilter(TextureFilter filter) {
        getTexture().setMagFilter(filter);
    }

    void setFilter(TextureFilter filter) {
        getTexture().setFilter(filter);
    }

    TextureFilter getMinFilter() const {
        return getTexture().getMinFilter();
    }

    TextureFilter getMagFilter() const {
        return getTexture().getMagFilter();
    }

    void setWrapU(TextureWrap wrap) {
        getTexture().setWrapU(wrap);
    }

    void setWrapV(TextureWrap wrap) {
        getTexture().setWrapV(wrap);
    }

    void setWrap(TextureWrap wrap) {
        getTexture().setWrap(wrap);
    }

    TextureWrap getWrapU() const {
        return getTexture().getWrapU();
    }

    TextureWrap getWrapV() const {
        return getTexture().getWrapV();
    }

    // === 保存（サブクラスでオーバーライド） ===

    // テクスチャ内容をファイルに保存
    // Image: 直接 pixels を保存（コピー不要）
    // Fbo: テクスチャからピクセルを読み取って保存
    virtual bool save(const fs::path& path) const {
        (void)path;
        return false;  // デフォルトは未実装
    }
};

} // namespace trussc
