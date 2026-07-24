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

// Add light (up to 8 max). Registered by address in the CURRENT window's
// context — the Light must outlive its registration (prefer stable storage).
inline void addLight(Light& light) {
    auto& lights = internal::currentWindowContext().activeLights;
    if (lights.size() < internal::maxLights) {
        // Duplicate check
        auto it = std::find(lights.begin(), lights.end(), &light);
        if (it == lights.end()) {
            lights.push_back(&light);
        }
    }
}

// Remove light (from the current window's context)
inline void removeLight(Light& light) {
    auto& lights = internal::currentWindowContext().activeLights;
    auto it = std::find(lights.begin(), lights.end(), &light);
    if (it != lights.end()) {
        lights.erase(it);
    }
}

// Clear all lights (in the current window's context)
inline void clearLights() {
    internal::currentWindowContext().activeLights.clear();
}

// Get number of active lights (in the current window's context)
inline int getNumLights() {
    return static_cast<int>(internal::currentWindowContext().activeLights.size());
}

// ---------------------------------------------------------------------------
// Material
// ---------------------------------------------------------------------------

// Set PBR material for subsequent mesh draws (in the current window's context)
inline void setMaterial(Material& material) {
    internal::currentWindowContext().currentMaterial = &material;
}

// Clear material (revert to default rendering)
inline void clearMaterial() {
    internal::currentWindowContext().currentMaterial = nullptr;
}

// ---------------------------------------------------------------------------
// Shadow mapping
// ---------------------------------------------------------------------------

// Begin a shadow depth pass from the given light's point of view.
// The light must already be in the activeLights list (via addLight).
// Between begin/end, call shadowDraw() for each shadow-casting mesh.
// Can be called for up to 4 different lights per frame
// (internal::maxShadowLights) — each pass renders into its own layer of a
// shared shadow map array. Extra passes beyond the limit are ignored with a
// one-time warning. Re-running a pass for the SAME light twice in one frame
// overwrites that light's layer (one shadow map content per light per frame).
inline void beginShadowPass(Light& light) {
    auto& lights = internal::currentWindowContext().activeLights;
    int idx = -1;
    for (int i = 0; i < (int)lights.size(); i++) {
        if (lights[i] == &light) { idx = i; break; }
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
    internal::currentWindowContext().cameraPosition = pos;
}

inline void setCameraPosition(float x, float y, float z) {
    internal::currentWindowContext().cameraPosition = Vec3(x, y, z);
}

inline const Vec3& getCameraPosition() {
    return internal::currentWindowContext().cameraPosition;
}

} // namespace trussc
