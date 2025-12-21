#pragma once

// =============================================================================
// tc3DGraphics.h - 3D graphics (lighting etc.)
// =============================================================================
//
// Lighting system API
// Performs lighting calculations on CPU and reflects in vertex colors
//
// Note: State is defined in tcLightingState.h
//
// =============================================================================

#include <algorithm>

namespace trussc {

// State in internal namespace is defined in tcLightingState.h

// ---------------------------------------------------------------------------
// Lighting API
// ---------------------------------------------------------------------------

// Enable lighting
inline void enableLighting() {
    internal::lightingEnabled = true;
}

// Disable lighting
inline void disableLighting() {
    internal::lightingEnabled = false;
}

// Whether lighting is enabled
inline bool isLightingEnabled() {
    return internal::lightingEnabled;
}

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

// Set material
inline void setMaterial(Material& material) {
    internal::currentMaterial = &material;
}

// Clear material (revert to default)
inline void clearMaterial() {
    internal::currentMaterial = nullptr;
}

// Set camera position (for specular calculation)
inline void setCameraPosition(const Vec3& pos) {
    internal::cameraPosition = pos;
}

inline void setCameraPosition(float x, float y, float z) {
    internal::cameraPosition = Vec3(x, y, z);
}

// Get camera position
inline const Vec3& getCameraPosition() {
    return internal::cameraPosition;
}

// calculateLighting() is defined in tcLight.h

} // namespace trussc
