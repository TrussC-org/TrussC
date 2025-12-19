// =============================================================================
// tcxBox2dPolygon.h - Box2D ポリゴンボディ
// =============================================================================

#pragma once

#include "tcxBox2dBody.h"
#include <vector>

namespace tcx::box2d {

// =============================================================================
// ポリゴンボディ
// =============================================================================
// 凸多角形を表現（Box2Dの制限により凸形状のみ、最大8頂点）
// =============================================================================
class PolyShape : public Body {
public:
    PolyShape() = default;
    ~PolyShape() override = default;

    // ムーブ
    PolyShape(PolyShape&& other) noexcept;
    PolyShape& operator=(PolyShape&& other) noexcept;

    // ---------------------------------------------------------------------
    // 作成
    // ---------------------------------------------------------------------

    // 頂点リストからポリゴンを作成
    // vertices: 頂点座標（ローカル座標、中心基準）
    // x, y: 中心座標（ワールド座標、ピクセル）
    void setup(World& world, const std::vector<tc::Vec2>& vertices, float x, float y);

    // Polylineからポリゴンを作成
    void setup(World& world, const tc::Path& polyline, float x, float y);

    // 正多角形を作成
    // sides: 辺の数（3〜8）
    // radius: 外接円の半径
    void setupRegular(World& world, float x, float y, float radius, int sides);

    // ---------------------------------------------------------------------
    // プロパティ
    // ---------------------------------------------------------------------
    const std::vector<tc::Vec2>& getVertices() const { return vertices_; }
    int getNumVertices() const { return static_cast<int>(vertices_.size()); }

    // ---------------------------------------------------------------------
    // 描画（Node::draw() をオーバーライド）
    // 原点(0,0)に描画。drawTree()が自動的に位置・回転を適用する
    // ---------------------------------------------------------------------
    void draw() override;

    // 塗りつぶしで描画
    void drawFill();

    // 色を設定して描画
    void draw(const tc::Color& color);

private:
    std::vector<tc::Vec2> vertices_;
};

} // namespace tcx::box2d
