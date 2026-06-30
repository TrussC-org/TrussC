#pragma once

// =============================================================================
// tcxObjLoader - Load Wavefront OBJ files into Mesh
// Uses tinyobjloader for parsing
// =============================================================================

#include <TrussC.h>
#include <string>
#include <vector>
#include <filesystem>

namespace tcx {

using namespace tc;  // core types (Mesh, Material, Image, Vec3, ...)
namespace fs = std::filesystem;

// A loaded group/object from OBJ file
struct ObjGroup {
    std::string name;
    Mesh mesh;
    Material material;   // PBR material (converted from MTL Phong params)
    Image texture;       // Loaded from MTL map_Kd (empty if none)
    bool hasTexture = false;
};

// =============================================================================
// ObjLoader
// =============================================================================
class ObjLoader {
public:
    ObjLoader() = default;

    // -------------------------------------------------------------------------
    // Load
    // -------------------------------------------------------------------------

    // Load OBJ file. Also loads MTL and textures if present.
    bool load(const fs::path& path);

    // -------------------------------------------------------------------------
    // Access results
    // -------------------------------------------------------------------------

    // Get all geometry merged into a single mesh
    Mesh getMesh() const;

    // Get groups/objects separately
    std::vector<ObjGroup>& getGroups() { return groups_; }
    const std::vector<ObjGroup>& getGroups() const { return groups_; }

    int getNumGroups() const { return static_cast<int>(groups_.size()); }

    // Clear loaded data
    void clear() { groups_.clear(); }

private:
    std::vector<ObjGroup> groups_;

    // Compute per-vertex normals (averaged from face normals)
    static void computeNormals(Mesh& mesh);
};

}  // namespace tcx

// -----------------------------------------------------------------------------
// Backward compatibility: tcxObj historically lived in `trussc` (tc).
// The canonical namespace is now `tcx`; this alias keeps existing
// `tc::Xxx` code compiling. DEPRECATED — will be removed in v1.0.0.
// (No [[deprecated]] attribute: under the usual `using namespace tc;` it would
//  warn on idiomatic unqualified use too. See README for migration.)
// -----------------------------------------------------------------------------
namespace trussc {
using tcx::ObjGroup;
using tcx::ObjLoader;
}  // namespace trussc
