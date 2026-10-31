#pragma once
#include "impl/tcDocumentInput.h"
#include "impl/tcImportError.h"

// =============================================================================
// tcIesProfile.h - IES photometric profile loader
// =============================================================================
//
// Parses IESNA LM-63 (.ies) files describing the angular intensity
// distribution of a luminaire.  The parsed data is baked into a small
// 1D (rotationally symmetric) or 2D (asymmetric) texture that the
// PBR pipeline can sample per-fragment to modulate light intensity.
//
// Usage:
//   IesProfile ies;
//   ies.load("spot.ies");
//   light.setIesProfile(&ies);    // weak pointer; IesProfile must outlive Light
//
// Internally the class owns raw sokol resources (sg_image / sg_view /
// sg_sampler) rather than a Texture instance, so it is self-contained
// and doesn't depend on the higher-level Texture class.
//
// =============================================================================

#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <cmath>
#include <algorithm>

#include "tcIesParse.h"

namespace trussc {

class IesProfile {
public:
    IesProfile() = default;

    ~IesProfile() {
        release();
    }

    // Non-copyable
    IesProfile(const IesProfile&) = delete;
    IesProfile& operator=(const IesProfile&) = delete;

    // Movable
    IesProfile(IesProfile&& other) noexcept {
        moveFrom(std::move(other));
    }
    IesProfile& operator=(IesProfile&& other) noexcept {
        if (this != &other) {
            release();
            moveFrom(std::move(other));
        }
        return *this;
    }

    // -------------------------------------------------------------------------
    // Loading
    // -------------------------------------------------------------------------

    // Load from file path (.ies). Path is resolved via getDataPath()
    // (relative to the project data/ directory).
    bool load(const fs::path& path) {
        fs::path resolved;
        try {
            resolved = internal::resolveLoadPath(path);
            if (!internal::checkLoadFile(resolved, "IesProfile")) return false;
            std::ifstream ifs(resolved, std::ios::binary);
            if (!ifs.is_open()) {
                internal::reportMissingLoad(resolved, "IesProfile");
                return false;
            }
            std::error_code ec;
            if (fs::file_size(resolved, ec) > 16u * 1024u * 1024u && !ec)
                logNotice("IesProfile") << "loading a local IES document larger than 16 MiB";
            std::string data;
            if (!internal::readDocumentBytes(ifs, data)) { internal::reportImportError("IesProfile", "failed to read the complete IES document: ", resolved); return false; }
            return loadDocument(data, &resolved);
        } catch (const std::exception&) {
            internal::reportImportError("IesProfile", "cannot allocate IES document: ", resolved.empty() ? path : resolved);
            return false;
        }
    }

    // Prepare the full replacement before retiring the previous profile.
    bool loadFromString(const std::string& data) { return loadDocument(data, nullptr); }

private:
    bool loadDocument(const std::string& data, const fs::path* sourcePath) {
        if (internal::sessionEndExit()) return false;
        try {
            internal::IesParseResult parsed;
            std::string error;
            if (!internal::parseIes(data, parsed, error)) {
                internal::reportImportError("IesProfile", error.c_str(), sourcePath);
                return false;
            }
            const int width = 256;
            const auto pixels = bakePixels(parsed, width);
            // Pre-own retirement storage so replacing/releasing GPU handles
            // cannot allocate after the preparation has succeeded.
            auto resources = std::make_unique<internal::TextureRetirement>();
            sg_image_desc desc = {};
            desc.type = SG_IMAGETYPE_2D;
            desc.width = width;
            desc.height = 1;
            desc.pixel_format = SG_PIXELFORMAT_RGBA8;
            desc.data.mip_levels[0].ptr = pixels.data();
            desc.data.mip_levels[0].size = pixels.size();
            desc.label = "tc_ies_profile";
            resources->image = sg_make_image(&desc);
            if (sg_query_image_state(resources->image) != SG_RESOURCESTATE_VALID) {
                internal::reportImportError("IesProfile", "cannot create IES image", sourcePath);
                return false;
            }
            sg_view_desc vd = {};
            vd.texture.image = resources->image;
            resources->view = sg_make_view(&vd);
            if (sg_query_view_state(resources->view) != SG_RESOURCESTATE_VALID) {
                internal::reportImportError("IesProfile", "cannot create IES view", sourcePath);
                return false;
            }
            sg_sampler_desc sd = {};
            sd.min_filter = SG_FILTER_LINEAR;
            sd.mag_filter = SG_FILTER_LINEAR;
            sd.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
            sd.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
            sd.label = "tc_ies_smp";
            resources->sampler = sg_make_sampler(&sd);
            if (sg_query_sampler_state(resources->sampler) != SG_RESOURCESTATE_VALID) {
                internal::reportImportError("IesProfile", "cannot create IES texture", sourcePath);
                return false;
            }

            // Commit only non-allocating swaps/assignments after all CPU/GPU
            // preparation; failed loads preserve both metadata and resources.
            release();
            vertAngles_.swap(parsed.vertAngles);
            horizAngles_.swap(parsed.horizAngles);
            candela_.swap(parsed.candela);
            maxCandela_ = parsed.maxCandela;
            maxVertAngle_ = vertAngles_.back() * PI_F / 180.0f;
            numVertAngles_ = static_cast<int>(vertAngles_.size());
            numHorizAngles_ = static_cast<int>(horizAngles_.size());
            texWidth_ = width;
            image_ = resources->image;
            view_ = resources->view;
            sampler_ = resources->sampler;
            resources_ = resources.release();
            loaded_ = true;
            return true;
        } catch (const std::exception&) {
            internal::reportImportError("IesProfile", "cannot allocate IES profile", sourcePath);
            return false;
        }
    }

    // -------------------------------------------------------------------------
    // Accessors
    // -------------------------------------------------------------------------

public:
    bool isLoaded() const { return loaded_; }

    // Max vertical angle in radians (typically PI for full sphere, PI/2 for downlight)
    float getMaxVerticalAngle() const { return maxVertAngle_; }

    // Peak candela value (for absolute photometry if needed)
    float getMaxCandela() const { return maxCandela_; }

    // Texture width (vertical angle samples)
    int getTextureWidth() const { return texWidth_; }

    // sokol handles for pipeline binding
    sg_view getView() const { return view_; }
    sg_sampler getSampler() const { return sampler_; }

private:
    // -------------------------------------------------------------------------
    // Linear interpolation of candela at a given vertical angle (degrees).
    // Averages across all horizontal angles for rotationally-averaged lookup.
    // -------------------------------------------------------------------------

    static float interpolateVertical(const internal::IesParseResult& parsed, float vertDeg) {
        const auto& vertAngles = parsed.vertAngles;
        const auto& candela = parsed.candela;
        // Clamp to range
        if (vertDeg <= vertAngles.front()) {
            float sum = 0;
            for (auto& row : candela) sum += row.front();
            return sum / candela.size();
        }
        if (vertDeg >= vertAngles.back()) {
            float sum = 0;
            for (auto& row : candela) sum += row.back();
            return sum / candela.size();
        }

        // Find bracketing indices
        int lo = 0;
        for (int i = 1; i < static_cast<int>(vertAngles.size()); i++) {
            if (vertAngles[i] >= vertDeg) { lo = i - 1; break; }
        }
        int hi = lo + 1;
        float t = (vertDeg - vertAngles[lo]) /
                  std::max(vertAngles[hi] - vertAngles[lo], 1e-6f);

        // Average across horizontal angles
        float sum = 0;
        for (auto& row : candela) {
            sum += row[lo] * (1.0f - t) + row[hi] * t;
        }
        return sum / candela.size();
    }

    // -------------------------------------------------------------------------
    // Bake into a 1D (height=1) RGBA8 texture.
    // R channel = normalized intensity [0,1].
    // -------------------------------------------------------------------------

    static std::vector<uint8_t> bakePixels(const internal::IesParseResult& parsed, int width) {
        std::vector<uint8_t> pixels(size_t(width) * 4);
        for (int x = 0; x < width; x++) {
            const float vertDeg = (static_cast<float>(x) + 0.5f) / width * parsed.vertAngles.back();
            const float cd = interpolateVertical(parsed, vertDeg);
            const float norm = parsed.maxCandela > 0.0f ? cd / parsed.maxCandela : 0.0f;
            const uint8_t val = static_cast<uint8_t>(std::clamp(norm, 0.0f, 1.0f) * 255.0f);
            pixels[x * 4 + 0] = val;
            pixels[x * 4 + 1] = val;
            pixels[x * 4 + 2] = val;
            pixels[x * 4 + 3] = 255;
        }
        return pixels;
    }

    // -------------------------------------------------------------------------
    // Resource management
    // -------------------------------------------------------------------------

    void release() noexcept {
        // Deferred destroy: a deferred PBR draw recorded this frame may still
        // bind these handles (drained in present(), after end-of-frame flush)
        if (resources_) internal::retireTexture(resources_);
        resources_ = nullptr;
        sampler_ = {}; view_ = {}; image_ = {};
        loaded_ = false;
    }

    void moveFrom(IesProfile&& other) {
        resources_ = other.resources_; other.resources_ = nullptr;
        image_   = other.image_;   other.image_   = {};
        view_    = other.view_;    other.view_    = {};
        sampler_ = other.sampler_; other.sampler_ = {};
        loaded_  = other.loaded_;  other.loaded_  = false;

        maxVertAngle_   = other.maxVertAngle_;
        maxCandela_     = other.maxCandela_;
        texWidth_       = other.texWidth_;
        numVertAngles_  = other.numVertAngles_;
        numHorizAngles_ = other.numHorizAngles_;
        vertAngles_     = std::move(other.vertAngles_);
        horizAngles_    = std::move(other.horizAngles_);
        candela_        = std::move(other.candela_);
    }

    // sokol resources and preallocated retirement ownership
    internal::TextureRetirement* resources_ = nullptr;
    sg_image   image_   = {};
    sg_view    view_    = {};
    sg_sampler sampler_ = {};
    bool loaded_ = false;

    // Parsed data
    float maxVertAngle_  = 3.14159265f;  // radians (default: full sphere)
    float maxCandela_    = 0.0f;
    int texWidth_        = 0;
    int numVertAngles_   = 0;
    int numHorizAngles_  = 0;
    std::vector<float> vertAngles_;
    std::vector<float> horizAngles_;
    std::vector<std::vector<float>> candela_;

    static constexpr float PI_F = 3.14159265358979323846f;
};

} // namespace trussc
