#pragma once

// =============================================================================
// tcHasTexture.h - Interface for objects that have a texture
// =============================================================================

// This file is included from TrussC.h
// Texture class must be included first

#include <filesystem>

namespace trussc {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// HasTexture - Base class for objects that have a texture
// ---------------------------------------------------------------------------
class HasTexture {
public:
    virtual ~HasTexture() = default;

    // === Texture access (pure virtual) ===

    virtual Texture& getTexture() = 0;
    virtual const Texture& getTexture() const = 0;

    // === State ===

    virtual bool hasTexture() const {
        return getTexture().isAllocated();
    }

    // === Draw (default implementation) ===

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

    // === Texture setting delegation ===

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

    // === Save (override in subclass) ===

    // Save texture contents to file
    // Image: saves pixels directly (no copy needed)
    // Fbo: reads pixels from texture and saves
    virtual bool save(const fs::path& path) const {
        (void)path;
        return false;  // Default is not implemented
    }
};

} // namespace trussc
