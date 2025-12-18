#pragma once

// =============================================================================
// tcLightingState.h - ライティングのグローバル状態（内部用）
// =============================================================================
//
// tcMesh.h からライティング状態にアクセスするために、
// tc3DGraphics.h より前にインクルードする必要がある
//
// =============================================================================

#include <vector>

namespace trussc {

// 前方宣言
class Light;
class Material;

// ---------------------------------------------------------------------------
// internal 名前空間 - ライティングのグローバル状態
// ---------------------------------------------------------------------------
namespace internal {
    // ライティングが有効かどうか
    inline bool lightingEnabled = false;

    // アクティブなライトのリスト（最大8個）
    inline std::vector<Light*> activeLights;
    inline constexpr int maxLights = 8;

    // 現在のマテリアル
    inline Material* currentMaterial = nullptr;

    // カメラ位置（スペキュラー計算用）
    inline Vec3 cameraPosition = {0, 0, 0};
}

} // namespace trussc
