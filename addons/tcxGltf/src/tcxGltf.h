#pragma once

// =============================================================================
// tcxGltf.h - glTF 2.0 model loader for TrussC
// =============================================================================
//
// Loads .gltf/.glb files and creates TrussC Mesh + Material + Texture
// objects. Uses cgltf for parsing.
//
// Usage:
//   GltfModel model;
//   model.load("helmet.glb");
//   model.draw();   // draws all nodes with their materials
//
// Supports:
//   - Mesh geometry: positions, normals, UVs, tangents, indices
//   - PBR materials: baseColor, metallicRoughness, normal, emissive, occlusion
//   - Textures: embedded (GLB) and external file references
//   - Node hierarchy: flattened to world-space transforms on load
//
// Does NOT support (yet):
//   - Animation, skinning, morph targets
//   - Multiple scenes (loads the default scene or scene 0; a file without
//     scenes loads every root node)
//   - Cameras, lights (KHR_lights_punctual)
//   - Primitives whose POSITION or index accessor has no data in memory (no
//     buffer view, or a buffer without data), including the valid glTF case
//     of an index accessor that is all zeros plus sparse values: such a
//     primitive is skipped with a warning, and the rest of the model loads
//
// =============================================================================

#include <TrussC.h>
#include <string>
#include <vector>
#include <memory>

namespace tcx::gltf {

using namespace tc;

class GltfModel {
public:
    GltfModel() = default;
    ~GltfModel() = default;

    // Non-copyable (owns textures)
    GltfModel(const GltfModel&) = delete;
    GltfModel& operator=(const GltfModel&) = delete;

    // Movable
    GltfModel(GltfModel&&) = default;
    GltfModel& operator=(GltfModel&&) = default;

    // -------------------------------------------------------------------------
    // Loading
    // -------------------------------------------------------------------------

    // Load a .gltf or .glb file. Path is resolved via getDataPath().
    bool load(const std::string& path);

    bool isLoaded() const { return loaded_; }

    // -------------------------------------------------------------------------
    // Drawing
    // -------------------------------------------------------------------------

    // Draw all nodes using their associated PBR materials.
    // Requires setMaterial() to activate PBR lighting.
    void draw() const;

    // -------------------------------------------------------------------------
    // Access
    // -------------------------------------------------------------------------

    struct Node {
        Mesh mesh;
        Material material;
        Mat4 transform;
        std::string name;
    };

    size_t getNodeCount() const { return nodes_.size(); }
    const Node& getNode(size_t i) const { return nodes_[i]; }
    Node& getNode(size_t i) { return nodes_[i]; }

private:
    std::vector<Node> nodes_;
    // Textures are shared across materials; stored here to keep them alive
    std::vector<std::unique_ptr<Texture>> textures_;
    bool loaded_ = false;
};

namespace internal {

// Test hook, not a user setting: when set, load() calls it once for each
// texture it is about to read, so a test can make that step throw and check
// that load() recovers. nullptr (the default) turns it off. Set it only while
// no load() is running.
void setTextureLoadHookForTests(void (*hook)());

} // namespace internal

} // namespace tcx::gltf

// -----------------------------------------------------------------------------
// Backward compatibility. The canonical namespace is now `tcx::gltf`. This
// silent alias keeps older code compiling: flat `tcx::GltfModel`.
// DEPRECATED — removed in v1.0.0.
// (No [[deprecated]] attribute: under the usual `using namespace tc;` it would
//  warn on idiomatic unqualified use too. See README for migration.)
// -----------------------------------------------------------------------------
namespace tcx { using gltf::GltfModel; } // deprecated: remove at v1.0.0
