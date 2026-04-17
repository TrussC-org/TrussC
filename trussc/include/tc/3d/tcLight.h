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

// Forward declarations
class Material;
class Texture;
class IesProfile;

// ---------------------------------------------------------------------------
// LightType - Type of light
// ---------------------------------------------------------------------------
enum class LightType {
    Directional,    // Parallel light (sunlight)
    Point,          // Point light
    Spot,           // Spot light (point + cone)
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

    // Set as spot light (cone with smooth falloff between inner and outer angle)
    // Angles are half-angles in radians, matching glTF KHR_lights_punctual.
    // Default: innerHalfAngle=0 (sharp center), outerHalfAngle=π/4 (90° full cone)
    void setSpot(const Vec3& position, const Vec3& direction,
                 float innerHalfAngle = 0.0f, float outerHalfAngle = 0.7854f) {
        type_ = LightType::Spot;
        position_ = position;
        float len = std::sqrt(direction.x * direction.x +
                              direction.y * direction.y +
                              direction.z * direction.z);
        if (len > 0) {
            direction_ = Vec3(direction.x / len, direction.y / len, direction.z / len);
        }
        spotInnerCos_ = std::cos(innerHalfAngle);
        spotOuterCos_ = std::cos(outerHalfAngle);
    }

    void setSpot(float px, float py, float pz,
                 float dx, float dy, float dz,
                 float innerHalfAngle = 0.0f, float outerHalfAngle = 0.7854f) {
        setSpot(Vec3(px, py, pz), Vec3(dx, dy, dz), innerHalfAngle, outerHalfAngle);
    }

    float getSpotInnerCos() const { return spotInnerCos_; }
    float getSpotOuterCos() const { return spotOuterCos_; }

    // === Projector (texture projection on Spot light) ===

    // Set a texture to project through the spot cone. The texture modulates
    // the light's color per-pixel in the projected area. Pass nullptr to
    // disable. The Texture must remain alive while the Light is in use.
    void setProjectionTexture(const Texture* tex) { projectionTexture_ = tex; }
    const Texture* getProjectionTexture() const { return projectionTexture_; }
    bool hasProjectionTexture() const { return projectionTexture_ != nullptr; }

    // Lens shift ([-1, 1], fraction of half-frame). Shifts the optical axis
    // relative to the projected image center — same concept as a real
    // projector's lens shift dial. Only meaningful for Spot/Projector lights.
    void setLensShift(float sx, float sy) { lensShiftX_ = sx; lensShiftY_ = sy; }
    float getLensShiftX() const { return lensShiftX_; }
    float getLensShiftY() const { return lensShiftY_; }

    // Aspect ratio of the projected image (width/height). Defaults to 16/9.
    // Overridden automatically from the texture dimensions if set.
    void setProjectorAspect(float a) { projectorAspect_ = a; }
    float getProjectorAspect() const { return projectorAspect_; }

    // Build the projector's view-projection matrix from spot params + lens shift.
    // Used by PbrPipeline to fill the projectorViewProj uniform.
    Mat4 computeProjectorViewProj(float nearClip = 0.1f, float farClip = 10000.0f) const {
        // View matrix: look along spot direction from position
        Vec3 up(0.0f, 1.0f, 0.0f);
        if (std::abs(direction_.y) > 0.99f) up = Vec3(0.0f, 0.0f, 1.0f);
        Vec3 target = Vec3(position_.x + direction_.x,
                           position_.y + direction_.y,
                           position_.z + direction_.z);
        Mat4 view = Mat4::lookAt(position_, target, up);

        // Projection: asymmetric frustum with lens shift
        float outerAngle = std::acos(std::max(-1.0f, std::min(1.0f, spotOuterCos_)));
        float halfH = nearClip * std::tan(outerAngle);
        float aspect = getProjectorAspect();
        float halfW = halfH * aspect;

        float shiftX = lensShiftX_ * halfW;
        float shiftY = lensShiftY_ * halfH;

        Mat4 proj = Mat4::frustum(-halfW + shiftX, halfW + shiftX,
                                  -halfH + shiftY, halfH + shiftY,
                                  nearClip, farClip);
        return proj * view;
    }

    // === IES photometric profile ===

    // Attach an IES angular intensity profile to this light. The IesProfile
    // must remain alive while the Light is in use (weak pointer).
    // IES modulates light intensity by angular distribution independently of
    // the projector texture (which modulates color).
    void setIesProfile(const IesProfile* ies) { iesProfile_ = ies; }
    const IesProfile* getIesProfile() const { return iesProfile_; }
    bool hasIesProfile() const { return iesProfile_ != nullptr; }

    // === Shadow mapping ===

    // Enable shadow casting for this light. Only one light with shadows is
    // supported at a time (v1). Resolution controls the depth texture size.
    void enableShadow(int resolution = 1024) { shadowEnabled_ = true; shadowResolution_ = resolution; }
    void disableShadow() { shadowEnabled_ = false; }
    bool isShadowEnabled() const { return shadowEnabled_; }
    int getShadowResolution() const { return shadowResolution_; }
    void setShadowBias(float bias) { shadowBias_ = bias; }
    float getShadowBias() const { return shadowBias_; }

    // TODO: focus blur requires aperture integration or prefiltered mip LOD heuristic

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

    float getConstantAttenuation() const { return constantAttenuation_; }
    float getLinearAttenuation() const { return linearAttenuation_; }
    float getQuadraticAttenuation() const { return quadraticAttenuation_; }

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
    // Lighting calculation using Phong model (PBR params → Phong conversion)
    Color calculatePhong(const Vec3& normal, const Vec3& lightDir,
                         const Vec3& viewDir, const Material& material,
                         float attenuation) const {
        // Derive Phong parameters from PBR material
        const Color& bc = material.getBaseColor();
        float met = material.getMetallic();
        float rough = material.getRoughness();
        Color matAmbient(bc.r * 0.2f, bc.g * 0.2f, bc.b * 0.2f, bc.a);
        Color matDiffuse = bc;
        // F0: dielectric=0.04, metal=baseColor
        Color matSpecular(0.04f + met * (bc.r - 0.04f),
                          0.04f + met * (bc.g - 0.04f),
                          0.04f + met * (bc.b - 0.04f), 1.0f);
        float inv = 1.0f - rough;
        float shininess = inv * inv * 128.0f;

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

    // Attenuation parameters (for Point/Spot light)
    float constantAttenuation_;
    float linearAttenuation_;
    float quadraticAttenuation_;

    // Spot light cone (cosines of half-angles)
    float spotInnerCos_ = 1.0f;
    float spotOuterCos_ = 0.707f;

    // Projector (texture projection through spot cone)
    const Texture* projectionTexture_ = nullptr;
    float lensShiftX_ = 0.0f;
    float lensShiftY_ = 0.0f;
    float projectorAspect_ = 16.0f / 9.0f;

    // IES photometric profile (angular intensity modulation)
    const IesProfile* iesProfile_ = nullptr;

    // Shadow mapping
    bool shadowEnabled_ = false;
    int shadowResolution_ = 1024;
    float shadowBias_ = 1.0f;
};

// ---------------------------------------------------------------------------
// Lighting calculation helper (called from Mesh)
// ---------------------------------------------------------------------------

// Calculate lighting result for given position and normal
// Sum contributions from all active lights
inline Color calculateLighting(const Vec3& worldPos, const Vec3& worldNormal,
                               const Material& material) {
    if (internal::activeLights.empty()) {
        return material.getBaseColor();
    }

    // Emission (emissive color * strength)
    const Color& em = material.getEmissive();
    float es = material.getEmissiveStrength();
    float r = em.r * es;
    float g = em.g * es;
    float b = em.b * es;

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

    return Color(r, g, b, material.getBaseColor().a);
}

} // namespace trussc
