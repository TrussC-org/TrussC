#pragma once

// =============================================================================
// tcLightingState.h - Lighting global state (internal use)
// =============================================================================
//
// Must be included before tc3DGraphics.h
// to allow tcMesh.h to access lighting state
//
// =============================================================================

#include <vector>

namespace trussc {

// Forward declarations
class Light;
class Material;

// ---------------------------------------------------------------------------
// internal namespace - Lighting global state
// ---------------------------------------------------------------------------
namespace internal {
    // Whether lighting is enabled
    inline bool lightingEnabled = false;

    // List of active lights (up to 8)
    inline std::vector<Light*> activeLights;
    inline constexpr int maxLights = 8;

    // Current material
    inline Material* currentMaterial = nullptr;

    // Camera position (for specular calculation)
    inline Vec3 cameraPosition = {0, 0, 0};
}

} // namespace trussc
