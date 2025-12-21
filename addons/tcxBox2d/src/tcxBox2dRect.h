// =============================================================================
// tcxBox2dRect.h - Box2D 矩形ボディ
// =============================================================================

#pragma once

#include "tcxBox2dBody.h"

namespace tcx::box2d {

// =============================================================================
// 矩形ボディ
// =============================================================================
class RectBody : public Body {
public:
    RectBody() = default;
    ~RectBody() override = default;

    // ムーブ
    RectBody(RectBody&& other) noexcept;
    RectBody& operator=(RectBody&& other) noexcept;

    // ---------------------------------------------------------------------
    // 作成
    // ---------------------------------------------------------------------

    // ワールドに矩形を作成
    // x, y: 中心座標（ピクセル）
    // width, height: サイズ（ピクセル）
    void setup(World& world, float x, float y, float width, float height);

    // ---------------------------------------------------------------------
    // プロパティ
    // ---------------------------------------------------------------------
    float getWidth() const { return width_; }
    float getHeight() const { return height_; }

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
    float width_ = 0;
    float height_ = 0;
};

} // namespace tcx::box2d
