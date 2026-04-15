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
class PbrMaterial;

// ---------------------------------------------------------------------------
// LightingMode - selects which lighting pipeline Mesh::draw() uses
// ---------------------------------------------------------------------------
// CpuPhong: legacy path. Per-vertex Phong computed on CPU, baked into vertex
//           colors, streamed through sokol_gl. Uses Material + enableLighting().
// GpuPbr:   new path. Per-pixel PBR evaluated on GPU through the meshPbr
//           shader. Uses PbrMaterial + addLight(). Requires normals on mesh.
// ---------------------------------------------------------------------------
enum class LightingMode {
    CpuPhong,
    GpuPbr,
};

// ---------------------------------------------------------------------------
// internal namespace - Lighting global state
// ---------------------------------------------------------------------------
namespace internal {
    // Active lighting pipeline. Defaults to CpuPhong for backward compat.
    inline LightingMode lightingMode = LightingMode::CpuPhong;

    // Whether lighting is enabled (applies to CpuPhong path only)
    inline bool lightingEnabled = false;

    // List of active lights (up to 8). Shared between CpuPhong and GpuPbr.
    inline std::vector<Light*> activeLights;
    inline constexpr int maxLights = 8;

    // Current Phong material (CpuPhong path)
    inline Material* currentMaterial = nullptr;

    // Current PBR material (GpuPbr path)
    inline PbrMaterial* currentPbrMaterial = nullptr;

    // Camera position (for specular calculation / PBR view vector)
    inline Vec3 cameraPosition = {0, 0, 0};

    // Global exposure scalar applied before ACES tonemap (GpuPbr path only)
    inline float pbrExposure = 1.0f;
}

} // namespace trussc
