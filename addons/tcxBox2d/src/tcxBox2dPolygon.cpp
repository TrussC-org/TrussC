// =============================================================================
// tcxBox2dPolygon.cpp - Box2D Polygon Body
// =============================================================================

#include "tcxBox2dPolygon.h"
#include <cmath>

namespace tcx::box2d {

PolyShape::PolyShape(PolyShape&& other) noexcept
    : Body(std::move(other))
    , vertices_(std::move(other.vertices_))
{
}

PolyShape& PolyShape::operator=(PolyShape&& other) noexcept {
    if (this != &other) {
        Body::operator=(std::move(other));
        vertices_ = std::move(other.vertices_);
    }
    return *this;
}

void PolyShape::setup(World& world, const std::vector<tc::Vec2>& vertices, float cx, float cy) {
    if (vertices.size() < 3 || vertices.size() > 8) {
        // Box2D only supports 3-8 vertices
        return;
    }

    world_ = &world;
    vertices_ = vertices;

    // Body definition
    b2BodyDef bodyDef;
    bodyDef.type = b2_dynamicBody;
    bodyDef.position = World::toBox2d(cx, cy);

    body_ = world.getWorld()->CreateBody(&bodyDef);

    // Convert vertices to Box2D format
    std::vector<b2Vec2> b2Vertices(vertices.size());
    for (size_t i = 0; i < vertices.size(); ++i) {
        b2Vertices[i] = World::toBox2d(vertices[i]);
    }

    // Polygon shape
    b2PolygonShape polygon;
    polygon.Set(b2Vertices.data(), static_cast<int32>(b2Vertices.size()));

    // Fixture definition
    b2FixtureDef fixtureDef;
    fixtureDef.shape = &polygon;
    fixtureDef.density = 1.0f;
    fixtureDef.friction = 0.3f;
    fixtureDef.restitution = 0.3f;

    body_->CreateFixture(&fixtureDef);

    // Store Body* in UserData (used by World::getBodyAtPoint())
    body_->GetUserData().pointer = reinterpret_cast<uintptr_t>(this);

    // Create collider component
    auto* collider = setupCollider<PolygonCollider2D>();
    collider->setVertexCount(static_cast<int>(vertices.size()));
}

void PolyShape::setup(World& world, const tc::Path& polyline, float cx, float cy) {
    std::vector<tc::Vec2> vertices;
    for (int i = 0; i < polyline.size(); ++i) {
        vertices.push_back(tc::Vec2(polyline[i].x, polyline[i].y));
    }
    setup(world, vertices, cx, cy);
}

void PolyShape::setupRegular(World& world, float cx, float cy, float radius, int sides) {
    if (sides < 3) sides = 3;
    if (sides > 8) sides = 8;

    std::vector<tc::Vec2> vertices(sides);
    float angleStep = 2.0f * M_PI / sides;

    for (int i = 0; i < sides; ++i) {
        float angle = i * angleStep - M_PI / 2;  // Start from top
        vertices[i] = tc::Vec2(
            std::cos(angle) * radius,
            std::sin(angle) * radius
        );
    }

    setup(world, vertices, cx, cy);
}

void PolyShape::draw() {
    if (!body_ || vertices_.empty()) return;

    // Draw at local origin (Node transform already applied)
    for (size_t i = 0; i < vertices_.size(); ++i) {
        size_t next = (i + 1) % vertices_.size();
        tc::drawLine(vertices_[i].x, vertices_[i].y,
                     vertices_[next].x, vertices_[next].y);
    }
}

void PolyShape::drawFill() {
    if (!body_ || vertices_.empty()) return;

    // Fill with triangle fan
    tc::Mesh mesh;
    mesh.setMode(tc::PrimitiveMode::TriangleFan);

    mesh.addVertex(tc::Vec3(0, 0, 0));  // Center
    for (const auto& v : vertices_) {
        mesh.addVertex(tc::Vec3(v.x, v.y, 0));
    }
    mesh.addVertex(tc::Vec3(vertices_[0].x, vertices_[0].y, 0));

    mesh.draw();
}

void PolyShape::draw(const tc::Color& color) {
    tc::setColor(color);
    draw();
}

} // namespace tcx::box2d
