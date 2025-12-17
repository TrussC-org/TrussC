// =============================================================================
// tcxBox2dCircle.cpp - Box2D 円形ボディ
// =============================================================================

#include "tcxBox2d/tcxBox2dCircle.h"

namespace tcx::box2d {

Circle::Circle(Circle&& other) noexcept
    : Body(std::move(other))
    , radius_(other.radius_)
{
    other.radius_ = 0;
}

Circle& Circle::operator=(Circle&& other) noexcept {
    if (this != &other) {
        Body::operator=(std::move(other));
        radius_ = other.radius_;
        other.radius_ = 0;
    }
    return *this;
}

void Circle::setup(World& world, float cx, float cy, float radius) {
    world_ = &world;
    radius_ = radius;

    // ボディ定義
    b2BodyDef bodyDef;
    bodyDef.type = b2_dynamicBody;
    bodyDef.position = World::toBox2d(cx, cy);

    // ボディを作成
    body_ = world.getWorld()->CreateBody(&bodyDef);

    // 円形状
    b2CircleShape circle;
    circle.m_radius = World::toBox2d(radius);

    // フィクスチャ定義
    b2FixtureDef fixtureDef;
    fixtureDef.shape = &circle;
    fixtureDef.density = 1.0f;
    fixtureDef.friction = 0.3f;
    fixtureDef.restitution = 0.5f;

    body_->CreateFixture(&fixtureDef);

    // UserData に Body* を保存（World::getBodyAtPoint() で使用）
    body_->GetUserData().pointer = reinterpret_cast<uintptr_t>(this);

    // Node の初期位置を設定
    x = cx;
    y = cy;
}

// Node用: 原点(0,0)に描画（drawTree()が変換を適用する）
void Circle::draw() {
    if (!body_) return;

    // 円を描画（原点中心）
    tc::drawCircle(0, 0, radius_);

    // 回転がわかるように線を引く
    tc::drawLine(0, 0, radius_, 0);
}

void Circle::drawFill() {
    if (!body_) return;

    // 塗りつぶしモードで円を描画
    tc::fill();
    tc::noStroke();
    tc::drawCircle(0, 0, radius_);

    // 回転がわかるように線を引く
    tc::stroke();
    tc::setColor(0);
    tc::drawLine(0, 0, radius_, 0);
}

void Circle::draw(const tc::Color& color) {
    tc::setColor(color);
    draw();
}

} // namespace tcx::box2d
