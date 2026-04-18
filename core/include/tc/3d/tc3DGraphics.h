#pragma once

// =============================================================================
// tc3DGraphics.h - 3D graphics (lighting, materials, shadows)
// =============================================================================
//
// PBR lighting system API.
// Material is evaluated on GPU via the meshPbr shader (Cook-Torrance GGX).
//
// Note: State is defined in tcLightingState.h
//
// =============================================================================

#include <algorithm>

namespace trussc {

// State in internal namespace is defined in tcLightingState.h

// ---------------------------------------------------------------------------
// Light management
// ---------------------------------------------------------------------------

// Add light (up to 8 max)
inline void addLight(Light& light) {
    if (internal::activeLights.size() < internal::maxLights) {
        // Duplicate check
        auto it = std::find(internal::activeLights.begin(),
                            internal::activeLights.end(), &light);
        if (it == internal::activeLights.end()) {
            internal::activeLights.push_back(&light);
        }
    }
}

// Remove light
inline void removeLight(Light& light) {
    auto it = std::find(internal::activeLights.begin(),
                        internal::activeLights.end(), &light);
    if (it != internal::activeLights.end()) {
        internal::activeLights.erase(it);
    }
}

// Clear all lights
inline void clearLights() {
    internal::activeLights.clear();
}

// Get number of active lights
inline int getNumLights() {
    return static_cast<int>(internal::activeLights.size());
}

// ---------------------------------------------------------------------------
// Material
// ---------------------------------------------------------------------------

// Set PBR material for subsequent mesh draws
inline void setMaterial(Material& material) {
    internal::currentMaterial = &material;
}

// Clear material (revert to default rendering)
inline void clearMaterial() {
    internal::currentMaterial = nullptr;
}

// ---------------------------------------------------------------------------
// Shadow mapping
// ---------------------------------------------------------------------------

// Begin a shadow depth pass from the given light's point of view.
// The light must already be in the activeLights list (via addLight).
// Between begin/end, call shadowDraw() for each shadow-casting mesh.
inline void beginShadowPass(Light& light) {
    int idx = -1;
    for (int i = 0; i < (int)internal::activeLights.size(); i++) {
        if (internal::activeLights[i] == &light) { idx = i; break; }
    }
    if (idx < 0) return;
    internal::getPbrPipeline().beginShadowPass(idx);
}

inline void endShadowPass() {
    internal::getPbrPipeline().endShadowPass();
}

// Draw a mesh into the shadow depth pass (depth-only, no material evaluation).
inline void shadowDraw(const Mesh& mesh) {
    internal::getPbrPipeline().shadowDrawMesh(mesh);
}

// ---------------------------------------------------------------------------
// Camera position (for specular / PBR view vector)
// ---------------------------------------------------------------------------

inline void setCameraPosition(const Vec3& pos) {
    internal::cameraPosition = pos;
}

inline void setCameraPosition(float x, float y, float z) {
    internal::cameraPosition = Vec3(x, y, z);
}

inline const Vec3& getCameraPosition() {
    return internal::cameraPosition;
}

} // namespace trussc
