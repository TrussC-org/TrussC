#pragma once

// =============================================================================
// tcPbrMaterial.h - Physically Based Rendering material
// =============================================================================
//
// Metallic-roughness workflow material for use with LightingMode::GpuPbr.
// Evaluated on GPU by the meshPbr shader.
//
// Parameters follow the glTF 2.0 PBR metallic-roughness specification:
// - baseColor: albedo for dielectrics, F0 tint for metals
// - metallic: 0 = dielectric, 1 = pure metal
// - roughness: 0 = mirror, 1 = fully diffuse
// - ao: ambient occlusion scalar (multiplier on indirect contribution)
// - emissive + emissiveStrength: self-illumination
//
// =============================================================================

#include <algorithm>

namespace trussc {

class Texture;  // forward declare (full definition in tcTexture.h, included later)

class PbrMaterial {
public:
    PbrMaterial() = default;

    // --- baseColor ---
    PbrMaterial& setBaseColor(const Color& c) { baseColor_ = c; return *this; }
    PbrMaterial& setBaseColor(float r, float g, float b, float a = 1.0f) {
        baseColor_ = Color(r, g, b, a);
        return *this;
    }
    const Color& getBaseColor() const { return baseColor_; }

    // --- metallic ---
    PbrMaterial& setMetallic(float m) {
        metallic_ = std::clamp(m, 0.0f, 1.0f);
        return *this;
    }
    float getMetallic() const { return metallic_; }

    // --- roughness ---
    // Clamped to a small minimum to avoid singular GGX at perfect mirror.
    PbrMaterial& setRoughness(float r) {
        roughness_ = std::clamp(r, 0.045f, 1.0f);
        return *this;
    }
    float getRoughness() const { return roughness_; }

    // --- ao ---
    PbrMaterial& setAo(float ao) {
        ao_ = std::clamp(ao, 0.0f, 1.0f);
        return *this;
    }
    float getAo() const { return ao_; }

    // --- emissive ---
    PbrMaterial& setEmissive(const Color& c) { emissive_ = c; return *this; }
    PbrMaterial& setEmissive(float r, float g, float b) {
        emissive_ = Color(r, g, b, 1.0f);
        return *this;
    }
    const Color& getEmissive() const { return emissive_; }

    PbrMaterial& setEmissiveStrength(float s) {
        emissiveStrength_ = std::max(0.0f, s);
        return *this;
    }
    float getEmissiveStrength() const { return emissiveStrength_; }

    // -------------------------------------------------------------------------
    // Preset materials
    // -------------------------------------------------------------------------

    static PbrMaterial gold() {
        PbrMaterial m;
        m.setBaseColor(1.000f, 0.766f, 0.336f);
        m.setMetallic(1.0f);
        m.setRoughness(0.15f);
        return m;
    }

    static PbrMaterial silver() {
        PbrMaterial m;
        m.setBaseColor(0.972f, 0.960f, 0.915f);
        m.setMetallic(1.0f);
        m.setRoughness(0.10f);
        return m;
    }

    static PbrMaterial copper() {
        PbrMaterial m;
        m.setBaseColor(0.955f, 0.637f, 0.538f);
        m.setMetallic(1.0f);
        m.setRoughness(0.20f);
        return m;
    }

    static PbrMaterial iron() {
        PbrMaterial m;
        m.setBaseColor(0.560f, 0.570f, 0.580f);
        m.setMetallic(1.0f);
        m.setRoughness(0.35f);
        return m;
    }

    // Plastic-like dielectric with the given color
    static PbrMaterial plastic(const Color& baseColor, float roughness = 0.5f) {
        PbrMaterial m;
        m.setBaseColor(baseColor);
        m.setMetallic(0.0f);
        m.setRoughness(roughness);
        return m;
    }

    // Rubber-like dielectric (high roughness)
    static PbrMaterial rubber(const Color& baseColor) {
        PbrMaterial m;
        m.setBaseColor(baseColor);
        m.setMetallic(0.0f);
        m.setRoughness(0.85f);
        return m;
    }

    // --- Normal map (optional) ---
    PbrMaterial& setNormalMap(const Texture* tex) { normalMap_ = tex; return *this; }
    const Texture* getNormalMap() const { return normalMap_; }
    bool hasNormalMap() const { return normalMap_ != nullptr; }

private:
    Color baseColor_        = Color(0.8f, 0.8f, 0.8f, 1.0f);
    float metallic_         = 0.0f;
    float roughness_        = 0.5f;
    float ao_               = 1.0f;
    Color emissive_         = Color(0.0f, 0.0f, 0.0f, 1.0f);
    float emissiveStrength_ = 0.0f;
    const Texture* normalMap_ = nullptr;
};

} // namespace trussc
