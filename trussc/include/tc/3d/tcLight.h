#pragma once

// =============================================================================
// tcLight.h - ライト（光源）
// =============================================================================
//
// Phong ライティングモデル用の光源定義
// CPU 側でライティング計算を行う
//
// 対応するライトタイプ:
// - Directional: 平行光源（太陽光など、位置に関係なく一定方向）
// - Point: 点光源（電球など、位置から放射状に光る）
//
// =============================================================================

#include <cmath>
#include <algorithm>

namespace trussc {

// 前方宣言
class Material;

// ---------------------------------------------------------------------------
// LightType - ライトの種類
// ---------------------------------------------------------------------------
enum class LightType {
    Directional,    // 平行光源（太陽光）
    Point,          // 点光源
    // Spot は将来対応
};

// ---------------------------------------------------------------------------
// Light クラス
// ---------------------------------------------------------------------------
class Light {
public:
    Light() {
        // デフォルト: 白い平行光源、上から
        type_ = LightType::Directional;
        direction_ = Vec3(0, -1, 0);  // 上から下へ
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

    // === ライトタイプ設定 ===

    // 平行光源として設定（方向を指定）
    void setDirectional(const Vec3& direction) {
        type_ = LightType::Directional;
        // 方向を正規化して保存（光が進む方向）
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

    // 点光源として設定（位置を指定）
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

    // === 色設定 ===

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

    // === 強度 ===

    void setIntensity(float i) { intensity_ = i; }
    float getIntensity() const { return intensity_; }

    // === 減衰（Point ライト用） ===
    // attenuation = 1.0 / (constant + linear * d + quadratic * d²)

    void setAttenuation(float constant, float linear, float quadratic) {
        constantAttenuation_ = constant;
        linearAttenuation_ = linear;
        quadraticAttenuation_ = quadratic;
    }

    // === 有効/無効 ===

    void enable() { enabled_ = true; }
    void disable() { enabled_ = false; }
    bool isEnabled() const { return enabled_; }

    // -------------------------------------------------------------------------
    // ライティング計算
    // -------------------------------------------------------------------------

    // 指定された位置・法線に対するライティングを計算
    // viewDir: カメラから頂点への方向（正規化）
    Color calculate(const Vec3& worldPos, const Vec3& worldNormal,
                    const Material& material, const Vec3& viewPos) const {
        if (!enabled_) {
            return Color(0, 0, 0, 0);
        }

        // ライト方向を計算
        Vec3 lightDir;
        float attenuation = 1.0f;

        if (type_ == LightType::Directional) {
            // 平行光源: 方向は一定（光が来る方向の逆）
            lightDir = Vec3(-direction_.x, -direction_.y, -direction_.z);
        } else {
            // 点光源: 頂点からライトへの方向
            Vec3 toLight(position_.x - worldPos.x,
                         position_.y - worldPos.y,
                         position_.z - worldPos.z);
            float dist = std::sqrt(toLight.x * toLight.x +
                                   toLight.y * toLight.y +
                                   toLight.z * toLight.z);
            if (dist > 0) {
                lightDir = Vec3(toLight.x / dist, toLight.y / dist, toLight.z / dist);
                // 減衰計算
                attenuation = 1.0f / (constantAttenuation_ +
                                      linearAttenuation_ * dist +
                                      quadraticAttenuation_ * dist * dist);
            } else {
                lightDir = Vec3(0, 1, 0);
            }
        }

        // 視線方向（頂点からカメラへ）
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

        // Phong ライティング計算
        return calculatePhong(worldNormal, lightDir, viewDir, material, attenuation);
    }

private:
    // Phong モデルでライティング計算
    Color calculatePhong(const Vec3& normal, const Vec3& lightDir,
                         const Vec3& viewDir, const Material& material,
                         float attenuation) const {
        const Color& matAmbient = material.getAmbient();
        const Color& matDiffuse = material.getDiffuse();
        const Color& matSpecular = material.getSpecular();
        float shininess = material.getShininess();

        // === Ambient（環境光） ===
        float ar = ambient_.r * matAmbient.r;
        float ag = ambient_.g * matAmbient.g;
        float ab = ambient_.b * matAmbient.b;

        // === Diffuse（拡散反射） ===
        // N・L（法線とライト方向の内積）
        float NdotL = normal.x * lightDir.x +
                      normal.y * lightDir.y +
                      normal.z * lightDir.z;
        NdotL = std::max(0.0f, NdotL);

        float dr = diffuse_.r * matDiffuse.r * NdotL;
        float dg = diffuse_.g * matDiffuse.g * NdotL;
        float db = diffuse_.b * matDiffuse.b * NdotL;

        // === Specular（鏡面反射） ===
        float sr = 0, sg = 0, sb = 0;
        if (NdotL > 0) {
            // 反射ベクトル R = 2(N・L)N - L
            float twoNdotL = 2.0f * NdotL;
            Vec3 reflect(twoNdotL * normal.x - lightDir.x,
                         twoNdotL * normal.y - lightDir.y,
                         twoNdotL * normal.z - lightDir.z);

            // R・V（反射ベクトルと視線方向の内積）
            float RdotV = reflect.x * viewDir.x +
                          reflect.y * viewDir.y +
                          reflect.z * viewDir.z;
            RdotV = std::max(0.0f, RdotV);

            float specFactor = std::pow(RdotV, shininess);
            sr = specular_.r * matSpecular.r * specFactor;
            sg = specular_.g * matSpecular.g * specFactor;
            sb = specular_.b * matSpecular.b * specFactor;
        }

        // 合成（強度と減衰を適用）
        float factor = intensity_ * attenuation;
        float r = ar + (dr + sr) * factor;
        float g = ag + (dg + sg) * factor;
        float b = ab + (db + sb) * factor;

        return Color(r, g, b, matDiffuse.a);
    }

    LightType type_;
    Vec3 direction_;     // Directional 用（光が進む方向）
    Vec3 position_;      // Point 用
    Color ambient_;      // 環境光色
    Color diffuse_;      // 拡散光色
    Color specular_;     // 鏡面反射色
    float intensity_;    // 強度
    bool enabled_;

    // 減衰パラメータ（Point ライト用）
    float constantAttenuation_;
    float linearAttenuation_;
    float quadraticAttenuation_;
};

// ---------------------------------------------------------------------------
// ライティング計算ヘルパー（Mesh から呼ばれる）
// ---------------------------------------------------------------------------

// 指定した位置・法線でのライティング結果を計算
// 全てのアクティブなライトからの寄与を合計する
inline Color calculateLighting(const Vec3& worldPos, const Vec3& worldNormal,
                               const Material& material) {
    if (internal::activeLights.empty()) {
        // ライトがない場合はマテリアルの Diffuse をそのまま返す
        return material.getDiffuse();
    }

    // 自己発光
    const Color& emission = material.getEmission();
    float r = emission.r;
    float g = emission.g;
    float b = emission.b;

    // 各ライトからの寄与を合計
    for (Light* light : internal::activeLights) {
        if (light && light->isEnabled()) {
            Color contribution = light->calculate(worldPos, worldNormal,
                                                  material, internal::cameraPosition);
            r += contribution.r;
            g += contribution.g;
            b += contribution.b;
        }
    }

    // クランプ
    r = std::min(1.0f, r);
    g = std::min(1.0f, g);
    b = std::min(1.0f, b);

    return Color(r, g, b, material.getDiffuse().a);
}

} // namespace trussc
