// =============================================================================
// tcxBox2dCircle.cpp - Box2D Circle Body
// =============================================================================

#include "tcxBox2dCircle.h"

namespace tcx::box2d {

CircleBody::CircleBody(CircleBody&& other) noexcept
    : Body(std::move(other))
    , radius_(other.radius_)
{
    other.radius_ = 0;
}

CircleBody& CircleBody::operator=(CircleBody&& other) noexcept {
    if (this != &other) {
        Body::operator=(std::move(other));
        radius_ = other.radius_;
        other.radius_ = 0;
    }
    return *this;
}

void CircleBody::setup(World& world, float cx, float cy, float radius) {
    world_ = &world;
    radius_ = radius;

    // Body definition
    b2BodyDef bodyDef;
    bodyDef.type = b2_dynamicBody;
    bodyDef.position = World::toBox2d(cx, cy);

    // Create body
    body_ = world.getWorld()->CreateBody(&bodyDef);

    // Circle shape
    b2CircleShape circle;
    circle.m_radius = World::toBox2d(radius);

    // Fixture definition
    b2FixtureDef fixtureDef;
    fixtureDef.shape = &circle;
    fixtureDef.density = 1.0f;
    fixtureDef.friction = 0.3f;
    fixtureDef.restitution = 0.5f;

    body_->CreateFixture(&fixtureDef);

    // Store Body* in UserData (used by World::getBodyAtPoint())
    body_->GetUserData().pointer = reinterpret_cast<uintptr_t>(this);

    // Create collider component
    auto* collider = setupCollider<CircleCollider2D>();
    collider->setRadius(radius);
}

void CircleBody::draw() {
    if (!body_) return;

    // Draw at local origin (Node transform already applied)
    tc::drawCircle(0, 0, radius_);
    tc::drawLine(0, 0, radius_, 0);  // Show rotation
}

void CircleBody::drawFill() {
    if (!body_) return;

    tc::fill();
    tc::noStroke();
    tc::drawCircle(0, 0, radius_);

    // Draw line to show rotation
    tc::stroke();
    tc::setColor(0.0f);
    tc::drawLine(0, 0, radius_, 0);
}

void CircleBody::draw(const tc::Color& color) {
    tc::setColor(color);
    draw();
}

} // namespace tcx::box2d
