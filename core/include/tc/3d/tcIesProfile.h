#pragma once

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
        fs::path resolved = getDataPath(path);
        std::ifstream ifs(resolved);
        if (!ifs.is_open()) {
            logWarning() << "[IesProfile] cannot open: " << resolved;
            return false;
        }
        std::string data((std::istreambuf_iterator<char>(ifs)),
                          std::istreambuf_iterator<char>());
        return loadFromString(data);
    }

    // Load from in-memory string
    bool loadFromString(const std::string& data) {
        release();
        if (!parse(data)) return false;
        bakeTexture();
        loaded_ = true;
        return true;
    }

    // -------------------------------------------------------------------------
    // Accessors
    // -------------------------------------------------------------------------

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
    // IES LM-63 parser
    // -------------------------------------------------------------------------

    bool parse(const std::string& data) {
        internal::IesParseResult parsed;
        std::string error;
        if (!internal::parseIes(data, parsed, error)) {
            logWarning() << "[IesProfile] " << error;
            return false;
        }

        vertAngles_ = std::move(parsed.vertAngles);
        horizAngles_ = std::move(parsed.horizAngles);
        candela_ = std::move(parsed.candela);
        maxCandela_ = parsed.maxCandela;
        maxVertAngle_ = vertAngles_.back() * PI_F / 180.0f;
        numVertAngles_ = static_cast<int>(vertAngles_.size());
        numHorizAngles_ = static_cast<int>(horizAngles_.size());
        return true;
    }

    // -------------------------------------------------------------------------
    // Linear interpolation of candela at a given vertical angle (degrees).
    // Averages across all horizontal angles for rotationally-averaged lookup.
    // -------------------------------------------------------------------------

    float interpolateVertical(float vertDeg) const {
        // Clamp to range
        if (vertDeg <= vertAngles_.front()) {
            float sum = 0;
            for (auto& row : candela_) sum += row.front();
            return sum / candela_.size();
        }
        if (vertDeg >= vertAngles_.back()) {
            float sum = 0;
            for (auto& row : candela_) sum += row.back();
            return sum / candela_.size();
        }

        // Find bracketing indices
        int lo = 0;
        for (int i = 1; i < numVertAngles_; i++) {
            if (vertAngles_[i] >= vertDeg) { lo = i - 1; break; }
        }
        int hi = lo + 1;
        float t = (vertDeg - vertAngles_[lo]) /
                  std::max(vertAngles_[hi] - vertAngles_[lo], 1e-6f);

        // Average across horizontal angles
        float sum = 0;
        for (auto& row : candela_) {
            sum += row[lo] * (1.0f - t) + row[hi] * t;
        }
        return sum / candela_.size();
    }

    // -------------------------------------------------------------------------
    // Bake into a 1D (height=1) RGBA8 texture.
    // R channel = normalized intensity [0,1].
    // -------------------------------------------------------------------------

    void bakeTexture() {
        texWidth_ = 256;  // vertical angle resolution
        const int w = texWidth_;
        const int h = 1;

        std::vector<uint8_t> pixels(w * h * 4);
        for (int x = 0; x < w; x++) {
            // Map texel center to vertical angle in degrees
            float vertDeg = ((float)x + 0.5f) / w * vertAngles_.back();
            float cd = interpolateVertical(vertDeg);
            float norm = (maxCandela_ > 0.0f) ? cd / maxCandela_ : 0.0f;
            uint8_t val = (uint8_t)(std::min(std::max(norm, 0.0f), 1.0f) * 255.0f);
            pixels[x * 4 + 0] = val;
            pixels[x * 4 + 1] = val;
            pixels[x * 4 + 2] = val;
            pixels[x * 4 + 3] = 255;
        }

        // Create sokol image
        sg_image_desc desc = {};
        desc.type = SG_IMAGETYPE_2D;
        desc.width = w;
        desc.height = h;
        desc.pixel_format = SG_PIXELFORMAT_RGBA8;
        desc.data.mip_levels[0].ptr = pixels.data();
        desc.data.mip_levels[0].size = pixels.size();
        desc.label = "tc_ies_profile";
        image_ = sg_make_image(&desc);

        sg_view_desc vd = {};
        vd.texture.image = image_;
        view_ = sg_make_view(&vd);

        sg_sampler_desc sd = {};
        sd.min_filter = SG_FILTER_LINEAR;
        sd.mag_filter = SG_FILTER_LINEAR;
        sd.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
        sd.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
        sd.label = "tc_ies_smp";
        sampler_ = sg_make_sampler(&sd);
    }

    // -------------------------------------------------------------------------
    // Resource management
    // -------------------------------------------------------------------------

    void release() {
        // Deferred destroy: a deferred PBR draw recorded this frame may still
        // bind these handles (drained in present(), after end-of-frame flush)
        internal::deferGpuDestroy(sampler_); sampler_ = {};
        internal::deferGpuDestroy(view_);    view_ = {};
        internal::deferGpuDestroy(image_);   image_ = {};
        loaded_ = false;
    }

    void moveFrom(IesProfile&& other) {
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

    // sokol resources
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
