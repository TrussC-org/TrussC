#pragma once

// =============================================================================
// tcxObjExporter - Export Mesh data to Wavefront OBJ format
// =============================================================================

#include <TrussC.h>
#include <string>
#include <vector>
#include <filesystem>

namespace tcx::obj {

using namespace tc;  // core types (Mesh, Image, Pixels, Color, ...)
namespace fs = std::filesystem;

// How to export vertex colors
enum class VertexColorMode {
    VertexRGB,    // Write as v x y z r g b (default, simple but non-standard)
    BakeTexture   // Bake vertex colors into a texture using UVs
};

// A mesh entry to be exported (mesh + optional name + optional texture)
struct ObjExportEntry {
    const Mesh* mesh = nullptr;
    std::string name;
    const Image* texture = nullptr;
};

// =============================================================================
// ObjExporter
// =============================================================================
class ObjExporter {
public:
    ObjExporter() = default;

    // -------------------------------------------------------------------------
    // Add meshes
    // -------------------------------------------------------------------------

    // Add mesh without texture
    ObjExporter& addMesh(const Mesh& mesh, const std::string& name = "") {
        entries_.push_back({&mesh, name.empty() ? defaultName() : name, nullptr});
        return *this;
    }

    // Add mesh with texture
    ObjExporter& addMesh(const Mesh& mesh, const std::string& name, const Image& texture) {
        entries_.push_back({&mesh, name.empty() ? defaultName() : name, &texture});
        return *this;
    }

    // -------------------------------------------------------------------------
    // Settings
    // -------------------------------------------------------------------------

    // Max texture dimension (width and height clamped independently)
    ObjExporter& setMaxTextureSize(int size) {
        maxTextureSize_ = size;
        return *this;
    }

    // Swap Y and Z axes (TrussC Y-up -> common 3D tools Z-up)
    ObjExporter& setFlipYZ(bool flip) {
        flipYZ_ = flip;
        return *this;
    }

    // Uniform scale applied to all vertices
    ObjExporter& setScale(float scale) {
        scale_ = scale;
        return *this;
    }

    // How to handle vertex colors (default: VertexRGB)
    ObjExporter& setVertexColorMode(VertexColorMode mode) {
        vertexColorMode_ = mode;
        return *this;
    }

    // Texture size for baking vertex colors (default: 1024)
    ObjExporter& setBakeTextureSize(int size) {
        bakeTextureSize_ = size;
        return *this;
    }

    // -------------------------------------------------------------------------
    // Export
    // -------------------------------------------------------------------------

    // Save to OBJ file. Generates .obj, .mtl, and texture files.
    // path: e.g. "output/model.obj"
    bool save(const fs::path& path) const;

    // Clear all entries
    void clear() { entries_.clear(); }

private:
    std::vector<ObjExportEntry> entries_;
    int maxTextureSize_ = 0;  // 0 = no limit
    bool flipYZ_ = false;
    float scale_ = 1.0f;
    VertexColorMode vertexColorMode_ = VertexColorMode::VertexRGB;
    int bakeTextureSize_ = 1024;

    std::string defaultName() const {
        return "object" + std::to_string(entries_.size());
    }

    // Check if entry needs a material (has texture or baked vertex colors)
    bool needsMaterial(const ObjExportEntry& entry) const;

    // Write geometry for one mesh entry
    void writeMeshGeometry(std::ofstream& file, const ObjExportEntry& entry,
                           int& vertexOffset, int& normalOffset, int& texCoordOffset) const;

    // Save texture (with optional resize)
    bool saveTexture(const Image& image, const fs::path& path) const;
    bool savePixels(const Pixels& pixels, const fs::path& path) const;

    // Bake vertex colors into a texture using UVs
    Pixels bakeVertexColors(const Mesh& mesh, int texSize) const;

    // Resize pixels to fit maxTextureSize
    static Pixels resizePixels(const Pixels& src, int maxSize);
};

}  // namespace tcx::obj

// -----------------------------------------------------------------------------
// Backward compatibility. The canonical namespace is now `tcx::obj`. These
// silent aliases keep older code compiling: flat `tcx::ObjExporter` and legacy
// `tc::ObjExporter` / `trussc::ObjExporter`. DEPRECATED — removed in v1.0.0.
// (No [[deprecated]] attribute: under the usual `using namespace tc;` it would
//  warn on idiomatic unqualified use too. See README for migration.)
// -----------------------------------------------------------------------------
namespace tcx    { using obj::VertexColorMode; using obj::ObjExportEntry; using obj::ObjExporter; } // deprecated: remove at v1.0.0
namespace trussc { using tcx::obj::VertexColorMode; using tcx::obj::ObjExportEntry; using tcx::obj::ObjExporter; } // deprecated: remove at v1.0.0
