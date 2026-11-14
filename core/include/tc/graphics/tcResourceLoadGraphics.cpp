// Resource preparation and publication are implemented in the host.
#include <TrussC.h>

namespace trussc {
namespace internal {

bool FontAtlasManager::prepareFile(const std::string& fontPath, int fontSize, int faceIndex, int atlasLimit) {
    maxAtlasSize_ = atlasLimit;
    cleanup();

    try {
        prepareReleaseStorage();
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
        // Same size limit as checkFontSfnt, checked before the buffer
        // is allocated.
        if ((uint64_t)end >= kMaxFontDataSize) {
            const std::string reason = std::string("FontAtlasManager: not a usable font (") + kTooLargeReason + "): ";
            internal::reportImportError("", reason.c_str(), fontPath.c_str());
            return false;
        }
        const size_t fileSize = (size_t)end;
        file.seekg(0, std::ios::beg);
        if (fileSize > 256u * 1024u * 1024u)
            logNotice("Font") << "loading a local font larger than 256 MiB";
        fontData_.resize(fileSize);
        if (!file.read(reinterpret_cast<char*>(fontData_.data()), fileSize)) {
            logError() << "FontAtlasManager: failed to read " << fontPath;
            cleanup();
            return false;
        }

        fontName_ = fontPath;
        if (!initFromFontData(fontSize, faceIndex, &path)) { cleanup(); return false; }
        return true;
    } catch (const std::bad_alloc&) {
        cleanup();
        internal::reportImportError("Font", "Out of memory while initializing font: ", fontPath.c_str());
        return false;
    } catch (const std::exception&) {
        return initializationFailed(fontPath.c_str());
    }
}

bool FontAtlasManager::setupFromMemory(const uint8_t* data, size_t size, int fontSize, int faceIndex) {
    maxAtlasSize_ = atlasLimit();
    cleanup();

    try {
        prepareReleaseStorage();
        if (!data && size > 0) {
            logError() << "FontAtlasManager: font data pointer is null";
            return false;
        }
        // Check the backend representation before copying memory input too.
        if (size >= kMaxFontDataSize) {
            internal::reportImportError("Font", kTooLargeReason);
            return false;
        }
        // Empty data is rejected by the sfnt check in initFromFontData.
        if (size) fontData_.assign(data, data + size);

        fontName_ = "font data in memory";
        if (!initFromFontData(fontSize, faceIndex)) { cleanup(); return false; }
        return true;
    } catch (const std::bad_alloc&) {
        cleanup();
        internal::reportImportError("Font", "Out of memory while initializing font");
        return false;
    } catch (const std::exception&) {
        return initializationFailed();
    }
}

void FontAtlasManager::warnIfGlyphsExceedPage() {
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

bool FontAtlasManager::initFromFontData(int fontSize, int fontIndex, const fs::path* sourcePath) {
    // The face index is checked against the number of faces in the data:
    // the font count of a collection, 1 for a single font.
    const uint64_t faces = faceCount(fontData_.data(), fontData_.size());
    if (faces > 0 && (fontIndex < 0 || (uint64_t)fontIndex >= faces)) {
        if (sourcePath) {
            char detail[160];
            std::snprintf(detail, sizeof(detail), "FontAtlasManager: face index %d is out of range (the font has %llu %s)",
                          fontIndex, static_cast<unsigned long long>(faces), faces == 1 ? "face" : "faces");
            internal::reportImportError("", detail, sourcePath);
        } else logError() << "FontAtlasManager: face index " << fontIndex
                   << " is out of range (the font has " << faces
                   << (faces == 1 ? " face)" : " faces)");
        fontData_.clear();
        return false;
    }

    // The sfnt structure is checked against the data size before the data
    // is given to stb_truetype.
    std::string reason;
    if (!checkFontSfnt(fontData_.data(), fontData_.size(), fontIndex, reason)) {
        if (sourcePath) {
            // Reason formatting remains inside prepareFile's recovery guard.
            reason.insert(0, "FontAtlasManager: not a usable font (");
            reason += ')';
            internal::reportImportError("", reason.c_str(), sourcePath);
        } else logError() << "FontAtlasManager: not a usable font (" << reason << ")";
        fontData_.clear();
        return false;
    }

    // Get font offset (required for .ttc files with multiple fonts)
    int offset = stbtt_GetFontOffsetForIndex(fontData_.data(), fontIndex);
    if (offset < 0) {
        if (sourcePath) {
            char detail[96];
            std::snprintf(detail, sizeof(detail), "FontAtlasManager: invalid font index %d", fontIndex);
            internal::reportImportError("", detail, sourcePath);
        } else logError() << "FontAtlasManager: invalid font index " << fontIndex;
        fontData_.clear();
        return false;
    }

    // Initialize with stb_truetype
    if (!stbtt_InitFont(&fontInfo_, fontData_.data(), offset)) {
        if (sourcePath) internal::reportImportError("", "FontAtlasManager: failed to init font", sourcePath);
        else logError() << "FontAtlasManager: failed to init font";
        fontData_.clear();
        return false;
    }

    // Usable glyph indices are below maxp numGlyphs, and for a CFF font
    // also below the number of CharStrings.
    glyphLimit_ = fontInfo_.numGlyphs;
    if (!fontInfo_.glyf) {
        const int count = cffCharStringsCount(fontInfo_.charstrings);
        if (count < 1) {
            if (sourcePath) internal::reportImportError("", "FontAtlasManager: not a usable font (CFF CharStrings INDEX cannot be read)", sourcePath);
            else logError() << "FontAtlasManager: not a usable font "
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

    // Create first atlas
    createNewAtlas();

    loaded_ = true;
    warnIfGlyphsExceedPage();
    return true;
}

size_t FontAtlasManager::createNewAtlas() {
    auto page = std::make_shared<AtlasState>();
    AtlasState& atlas = *page;
    atlas.width_ = INITIAL_ATLAS_SIZE;
    // Font receives physical pixels (Font multiplies logical size by DPI).
    // Use the face's bounding box width and the larger of its box/line height,
    // then include stb's prefilter margin, padding and aligned shelf strides.
    int bx0, by0, bx1, by1;
    stbtt_GetFontBoundingBox(&fontInfo_, &bx0, &by0, &bx1, &by1);
    const double rasterScale = double(scale_) * oversample_;
    const double boxWidth = std::ceil(bx1 * rasterScale) - std::floor(bx0 * rasterScale);
    const double boxHeight = std::ceil(by1 * rasterScale) - std::floor(by0 * rasterScale);
    const double height = std::max(boxHeight, std::ceil(double(ascent_ - descent_) * oversample_));
    const int gap = glyphGap(int(std::min(std::max(boxWidth, height) + oversample_ - 1, double(maxAtlasSize_))));
    const double cellWidth = std::max(16.0, std::ceil((boxWidth + oversample_ - 1 + gap) / 16) * 16);
    const double cellHeight = std::max(16.0, std::ceil((height + oversample_ - 1 + gap) / 16) * 16);
    const int inset = alignGlyphOffset(GLYPH_PADDING);
    // Reserve about 32 cells before packing; later CPU growth is still allowed
    // until the first recorded draw seals the page's dimensions.
    while (atlas.width_ * 2 <= maxAtlasSize_ &&
           std::floor((atlas.width_ - inset) / cellWidth) *
           std::floor((atlas.width_ - inset) / cellHeight) < 32) atlas.width_ *= 2;
    atlas.height_ = atlas.width_;
    atlas.currentX_ = inset;
    atlas.currentY_ = inset;
    atlas.rowHeight_ = 0;
    atlas.pixels_.resize((size_t)atlas.width_ * atlas.height_, 0);
    atlas.textureDirty_ = true;

    atlases_.push_back(std::move(page));
    return atlases_.size() - 1;
}

// Repack existing glyph bytes at aligned offsets. Zero-filled aligned cells
// keep the mip-averaging phase through L4 without retaining old page slack.
void FontAtlasManager::processCompaction() {
    try {
        // A font can be prepared before GPU setup, with a CPU fallback limit.
        const int mergeLimit = sg_isvalid() ? atlasLimit() : maxAtlasSize_;
        int cap = INITIAL_ATLAS_SIZE;
        while (cap <= mergeLimit / 2) cap *= 2;
        size_t candidates = 0;
        for (const auto& page : atlases_)
            if (page->frozen_ && page->width_ < cap) ++candidates;
        if (candidates < (compactRequested_ ? 2u : 4u)) {
            compactRequested_ = false;
            return;
        }
        compactRequested_ = false;
        std::vector<bool> eligible(atlases_.size(), false);
        for (size_t i = 0; i < atlases_.size(); ++i)
            eligible[i] = atlases_[i]->frozen_ && atlases_[i]->width_ < cap;
        struct Move { uint32_t codepoint; size_t source; int x, y, width, height; };
        std::vector<Move> moves;
        for (const auto& entry : glyphs_) {
            const auto& glyph = entry.second;
            if (!glyph.valid_ || !eligible[glyph.atlasIndex_]) continue;
            const auto& page = *atlases_[glyph.atlasIndex_];
            const int x = int(std::lround(glyph.u0_ * page.width_));
            const int y = int(std::lround(glyph.v0_ * page.height_));
            const int width = int(std::lround(glyph.u1_ * page.width_)) - x;
            const int height = int(std::lround(glyph.v1_ * page.height_)) - y;
            if (width == 0 || height == 0) continue; // Spaces need no atlas cell.
            moves.push_back({entry.first, glyph.atlasIndex_, x, y, width, height});
        }
        if (moves.empty()) return;
        std::sort(moves.begin(), moves.end(), [](const Move& a, const Move& b) {
            if (a.height != b.height) return a.height > b.height;
            if (a.width != b.width) return a.width > b.width;
            return a.codepoint < b.codepoint;
        });
        struct Placement { size_t move; int x, y; };
        auto pack = [&](int side, size_t first, size_t last, std::vector<Placement>& packed) {
            packed.clear();
            int x = alignGlyphOffset(GLYPH_PADDING), y = x, row = 0;
            size_t i = first;
            for (; i < last; ++i) {
                const auto& glyph = moves[i];
                const int gap = glyphGap(std::max(glyph.width, glyph.height));
                if (x + glyph.width + gap > side) {
                    x = alignGlyphOffset(GLYPH_PADDING);
                    y = alignGlyphOffset(y + row);
                    row = 0;
                }
                if (x + glyph.width + gap > side ||
                    y + glyph.height + gap > side) break;
                packed.push_back({i, x, y});
                x = alignGlyphOffset(x + glyph.width + gap);
                row = std::max(row, glyph.height + gap);
            }
            return i;
        };
        std::vector<std::shared_ptr<AtlasState>> replacement;
        auto glyphs = glyphs_;
        std::vector<Placement> packed;
        // Copy and UV remapping accept any source region, including a whole
        // frozen page. Packing policy only chooses those regions and offsets.
        auto copyRegion = [](const AtlasState& source, AtlasState& destination,
                             int x, int y, int width, int height, int dx, int dy) {
            for (int row = 0; row < height; ++row)
                std::memcpy(destination.pixels_.data() + size_t(dy + row) * destination.width_ + dx,
                    source.pixels_.data() + size_t(y + row) * source.width_ + x, width);
        };
        auto remapGlyph = [](GlyphInfo& glyph, const AtlasState& source,
                             const AtlasState& destination, size_t index, int dx, int dy) {
            glyph.atlasIndex_ = index;
            glyph.u0_ = (glyph.u0_ * source.width_ + dx) / destination.width_;
            glyph.u1_ = (glyph.u1_ * source.width_ + dx) / destination.width_;
            glyph.v0_ = (glyph.v0_ * source.height_ + dy) / destination.height_;
            glyph.v1_ = (glyph.v1_ * source.height_ + dy) / destination.height_;
        };
        size_t first = 0;
        while (first < moves.size()) {
            const size_t last = pack(cap, first, moves.size(), packed);
            if (last == first) return; // Preserve the old atlas if packing fails.
            int side = INITIAL_ATLAS_SIZE;
            while (side < cap && pack(side, first, last, packed) != last) side *= 2;
            if (pack(side, first, last, packed) != last) return;
            auto page = std::make_shared<AtlasState>();
            page->width_ = page->height_ = side;
            page->pixels_.resize(size_t(side) * side, 0);
            page->sealed_ = page->frozen_ = page->compacted_ = true;
            page->mipmaps_ = wantMipmaps_;
            for (const auto& destination : packed) {
                const auto& move = moves[destination.move];
                const auto& source = *atlases_[move.source];
                copyRegion(source, *page, move.x, move.y, move.width, move.height, destination.x, destination.y);
                auto& glyph = glyphs.at(move.codepoint);
                remapGlyph(glyph, source, *page, replacement.size(), destination.x - move.x, destination.y - move.y);
            }
            replacement.push_back(std::move(page));
            first = last;
        }
        if (replacement.size() >= candidates) return; // Do not increase the page count.
        std::vector<size_t> indices(atlases_.size());
        for (size_t i = 0; i < atlases_.size(); ++i) if (!eligible[i]) {
            indices[i] = replacement.size();
            replacement.push_back(atlases_[i]);
        }
        for (const auto& entry : glyphs_) {
            const auto& glyph = entry.second;
            if (!glyph.valid_) continue;
            auto& destination = glyphs.at(entry.first);
            if (!eligible[glyph.atlasIndex_]) {
                destination.atlasIndex_ = indices[glyph.atlasIndex_];
            } else if (glyph.u0_ == glyph.u1_ || glyph.v0_ == glyph.v1_) {
                destination.atlasIndex_ = 0;
                destination.u0_ = destination.v0_ = destination.u1_ = destination.v1_ = 0;
            }
        }
        // Allocate all CPU state before GPU work and publish only on success.
        struct MergeScratch {
            sg_image image{};
            ~MergeScratch() { deferGpuDestroy(image); }
        } scratch;
        if (sg_isvalid()) for (auto& page : replacement)
            if (page->compacted_ && !page->textureValid_ && !uploadCompactedAtlas(*page, scratch.image)) return;
        atlases_.swap(replacement);
        glyphs_.swap(glyphs);
        ++atlasGeneration_;
    } catch (const std::bad_alloc&) {
        logWarning("Font") << "atlas compaction deferred: out of memory";
        compactRequested_ = true;
    }
}

// Reuse Fbo pass resources and GL whole-image LOD handling, with a linear
// coverage filter matching the CPU mip chain for immutable R8 snapshots.
bool FontAtlasManager::uploadCompactedAtlas(AtlasState& atlas, sg_image& scratch) {
    if (!sg_query_pixelformat(SG_PIXELFORMAT_R8).render) return false;
    Fbo::ensureSharedMip(TextureFormat::R8);
    auto& shared = Fbo::getSharedMip(TextureFormat::R8);
    if (!shared.ready) return false;
    if (!shared.coverageShader.id) {
        shared.coverageShader = sg_make_shader(tc_fbomip_coverage_downsample_shader_desc(sg_query_backend()));
        if (!internalShaderReady(shared.coverageShader, "Font atlas mipmap")) return false;
        sg_pipeline_desc pipeline{};
        pipeline.shader = shared.coverageShader;
        pipeline.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;
        pipeline.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;
        pipeline.primitive_type = SG_PRIMITIVETYPE_TRIANGLE_STRIP;
        shared.coverageRecipe.reset(PipelineKey::raw(PipelineRole::Mip, pipeline));
    }
    if (sg_query_shader_state(shared.coverageShader) != SG_RESOURCESTATE_VALID) return false;
    struct ScratchImages {
        sg_image upload{};
        ~ScratchImages() { deferGpuDestroy(upload); }
    } temporary;
    struct ScratchViews {
        sg_view source{}, attachment{};
        ~ScratchViews() { deferGpuDestroy(source); deferGpuDestroy(attachment); }
    };
    sg_image_desc desc{};
    desc.width = atlas.width_;
    desc.height = atlas.height_;
    desc.pixel_format = SG_PIXELFORMAT_R8;
    desc.data = {atlas.pixels_.data(), atlas.pixels_.size()};
    const sg_image upload = temporary.upload = sg_make_image(&desc);
    desc.data = {};
    if (atlas.mipmaps_) for (int side = atlas.width_; side > 1; side /= 2) ++desc.num_mipmaps;
    // The zero-initialized descriptor's default is one mip, but explicit
    // arithmetic needs to count mip 0 as well.
    ++desc.num_mipmaps;
    desc.usage.color_attachment = true;
    atlas.texture_ = sg_make_image(&desc);
    if (desc.num_mipmaps > 1) {
        auto scratchDesc = desc;
        scratchDesc.width = std::max(1, desc.width / 2);
        scratchDesc.height = std::max(1, desc.height / 2);
        --scratchDesc.num_mipmaps;
        const auto existing = sg_query_image_desc(scratch);
        if (existing.width != scratchDesc.width || existing.height != scratchDesc.height) {
            deferGpuDestroy(scratch);
            scratch = sg_make_image(&scratchDesc);
        }
    }
    bool valid = sg_query_image_state(upload) == SG_RESOURCESTATE_VALID &&
        sg_query_image_state(atlas.texture_) == SG_RESOURCESTATE_VALID &&
        (desc.num_mipmaps == 1 || sg_query_image_state(scratch) == SG_RESOURCESTATE_VALID);
    auto drawLevel = [&](sg_image source, int sourceLevel, sg_image destination, int level,
                         int width, int height, bool downsample) {
        ScratchViews views;
        sg_view_desc viewDesc{};
        viewDesc.texture.image = source;
        viewDesc.texture.mip_levels = {sourceLevel, 1};
        const sg_view sourceView = views.source = sg_make_view(&viewDesc);
        viewDesc = {};
        viewDesc.color_attachment.image = destination;
        viewDesc.color_attachment.mip_level = level;
        const sg_view attachment = views.attachment = sg_make_view(&viewDesc);
        bool ok = sg_query_view_state(sourceView) == SG_RESOURCESTATE_VALID &&
            sg_query_view_state(attachment) == SG_RESOURCESTATE_VALID;
        if (ok) {
            sg_pass pass{};
            pass.attachments.colors[0] = attachment;
            pass.action.colors[0].load_action = SG_LOADACTION_DONTCARE;
            TargetPassScope scope(pass, width, height, SG_PIXELFORMAT_R8);
            ok = scope.active() && Fbo::applyMipPipeline_(downsample ? shared.coverageRecipe : shared.blitRecipe);
            if (ok) {
                sg_bindings bindings{};
                bindings.vertex_buffers[0] = shared.vbuf;
                bindings.views[VIEW_tc_fbomip_srcTex] = sourceView;
                bindings.samplers[SMP_tc_fbomip_srcSmp] = shared.sampler;
                sg_apply_bindings(&bindings);
                Fbo::applyMipSource_(sourceLevel, true);
                sg_draw(0, 4, 1);
            }
        }
        return ok;
    };
    if (valid) valid = drawLevel(upload, 0, atlas.texture_, 0, atlas.width_, atlas.height_, false);
    for (int level = 1; valid && level < desc.num_mipmaps; ++level) {
        const int width = std::max(1, atlas.width_ >> level);
        const int height = std::max(1, atlas.height_ >> level);
        valid = drawLevel(atlas.texture_, level - 1, scratch, level - 1, width, height, true) &&
            drawLevel(scratch, level - 1, atlas.texture_, level, width, height, false);
    }
    if (valid) {
        sg_view_desc viewDesc{};
        viewDesc.texture.image = atlas.texture_;
        atlas.view_ = sg_make_view(&viewDesc);
        valid = sg_query_view_state(atlas.view_) == SG_RESOURCESTATE_VALID;
    }
    atlas.textureValid_ = valid;
    return valid;
}

std::shared_ptr<FontAtlasManager> SharedFontCache::publishPrepared(
        const FontCacheKey& key, std::shared_ptr<FontAtlasManager> candidate) {
    if (auto cached = get(key)) return cached;
    if (candidate) {
        probeResourceLoad("font.publish", {});
        cache_[key] = candidate;
    }
    return candidate;
}

} // namespace internal

void Image::loadAsync(const fs::path& path, bool mipmaps) {
    auto state = async_.begin(onLoad, [this](AsyncState& ready) {
        clearCommitted();
        if (ready.result) {
            pixels_ = std::move(ready.pixels);
            ready.result = finishLoad(ready.path, ready.mipmaps);
        }
    });
    state->path = internal::resolveLoadPath(path);
    state->localFile = internal::resourceLoadIsLocalPath(path, state->path);
    state->mipmaps = mipmaps;
    internal::probeResourceLoad("image.dispatch", state->path);
    internal::startExternalResourceLoad(state, [](auto* raw) {
        auto state = std::static_pointer_cast<AsyncState>(raw->shared_from_this());
        internal::probeResourceLoad("image.decode", state->path);
        if (state->cancelled.load()) return;
        state->result = state->pixels.load(state->path);
        internal::probeResourceLoad("image.ready", state->path);
        if (!state->cancelled.load()) internal::completeResourceLoad(state);
        internal::probeResourceLoad("image.queued", state->path);
    });
}

LoadResult Image::finishLoad(const fs::path& path, bool mipmaps) try {
    auto result = internal::checkImageSize(pixels_.getWidth(), pixels_.getHeight());
    if (!result) {
        logError("Image") << result.message << " (" << pixels_.getWidth()
                          << "x" << pixels_.getHeight() << "): "
                          << internal::pathToDisplayUtf8(internal::resolveLoadPath(path));
        clearCommitted();
        return result;
    }
    mipmaps_ = mipmaps;
    usage_ = TextureUsage::Immutable;
    if (!texture_.allocateImageLoad(pixels_, mipmaps)) {
        clearCommitted();
        return internal::imageDecodeOutOfMemory();
    }
    if (sg_isvalid() && !texture_.isAllocated()) {
        clearCommitted();
        return LoadResult::fail(LoadError::Unknown, "image texture upload failed");
    }
    return LoadResult::success();
} catch (const std::bad_alloc&) {
    clear();
    return internal::imageDecodeOutOfMemory();
}

float Font::loadDpiScale() {
    // Construction/callback ownership wins over the caller's drawing context.
    if (auto owner = internal::currentAppOwner()) {
        if (owner->context && !owner->retiring)
            return internal::windowDimensions(*owner->context).dpiScale;
        return 1.0f;
    }
    // CPU resource preparation without a Window uses logical pixels. With
    // Windows open, omission follows the same sole-Window rule as queries.
    if (internal::openWindows().empty()) return 1.0f;
    if (auto* window = internal::resolveWindow(true))
        return internal::windowDimensions(trussc::internal::WindowRequestAccess::context(*window)).dpiScale;
    return 1.0f;
}

void Font::loadAsync(const fs::path& nameOrPath, int size, int faceIndex) {
    cancelAsync();
    auto state = async_.begin(onLoad, [this](AsyncState& ready) { applyAsync(ready); });
    // Capture the same owner/sole-Window DPI as load(), before process delivery.
    state->dpi = loadDpiScale();
    state->size = size;
    const auto resolvedPath = internal::resolveLoadPath(nameOrPath);
    state->input = resolveInput(nameOrPath, resolvedPath, faceIndex);
    state->localFile = internal::resourceLoadIsLocalPath(nameOrPath,
        internal::utf8ToPath(state->input.path));
    state->key = {state->input.path, (int)(size * state->dpi + 0.5f),
                  state->input.face, mipmaps_, oversample_};
    state->limit = internal::FontAtlasManager::atlasLimit();
    // Cache access and sampler creation stay on main, including cache hits.
    initResources();
    if (isUrl(state->input.path)) {
#ifdef __EMSCRIPTEN__
        if (auto cached = internal::SharedFontCache::getInstance().get(state->key)) {
            state->candidate = std::move(cached);
            state->cached = true;
            state->result = LoadResult::success();
            internal::completeResourceLoad(state);
        } else {
            urlState_ = state;
            internal::startExternalResourceLoad(state, [](auto* raw) {
                auto state = std::static_pointer_cast<AsyncState>(raw->shared_from_this());
                state->backendPending = true;
                startUrlAsync(state);
            });
        }
#else
        logError() << "Font: URL loading only supported in WebAssembly";
        state->result = LoadResult::fail(LoadError::UnsupportedFormat,
            "URL loading only supported in WebAssembly: " + state->input.path);
        state->unsupportedUrl = true;
        internal::probeResourceLoad("font.unsupported-url", internal::utf8ToPath(state->input.path));
        internal::completeResourceLoad(state);
#endif
        return;
    }
    // Match load(): the data-file check precedes a cache lookup.
    if (state->input.isDataFile) {
        state->result = internal::checkLoadFile(internal::utf8ToPath(state->input.path), "Font");
        if (!state->result) {
            internal::probeResourceLoad("font.preflight-failure", internal::utf8ToPath(state->input.path));
            internal::completeResourceLoad(state);
            return;
        }
    }
    if (auto cached = internal::SharedFontCache::getInstance().get(state->key)) {
        state->candidate = std::move(cached);
        state->cached = true;
        state->result = LoadResult::success();
        internal::probeResourceLoad("font.cache-hit", internal::utf8ToPath(state->input.path));
        internal::completeResourceLoad(state);
        return;
    }
    internal::startExternalResourceLoad(state, [](auto* raw) {
        auto state = std::static_pointer_cast<AsyncState>(raw->shared_from_this());
        // Retain the probe path before preparation can exhaust memory. The
        // admitted host job already owns the allocation-free completion slot.
        fs::path path;
        try {
            path = internal::utf8ToPath(state->input.path);
            internal::probeResourceLoad("font.decode", path);
            if (state->cancelled.load()) return;
            state->candidate = std::make_shared<internal::FontAtlasManager>();
            state->candidate->setOversample(state->key.oversample);
            state->candidate->setMipmaps(state->key.mipmaps);
            if (!state->candidate->prepareFile(state->input.path, state->key.fontSize,
                    state->key.faceIndex, state->limit)) state->candidate.reset();
            state->result = classifyLoad(state->candidate != nullptr, state->input.name,
                                          state->input.path, state->input.face);
        } catch (const std::exception&) {
            state->candidate.reset();
            state->result.error = LoadError::DecodeFailed;
            state->result.message.clear();
            internal::reportImportError("Font", "asynchronous font initialization failed: ", state->input.path.c_str());
        }
        internal::probeResourceLoad("font.ready", path);
        if (!state->cancelled.load()) internal::completeResourceLoad(state);
        internal::probeResourceLoad("font.queued", path);
    });
}

Font::LoadInput Font::resolveInput(const fs::path& nameOrPath, const fs::path& resolvedPath, int faceIndex) {
    std::string nameStr = internal::pathToUtf8(nameOrPath);
    std::string actualPath = nameStr;
    int actualFace = faceIndex;
    bool isDataFile = false;
    if (!isUrl(nameStr)) {
        const fs::path filePath = resolvedPath;   // absolute paths pass through
        actualPath = internal::pathToUtf8(filePath);
        isDataFile = internal::isLoadFile(filePath);
        if (!isDataFile) {
            // Not a usable file path — try as a system font name. The OS
            // gives the file and the face inside it.
            internal::SystemFontFace resolved = internal::systemFontFace(nameStr);
            if (!resolved.path.empty()) {
                actualPath = internal::pathToUtf8(resolved.path);
                actualFace = resolved.index;
                if (faceIndex != 0) {
                    logWarning("Font") << "faceIndex " << faceIndex
                                       << " applies to font files; \"" << nameStr
                                       << "\" is a system font name and uses face "
                                       << actualFace;
                }
                logNotice("Font") << "Resolved \"" << nameStr
                                  << "\" → " << actualPath << " (face " << actualFace
                                  << ")";
            }
            // If resolution failed, keep the data-folder path: the load
            // error then names the input and where it was looked for.
        }
    }

    return {std::move(nameStr), std::move(actualPath), actualFace, isDataFile};
}

LoadResult Font::classifyLoad(bool loaded, const std::string& nameStr,
                               const std::string& actualPath, int actualFace) {
    if (loaded) return LoadResult::success();
    // Classify: the resolved path doesn't exist on disk -> the font
    // was never found (bad path or unresolvable system-font name);
    // otherwise the file opened but stb_truetype rejected it.
    std::error_code ec;
    if (!isUrl(actualPath) && !fs::exists(internal::utf8ToPath(actualPath), ec)) {
        std::string msg = "font not found: \"" + nameStr + "\"";
        if (actualPath != nameStr) msg += " (resolved to \"" + actualPath + "\")";
        return LoadResult::fail(LoadError::FileNotFound, msg);
    }
    std::string msg = "failed to init font: " + actualPath;
    if (actualFace != 0) msg += " (face " + std::to_string(actualFace) + ")";
    return LoadResult::fail(LoadError::DecodeFailed, msg);
}

void Font::applyAsync(AsyncState& ready) {
    dpiScale_ = ready.dpi;
    logicalSize_ = ready.size;
    if (!ready.result && !ready.unsupportedUrl) {
        atlasManager_.reset();
        internal::SharedFontCache::getInstance().releaseIfUnused(cacheKey_);
        if (ready.urlTimeout) cacheKey_.fontPath.clear();
        return;
    }
    auto previousKey = cacheKey_;
    const bool hadAtlas = atlasManager_ != nullptr;
    cacheKey_ = ready.key;
    if (ready.unsupportedUrl) return; // native URL load keeps the old atlas
    atlasManager_ = internal::SharedFontCache::getInstance().publishPrepared(ready.key, std::move(ready.candidate));
    if (hadAtlas && !(previousKey == cacheKey_)) {
        internal::SharedFontCache::getInstance().releaseIfUnused(previousKey);
    }
    if (atlasManager_) ready.result = LoadResult::success();
}

bool Texture::allocateImageLoad(const Pixels& pixels, bool mipmaps) {
    if (!sg_isvalid() || !mipmaps) {
        allocatePixels(pixels, TextureUsage::Immutable, mipmaps, nullptr);
        return !sg_isvalid() || bool(retirement_);
    }
    internal::ImageDecodeAllocationScope allocation;
    std::vector<uint8_t> blob;
    Pixels current;
    const Pixels* source = &pixels;
    for (;;) {
        const auto* bytes = static_cast<const uint8_t*>(source->getDataVoid());
        blob.insert(blob.end(), bytes, bytes + source->getTotalBytes());
        if (source->getWidth() == 1 && source->getHeight() == 1) break;
        Pixels next;
        if (!internal::ImageLoadAccess::mipLevel(next, *source)) return false;
        current = std::move(next);
        source = &current;
    }
    const sg_range data = {blob.data(), blob.size()};
    allocatePixels(pixels, TextureUsage::Immutable, true, &data);
    return bool(retirement_);
}

void Texture::allocatePixels(const Pixels& pixels, TextureUsage usage, bool mipmaps,
                    const sg_range* imageLoadMips) {
    clear();

    width_ = pixels.getWidth();
    height_ = pixels.getHeight();
    channels_ = pixels.getChannels();
    usage_ = usage;
    const bool mipmapSupported = (usage == TextureUsage::Immutable ||
                                  usage == TextureUsage::Dynamic);
    mipmapped_ = mipmaps && mipmapSupported;
    if (mipmapped_) {
        numMipLevels_ = 1 + (int)std::floor(std::log2((float)std::max(width_, height_)));
        if (numMipLevels_ < 1) numMipLevels_ = 1;
    } else {
        numMipLevels_ = 1;
    }

    if (pixels.isFloat()) {
        pixelFormat_ = SG_PIXELFORMAT_RGBA32F;
    }

    if (usage == TextureUsage::Immutable) {
        createResources(pixels.getDataVoid(), imageLoadMips);
    } else {
        createResources(nullptr);
    }
}

void Texture::createResources(const void* initialData, const sg_range* imageLoadMips) {
    // CPU-only Images are valid in headless apps and before graphics setup.
    // Leave the texture unallocated so their updates and draws are no-ops.
    if (!sg_isvalid()) {
        if (usage_ == TextureUsage::Storage)
            logWarning("Texture") << "Storage textures require an initialized GPU";
        return;
    }
    if (!prepareRetirement_()) return;

    auto sizeResult = internal::checkImageSize(width_, height_);
    if (!sizeResult) {
        logError("Texture") << sizeResult.message;
        return;
    }

    // Create image
    sg_image_desc img_desc = {};
    img_desc.width = width_;
    img_desc.height = height_;

    // Determine pixel format
    if (pixelFormat_ != SG_PIXELFORMAT_NONE) {
        img_desc.pixel_format = pixelFormat_;
    } else {
        img_desc.pixel_format = (channels_ == 4) ? SG_PIXELFORMAT_RGBA8 : SG_PIXELFORMAT_R8;
    }

    if (usage_ == TextureUsage::Storage) {
        const auto format = img_desc.pixel_format;
        const bool supportedFormat = format == SG_PIXELFORMAT_RGBA8 || format == SG_PIXELFORMAT_RGBA16F ||
            format == SG_PIXELFORMAT_RGBA32F || format == SG_PIXELFORMAT_R32F;
        const auto caps = sg_query_pixelformat(format);
        if (supportedFormat && !caps.filter) {
            const char* name = format == SG_PIXELFORMAT_RGBA8 ? "RGBA8" :
                format == SG_PIXELFORMAT_RGBA16F ? "RGBA16F" :
                format == SG_PIXELFORMAT_RGBA32F ? "RGBA32F" : "R32F";
            logWarning("Texture") << "Storage format " << name
                << " is not filterable on this GPU; normal Texture drawing requires filtering";
            return;
        }
        if (!sg_query_features().compute || !supportedFormat || !caps.write || !caps.sample ||
            sampleCount_ != 1 || numMipLevels_ != 1 || width_ <= 0 || height_ <= 0) {
            logWarning("Texture") << "Unsupported storage texture: requires compute, a writable RGBA8/RGBA16F/RGBA32F/R32F format, single-sample 2D and mip 0";
            return;
        }
    }

    // Compute data size based on pixel format
    size_t bpp = computeBytesPerPixel();
    bool isFloat = (pixelFormat_ == SG_PIXELFORMAT_RGBA32F);
    size_t dataSize = (size_t)width_ * height_ * bpp;

    // Contiguous mip-major storage, kept alive until sg_make_image copies it.
    std::vector<uint8_t> mipBlob;

    switch (usage_) {
        case TextureUsage::Immutable:
            if (initialData) {
                img_desc.data.ptr = initialData;
                img_desc.data.size = dataSize;

                // Generate mip chain via chained Pixels::halve().
                // Pixels::halve is gamma-correct for U8 (matches the FBO
                // mipmap downsample) and direct-average for F32 (assumed
                // already linear). Restricted to channels_ == 4 same as
                // before, since Pixels currently only handles 4-channel
                // RGBA / RGBAF32 here.
                if (imageLoadMips) {
                    img_desc.data = *imageLoadMips;
                    img_desc.num_mipmaps = numMipLevels_;
                } else if (mipmapped_ && channels_ == 4) {
                    int numLevels = 1 + (int)std::floor(std::log2((float)std::max(width_, height_)));
                    img_desc.num_mipmaps = numLevels;

                    Pixels current;
                    if (isFloat) {
                        current.setFromFloats(static_cast<const float*>(initialData),
                                              width_, height_, channels_);
                    } else {
                        current.setFromPixels(static_cast<const unsigned char*>(initialData),
                                              width_, height_, channels_);
                    }
                    const auto* baseBytes = static_cast<const uint8_t*>(initialData);
                    mipBlob.assign(baseBytes, baseBytes + dataSize);
                    for (int level = 1; level < numLevels; level++) {
                        current.halve();
                        const auto* bytes = static_cast<const uint8_t*>(current.getDataVoid());
                        mipBlob.insert(mipBlob.end(), bytes, bytes + current.getTotalBytes());
                    }
                    img_desc.data = {mipBlob.data(), mipBlob.size()};
                }
            }
            break;
        case TextureUsage::Dynamic:
        case TextureUsage::Stream:
            img_desc.usage.copy_dst = true;
            img_desc.num_mipmaps = numMipLevels_;
            break;
        case TextureUsage::Storage:
            img_desc.usage.storage_image = true;
            break;
        case TextureUsage::RenderTarget:
            img_desc.usage.color_attachment = true;
            img_desc.usage.resolve_attachment = true;  // Can also be used as MSAA resolve target
            img_desc.sample_count = sampleCount_;
            img_desc.num_mipmaps = numMipLevels_;
            break;
    }

    image_ = sg_make_image(&img_desc);
    if (sg_query_image_state(image_) != SG_RESOURCESTATE_VALID) {
        logError("Texture") << "GPU texture allocation failed ("
            << width_ << "x" << height_ << ", format "
            << (int)img_desc.pixel_format << ")";
        sg_destroy_image(image_);
        image_ = {};
        return;
    }

    // Create texture view (for sampling)
    sg_view_desc view_desc = {};
    view_desc.texture.image = image_;
    view_ = sg_make_view(&view_desc);

    if (usage_ == TextureUsage::Storage) {
        sg_view_desc storage = {};
        storage.storage_image.image = image_;
        storageView_ = sg_make_view(&storage);
    }

    // Create attachment view for RenderTarget. `attachmentView_` always
    // targets mip 0 (preserves the existing single-attachment API).
    // For mipLevels > 1, also build a per-mip attachment view list so
    // callers can render into each level individually (see
    // `getAttachmentViewForMip()`), used by Fbo to generate mipmaps.
    if (usage_ == TextureUsage::RenderTarget) {
        sg_view_desc att_desc = {};
        att_desc.color_attachment.image = image_;
        attachmentView_ = sg_make_view(&att_desc);
        if (numMipLevels_ > 1) {
            // Independent views per mip level (mip 0 is also a fresh view,
            // not aliased to `attachmentView_`, so clear() can destroy
            // both without a double-free).
            mipAttachmentViews_.resize(numMipLevels_);
            for (int level = 0; level < numMipLevels_; level++) {
                sg_view_desc mip_att_desc = {};
                mip_att_desc.color_attachment.image = image_;
                mip_att_desc.color_attachment.mip_level = level;
                mipAttachmentViews_[level] = sg_make_view(&mip_att_desc);
            }
            // Single-level sampling views for mipmap generation passes
            mipSamplingViews_.resize(numMipLevels_);
            for (int level = 0; level < numMipLevels_; level++) {
                sg_view_desc mip_tex_desc = {};
                mip_tex_desc.texture.image = image_;
                mip_tex_desc.texture.mip_levels.base = level;
                mip_tex_desc.texture.mip_levels.count = 1;
                mipSamplingViews_[level] = sg_make_view(&mip_tex_desc);
            }
        }
    }

    // Create sampler
    createSampler();

    finishResources_();
}

#ifdef __EMSCRIPTEN__

void Font::finishUrlAsync(internal::BoundedFontFetch* fetch, bool success) {
    if (internal::frameworkShutdown()) return;
    auto* holder = static_cast<std::shared_ptr<AsyncState>*>(fetch->userData);
    auto state = *holder;
    if (state->fetchComplete) return;
    state->fetchComplete = true;
    if (state->timeoutHolder) {
        emscripten_clear_timeout(state->timeoutId);
        delete state->timeoutHolder;
        state->timeoutHolder = nullptr;
        state->timeoutId = 0;
    }
    state->fetch = nullptr;
    state->fetchHolder = nullptr;
    state->cancelBackend = {};
    if (!state->cancelled.load() && state->result.error != LoadError::Unknown) {
        auto preparation = [&] {
            if (success) {
                internal::probeResourceLoad("font.url-decode", {});
                state->candidate = std::make_shared<internal::FontAtlasManager>();
                state->candidate->setOversample(state->key.oversample);
                state->candidate->setMipmaps(state->key.mipmaps);
                if (!state->candidate->setupFromMemory(reinterpret_cast<const uint8_t*>(fetch->data),
                        fetch->numBytes, state->key.fontSize, state->key.faceIndex)) state->candidate.reset();
                if (state->candidate) logNotice("Font") << "Loaded from URL: " << state->key.fontPath;
            } else {
                state->urlTimeout = state->urlTimeout || emscripten_get_now() >= state->deadline;
                if (!state->urlTimeout && fetch->status != -1)
                    logError() << "Font: failed to fetch " << state->key.fontPath << " (status: " << fetch->status << ")";
            }
            state->result = state->urlTimeout
                ? internal::reportWebLoadTimeout(internal::utf8ToPath(state->key.fontPath), "Font")
                : classifyLoad(state->candidate != nullptr, state->input.name, state->input.path, state->input.face);
        };
        (void)internal::prepareResourceLoad(*state, std::ref(preparation));
    }
    internal::closeBoundedFontFetch(fetch);
    delete holder;
    internal::completeResourceLoad(state);
}

void Font::startUrlAsync(const std::shared_ptr<AsyncState>& state) {
    if (state->cancelled.load()) return;
    state->deadline = emscripten_get_now() + internal::webLoadTimeoutMSecs;
    state->fetchHolder = new (std::nothrow) std::shared_ptr<AsyncState>(state);
    if (!state->fetchHolder) {
        state->result.error = LoadError::Unknown;
        internal::completeResourceLoad(state);
        return;
    }
    // The host owns State while this non-owning, inline callback runs.
    // Install it before backend/registration allocation can fail.
    state->cancelBackend = [raw = state.get()] {
        if (raw->fetch) finishUrlAsync(raw->fetch, false);
        else {
            auto live = raw->shared_from_this();
            raw->fetchComplete = true;
            delete std::exchange(raw->fetchHolder, nullptr);
            raw->cancelBackend = {};
            internal::completeResourceLoad(live);
        }
    };
    auto* fetch = internal::startBoundedFontFetch(state->key.fontPath.c_str(), state->fetchHolder,
        internal::webLoadTimeoutMSecs, finishUrlAsync);
    if (state->fetchComplete) return; // an inline backend failure already completed
    state->fetch = fetch;
    if (!fetch) {
        state->result.error = LoadError::Unknown;
        auto cancel = std::move(state->cancelBackend);
        cancel();
        return;
    }
    // Cancel both this timer and its guest-owned State before hot reload.
    state->timeoutHolder = new (std::nothrow) std::shared_ptr<AsyncState>(state);
    if (!state->timeoutHolder) {
        state->result.error = LoadError::Unknown;
        auto cancel = std::move(state->cancelBackend);
        cancel();
        return;
    }
    state->timeoutId = emscripten_set_timeout([](void* data) {
        std::unique_ptr<std::shared_ptr<AsyncState>> timer(static_cast<std::shared_ptr<AsyncState>*>(data));
        auto state = *timer;
        state->timeoutHolder = nullptr;
        state->timeoutId = 0;
        if (state->fetchComplete) return;
        state->urlTimeout = true;
        finishUrlAsync(state->fetch, false);
    }, internal::webLoadTimeoutMSecs, state->timeoutHolder);
}

#endif
} // namespace trussc
