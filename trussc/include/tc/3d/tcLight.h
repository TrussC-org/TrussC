#pragma once

// =============================================================================
// tcLight.h - Light (light source)
// =============================================================================
//
// Light source definition for Phong lighting model
// Lighting calculation is performed on CPU side
//
// Supported light types:
// - Directional: Parallel light source (like sunlight, constant direction regardless of position)
// - Point: Point light source (like a bulb, radiates from a position)
//
// =============================================================================

#include <cmath>
#include <algorithm>

namespace trussc {

// Forward declaration
class Material;

// ---------------------------------------------------------------------------
// LightType - Type of light
// ---------------------------------------------------------------------------
enum class LightType {
    Directional,    // Parallel light (sunlight)
    Point,          // Point light
    // Spot to be supported in future
};

// ---------------------------------------------------------------------------
// Light class
// ---------------------------------------------------------------------------
class Light {
public:
    Light() {
        // Default: white directional light from above
        type_ = LightType::Directional;
        direction_ = Vec3(0, -1, 0);  // From top to bottom
        position_ = Vec3(0, 0, 0);
        ambient_ = Color(0.2f, 0.2f, 0.2f, 1.0f);
        diffuse_ = Color(1.0f, 1.0f, 1.0f, 1.0f);
        specular_ = Color(1.0f, 1.0f, 1.0f, 1.0f);
        intensity_ = 1.0f;
        enabled_ = true;
        constantAttenuation_ = 1.0f;
        linearAttenuation_ = 0.0f;
        quadraticAttenuation_ = 0.0f;
    }

    // === Light type settings ===

    // Set as directional light (specify direction)
    void setDirectional(const Vec3& direction) {
        type_ = LightType::Directional;
        // Normalize and store direction (direction light travels)
        float len = std::sqrt(direction.x * direction.x +
                              direction.y * direction.y +
                              direction.z * direction.z);
        if (len > 0) {
            direction_ = Vec3(direction.x / len, direction.y / len, direction.z / len);
        }
    }

    void setDirectional(float dx, float dy, float dz) {
        setDirectional(Vec3(dx, dy, dz));
    }

    // Set as point light (specify position)
    void setPoint(const Vec3& position) {
        type_ = LightType::Point;
        position_ = position;
    }

    void setPoint(float x, float y, float z) {
        setPoint(Vec3(x, y, z));
    }

    LightType getType() const { return type_; }
    const Vec3& getDirection() const { return direction_; }
    const Vec3& getPosition() const { return position_; }

    // === Color settings ===

    void setAmbient(const Color& c) { ambient_ = c; }
    void setAmbient(float r, float g, float b, float a = 1.0f) {
        ambient_ = Color(r, g, b, a);
    }
    const Color& getAmbient() const { return ambient_; }

    void setDiffuse(const Color& c) { diffuse_ = c; }
    void setDiffuse(float r, float g, float b, float a = 1.0f) {
        diffuse_ = Color(r, g, b, a);
    }
    const Color& getDiffuse() const { return diffuse_; }

    void setSpecular(const Color& c) { specular_ = c; }
    void setSpecular(float r, float g, float b, float a = 1.0f) {
        specular_ = Color(r, g, b, a);
    }
    const Color& getSpecular() const { return specular_; }

    // === Intensity ===

    void setIntensity(float i) { intensity_ = i; }
    float getIntensity() const { return intensity_; }

    // === Attenuation (for Point light) ===
    // attenuation = 1.0 / (constant + linear * d + quadratic * d²)

    void setAttenuation(float constant, float linear, float quadratic) {
        constantAttenuation_ = constant;
        linearAttenuation_ = linear;
        quadraticAttenuation_ = quadratic;
    }

    // === Enable/Disable ===

    void enable() { enabled_ = true; }
    void disable() { enabled_ = false; }
    bool isEnabled() const { return enabled_; }

    // -------------------------------------------------------------------------
    // Lighting calculation
    // -------------------------------------------------------------------------

    // Calculate lighting for given position and normal
    // viewDir: direction from camera to vertex (normalized)
    Color calculate(const Vec3& worldPos, const Vec3& worldNormal,
                    const Material& material, const Vec3& viewPos) const {
        if (!enabled_) {
            return Color(0, 0, 0, 0);
        }

        // Calculate light direction
        Vec3 lightDir;
        float attenuation = 1.0f;

        if (type_ == LightType::Directional) {
            // Directional: direction is constant (opposite of light travel direction)
            lightDir = Vec3(-direction_.x, -direction_.y, -direction_.z);
        } else {
            // Point light: direction from vertex to light
            Vec3 toLight(position_.x - worldPos.x,
                         position_.y - worldPos.y,
                         position_.z - worldPos.z);
            float dist = std::sqrt(toLight.x * toLight.x +
                                   toLight.y * toLight.y +
                                   toLight.z * toLight.z);
            if (dist > 0) {
                lightDir = Vec3(toLight.x / dist, toLight.y / dist, toLight.z / dist);
                // Attenuation calculation
                attenuation = 1.0f / (constantAttenuation_ +
                                      linearAttenuation_ * dist +
                                      quadraticAttenuation_ * dist * dist);
            } else {
                lightDir = Vec3(0, 1, 0);
            }
        }

        // View direction (from vertex to camera)
        Vec3 viewDir(viewPos.x - worldPos.x,
                     viewPos.y - worldPos.y,
                     viewPos.z - worldPos.z);
        float viewLen = std::sqrt(viewDir.x * viewDir.x +
                                  viewDir.y * viewDir.y +
                                  viewDir.z * viewDir.z);
        if (viewLen > 0) {
            viewDir.x /= viewLen;
            viewDir.y /= viewLen;
            viewDir.z /= viewLen;
        }

        // Phong lighting calculation
        return calculatePhong(worldNormal, lightDir, viewDir, material, attenuation);
    }

private:
    // Lighting calculation using Phong model
    Color calculatePhong(const Vec3& normal, const Vec3& lightDir,
                         const Vec3& viewDir, const Material& material,
                         float attenuation) const {
        const Color& matAmbient = material.getAmbient();
        const Color& matDiffuse = material.getDiffuse();
        const Color& matSpecular = material.getSpecular();
        float shininess = material.getShininess();

        // === Ambient ===
        float ar = ambient_.r * matAmbient.r;
        float ag = ambient_.g * matAmbient.g;
        float ab = ambient_.b * matAmbient.b;

        // === Diffuse ===
        // N dot L (dot product of normal and light direction)
        float NdotL = normal.x * lightDir.x +
                      normal.y * lightDir.y +
                      normal.z * lightDir.z;
        NdotL = std::max(0.0f, NdotL);

        float dr = diffuse_.r * matDiffuse.r * NdotL;
        float dg = diffuse_.g * matDiffuse.g * NdotL;
        float db = diffuse_.b * matDiffuse.b * NdotL;

        // === Specular ===
        float sr = 0, sg = 0, sb = 0;
        if (NdotL > 0) {
            // Reflection vector R = 2(N dot L)N - L
            float twoNdotL = 2.0f * NdotL;
            Vec3 reflect(twoNdotL * normal.x - lightDir.x,
                         twoNdotL * normal.y - lightDir.y,
                         twoNdotL * normal.z - lightDir.z);

            // R dot V (dot product of reflection vector and view direction)
            float RdotV = reflect.x * viewDir.x +
                          reflect.y * viewDir.y +
                          reflect.z * viewDir.z;
            RdotV = std::max(0.0f, RdotV);

            float specFactor = std::pow(RdotV, shininess);
            sr = specular_.r * matSpecular.r * specFactor;
            sg = specular_.g * matSpecular.g * specFactor;
            sb = specular_.b * matSpecular.b * specFactor;
        }

        // Combine (apply intensity and attenuation)
        float factor = intensity_ * attenuation;
        float r = ar + (dr + sr) * factor;
        float g = ag + (dg + sg) * factor;
        float b = ab + (db + sb) * factor;

        return Color(r, g, b, matDiffuse.a);
    }

    LightType type_;
    Vec3 direction_;     // For Directional (direction light travels)
    Vec3 position_;      // For Point
    Color ambient_;      // Ambient light color
    Color diffuse_;      // Diffuse light color
    Color specular_;     // Specular reflection color
    float intensity_;    // Intensity
    bool enabled_;

    // Attenuation parameters (for Point light)
    float constantAttenuation_;
    float linearAttenuation_;
    float quadraticAttenuation_;
};

// ---------------------------------------------------------------------------
// Lighting calculation helper (called from Mesh)
// ---------------------------------------------------------------------------

// Calculate lighting result for given position and normal
// Sum contributions from all active lights
inline Color calculateLighting(const Vec3& worldPos, const Vec3& worldNormal,
                               const Material& material) {
    if (internal::activeLights.empty()) {
        // If no lights, return material's Diffuse as-is
        return material.getDiffuse();
    }

    // Emission
    const Color& emission = material.getEmission();
    float r = emission.r;
    float g = emission.g;
    float b = emission.b;

    // Sum contributions from each light
    for (Light* light : internal::activeLights) {
        if (light && light->isEnabled()) {
            Color contribution = light->calculate(worldPos, worldNormal,
                                                  material, internal::cameraPosition);
            r += contribution.r;
            g += contribution.g;
            b += contribution.b;
        }
    }

    // Clamp
    r = std::min(1.0f, r);
    g = std::min(1.0f, g);
    b = std::min(1.0f, b);

    return Color(r, g, b, material.getDiffuse().a);
}

} // namespace trussc
