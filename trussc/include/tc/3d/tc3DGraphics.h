#pragma once

// =============================================================================
// tc3DGraphics.h - 3D グラフィックス（ライティング等）
// =============================================================================
//
// ライティングシステムの API
// CPU 側でライティング計算を行い、頂点カラーに反映する
//
// 注意: 状態は tcLightingState.h で定義されている
//
// =============================================================================

#include <algorithm>

namespace trussc {

// internal 名前空間の状態は tcLightingState.h で定義済み

// ---------------------------------------------------------------------------
// ライティング API
// ---------------------------------------------------------------------------

// ライティングを有効化
inline void enableLighting() {
    internal::lightingEnabled = true;
}

// ライティングを無効化
inline void disableLighting() {
    internal::lightingEnabled = false;
}

// ライティングが有効かどうか
inline bool isLightingEnabled() {
    return internal::lightingEnabled;
}

// ライトを追加（最大8個まで）
inline void addLight(Light& light) {
    if (internal::activeLights.size() < internal::maxLights) {
        // 重複チェック
        auto it = std::find(internal::activeLights.begin(),
                            internal::activeLights.end(), &light);
        if (it == internal::activeLights.end()) {
            internal::activeLights.push_back(&light);
        }
    }
}

// ライトを削除
inline void removeLight(Light& light) {
    auto it = std::find(internal::activeLights.begin(),
                        internal::activeLights.end(), &light);
    if (it != internal::activeLights.end()) {
        internal::activeLights.erase(it);
    }
}

// 全ライトをクリア
inline void clearLights() {
    internal::activeLights.clear();
}

// アクティブなライトの数を取得
inline int getNumLights() {
    return static_cast<int>(internal::activeLights.size());
}

// マテリアルを設定
inline void setMaterial(Material& material) {
    internal::currentMaterial = &material;
}

// マテリアルをクリア（デフォルトに戻す）
inline void clearMaterial() {
    internal::currentMaterial = nullptr;
}

// カメラ位置を設定（スペキュラー計算用）
inline void setCameraPosition(const Vec3& pos) {
    internal::cameraPosition = pos;
}

inline void setCameraPosition(float x, float y, float z) {
    internal::cameraPosition = Vec3(x, y, z);
}

// カメラ位置を取得
inline const Vec3& getCameraPosition() {
    return internal::cameraPosition;
}

// calculateLighting() は tcLight.h で定義されている

} // namespace trussc
