// Resource preparation and publication own storage while allocations can fail.
// Keep these definitions in the exception-enabled host, including all owning
// helper frames; CMake also propagates exception support for header loaders.
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
            try { ready.result = finishLoad(ready.path, ready.mipmaps); }
            catch (...) { clearCommitted(); throw; }
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

void Font::loadAsync(const fs::path& nameOrPath, int size, int faceIndex) {
    cancelAsync();
    auto state = async_.begin(onLoad, [this](AsyncState& ready) { applyAsync(ready); });
    state->dpi = sapp_dpi_scale();
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
            state->preflightFailure = true;
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
    if (ready.preflightFailure || ready.urlTimeout) {
        atlasManager_.reset();
        if (ready.urlTimeout) cacheKey_.fontPath.clear();
        if (hadAtlas) internal::SharedFontCache::getInstance().releaseIfUnused(previousKey);
        return;
    }
    if (!ready.result) {
        atlasManager_.reset();
        if (hadAtlas) internal::SharedFontCache::getInstance().releaseIfUnused(previousKey);
        return;
    }
    try {
        atlasManager_ = internal::SharedFontCache::getInstance().publishPrepared(ready.key, std::move(ready.candidate));
    } catch (...) {
        cacheKey_ = std::move(previousKey);
        throw;
    }
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
    std::array<Pixels, SG_MAX_MIPMAPS> chain;
    sg_image_data data = {};
    data.mip_levels[0].ptr = pixels.getDataVoid();
    data.mip_levels[0].size = pixels.getTotalBytes();
    const Pixels* source = &pixels;
    for (int level = 1; level < SG_MAX_MIPMAPS &&
         (source->getWidth() > 1 || source->getHeight() > 1); ++level) {
        if (!internal::ImageLoadAccess::mipLevel(chain[level], *source)) return false;
        source = &chain[level];
        data.mip_levels[level].ptr = source->getDataVoid();
        data.mip_levels[level].size = source->getTotalBytes();
    }
    allocatePixels(pixels, TextureUsage::Immutable, true, &data);
    return bool(retirement_);
}

void Texture::allocatePixels(const Pixels& pixels, TextureUsage usage, bool mipmaps,
                    const sg_image_data* imageLoadMips) {
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
        // Bound (gpu): image-derived mip counts must fit the GPU API descriptor array. Overflow clamps the mip count.
        if (numMipLevels_ > SG_MAX_MIPMAPS) numMipLevels_ = SG_MAX_MIPMAPS;
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

void Texture::createResources(const void* initialData, const sg_image_data* imageLoadMips) {
    // CPU-only Images are valid in headless apps and before graphics setup.
    // Leave the texture unallocated so their updates and draws are no-ops.
    if (!sg_isvalid()) return;
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

    // Compute data size based on pixel format
    size_t bpp = computeBytesPerPixel();
    bool isFloat = (pixelFormat_ == SG_PIXELFORMAT_RGBA32F);
    size_t dataSize = (size_t)width_ * height_ * bpp;

    // Storage for mip chain Pixels (kept alive until sg_make_image
    // copies them). Used by the Immutable + auto-mipmap path below.
    std::vector<Pixels> mipChain;

    switch (usage_) {
        case TextureUsage::Immutable:
            if (initialData) {
                img_desc.data.mip_levels[0].ptr = initialData;
                img_desc.data.mip_levels[0].size = dataSize;

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
                    // Bound (gpu): image-derived mip counts must fit the GPU API descriptor array. Overflow clamps the mip count.
                    if (numLevels > SG_MAX_MIPMAPS) numLevels = SG_MAX_MIPMAPS;
                    img_desc.num_mipmaps = numLevels;

                    Pixels current;
                    if (isFloat) {
                        current.setFromFloats(static_cast<const float*>(initialData),
                                              width_, height_, channels_);
                    } else {
                        current.setFromPixels(static_cast<const unsigned char*>(initialData),
                                              width_, height_, channels_);
                    }
                    mipChain.reserve(numLevels - 1);
                    for (int level = 1; level < numLevels; level++) {
                        current.halve();
                        mipChain.emplace_back(current.clone());
                        img_desc.data.mip_levels[level].ptr  = mipChain.back().getDataVoid();
                        img_desc.data.mip_levels[level].size = mipChain.back().getTotalBytes();
                    }
                }
            }
            break;
        case TextureUsage::Dynamic:
            // D3D11's DYNAMIC usage only supports a single mip level.
            // When mipmaps are requested we still allocate the base
            // image as a plain dynamic texture (1 mip); the full mip
            // chain is built on each loadData() call by recreating the
            // image as immutable with all levels baked in.
            img_desc.usage.dynamic_update = true;
            break;
        case TextureUsage::Stream:
            img_desc.usage.stream_update = true;
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

    allocated_ = true;
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
