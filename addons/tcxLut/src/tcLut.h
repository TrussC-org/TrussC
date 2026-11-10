#pragma once
#include "impl/tcDocumentInput.h"
#include "impl/tcImportError.h"

// =============================================================================
// tcxLut - 1D / 3D LUT (Look-Up Table) for color grading
// =============================================================================
//
// Addon for TrussC. Add "tcxLut" to addons.make to use.
//
// Supports .cube file format (industry standard for color LUTs)
// Only the 0..1 input domain and full-range LUTs are supported;
// other declared ranges fail to load with a keyword-specific error.
// LUT_1D_SIZE, LUT_3D_SIZE, or both (1D shaper followed by 3D).
// CPU: lut.apply(color). GPU: LutShader::setLut(lut), then draw().
//
// Usage example:
//   tcx::lut::Lut3D lut;
//   lut.load("luts/cinematic.cube");   // relative: bin/data/luts/ (getDataPath)
//
//   Color graded = lut.apply(Color(0.25f, 0.5f, 0.75f));
//
// =============================================================================

#include <TrussC.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cmath>
#include <algorithm>
#include <iomanip>

#include "shaders/lut.glsl.h"
#include "tcLutParse.h"

namespace tcx::lut {

using namespace tc;

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Lut3D retains its name for compatibility; load() also accepts 1D LUTs.
// ---------------------------------------------------------------------------
class Lut3D {
public:
    Lut3D() { internal::textureCount()++; }
    ~Lut3D() { clear(); internal::textureCount()--; }

    // Copy prohibited
    Lut3D(const Lut3D&) = delete;
    Lut3D& operator=(const Lut3D&) = delete;

    // Move support
    Lut3D(Lut3D&& other) noexcept {
        moveFrom(std::move(other));
    }

    Lut3D& operator=(Lut3D&& other) noexcept {
        if (this != &other) {
            clear();
            moveFrom(std::move(other));
        }
        return *this;
    }

    // =========================================================================
    // Loading
    // =========================================================================

    // Load from .cube file. Relative paths resolve via getDataPath, like
    // Image::load.
    // Accepts LUT_1D_SIZE, LUT_3D_SIZE, or both (1D rows first).
    // Range metadata must declare the default 0..1 input domain. Resolve's
    // LUT_IN_VIDEO_RANGE / LUT_OUT_VIDEO_RANGE flags are unsupported.
    // OOM recovery holds in Release, or with iterator debugging off
    // (_ITERATOR_DEBUG_LEVEL=0); see docs/ADDONS.md.
    bool load(const fs::path& filePath) {
        fs::path path;
        try {
            path = getDataPath(filePath); // absolute paths pass through
            if (!internal::isLoadFile(path)) {
                internal::reportImportError("Lut3D", "failed to open LUT document: ", path);
                return false;
            }
            std::ifstream file(path, std::ios::binary);
            if (!file) {
                internal::reportImportError("Lut3D", "failed to open LUT document: ", path);
                return false;
            }
            std::error_code ec;
            if (fs::file_size(path, ec) > 16u * 1024u * 1024u && !ec)
                logNotice("Lut3D") << "loading a local LUT larger than 16 MiB";
            detail::CubeData parsed;
            std::string error;
            std::string bytes;
            const char* readError = nullptr;
            if (!internal::readDocumentBytes(file, bytes, &readError)) {
                internal::reportImportError("Lut3D", "failed to read the complete LUT document: ", path, readError);
                return false;
            }
            std::istringstream input(std::move(bytes));
            if (!detail::parseCube(input, parsed, error)) {
                internal::reportImportError("Lut3D", error.c_str(), &path);
                return false;
            }
            // Prepare all allocating metadata and resources before publishing.
            std::string title = parsed.title.empty() ? pathToUtf8(path.stem()) : std::move(parsed.title);
            Lut3D prepared;
            prepared.filter_ = filter_;
            const float identity[] = {0,0,0, 1,0,0, 0,1,0, 1,1,0,
                                      0,0,1, 1,0,1, 0,1,1, 1,1,1};
            prepared.allocateDocument(parsed.side ? parsed.side : 2,
                                      parsed.side ? parsed.rgb.data() : identity, &path);
            if (!prepared.isAllocated()) return false;
            if (parsed.curveSize && !prepared.allocateCurve(parsed.curveSize, parsed.curve.data(), &path)) return false;
            prepared.size_ = parsed.side;
            prepared.title_.swap(title);
            std::swap(image_, prepared.image_);
            std::swap(view_, prepared.view_);
            std::swap(sampler_, prepared.sampler_);
            std::swap(size_, prepared.size_);
            std::swap(allocated_, prepared.allocated_);
            std::swap(curveImage_, prepared.curveImage_);
            std::swap(curveView_, prepared.curveView_);
            std::swap(curveSampler_, prepared.curveSampler_);
            std::swap(curveSize_, prepared.curveSize_);
            curve_.swap(prepared.curve_);
            rgba_.swap(prepared.rgba_);
            title_.swap(prepared.title_);
            return true;
        } catch (const std::exception&) {
            internal::reportImportError("Lut3D", "cannot allocate LUT document: ", path.empty() ? filePath : path);
            return false;
        }
    }

    // Allocate empty or with data
    void allocate(int size, const float* rgbData = nullptr) { allocateDocument(size, rgbData, nullptr); }

private:
    void allocateDocument(int size, const float* rgbData, const fs::path* sourcePath) {
        clear();

        size_t rgbaBytes = 0;
        // Bound (gpu): 3D texture dimensions must fit the active device before CPU conversion. Overflow rejects the whole LUT and is counted.
        if (!detail::cubeElements(size, 4, rgbaBytes) || !sg_isvalid() ||
            size > sg_query_limits().max_image_size_3d) {
            internal::reportImportError("Lut3D", "LUT dimensions exceed the texture or storage range", sourcePath);
            return;
        }
        size_ = size;

        // Convert RGB float data to RGBA8
        // .cube format: R varies fastest, then G, then B (depth)
        std::vector<unsigned char> rgba;
        try { rgba.resize(rgbaBytes); }
        catch (const std::exception&) { internal::reportImportError("Lut3D", "cannot allocate LUT conversion", sourcePath); size_ = 0; return; }

        if (rgbData) {
            for (size_t i = 0; i < rgbaBytes / 4; i++) {
                rgba[i * 4 + 0] = static_cast<unsigned char>(std::clamp(rgbData[i * 3 + 0] * 255.0f, 0.0f, 255.0f));
                rgba[i * 4 + 1] = static_cast<unsigned char>(std::clamp(rgbData[i * 3 + 1] * 255.0f, 0.0f, 255.0f));
                rgba[i * 4 + 2] = static_cast<unsigned char>(std::clamp(rgbData[i * 3 + 2] * 255.0f, 0.0f, 255.0f));
                rgba[i * 4 + 3] = 255;
            }
        } else {
            std::fill(rgba.begin(), rgba.end(), 0);
        }

        // Create 3D image
        sg_image_desc img_desc = {};
        img_desc.type = SG_IMAGETYPE_3D;
        img_desc.width = size;
        img_desc.height = size;
        img_desc.num_slices = size;  // depth for 3D textures
        img_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
        img_desc.data.ptr = rgba.data();
        img_desc.data.size = rgba.size();
        img_desc.label = "lut3d_image";

        image_ = sg_make_image(&img_desc);
        if (sg_query_image_state(image_) != SG_RESOURCESTATE_VALID) {
            logError() << "Lut3D: failed to create 3D texture";
            sg_destroy_image(image_);
            image_ = {};
            size_ = 0;
            return;
        }

        // Create texture view
        sg_view_desc view_desc = {};
        view_desc.texture.image = image_;
        view_ = sg_make_view(&view_desc);

        // Create sampler
        createSampler();

        if (sg_query_view_state(view_) != SG_RESOURCESTATE_VALID ||
            sg_query_sampler_state(sampler_) != SG_RESOURCESTATE_VALID) {
            internal::reportImportError("Lut3D", "failed to create LUT view or sampler", sourcePath);
            sg_destroy_sampler(sampler_);
            sg_destroy_view(view_);
            sg_destroy_image(image_);
            sampler_ = {}; view_ = {}; image_ = {}; size_ = 0;
            return;
        }
        allocated_ = true;
        rgba_.swap(rgba);
        // Bind an identity curve for 3D-only LUTs as well, so every shader
        // binding is valid on all backends. The size uniform skips this stage.
        const float identityCurve[] = {0,0,0, 1,1,1};
        try {
            if (!allocateCurve(2, identityCurve, sourcePath)) { clear(); return; }
        } catch (const std::exception&) {
            internal::reportImportError("Lut3D", "cannot allocate 1D LUT conversion", sourcePath);
            clear(); return;
        }
        curveSize_ = 0;
    }

    bool allocateCurve(int size, const float* data, const fs::path* sourcePath) {
        // Bound (gpu): the 1D table is an Nx1 texture and must fit the active device.
        if (size > sg_query_limits().max_image_size_2d) {
            internal::reportImportError("Lut3D", "1D LUT dimensions exceed the texture range", sourcePath);
            return false;
        }
        std::vector<float> curve(data, data + size_t(size) * 3);
        std::vector<float> rgba(size_t(size) * 4, 1.0f);
        for (int i = 0; i < size; ++i)
            for (size_t channel = 0; channel < 3; ++channel)
                rgba[size_t(i) * 4 + channel] = curve[size_t(i) * 3 + channel];
        sg_image_desc desc = {};
        desc.width = size;
        desc.height = 1;
        desc.pixel_format = SG_PIXELFORMAT_RGBA32F;
        desc.data = {rgba.data(), rgba.size() * sizeof(float)};
        desc.label = "lut1d_image";
        sg_image image = sg_make_image(&desc);
        sg_view_desc viewDesc = {};
        viewDesc.texture.image = image;
        sg_view view = sg_make_view(&viewDesc);
        sg_sampler_desc samplerDesc = {};
        samplerDesc.min_filter = SG_FILTER_NEAREST;
        samplerDesc.mag_filter = SG_FILTER_NEAREST;
        samplerDesc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
        samplerDesc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
        sg_sampler sampler = sg_make_sampler(&samplerDesc);
        if (sg_query_image_state(image) != SG_RESOURCESTATE_VALID ||
            sg_query_view_state(view) != SG_RESOURCESTATE_VALID ||
            sg_query_sampler_state(sampler) != SG_RESOURCESTATE_VALID) {
            sg_destroy_sampler(sampler); sg_destroy_view(view); sg_destroy_image(image);
            internal::reportImportError("Lut3D", "failed to create 1D LUT texture", sourcePath);
            return false;
        }
        if (curveImage_.id) {
            sg_destroy_sampler(curveSampler_); sg_destroy_view(curveView_); sg_destroy_image(curveImage_);
        }
        curveImage_ = image; curveView_ = view; curveSampler_ = sampler;
        curveSize_ = size;
        curve_.swap(curve);
        return true;
    }

public:

    // Release resources
    void clear() {
        if (allocated_) {
            sg_destroy_sampler(curveSampler_);
            sg_destroy_view(curveView_);
            sg_destroy_image(curveImage_);
            sg_destroy_sampler(sampler_);
            sg_destroy_view(view_);
            sg_destroy_image(image_);
            allocated_ = false;
        }
        size_ = 0;
        title_.clear();
        image_ = {};
        view_ = {};
        sampler_ = {};
        curveImage_ = {}; curveView_ = {}; curveSampler_ = {}; curveSize_ = 0;
        curve_.clear(); rgba_.clear();
    }

    // =========================================================================
    // State
    // =========================================================================

    bool isAllocated() const { return allocated_; }
    // 3D side length, or zero for a 1D-only or empty LUT.
    int getSize() const { return size_; }
    const std::string& getTitle() const { return title_; }

    // Apply the loaded table(s) to finite RGB input, clamped to 0..1 at each
    // stage. Alpha is preserved. An empty LUT returns the input unchanged.
    // 1D uses per-channel linear interpolation. 3D follows setFilter(), with
    // the same RGBA8 table quantization as the GPU path.
    Color apply(const Color& color) const {
        if (!allocated_) return color;
        float values[] = {color.r, color.g, color.b};
        if (curveSize_) {
            for (size_t channel = 0; channel < 3; ++channel) {
                const float position = std::clamp(values[channel], 0.0f, 1.0f) * (curveSize_ - 1);
                const int lower = int(position);
                const int upper = std::min(lower + 1, curveSize_ - 1);
                const float fraction = position - lower;
                values[channel] = curve_[size_t(lower) * 3 + channel] * (1 - fraction) +
                                  curve_[size_t(upper) * 3 + channel] * fraction;
            }
        }
        if (size_) {
            int lower[3], upper[3];
            float fraction[3];
            for (size_t channel = 0; channel < 3; ++channel) {
                const float position = std::clamp(values[channel], 0.0f, 1.0f) * (size_ - 1);
                lower[channel] = filter_ == TextureFilter::Nearest ? int(std::floor(position + 0.5f)) : int(position);
                upper[channel] = std::min(lower[channel] + 1, size_ - 1);
                fraction[channel] = filter_ == TextureFilter::Nearest ? 0.0f : position - lower[channel];
            }
            std::fill(std::begin(values), std::end(values), 0.0f);
            for (int b = 0; b < 2; ++b) for (int g = 0; g < 2; ++g) for (int r = 0; r < 2; ++r) {
                const float weight = (r ? fraction[0] : 1 - fraction[0]) *
                                     (g ? fraction[1] : 1 - fraction[1]) *
                                     (b ? fraction[2] : 1 - fraction[2]);
                const size_t index = ((size_t(b ? upper[2] : lower[2]) * size_ +
                                      (g ? upper[1] : lower[1])) * size_ + (r ? upper[0] : lower[0])) * 4;
                for (size_t channel = 0; channel < 3; ++channel)
                    values[channel] += weight * (rgba_[index + channel] / 255.0f);
            }
        }
        return Color(values[0], values[1], values[2], color.a);
    }

    // =========================================================================
    // Filter settings
    // =========================================================================

    void setFilter(TextureFilter filter) {
        if (filter_ != filter) {
            filter_ = filter;
            recreateSampler();
        }
    }

    TextureFilter getFilter() const { return filter_; }

    // =========================================================================
    // Sokol resource access (for shader binding)
    // =========================================================================

    sg_image getImage() const { return image_; }
    sg_view getView() const { return view_; }
    sg_sampler getSampler() const { return sampler_; }

private:
    friend class LutShader;
    sg_image image_ = {};
    sg_view view_ = {};
    sg_sampler sampler_ = {};
    sg_image curveImage_ = {};
    sg_view curveView_ = {};
    sg_sampler curveSampler_ = {};
    int curveSize_ = 0;
    std::vector<float> curve_;
    std::vector<unsigned char> rgba_;

    int size_ = 0;
    std::string title_;
    bool allocated_ = false;
    TextureFilter filter_ = TextureFilter::Linear;

    void createSampler() {
        sg_sampler_desc smp_desc = {};
        smp_desc.min_filter = (filter_ == TextureFilter::Nearest) ? SG_FILTER_NEAREST : SG_FILTER_LINEAR;
        smp_desc.mag_filter = (filter_ == TextureFilter::Nearest) ? SG_FILTER_NEAREST : SG_FILTER_LINEAR;
        smp_desc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
        smp_desc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
        smp_desc.wrap_w = SG_WRAP_CLAMP_TO_EDGE;
        smp_desc.label = "lut3d_sampler";
        sampler_ = sg_make_sampler(&smp_desc);
    }

    void recreateSampler() {
        if (!allocated_) return;
        sg_destroy_sampler(sampler_);
        createSampler();
    }

    void moveFrom(Lut3D&& other) {
        image_ = other.image_;
        view_ = other.view_;
        sampler_ = other.sampler_;
        size_ = other.size_;
        title_ = std::move(other.title_);
        allocated_ = other.allocated_;
        filter_ = other.filter_;
        curveImage_ = other.curveImage_; curveView_ = other.curveView_; curveSampler_ = other.curveSampler_;
        curveSize_ = other.curveSize_;
        curve_ = std::move(other.curve_); rgba_ = std::move(other.rgba_);

        other.image_ = {};
        other.view_ = {};
        other.sampler_ = {};
        other.size_ = 0;
        other.allocated_ = false;
        other.curveImage_ = {}; other.curveView_ = {}; other.curveSampler_ = {}; other.curveSize_ = 0;
    }
};

// =============================================================================
// LUT Generation Utilities
// =============================================================================

// Generate identity LUT (no color change)
inline void generateIdentity(float* data, int size) {
    float scale = 1.0f / (size - 1);
    size_t idx = 0;
    for (int b = 0; b < size; b++) {
        for (int g = 0; g < size; g++) {
            for (int r = 0; r < size; r++) {
                data[idx++] = r * scale;
                data[idx++] = g * scale;
                data[idx++] = b * scale;
            }
        }
    }
}

// Generate vintage/faded LUT (lifted blacks, warm tones)
inline void generateVintage(float* data, int size) {
    float scale = 1.0f / (size - 1);
    size_t idx = 0;
    for (int b = 0; b < size; b++) {
        for (int g = 0; g < size; g++) {
            for (int r = 0; r < size; r++) {
                float rf = r * scale;
                float gf = g * scale;
                float bf = b * scale;

                // Lift shadows
                rf = rf * 0.85f + 0.05f;
                gf = gf * 0.85f + 0.03f;
                bf = bf * 0.80f + 0.02f;

                // Warm shift
                rf = std::min(1.0f, rf * 1.1f);
                bf = bf * 0.9f;

                // Reduce saturation
                float lum = rf * 0.299f + gf * 0.587f + bf * 0.114f;
                rf = rf * 0.7f + lum * 0.3f;
                gf = gf * 0.7f + lum * 0.3f;
                bf = bf * 0.7f + lum * 0.3f;

                data[idx++] = std::clamp(rf, 0.0f, 1.0f);
                data[idx++] = std::clamp(gf, 0.0f, 1.0f);
                data[idx++] = std::clamp(bf, 0.0f, 1.0f);
            }
        }
    }
}

// Generate cinematic LUT (orange/teal split toning)
inline void generateCinematic(float* data, int size) {
    float scale = 1.0f / (size - 1);
    size_t idx = 0;
    for (int b = 0; b < size; b++) {
        for (int g = 0; g < size; g++) {
            for (int r = 0; r < size; r++) {
                float rf = r * scale;
                float gf = g * scale;
                float bf = b * scale;

                float lum = rf * 0.299f + gf * 0.587f + bf * 0.114f;

                // Orange in highlights
                float orangeR = std::min(1.0f, rf * 1.15f);
                float orangeG = gf * 0.95f;
                float orangeB = bf * 0.75f;

                // Teal in shadows
                float tealR = rf * 0.85f;
                float tealG = std::min(1.0f, gf * 1.05f);
                float tealB = std::min(1.0f, bf * 1.15f);

                // Blend based on luminance
                float t = lum;
                rf = tealR * (1.0f - t) + orangeR * t;
                gf = tealG * (1.0f - t) + orangeG * t;
                bf = tealB * (1.0f - t) + orangeB * t;

                // Boost contrast
                rf = std::pow(rf, 1.1f);
                gf = std::pow(gf, 1.1f);
                bf = std::pow(bf, 1.1f);

                data[idx++] = std::clamp(rf, 0.0f, 1.0f);
                data[idx++] = std::clamp(gf, 0.0f, 1.0f);
                data[idx++] = std::clamp(bf, 0.0f, 1.0f);
            }
        }
    }
}

// Generate film noir LUT (high contrast B&W with slight blue)
inline void generateFilmNoir(float* data, int size) {
    float scale = 1.0f / (size - 1);
    size_t idx = 0;
    for (int b = 0; b < size; b++) {
        for (int g = 0; g < size; g++) {
            for (int r = 0; r < size; r++) {
                float rf = r * scale;
                float gf = g * scale;
                float bf = b * scale;

                // Convert to luminance
                float lum = rf * 0.299f + gf * 0.587f + bf * 0.114f;

                // Apply S-curve for contrast
                lum = lum * lum * (3.0f - 2.0f * lum);  // smoothstep
                lum = std::pow(lum, 1.2f);  // boost contrast

                // Slight blue tint
                rf = lum * 0.95f;
                gf = lum * 0.97f;
                bf = std::min(1.0f, lum * 1.05f);

                data[idx++] = std::clamp(rf, 0.0f, 1.0f);
                data[idx++] = std::clamp(gf, 0.0f, 1.0f);
                data[idx++] = std::clamp(bf, 0.0f, 1.0f);
            }
        }
    }
}

// Generate warm LUT
inline void generateWarm(float* data, int size) {
    float scale = 1.0f / (size - 1);
    size_t idx = 0;
    for (int b = 0; b < size; b++) {
        for (int g = 0; g < size; g++) {
            for (int r = 0; r < size; r++) {
                float rf = r * scale;
                float gf = g * scale;
                float bf = b * scale;

                // Warm color shift
                rf = std::min(1.0f, rf * 1.1f + 0.02f);
                gf = gf * 1.02f;
                bf = bf * 0.85f;

                data[idx++] = std::clamp(rf, 0.0f, 1.0f);
                data[idx++] = std::clamp(gf, 0.0f, 1.0f);
                data[idx++] = std::clamp(bf, 0.0f, 1.0f);
            }
        }
    }
}

// Generate cool LUT
inline void generateCool(float* data, int size) {
    float scale = 1.0f / (size - 1);
    size_t idx = 0;
    for (int b = 0; b < size; b++) {
        for (int g = 0; g < size; g++) {
            for (int r = 0; r < size; r++) {
                float rf = r * scale;
                float gf = g * scale;
                float bf = b * scale;

                // Cool color shift
                rf = rf * 0.9f;
                gf = gf * 1.0f;
                bf = std::min(1.0f, bf * 1.15f + 0.02f);

                data[idx++] = std::clamp(rf, 0.0f, 1.0f);
                data[idx++] = std::clamp(gf, 0.0f, 1.0f);
                data[idx++] = std::clamp(bf, 0.0f, 1.0f);
            }
        }
    }
}

// Generate cyberpunk LUT (neon pink/cyan)
inline void generateCyberpunk(float* data, int size) {
    float scale = 1.0f / (size - 1);
    size_t idx = 0;
    for (int b = 0; b < size; b++) {
        for (int g = 0; g < size; g++) {
            for (int r = 0; r < size; r++) {
                float rf = r * scale;
                float gf = g * scale;
                float bf = b * scale;

                float lum = rf * 0.299f + gf * 0.587f + bf * 0.114f;

                // Magenta/pink shift
                float magR = std::min(1.0f, rf * 1.2f + 0.1f);
                float magG = gf * 0.5f;
                float magB = std::min(1.0f, bf * 1.3f + 0.15f);

                // Cyan shift
                float cyanR = rf * 0.3f;
                float cyanG = std::min(1.0f, gf * 1.1f + 0.1f);
                float cyanB = std::min(1.0f, bf * 1.2f + 0.1f);

                // Split: highlights -> magenta, shadows -> cyan
                float t = lum;
                rf = cyanR * (1.0f - t) + magR * t;
                gf = cyanG * (1.0f - t) + magG * t;
                bf = cyanB * (1.0f - t) + magB * t;

                // Boost saturation
                float newLum = rf * 0.299f + gf * 0.587f + bf * 0.114f;
                rf = newLum + (rf - newLum) * 1.3f;
                gf = newLum + (gf - newLum) * 1.3f;
                bf = newLum + (bf - newLum) * 1.3f;

                data[idx++] = std::clamp(rf, 0.0f, 1.0f);
                data[idx++] = std::clamp(gf, 0.0f, 1.0f);
                data[idx++] = std::clamp(bf, 0.0f, 1.0f);
            }
        }
    }
}

// Generate pastel LUT (soft, desaturated colors)
inline void generatePastel(float* data, int size) {
    float scale = 1.0f / (size - 1);
    size_t idx = 0;
    for (int b = 0; b < size; b++) {
        for (int g = 0; g < size; g++) {
            for (int r = 0; r < size; r++) {
                float rf = r * scale;
                float gf = g * scale;
                float bf = b * scale;

                // Lift shadows, compress highlights
                rf = rf * 0.6f + 0.3f;
                gf = gf * 0.6f + 0.3f;
                bf = bf * 0.6f + 0.3f;

                // Desaturate
                float lum = rf * 0.299f + gf * 0.587f + bf * 0.114f;
                rf = rf * 0.5f + lum * 0.5f;
                gf = gf * 0.5f + lum * 0.5f;
                bf = bf * 0.5f + lum * 0.5f;

                data[idx++] = std::clamp(rf, 0.0f, 1.0f);
                data[idx++] = std::clamp(gf, 0.0f, 1.0f);
                data[idx++] = std::clamp(bf, 0.0f, 1.0f);
            }
        }
    }
}

// Save LUT data as .cube file
inline bool saveCube(const fs::path& path, const float* data, int size, const std::string& title = "") {
    std::ofstream file(path);
    if (!file) {
        logError() << "lut::saveCube: failed to open " << path;
        return false;
    }

    // Header
    if (!title.empty()) {
        file << "TITLE \"" << title << "\"\n";
    }
    file << "LUT_3D_SIZE " << size << "\n";
    file << "\n";

    // Data (R varies fastest, then G, then B)
    size_t idx = 0;
    for (int b = 0; b < size; b++) {
        for (int g = 0; g < size; g++) {
            for (int r = 0; r < size; r++) {
                file << std::fixed << std::setprecision(6)
                     << data[idx] << " "
                     << data[idx + 1] << " "
                     << data[idx + 2] << "\n";
                idx += 3;
            }
        }
    }

    return true;
}

// Create and allocate a Lut3D with generated data
inline Lut3D createIdentity(int size = 32) {
    Lut3D lut;
    size_t count = 0;
    if (!detail::cubeElements(size, 3, count) || count > std::vector<float>().max_size()) {
        internal::reportImportError("Lut3D", "invalid LUT dimensions");
        return lut;
    }
    std::vector<float> data;
    try { data.resize(count); }
    catch (const std::exception&) { internal::reportImportError("Lut3D", "cannot allocate LUT table"); return lut; }
    generateIdentity(data.data(), size);
    lut.allocate(size, data.data());
    return lut;
}

inline Lut3D createVintage(int size = 32) {
    Lut3D lut;
    size_t count = 0;
    if (!detail::cubeElements(size, 3, count) || count > std::vector<float>().max_size()) {
        internal::reportImportError("Lut3D", "invalid LUT dimensions");
        return lut;
    }
    std::vector<float> data;
    try { data.resize(count); }
    catch (const std::exception&) { internal::reportImportError("Lut3D", "cannot allocate LUT table"); return lut; }
    generateVintage(data.data(), size);
    lut.allocate(size, data.data());
    return lut;
}

inline Lut3D createCinematic(int size = 32) {
    Lut3D lut;
    size_t count = 0;
    if (!detail::cubeElements(size, 3, count) || count > std::vector<float>().max_size()) {
        internal::reportImportError("Lut3D", "invalid LUT dimensions");
        return lut;
    }
    std::vector<float> data;
    try { data.resize(count); }
    catch (const std::exception&) { internal::reportImportError("Lut3D", "cannot allocate LUT table"); return lut; }
    generateCinematic(data.data(), size);
    lut.allocate(size, data.data());
    return lut;
}

inline Lut3D createFilmNoir(int size = 32) {
    Lut3D lut;
    size_t count = 0;
    if (!detail::cubeElements(size, 3, count) || count > std::vector<float>().max_size()) {
        internal::reportImportError("Lut3D", "invalid LUT dimensions");
        return lut;
    }
    std::vector<float> data;
    try { data.resize(count); }
    catch (const std::exception&) { internal::reportImportError("Lut3D", "cannot allocate LUT table"); return lut; }
    generateFilmNoir(data.data(), size);
    lut.allocate(size, data.data());
    return lut;
}

inline Lut3D createWarm(int size = 32) {
    Lut3D lut;
    size_t count = 0;
    if (!detail::cubeElements(size, 3, count) || count > std::vector<float>().max_size()) {
        internal::reportImportError("Lut3D", "invalid LUT dimensions");
        return lut;
    }
    std::vector<float> data;
    try { data.resize(count); }
    catch (const std::exception&) { internal::reportImportError("Lut3D", "cannot allocate LUT table"); return lut; }
    generateWarm(data.data(), size);
    lut.allocate(size, data.data());
    return lut;
}

inline Lut3D createCool(int size = 32) {
    Lut3D lut;
    size_t count = 0;
    if (!detail::cubeElements(size, 3, count) || count > std::vector<float>().max_size()) {
        internal::reportImportError("Lut3D", "invalid LUT dimensions");
        return lut;
    }
    std::vector<float> data;
    try { data.resize(count); }
    catch (const std::exception&) { internal::reportImportError("Lut3D", "cannot allocate LUT table"); return lut; }
    generateCool(data.data(), size);
    lut.allocate(size, data.data());
    return lut;
}

inline Lut3D createCyberpunk(int size = 32) {
    Lut3D lut;
    size_t count = 0;
    if (!detail::cubeElements(size, 3, count) || count > std::vector<float>().max_size()) {
        internal::reportImportError("Lut3D", "invalid LUT dimensions");
        return lut;
    }
    std::vector<float> data;
    try { data.resize(count); }
    catch (const std::exception&) { internal::reportImportError("Lut3D", "cannot allocate LUT table"); return lut; }
    generateCyberpunk(data.data(), size);
    lut.allocate(size, data.data());
    return lut;
}

inline Lut3D createPastel(int size = 32) {
    Lut3D lut;
    size_t count = 0;
    if (!detail::cubeElements(size, 3, count) || count > std::vector<float>().max_size()) {
        internal::reportImportError("Lut3D", "invalid LUT dimensions");
        return lut;
    }
    std::vector<float> data;
    try { data.resize(count); }
    catch (const std::exception&) { internal::reportImportError("Lut3D", "cannot allocate LUT table"); return lut; }
    generatePastel(data.data(), size);
    lut.allocate(size, data.data());
    return lut;
}

// =============================================================================
// LutShader - Shader specialized for LUT color grading
// =============================================================================
// Inherits from Shader and provides convenient LUT-specific functionality.
// Built-in LUT shader is automatically available (no external shader needed).
//
// Usage:
//   LutShader lutShader;
//   lutShader.load();  // Uses built-in shader
//   lutShader.setLut(myLut);
//   lutShader.setTexture(videoGrabber.getTexture());
//   lutShader.setBlend(0.8f);
//   lutShader.draw(0, 0, 800, 600);
// =============================================================================
class LutShader : public Shader {
public:
    LutShader() = default;

    // Load with built-in LUT shader (no external shader file needed)
    bool load() {
        return Shader::load(lut_apply_shader_desc);
    }

    // -------------------------------------------------------------------------
    // LUT settings
    // -------------------------------------------------------------------------

    void setLut(const Lut3D& lut) {
        currentLut = &lut;
    }

    void setLut(const Lut3D* lut) {
        currentLut = lut;
    }

    // Set blend amount (0 = original, 1 = full LUT effect)
    void setBlend(float b) {
        blend = std::clamp(b, 0.0f, 1.0f);
    }

    float getBlend() const { return blend; }

    // -------------------------------------------------------------------------
    // Source texture
    // -------------------------------------------------------------------------

    void setTexture(sg_view view, sg_sampler sampler, int width, int height) {
        sourceView = view;
        sourceSampler = sampler;
        texWidth = width;
        texHeight = height;
    }

    template<typename T>
    void setTexture(const T& tex) {
        sourceView = tex.getView();
        sourceSampler = tex.getSampler();
        texWidth = tex.getWidth();
        texHeight = tex.getHeight();
    }

    // -------------------------------------------------------------------------
    // Draw with LUT applied (Image-like API)
    // -------------------------------------------------------------------------

    // Draw full texture stretched to specified area
    void draw(float x, float y, float w, float h) {
        drawSubsection(x, y, w, h, 0, 0, (float)texWidth, (float)texHeight);
    }

    // Draw full texture at original size
    void draw(float x, float y) {
        draw(x, y, (float)texWidth, (float)texHeight);
    }

    // Draw a subsection of the texture
    // x, y, w, h: destination rectangle
    // sx, sy, sw, sh: source rectangle (in texture coordinates)
    void drawSubsection(float x, float y, float w, float h,
                        float sx, float sy, float sw, float sh) {
        if (!loaded || !currentLut || !currentLut->isAllocated() || !sourceView.id) return;

        // Ensure render pass is active
        internal::ensureCurrentPass();

        // Consume all preceding layers before direct drawing.
        internal::currentTarget().flushLayers();
        internal::applyTargetDrawingState();

        const auto selected = selectPipelineForCurrentTarget_();
        if (!selected.raw.id) return;
        sg_apply_pipeline(selected.raw);
        lut_vs_params_t params = {};
        const auto& target = internal::currentTarget();
        const auto mvp = (target.camera.currentProjectionMatrix * target.camera.currentViewMatrix * target.currentMatrix).transposed();
        std::memcpy(params.mvp, mvp.m, sizeof(params.mvp));
        params.rect[0] = x; params.rect[1] = y; params.rect[2] = w; params.rect[3] = h;
        sg_range vertices = {&params, sizeof(params)};
        sg_apply_uniforms(UB_lut_vs_params, &vertices);

        // Setup bindings
        sg_bindings bind = {};
        bind.vertex_buffers[0] = vertexBuffer;
        bind.index_buffer = indexBuffer;
        bind.views[0] = sourceView;
        bind.views[1] = currentLut->getView();
        bind.samplers[0] = sourceSampler;
        bind.samplers[1] = currentLut->getSampler();
        bind.views[2] = currentLut->curveView_;
        bind.samplers[2] = currentLut->curveSampler_;
        sg_apply_bindings(&bind);

        // Calculate UV coordinates for subsection
        float u0 = sx / texWidth;
        float v0 = sy / texHeight;
        float u1 = (sx + sw) / texWidth;
        float v1 = (sy + sh) / texHeight;

        // Apply uniforms (must match shader's fs_params layout)
        float uniforms[8] = {
            (float)currentLut->getSize(),  // lutSize
            blend,                          // blend
            (float)currentLut->curveSize_, 0, // curveSize, padding
            u0, v0, u1 - u0, v1 - v0        // viewport (UV offset and scale)
        };
        sg_range range = { uniforms, sizeof(uniforms) };
        sg_apply_uniforms(0, &range);

        // Draw quad
        sg_draw(0, 6, 1);

        sg_reset_state_cache();
        internal::applyTargetDrawingState();
    }

protected:
    // Override pipeline creation for LUT-specific layout
    sg_pipeline_desc createPipelineDesc() override {
        sg_pipeline_desc desc = {};

        // LUT shader uses position(2) + texcoord(2)
        desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;  // position
        desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;  // texcoord

        // Alpha blending
        desc.colors[0].blend.enabled = true;
        desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
        desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;

        desc.index_type = SG_INDEXTYPE_UINT16;
        desc.label = "lut_shader_pipeline";
        return desc;
    }

    // Override to create fullscreen quad vertex buffer
    void createVertexBuffer() override {
        // Fullscreen quad vertices (NDC coordinates)
        // UV coords are adjusted via uniforms
        float vertices[] = {
            // position    texcoord
            -1.0f, -1.0f,  0.0f, 1.0f,
             1.0f, -1.0f,  1.0f, 1.0f,
             1.0f,  1.0f,  1.0f, 0.0f,
            -1.0f,  1.0f,  0.0f, 0.0f,
        };

        sg_buffer_desc vbufDesc = {};
        vbufDesc.data = SG_RANGE(vertices);
        vbufDesc.label = "lut_shader_vertices";
        vertexBuffer = sg_make_buffer(&vbufDesc);

        // Index buffer
        uint16_t indices[] = { 0, 1, 2, 0, 2, 3 };
        sg_buffer_desc ibufDesc = {};
        ibufDesc.usage.index_buffer = true;
        ibufDesc.data = SG_RANGE(indices);
        ibufDesc.label = "lut_shader_indices";
        indexBuffer = sg_make_buffer(&ibufDesc);
    }

    // Override bindings setup (used by base class submitVertices)
    void setupBindings(sg_bindings& bind) override {
        if (sourceView.id) {
            bind.views[0] = sourceView;
            bind.samplers[0] = sourceSampler;
        }
        if (currentLut && currentLut->isAllocated()) {
            bind.views[1] = currentLut->getView();
            bind.samplers[1] = currentLut->getSampler();
            bind.views[2] = currentLut->curveView_;
            bind.samplers[2] = currentLut->curveSampler_;
        }
    }

private:
    const Lut3D* currentLut = nullptr;
    sg_view sourceView = {};
    sg_sampler sourceSampler = {};
    int texWidth = 0;
    int texHeight = 0;
    float blend = 1.0f;
};

} // namespace tcx::lut

// -----------------------------------------------------------------------------
// Backward compatibility: canonical is now tcx::lut. Flat tcx:: alias kept until
// v1.0.0. (No [[deprecated]] attribute: under the usual `using namespace tc;`
// it would warn on idiomatic unqualified use too.)
// -----------------------------------------------------------------------------
namespace tcx { // deprecated: remove at v1.0.0
    using lut::Lut3D;
    using lut::LutShader;
    using lut::generateIdentity;
    using lut::generateVintage;
    using lut::generateCinematic;
    using lut::generateFilmNoir;
    using lut::generateWarm;
    using lut::generateCool;
    using lut::generateCyberpunk;
    using lut::generatePastel;
    using lut::saveCube;
    using lut::createIdentity;
    using lut::createVintage;
    using lut::createCinematic;
    using lut::createFilmNoir;
    using lut::createWarm;
    using lut::createCool;
    using lut::createCyberpunk;
    using lut::createPastel;
}
