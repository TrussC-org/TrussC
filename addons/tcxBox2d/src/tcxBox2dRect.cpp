// =============================================================================
// tcxBox2dRect.cpp - Box2D 矩形ボディ
// =============================================================================

#include "tcxBox2dRect.h"

namespace tcx::box2d {

RectBody::RectBody(RectBody&& other) noexcept
    : Body(std::move(other))
    , width_(other.width_)
    , height_(other.height_)
{
    other.width_ = 0;
    other.height_ = 0;
}

RectBody& RectBody::operator=(RectBody&& other) noexcept {
    if (this != &other) {
        Body::operator=(std::move(other));
        width_ = other.width_;
        height_ = other.height_;
        other.width_ = 0;
        other.height_ = 0;
    }
    return *this;
}

void RectBody::setup(World& world, float cx, float cy, float width, float height) {
    world_ = &world;
    width_ = width;
    height_ = height;

    // ボディ定義
    b2BodyDef bodyDef;
    bodyDef.type = b2_dynamicBody;
    bodyDef.position = World::toBox2d(cx, cy);

    // ボディを作成
    body_ = world.getWorld()->CreateBody(&bodyDef);

    // 矩形形状（Box2DはSetAsBoxで半サイズを指定）
    b2PolygonShape box;
    box.SetAsBox(World::toBox2d(width / 2), World::toBox2d(height / 2));

    // フィクスチャ定義
    b2FixtureDef fixtureDef;
    fixtureDef.shape = &box;
    fixtureDef.density = 1.0f;
    fixtureDef.friction = 0.3f;
    fixtureDef.restitution = 0.3f;

    body_->CreateFixture(&fixtureDef);

    // UserData に Body* を保存（World::getBodyAtPoint() で使用）
    body_->GetUserData().pointer = reinterpret_cast<uintptr_t>(this);

    // Node の初期位置を設定
    x = cx;
    y = cy;
}

// Node用: 原点(0,0)に描画（drawTree()が変換を適用する）
void RectBody::draw() {
    if (!body_) return;

    // 矩形を描画（中心基準）
    tc::drawRect(-width_ / 2, -height_ / 2, width_, height_);
}

void RectBody::drawFill() {
    if (!body_) return;

    // 塗りつぶしモードで矩形を描画
    tc::fill();
    tc::noStroke();
    tc::drawRect(-width_ / 2, -height_ / 2, width_, height_);
}

void RectBody::draw(const tc::Color& color) {
    tc::setColor(color);
    draw();
}

} // namespace tcx::box2d
