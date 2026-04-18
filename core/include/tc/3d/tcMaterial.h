#pragma once

// =============================================================================
// tcMaterial.h - PBR Material
// =============================================================================
//
// Metallic-roughness workflow material evaluated on GPU by the meshPbr shader.
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

class Material {
public:
    Material() = default;

    // --- baseColor ---
    Material& setBaseColor(const Color& c) { baseColor_ = c; return *this; }
    Material& setBaseColor(float r, float g, float b, float a = 1.0f) {
        baseColor_ = Color(r, g, b, a);
        return *this;
    }
    const Color& getBaseColor() const { return baseColor_; }

    // --- metallic ---
    Material& setMetallic(float m) {
        metallic_ = std::clamp(m, 0.0f, 1.0f);
        return *this;
    }
    float getMetallic() const { return metallic_; }

    // --- roughness ---
    // Clamped to a small minimum to avoid singular GGX at perfect mirror.
    Material& setRoughness(float r) {
        roughness_ = std::clamp(r, 0.045f, 1.0f);
        return *this;
    }
    float getRoughness() const { return roughness_; }

    // --- ao ---
    Material& setAo(float ao) {
        ao_ = std::clamp(ao, 0.0f, 1.0f);
        return *this;
    }
    float getAo() const { return ao_; }

    // --- emissive ---
    Material& setEmissive(const Color& c) { emissive_ = c; return *this; }
    Material& setEmissive(float r, float g, float b) {
        emissive_ = Color(r, g, b, 1.0f);
        return *this;
    }
    const Color& getEmissive() const { return emissive_; }

    Material& setEmissiveStrength(float s) {
        emissiveStrength_ = std::max(0.0f, s);
        return *this;
    }
    float getEmissiveStrength() const { return emissiveStrength_; }

    // -------------------------------------------------------------------------
    // Preset materials
    // -------------------------------------------------------------------------

    static Material gold() {
        Material m;
        m.setBaseColor(1.000f, 0.766f, 0.336f);
        m.setMetallic(1.0f);
        m.setRoughness(0.15f);
        return m;
    }

    static Material silver() {
        Material m;
        m.setBaseColor(0.972f, 0.960f, 0.915f);
        m.setMetallic(1.0f);
        m.setRoughness(0.10f);
        return m;
    }

    static Material copper() {
        Material m;
        m.setBaseColor(0.955f, 0.637f, 0.538f);
        m.setMetallic(1.0f);
        m.setRoughness(0.20f);
        return m;
    }

    static Material iron() {
        Material m;
        m.setBaseColor(0.560f, 0.570f, 0.580f);
        m.setMetallic(1.0f);
        m.setRoughness(0.35f);
        return m;
    }

    static Material bronze() {
        Material m;
        m.setBaseColor(0.800f, 0.500f, 0.200f);
        m.setMetallic(1.0f);
        m.setRoughness(0.30f);
        return m;
    }

    static Material emerald() {
        Material m;
        m.setBaseColor(0.150f, 0.650f, 0.150f);
        m.setMetallic(0.0f);
        m.setRoughness(0.15f);
        return m;
    }

    static Material ruby() {
        Material m;
        m.setBaseColor(0.650f, 0.100f, 0.100f);
        m.setMetallic(0.0f);
        m.setRoughness(0.15f);
        return m;
    }

    // Plastic-like dielectric with the given color
    static Material plastic(const Color& baseColor, float roughness = 0.5f) {
        Material m;
        m.setBaseColor(baseColor);
        m.setMetallic(0.0f);
        m.setRoughness(roughness);
        return m;
    }

    // Rubber-like dielectric (high roughness)
    static Material rubber(const Color& baseColor) {
        Material m;
        m.setBaseColor(baseColor);
        m.setMetallic(0.0f);
        m.setRoughness(0.85f);
        return m;
    }

    // Convert Phong material parameters to PBR.
    // Uses the physically-derived Filament formula: roughness = sqrt(2 / (shininess + 2))
    // Metallic is estimated from specular color luminance (heuristic).
    // Input colors are assumed to be in sRGB space (typical for OBJ/MTL files)
    // and are converted to linear for PBR shader consumption.
    static Material fromPhong(const Color& diffuse, const Color& specular,
                              float shininess, const Color& emissive = Color(0,0,0)) {
        // sRGB to linear conversion
        auto toLinear = [](float s) { return std::pow(s, 2.2f); };
        Material m;
        m.setBaseColor(toLinear(diffuse.r), toLinear(diffuse.g), toLinear(diffuse.b), diffuse.a);
        // Roughness: match Phong cosⁿ lobe width with GGX distribution
        float rough = std::sqrt(2.0f / (shininess + 2.0f));
        m.setRoughness(rough);
        // Metallic heuristic: if specular color is similar to diffuse, likely metallic.
        // For typical Phong materials (white specular on colored diffuse), metallic ≈ 0.
        float sR = toLinear(specular.r), sG = toLinear(specular.g), sB = toLinear(specular.b);
        float specLum = sR * 0.2126f + sG * 0.7152f + sB * 0.0722f;
        m.setMetallic(specLum > 0.5f ? std::clamp(specLum, 0.0f, 1.0f) : 0.0f);
        // Emissive
        float emLum = emissive.r + emissive.g + emissive.b;
        if (emLum > 0.001f) {
            m.setEmissive(toLinear(emissive.r), toLinear(emissive.g), toLinear(emissive.b));
            m.setEmissiveStrength(1.0f);
        }
        return m;
    }

    // --- Normal map (optional) ---
    Material& setNormalMap(const Texture* tex) { normalMap_ = tex; return *this; }
    const Texture* getNormalMap() const { return normalMap_; }
    bool hasNormalMap() const { return normalMap_ != nullptr; }

    // -------------------------------------------------------------------------
    // PBR texture maps (optional, glTF 2.0 compatible)
    // -------------------------------------------------------------------------
    // Each texture modulates the corresponding scalar parameter. When bound,
    // the shader samples the texture and multiplies by the scalar factor.
    // When not bound, only the scalar is used.

    // Base color texture (RGBA). RGB multiplied with baseColor scalar.
    // Alpha multiplied with baseColor.a for cutout transparency.
    Material& setBaseColorTexture(const Texture* tex) { baseColorTex_ = tex; return *this; }
    const Texture* getBaseColorTexture() const { return baseColorTex_; }
    bool hasBaseColorTexture() const { return baseColorTex_ != nullptr; }

    // Metallic-roughness texture (glTF convention: G=roughness, B=metallic).
    // Multiplied with scalar metallic/roughness values.
    Material& setMetallicRoughnessTexture(const Texture* tex) { metallicRoughnessTex_ = tex; return *this; }
    const Texture* getMetallicRoughnessTexture() const { return metallicRoughnessTex_; }
    bool hasMetallicRoughnessTexture() const { return metallicRoughnessTex_ != nullptr; }

    // Emissive texture (RGB). Multiplied with emissive scalar color.
    Material& setEmissiveTexture(const Texture* tex) { emissiveTex_ = tex; return *this; }
    const Texture* getEmissiveTexture() const { return emissiveTex_; }
    bool hasEmissiveTexture() const { return emissiveTex_ != nullptr; }

    // Occlusion texture (R channel). Multiplied with ao scalar.
    Material& setOcclusionTexture(const Texture* tex) { occlusionTex_ = tex; return *this; }
    const Texture* getOcclusionTexture() const { return occlusionTex_; }
    bool hasOcclusionTexture() const { return occlusionTex_ != nullptr; }

private:
    Color baseColor_        = Color(0.8f, 0.8f, 0.8f, 1.0f);
    float metallic_         = 0.0f;
    float roughness_        = 0.5f;
    float ao_               = 1.0f;
    Color emissive_         = Color(0.0f, 0.0f, 0.0f, 1.0f);
    float emissiveStrength_ = 0.0f;
    const Texture* normalMap_ = nullptr;
    const Texture* baseColorTex_ = nullptr;
    const Texture* metallicRoughnessTex_ = nullptr;
    const Texture* emissiveTex_ = nullptr;
    const Texture* occlusionTex_ = nullptr;
};

} // namespace trussc
