#pragma once

#include "tcMath.h"

namespace trussc {

// =============================================================================
// Ray - 光線（始点 + 方向）
// Hit Test を統一的に扱うための基本構造体
// =============================================================================

struct Ray {
    Vec3 origin;      // 始点
    Vec3 direction;   // 方向（正規化されている前提）

    // コンストラクタ
    Ray() : origin(0, 0, 0), direction(0, 0, -1) {}
    Ray(const Vec3& o, const Vec3& d) : origin(o), direction(d.normalized()) {}

    // t の位置を取得: P(t) = origin + direction * t
    Vec3 at(float t) const {
        return origin + direction * t;
    }

    // 逆行列で変換（ノードのローカル空間に変換）
    // origin: 点として変換
    // direction: 方向として変換（平行移動の影響を受けない）
    Ray transformed(const Mat4& inverseMatrix) const {
        // origin を点として変換
        Vec3 newOrigin = inverseMatrix * origin;

        // direction を方向として変換（w=0 として扱う）
        // 平行移動の影響を除外するため、行列の3x3部分だけ使う
        Vec3 newDir;
        newDir.x = inverseMatrix.m[0] * direction.x + inverseMatrix.m[1] * direction.y + inverseMatrix.m[2] * direction.z;
        newDir.y = inverseMatrix.m[4] * direction.x + inverseMatrix.m[5] * direction.y + inverseMatrix.m[6] * direction.z;
        newDir.z = inverseMatrix.m[8] * direction.x + inverseMatrix.m[9] * direction.y + inverseMatrix.m[10] * direction.z;

        return Ray(newOrigin, newDir);  // コンストラクタで正規化される
    }

    // ==========================================================================
    // 2D用ヘルパー: マウス座標からRayを生成（正射影）
    // ==========================================================================

    // 2Dモード（正射影）: マウス位置から Z 軸に平行な Ray を生成
    // カメラは Z+ 方向から Z- 方向を見ている前提
    static Ray fromScreenPoint2D(float screenX, float screenY, float startZ = 1000.0f) {
        return Ray(
            Vec3(screenX, screenY, startZ),
            Vec3(0, 0, -1)
        );
    }

    // ==========================================================================
    // 平面との交差判定
    // ==========================================================================

    // Z=0 平面との交差（2D UI 用）
    // 交差した場合 true を返し、outT に交差距離、outPoint に交差点を設定
    bool intersectZPlane(float& outT, Vec3& outPoint) const {
        // direction.z が 0 に近い場合は平行（交差しない）
        if (std::abs(direction.z) < 1e-6f) {
            return false;
        }

        // Z=0 平面との交差: origin.z + t * direction.z = 0
        float t = -origin.z / direction.z;

        // t < 0 は Ray の後ろ側（交差しない）
        if (t < 0) {
            return false;
        }

        outT = t;
        outPoint = at(t);
        return true;
    }

    // 任意の平面との交差
    // plane: 平面の法線（正規化済み）
    // planeD: 平面の距離（原点からの符号付き距離）
    bool intersectPlane(const Vec3& planeNormal, float planeD, float& outT, Vec3& outPoint) const {
        float denom = direction.dot(planeNormal);

        // 平行チェック
        if (std::abs(denom) < 1e-6f) {
            return false;
        }

        float t = -(origin.dot(planeNormal) + planeD) / denom;

        if (t < 0) {
            return false;
        }

        outT = t;
        outPoint = at(t);
        return true;
    }

    // ==========================================================================
    // 球との交差判定
    // ==========================================================================

    // 原点を中心とする球との交差
    bool intersectSphere(float radius, float& outT) const {
        // |origin + t * direction|^2 = radius^2
        // a*t^2 + b*t + c = 0
        float a = direction.dot(direction);  // 正規化されていれば 1
        float b = 2.0f * origin.dot(direction);
        float c = origin.dot(origin) - radius * radius;

        float discriminant = b * b - 4 * a * c;
        if (discriminant < 0) {
            return false;
        }

        float sqrtD = std::sqrt(discriminant);
        float t1 = (-b - sqrtD) / (2 * a);
        float t2 = (-b + sqrtD) / (2 * a);

        // 最も近い正の t を選択
        if (t1 > 0) {
            outT = t1;
            return true;
        }
        if (t2 > 0) {
            outT = t2;
            return true;
        }

        return false;
    }

    // ==========================================================================
    // AABB（軸平行バウンディングボックス）との交差判定
    // ==========================================================================

    // min/max で定義される AABB との交差
    bool intersectAABB(const Vec3& boxMin, const Vec3& boxMax, float& outT) const {
        float tmin = 0.0f;
        float tmax = std::numeric_limits<float>::max();

        for (int i = 0; i < 3; i++) {
            float invD = 1.0f / direction[i];
            float t0 = (boxMin[i] - origin[i]) * invD;
            float t1 = (boxMax[i] - origin[i]) * invD;

            if (invD < 0.0f) {
                std::swap(t0, t1);
            }

            tmin = std::max(tmin, t0);
            tmax = std::min(tmax, t1);

            if (tmax < tmin) {
                return false;
            }
        }

        outT = tmin;
        return true;
    }
};

} // namespace trussc
