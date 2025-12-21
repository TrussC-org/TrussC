#pragma once

// =============================================================================
// tcMaterial.h - Material
// =============================================================================
//
// Material definition for Phong lighting model
// Used for lighting calculations on CPU side
//
// =============================================================================

namespace trussc {

// ---------------------------------------------------------------------------
// Material class
// ---------------------------------------------------------------------------
class Material {
public:
    Material() {
        // Default: white plastic-like
        ambient_ = Color(0.2f, 0.2f, 0.2f, 1.0f);
        diffuse_ = Color(0.8f, 0.8f, 0.8f, 1.0f);
        specular_ = Color(0.5f, 0.5f, 0.5f, 1.0f);
        emission_ = Color(0.0f, 0.0f, 0.0f, 1.0f);
        shininess_ = 32.0f;
    }

    // === Ambient (ambient reflectance) ===
    void setAmbient(const Color& c) { ambient_ = c; }
    void setAmbient(float r, float g, float b, float a = 1.0f) {
        ambient_ = Color(r, g, b, a);
    }
    const Color& getAmbient() const { return ambient_; }

    // === Diffuse (diffuse reflectance) ===
    void setDiffuse(const Color& c) { diffuse_ = c; }
    void setDiffuse(float r, float g, float b, float a = 1.0f) {
        diffuse_ = Color(r, g, b, a);
    }
    const Color& getDiffuse() const { return diffuse_; }

    // === Specular (specular reflectance) ===
    void setSpecular(const Color& c) { specular_ = c; }
    void setSpecular(float r, float g, float b, float a = 1.0f) {
        specular_ = Color(r, g, b, a);
    }
    const Color& getSpecular() const { return specular_; }

    // === Emission (self-illumination) ===
    void setEmission(const Color& c) { emission_ = c; }
    void setEmission(float r, float g, float b, float a = 1.0f) {
        emission_ = Color(r, g, b, a);
    }
    const Color& getEmission() const { return emission_; }

    // === Shininess ===
    // Higher values produce sharper specular highlights (around 1-128)
    void setShininess(float s) { shininess_ = s; }
    float getShininess() const { return shininess_; }

    // === Batch setting ===
    // Set Ambient/Diffuse together from color (simple setting)
    void setColor(const Color& c) {
        ambient_ = Color(c.r * 0.2f, c.g * 0.2f, c.b * 0.2f, c.a);
        diffuse_ = c;
    }

    // -------------------------------------------------------------------------
    // Preset materials
    // -------------------------------------------------------------------------

    // Gold
    static Material gold() {
        Material mat;
        mat.setAmbient(0.24725f, 0.1995f, 0.0745f);
        mat.setDiffuse(0.75164f, 0.60648f, 0.22648f);
        mat.setSpecular(0.628281f, 0.555802f, 0.366065f);
        mat.setShininess(51.2f);
        return mat;
    }

    // Silver
    static Material silver() {
        Material mat;
        mat.setAmbient(0.19225f, 0.19225f, 0.19225f);
        mat.setDiffuse(0.50754f, 0.50754f, 0.50754f);
        mat.setSpecular(0.508273f, 0.508273f, 0.508273f);
        mat.setShininess(51.2f);
        return mat;
    }

    // Bronze
    static Material bronze() {
        Material mat;
        mat.setAmbient(0.2125f, 0.1275f, 0.054f);
        mat.setDiffuse(0.714f, 0.4284f, 0.18144f);
        mat.setSpecular(0.393548f, 0.271906f, 0.166721f);
        mat.setShininess(25.6f);
        return mat;
    }

    // Copper
    static Material copper() {
        Material mat;
        mat.setAmbient(0.19125f, 0.0735f, 0.0225f);
        mat.setDiffuse(0.7038f, 0.27048f, 0.0828f);
        mat.setSpecular(0.256777f, 0.137622f, 0.086014f);
        mat.setShininess(12.8f);
        return mat;
    }

    // Plastic (specified color)
    static Material plastic(const Color& baseColor) {
        Material mat;
        mat.setAmbient(baseColor.r * 0.1f, baseColor.g * 0.1f, baseColor.b * 0.1f);
        mat.setDiffuse(baseColor.r * 0.6f, baseColor.g * 0.6f, baseColor.b * 0.6f);
        mat.setSpecular(0.7f, 0.7f, 0.7f);
        mat.setShininess(32.0f);
        return mat;
    }

    // Rubber (specified color, low gloss)
    static Material rubber(const Color& baseColor) {
        Material mat;
        mat.setAmbient(baseColor.r * 0.05f, baseColor.g * 0.05f, baseColor.b * 0.05f);
        mat.setDiffuse(baseColor.r * 0.5f, baseColor.g * 0.5f, baseColor.b * 0.5f);
        mat.setSpecular(0.1f, 0.1f, 0.1f);
        mat.setShininess(10.0f);
        return mat;
    }

    // Emerald
    static Material emerald() {
        Material mat;
        mat.setAmbient(0.0215f, 0.1745f, 0.0215f);
        mat.setDiffuse(0.07568f, 0.61424f, 0.07568f);
        mat.setSpecular(0.633f, 0.727811f, 0.633f);
        mat.setShininess(76.8f);
        return mat;
    }

    // Ruby
    static Material ruby() {
        Material mat;
        mat.setAmbient(0.1745f, 0.01175f, 0.01175f);
        mat.setDiffuse(0.61424f, 0.04136f, 0.04136f);
        mat.setSpecular(0.727811f, 0.626959f, 0.626959f);
        mat.setShininess(76.8f);
        return mat;
    }

private:
    Color ambient_;      // Ambient reflectance
    Color diffuse_;      // Diffuse reflectance
    Color specular_;     // Specular reflectance
    Color emission_;     // Self-illumination
    float shininess_;    // Shininess (Phong exponent)
};

} // namespace trussc
