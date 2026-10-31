#pragma once
#include "impl/tcDocumentInput.h"
#include "impl/tcImportError.h"

// =============================================================================
// tcxLut - 3D LUT (Look-Up Table) for color grading
// =============================================================================
//
// Addon for TrussC. Add "tcxLut" to addons.make to use.
//
// Supports .cube file format (industry standard for color LUTs)
// Uses 3D texture for GPU-accelerated color transformation
//
// Usage example:
//   tcx::lut::Lut3D lut;
//   lut.load("luts/cinematic.cube");   // relative: bin/data/luts/ (getDataPath)
//
//   // In shader, sample using: texture(lut3D, color.rgb)
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
// Lut3D class - 3D LUT texture for color grading
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
    // OOM recovery holds in Release, or with iterator debugging off
    // (_ITERATOR_DEBUG_LEVEL=0); see docs/ADDONS.md.
    bool load(const fs::path& filePath) {
        fs::path path;
        try {
            path = getDataPath(filePath); // absolute paths pass through
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
            if (!internal::readDocumentBytes(file, bytes)) {
                internal::reportImportError("Lut3D", "failed to read the complete LUT document: ", path);
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
            prepared.allocateDocument(parsed.side, parsed.rgb.data(), &path);
            if (!prepared.isAllocated()) return false;
            prepared.title_.swap(title);
            std::swap(image_, prepared.image_);
            std::swap(view_, prepared.view_);
            std::swap(sampler_, prepared.sampler_);
            std::swap(size_, prepared.size_);
            std::swap(allocated_, prepared.allocated_);
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
        if (internal::sessionEndExit()) return;
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
        img_desc.data.mip_levels[0].ptr = rgba.data();
        img_desc.data.mip_levels[0].size = rgba.size();
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
    }

public:

    // Release resources
    void clear() {
        if (allocated_) {
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
    }

    // =========================================================================
    // State
    // =========================================================================

    bool isAllocated() const { return allocated_; }
    int getSize() const { return size_; }
    const std::string& getTitle() const { return title_; }

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
    sg_image image_ = {};
    sg_view view_ = {};
    sg_sampler sampler_ = {};

    int size_ = 0;
    std::string title_;
    bool allocated_ = false;
    TextureFilter filter_ = TextureFilter::Linear;

    void createSampler() {
        if (internal::sessionEndExit()) return;
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

        other.image_ = {};
        other.view_ = {};
        other.sampler_ = {};
        other.size_ = 0;
        other.allocated_ = false;
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
        if (!loaded || !currentLut || !sourceView.id) return;

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
            0, 0,                           // padding
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
