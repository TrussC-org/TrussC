#pragma once

// =============================================================================
// tcMaterial.h - マテリアル（材質）
// =============================================================================
//
// Phong ライティングモデル用のマテリアル定義
// CPU 側でライティング計算に使用する
//
// =============================================================================

namespace trussc {

// ---------------------------------------------------------------------------
// Material クラス
// ---------------------------------------------------------------------------
class Material {
public:
    Material() {
        // デフォルト: 白いプラスチック風
        ambient_ = Color(0.2f, 0.2f, 0.2f, 1.0f);
        diffuse_ = Color(0.8f, 0.8f, 0.8f, 1.0f);
        specular_ = Color(0.5f, 0.5f, 0.5f, 1.0f);
        emission_ = Color(0.0f, 0.0f, 0.0f, 1.0f);
        shininess_ = 32.0f;
    }

    // === Ambient（環境光反射率） ===
    void setAmbient(const Color& c) { ambient_ = c; }
    void setAmbient(float r, float g, float b, float a = 1.0f) {
        ambient_ = Color(r, g, b, a);
    }
    const Color& getAmbient() const { return ambient_; }

    // === Diffuse（拡散反射率） ===
    void setDiffuse(const Color& c) { diffuse_ = c; }
    void setDiffuse(float r, float g, float b, float a = 1.0f) {
        diffuse_ = Color(r, g, b, a);
    }
    const Color& getDiffuse() const { return diffuse_; }

    // === Specular（鏡面反射率） ===
    void setSpecular(const Color& c) { specular_ = c; }
    void setSpecular(float r, float g, float b, float a = 1.0f) {
        specular_ = Color(r, g, b, a);
    }
    const Color& getSpecular() const { return specular_; }

    // === Emission（自己発光） ===
    void setEmission(const Color& c) { emission_ = c; }
    void setEmission(float r, float g, float b, float a = 1.0f) {
        emission_ = Color(r, g, b, a);
    }
    const Color& getEmission() const { return emission_; }

    // === Shininess（光沢度） ===
    // 値が大きいほど鏡面ハイライトが鋭くなる（1〜128程度）
    void setShininess(float s) { shininess_ = s; }
    float getShininess() const { return shininess_; }

    // === 一括設定 ===
    // 色を指定して Ambient/Diffuse を一括設定（簡易設定）
    void setColor(const Color& c) {
        ambient_ = Color(c.r * 0.2f, c.g * 0.2f, c.b * 0.2f, c.a);
        diffuse_ = c;
    }

    // -------------------------------------------------------------------------
    // プリセットマテリアル
    // -------------------------------------------------------------------------

    // ゴールド（金）
    static Material gold() {
        Material mat;
        mat.setAmbient(0.24725f, 0.1995f, 0.0745f);
        mat.setDiffuse(0.75164f, 0.60648f, 0.22648f);
        mat.setSpecular(0.628281f, 0.555802f, 0.366065f);
        mat.setShininess(51.2f);
        return mat;
    }

    // シルバー（銀）
    static Material silver() {
        Material mat;
        mat.setAmbient(0.19225f, 0.19225f, 0.19225f);
        mat.setDiffuse(0.50754f, 0.50754f, 0.50754f);
        mat.setSpecular(0.508273f, 0.508273f, 0.508273f);
        mat.setShininess(51.2f);
        return mat;
    }

    // ブロンズ（青銅）
    static Material bronze() {
        Material mat;
        mat.setAmbient(0.2125f, 0.1275f, 0.054f);
        mat.setDiffuse(0.714f, 0.4284f, 0.18144f);
        mat.setSpecular(0.393548f, 0.271906f, 0.166721f);
        mat.setShininess(25.6f);
        return mat;
    }

    // 銅
    static Material copper() {
        Material mat;
        mat.setAmbient(0.19125f, 0.0735f, 0.0225f);
        mat.setDiffuse(0.7038f, 0.27048f, 0.0828f);
        mat.setSpecular(0.256777f, 0.137622f, 0.086014f);
        mat.setShininess(12.8f);
        return mat;
    }

    // プラスチック（指定色）
    static Material plastic(const Color& baseColor) {
        Material mat;
        mat.setAmbient(baseColor.r * 0.1f, baseColor.g * 0.1f, baseColor.b * 0.1f);
        mat.setDiffuse(baseColor.r * 0.6f, baseColor.g * 0.6f, baseColor.b * 0.6f);
        mat.setSpecular(0.7f, 0.7f, 0.7f);
        mat.setShininess(32.0f);
        return mat;
    }

    // ゴム（指定色、低光沢）
    static Material rubber(const Color& baseColor) {
        Material mat;
        mat.setAmbient(baseColor.r * 0.05f, baseColor.g * 0.05f, baseColor.b * 0.05f);
        mat.setDiffuse(baseColor.r * 0.5f, baseColor.g * 0.5f, baseColor.b * 0.5f);
        mat.setSpecular(0.1f, 0.1f, 0.1f);
        mat.setShininess(10.0f);
        return mat;
    }

    // エメラルド
    static Material emerald() {
        Material mat;
        mat.setAmbient(0.0215f, 0.1745f, 0.0215f);
        mat.setDiffuse(0.07568f, 0.61424f, 0.07568f);
        mat.setSpecular(0.633f, 0.727811f, 0.633f);
        mat.setShininess(76.8f);
        return mat;
    }

    // ルビー
    static Material ruby() {
        Material mat;
        mat.setAmbient(0.1745f, 0.01175f, 0.01175f);
        mat.setDiffuse(0.61424f, 0.04136f, 0.04136f);
        mat.setSpecular(0.727811f, 0.626959f, 0.626959f);
        mat.setShininess(76.8f);
        return mat;
    }

private:
    Color ambient_;      // 環境光反射率
    Color diffuse_;      // 拡散反射率
    Color specular_;     // 鏡面反射率
    Color emission_;     // 自己発光
    float shininess_;    // 光沢度（Phong指数）
};

} // namespace trussc
