// Internal fixture access: CPU-only recordings retain the same page shared_ptr
// as production RenderTarget::fontAtlasUploads, without requiring sg_alloc_view.
namespace trussc::internal {
struct FontAtlasCompactionTestAccess {
    static size_t placementCount(const GlyphBatch& batch) { return batch.placements_.size(); }
    static size_t placementCapacity(const GlyphBatch& batch) { return batch.placements_.capacity(); }
    static void seedMixed(FontAtlasManager& manager, int side = 30, bool mainLayout = false) {
        manager.atlases_.clear();
        manager.glyphs_.clear();
        for (int i = 0; i < 4; ++i) {
            auto page = std::make_shared<AtlasState>();
            page->width_ = page->height_ = 256;
            page->sealed_ = page->frozen_ = true;
            page->textureDirty_ = page->mipmaps_ = true;
            page->pixels_.resize(256 * 256, 0);
            for (int j = 0; j < 2; ++j) {
                const int shape = j == 0 ? i : 7 - i;
                const int gap = mainLayout ? 2 : manager.glyphGap(side);
                const int x = 16 + manager.alignGlyphOffset(side + gap) * j, y = 16;
                GlyphInfo glyph{};
                glyph.valid_ = true;
                glyph.atlasIndex_ = i;
                glyph.width_ = glyph.height_ = float(side);
                glyph.advance_ = float(side + 2);
                glyph.u0_ = float(x) / 256;
                glyph.v0_ = float(y) / 256;
                glyph.u1_ = float(x + side) / 256;
                glyph.v1_ = float(y + side) / 256;
                manager.glyphs_[0x4E00u + shape] = glyph;
                for (int row = 0; row < side; ++row) for (int col = 0; col < side; ++col) {
                    bool ink = true;
                    // Solid beside a one-texel hollow square; asymmetric top
                    // marks and an L distinguish storage rows independently.
                    if (shape == 7) ink = row == 0 || row == side - 1 || col == 0 || col == side - 1;
                    if (shape == 2) ink = row < 3;
                    if (shape == 3) ink = row < 3 || col < 3;
                    page->pixels_[size_t(y + row) * 256 + x + col] = ink ? 255 : 0;
                }
            }
            manager.atlases_.push_back(std::move(page));
        }
    }
    static auto retain(FontAtlasManager& manager, size_t index) { return manager.atlases_[index]; }
    static void sealLast(FontAtlasManager& manager) { manager.atlases_.back()->sealed_ = true; }
    static void seed(FontAtlasManager& manager, int count, int side, int cap) {
        manager.maxAtlasSize_ = cap;
        const size_t base = manager.atlases_.size();
        const uint32_t firstCodepoint = uint32_t(manager.glyphs_.size());
        for (int i = 0; i < count; ++i) {
            auto page = std::make_shared<AtlasState>();
            page->width_ = page->height_ = side;
            page->sealed_ = page->frozen_ = true;
            page->textureDirty_ = page->mipmaps_ = true;
            page->pixels_.resize(size_t(side) * side, 0);
            page->pixels_[size_t(16) * side + 16] = uint8_t(firstCodepoint + i + 1);
            manager.atlases_.push_back(page);
            GlyphInfo glyph{};
            glyph.valid_ = true;
            glyph.atlasIndex_ = base + size_t(i);
            glyph.u0_ = glyph.v0_ = 16.0f / side;
            // Leave room for size-dependent gaps in the small capped-page checks.
            glyph.u1_ = glyph.v1_ = float(side - 48) / side;
            manager.glyphs_[firstCodepoint + uint32_t(i)] = glyph;
        }
    }
    static void addEmptyGlyphs(FontAtlasManager& manager, int count) {
        for (int i = 0; i < count; ++i) {
            GlyphInfo glyph{};
            glyph.valid_ = true;
            manager.glyphs_[0x10000u + uint32_t(i)] = glyph;
        }
    }
    static void seedBenchmark(FontAtlasManager& manager, int cap) {
        manager.maxAtlasSize_ = cap;
        uint32_t codepoint = 0;
        for (int i = 0; i < 90; ++i) {
            auto page = std::make_shared<AtlasState>();
            page->width_ = page->height_ = 2048;
            page->sealed_ = page->frozen_ = true;
            page->textureDirty_ = page->mipmaps_ = true;
            page->pixels_.resize(size_t(2048) * 2048, 0);
            // Fifty six-glyph pages and forty five-glyph pages: 500 large
            // coverage rectangles in the legacy 90-page storage workload.
            const int count = i < 50 ? 6 : 5;
            for (int j = 0; j < count; ++j, ++codepoint) {
                const int x = 16 + (j % 3) * 624, y = 16 + (j / 3) * 720;
                for (int row = 0; row < 704; ++row)
                    std::fill_n(page->pixels_.data() + size_t(y + row) * 2048 + x,
                        608, uint8_t(1 + (codepoint * 53) % 255));
                GlyphInfo glyph{};
                glyph.valid_ = true;
                glyph.atlasIndex_ = size_t(i);
                glyph.u0_ = float(x) / 2048;
                glyph.v0_ = float(y) / 2048;
                glyph.u1_ = float(x + 608) / 2048;
                glyph.v1_ = float(y + 704) / 2048;
                manager.glyphs_[codepoint] = glyph;
            }
            manager.atlases_.push_back(std::move(page));
        }
    }

};
}
