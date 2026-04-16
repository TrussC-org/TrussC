#pragma once

// =============================================================================
// tcxGltf.h - glTF 2.0 model loader for TrussC
// =============================================================================
//
// Loads .gltf/.glb files and creates TrussC Mesh + PbrMaterial + Texture
// objects. Uses cgltf for parsing.
//
// Usage:
//   GltfModel model;
//   model.load("helmet.glb");
//   model.draw();   // draws all nodes with their PBR materials
//
// Supports:
//   - Mesh geometry: positions, normals, UVs, tangents, indices
//   - PBR materials: baseColor, metallicRoughness, normal, emissive, occlusion
//   - Textures: embedded (GLB) and external file references
//   - Node hierarchy: flattened to world-space transforms on load
//
// Does NOT support (yet):
//   - Animation, skinning, morph targets
//   - Multiple scenes (loads scene 0 or the default scene)
//   - Cameras, lights (KHR_lights_punctual)
//
// =============================================================================

#include <TrussC.h>
#include <string>
#include <vector>
#include <memory>

namespace tcx {

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
    // Requires LightingMode::GpuPbr to be active (via setPbrMaterial).
    void draw() const;

    // -------------------------------------------------------------------------
    // Access
    // -------------------------------------------------------------------------

    struct Node {
        Mesh mesh;
        PbrMaterial material;
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

} // namespace tcx
