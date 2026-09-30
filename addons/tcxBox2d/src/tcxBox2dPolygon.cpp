// =============================================================================
// tcxBox2dPolygon.cpp - Box2D Polygon Body
// =============================================================================

#include "tcxBox2dPolygon.h"
#include "tcxBox2dPolygonCheck.h"
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
    // Check before creating anything: Box2D would assert (Debug) or build a
    // 2x2 m box (Release) for points it can't use.
    b2PolygonShape polygon;
    std::vector<tc::Vec2> hull;
    detail::PolygonError err = detail::makePolygonShape(vertices, polygon, hull);
    if (err != detail::PolygonError::None) {
        auto log = tc::logWarning();
        log << "tcxBox2d: PolyShape::setup() got " << vertices.size() << " points: "
            << detail::describePolygonError(err) << ".";
        if (err == detail::PolygonError::TooManyPoints) {
            log << " Use setupConvex() for a convex approximation.";
        }
        log << " Body not created.";
        return;
    }
    createBody(world, polygon, hull, cx, cy);
}

void PolyShape::setup(World& world, const tc::Path& polyline, float cx, float cy) {
    setup(world, detail::pathPoints(polyline), cx, cy);
}

void PolyShape::setupConvex(World& world, const std::vector<tc::Vec2>& points, float cx, float cy) {
    std::vector<tc::Vec2> reduced = detail::reducedConvexHull(points);
    if (reduced.size() < 3) {
        tc::logWarning() << "tcxBox2d: PolyShape::setupConvex() got " << points.size()
                         << " points: " << detail::describeCollapsedHull("setupConvex()")
                         << ". Body not created.";
        return;
    }
    b2PolygonShape polygon;
    std::vector<tc::Vec2> hull;
    detail::PolygonError err = detail::makePolygonShape(reduced, polygon, hull);
    if (err != detail::PolygonError::None) {
        tc::logWarning() << "tcxBox2d: PolyShape::setupConvex() got " << points.size()
                         << " points whose convex hull can't make a polygon: "
                         << detail::describePolygonError(err) << ". Body not created.";
        return;
    }
    createBody(world, polygon, hull, cx, cy);
}

void PolyShape::setupConvex(World& world, const tc::Path& path, float cx, float cy) {
    setupConvex(world, detail::pathPoints(path), cx, cy);
}

void PolyShape::createBody(World& world, const b2PolygonShape& polygon,
                           const std::vector<tc::Vec2>& hull, float cx, float cy) {
    world_ = &world;
    vertices_ = hull;

    // Body definition
    b2BodyDef bodyDef;
    bodyDef.type = b2_dynamicBody;
    bodyDef.position = World::toBox2d(cx, cy);

    body_ = world.getWorld()->CreateBody(&bodyDef);

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
    collider->setVertexCount(static_cast<int>(hull.size()));
}

void PolyShape::setupRegular(World& world, float cx, float cy, float radius, int sides) {
    if (sides < 3) sides = 3;
    if (sides > 8) sides = 8;

    std::vector<tc::Vec2> vertices(sides);
    float angleStep = tc::TAU / sides;

    for (int i = 0; i < sides; ++i) {
        float angle = i * angleStep - tc::QUARTER_TAU;  // Start from top
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

    // Fill with a triangle fan from the first vertex. vertices_ is convex (the
    // hull Box2D built), but the body origin need not lie inside it.
    tc::Mesh mesh;
    mesh.setMode(tc::PrimitiveMode::TriangleFan);

    for (const auto& v : vertices_) {
        mesh.addVertex(tc::Vec3(v.x, v.y, 0));
    }

    mesh.draw();
}

void PolyShape::draw(const tc::Color& color) {
    tc::setColor(color);
    draw();
}

} // namespace tcx::box2d
