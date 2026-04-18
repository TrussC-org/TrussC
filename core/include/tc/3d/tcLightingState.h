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
    // List of active lights (up to 8).
    inline std::vector<Light*> activeLights;
    inline constexpr int maxLights = 8;

    // Current material (PBR metallic-roughness)
    inline Material* currentMaterial = nullptr;

    // Camera position (for specular calculation / PBR view vector)
    inline Vec3 cameraPosition = {0, 0, 0};

    // Global exposure scalar applied before ACES tonemap
    inline float pbrExposure = 1.0f;
}

} // namespace trussc
