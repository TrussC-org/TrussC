#pragma once

// このファイルは TrussC.h からインクルードされる
// internal 名前空間の変数にアクセスするため

#include <vector>

namespace trussc {

// シェイプ描画の内部状態
namespace internal {
    inline std::vector<Vec3> shapeVertices;
    inline bool shapeStarted = false;
}

// シェイプ描画を開始
// fill と stroke の両方に対応
inline void beginShape() {
    internal::shapeVertices.clear();
    internal::shapeStarted = true;
}

// 頂点を追加（2D）
inline void vertex(float x, float y) {
    if (internal::shapeStarted) {
        internal::shapeVertices.push_back(Vec3{x, y, 0.0f});
    }
}

// 頂点を追加（3D）
inline void vertex(float x, float y, float z) {
    if (internal::shapeStarted) {
        internal::shapeVertices.push_back(Vec3{x, y, z});
    }
}

// 頂点を追加（Vec2）
inline void vertex(const Vec2& v) {
    vertex(v.x, v.y);
}

// 頂点を追加（Vec3）
inline void vertex(const Vec3& v) {
    vertex(v.x, v.y, v.z);
}

// シェイプ描画を終了
// close: true で始点と終点を結ぶ
inline void endShape(bool close = false) {
    if (!internal::shapeStarted || internal::shapeVertices.empty()) {
        internal::shapeStarted = false;
        return;
    }

    auto& verts = internal::shapeVertices;
    size_t n = verts.size();

    // fill モード: 三角形ファン（凸形状のみ正しく描画）
    if (internal::fillEnabled && n >= 3) {
        sgl_begin_triangles();
        sgl_c4f(internal::currentR, internal::currentG, internal::currentB, internal::currentA);
        // 三角形ファン: 0番目の頂点を中心に
        for (size_t i = 1; i < n - 1; i++) {
            sgl_v3f(verts[0].x, verts[0].y, verts[0].z);
            sgl_v3f(verts[i].x, verts[i].y, verts[i].z);
            sgl_v3f(verts[i+1].x, verts[i+1].y, verts[i+1].z);
        }
        sgl_end();
    }

    // stroke モード: ラインストリップ
    if (internal::strokeEnabled && n >= 2) {
        sgl_c4f(internal::currentR, internal::currentG, internal::currentB, internal::currentA);
        sgl_begin_line_strip();
        for (size_t i = 0; i < n; i++) {
            sgl_v3f(verts[i].x, verts[i].y, verts[i].z);
        }
        if (close && n > 2) {
            sgl_v3f(verts[0].x, verts[0].y, verts[0].z);
        }
        sgl_end();
    }

    internal::shapeVertices.clear();
    internal::shapeStarted = false;
}

} // namespace trussc
