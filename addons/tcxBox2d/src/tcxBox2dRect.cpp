// =============================================================================
// tcxBox2dRect.cpp - Box2D Rectangle Body
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
    std::weak_ptr<int> ownerLife = objectLife;
    auto worldLife = world.aliveToken();
    destroy();
    if (ownerLife.expired() || worldLife.expired()) return;
    // Exit callbacks may have already set this owner up again. Keep that body.
    if (body_ || !world.getWorld()) return;
    world_ = &world;
    width_ = width;
    height_ = height;

    // Body definition
    b2BodyDef bodyDef;
    bodyDef.type = b2_dynamicBody;
    bodyDef.position = World::toBox2d(cx, cy);

    // Create body
    setBody(world.getWorld()->CreateBody(&bodyDef));

    // Rectangle shape (Box2D SetAsBox uses half-size)
    b2PolygonShape box;
    box.SetAsBox(World::toBox2d(width / 2), World::toBox2d(height / 2));

    // Fixture definition
    b2FixtureDef fixtureDef;
    fixtureDef.shape = &box;
    fixtureDef.density = 1.0f;
    fixtureDef.friction = 0.3f;
    fixtureDef.restitution = 0.3f;

    body_->CreateFixture(&fixtureDef);

    // Create collider component
    auto* collider = setupCollider<BoxCollider2D>();
    collider->setSize(width, height);
}

void RectBody::draw() {
    if (!body_) return;

    // Draw at local origin (Node transform already applied)
    tc::drawRect(-width_ / 2, -height_ / 2, width_, height_);
}

void RectBody::drawFill() {
    if (!body_) return;

    tc::fill();
    tc::drawRect(-width_ / 2, -height_ / 2, width_, height_);
}

void RectBody::draw(const tc::Color& color) {
    tc::setColor(color);
    draw();
}

} // namespace tcx::box2d
