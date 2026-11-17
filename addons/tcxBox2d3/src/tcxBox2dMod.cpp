#include "tcxBox2dMod.h"
#include <cmath>

namespace tcx::box2d {
namespace {
constexpr float pixelsPerMeter = 30.0f;
float parentAngle(const tc::Node& node) {
    float angle = 0.0f;
    for (auto parent = node.getParent(); parent; parent = parent->getParent()) angle += parent->getRot();
    return angle;
}
b2Vec2 meters(tc::Vec2 value) { return {value.x / pixelsPerMeter, value.y / pixelsPerMeter}; }
tc::Vec2 pixels(b2Vec2 value) { return {value.x * pixelsPerMeter, value.y * pixelsPerMeter}; }
void polygonWarning() {
    tc::logWarning() << "tcxBox2d3: addPolygon rejected degenerate input or skipped unusable triangles; "
                        "pass a simple, non-overlapping outline with separated corners and nonzero area.";
}
// Like v2's compound path: only a single convex ring takes the fast path.
// Same-sign turns alone are insufficient (a star can wind more than once).
bool convexRing(const tc::Path& path, std::vector<b2Vec2>& points) {
    int rings = 0;
    const auto& vertices = path.getVertices();
    for (size_t i = 0; i < path.getNumSubpaths(); ++i) {
        auto [start, end] = path.getSubpathRange(i);
        if (end - start < 3) continue;
        if (++rings > 1) return false;
        for (size_t j = start; j < end; ++j) {
            auto p = meters({vertices[j].x, vertices[j].y});
            if (points.empty() || p.x != points.back().x || p.y != points.back().y) points.push_back(p);
        }
        if (points.size() > 1 && points.front().x == points.back().x && points.front().y == points.back().y)
            points.pop_back();
    }
    if (rings != 1 || points.size() < 3 || points.size() > B2_MAX_POLYGON_VERTICES) return false;
    bool left = false, right = false;
    double turning = 0.0;
    for (size_t i = 0; i < points.size(); ++i) {
        auto a = points[i], b = points[(i + 1) % points.size()], c = points[(i + 2) % points.size()];
        double ex = double(b.x) - a.x, ey = double(b.y) - a.y;
        double fx = double(c.x) - b.x, fy = double(c.y) - b.y;
        double cross = ex * fy - ey * fx, dot = ex * fx + ey * fy;
        if (cross == 0.0 && dot < 0.0) return false;
        left |= cross > 0.0;
        right |= cross < 0.0;
        turning += std::atan2(cross, dot);
    }
    return (left != right) && std::abs(turning) < 1.5 * tc::TAU;
}
bool makePolygon(const b2Vec2* vertices, int count, b2Polygon& polygon) {
    auto hull = b2ComputeHull(vertices, count);
    if (hull.count < 3 || !b2ValidateHull(&hull)) return false;
    polygon = b2MakePolygon(&hull, 0.0f);
    return true;
}
} // namespace

b2BodyId RigidBody2D::id() const {
    // Body ids do not carry the world's generation: check both ids.
    return b2World_IsValid(worldId_) && b2Body_IsValid(bodyId_) ? bodyId_ : b2_nullBodyId;
}
void RigidBody2D::setup() {
    auto owners = owners_.lock();
    if (!owners || !b2World_IsValid(owners->worldId)) {
        tc::logWarning() << "tcxBox2d3: RigidBody2D needs a live, setup World before Node setup.";
        return;
    }
    worldId_ = owners->worldId;
    auto def = b2DefaultBodyDef();
    def.type = type_;
    const auto pos = getOwner()->getGlobalPos();
    def.position = meters({pos.x, pos.y});
    def.rotation = b2MakeRot(getOwner()->getRot() + parentAngle(*getOwner()));
    def.userData = this;
    bodyId_ = b2CreateBody(worldId_, &def);
    owners->bodies.insert(this);
}
void RigidBody2D::onDestroy() {
    if (auto owners = owners_.lock(); owners && b2StoreWorldId(owners->worldId) == b2StoreWorldId(worldId_) &&
        B2_ID_EQUALS(owners->draggedBody, bodyId_)) World::clearDrag(*owners);
    if (b2Body_IsValid(id())) b2DestroyBody(bodyId_);
    bodyId_ = b2_nullBodyId;
    if (auto owners = owners_.lock()) owners->bodies.erase(this);
    shapes_.clear();
    shapeFlags_.clear();
    appliedShapeCount_ = 0;
    appliedSensorListeners_ = false;
    appliedHitListeners_ = false;
}
void RigidBody2D::rememberShape(b2ShapeId shape, const b2ShapeDef& def) {
    shapes_.push_back(shape);
    shapeFlags_.push_back({shape, def.enableSensorEvents, def.enableHitEvents});
}
void RigidBody2D::updateEventFlags(bool worldSensorListeners) {
    const bool hit = onHit.listenerCount() != 0;
    const bool sensorChanged = worldSensorListeners != appliedSensorListeners_;
    const bool hitChanged = hit != appliedHitListeners_;
    // Leave raw overrides alone until the corresponding listener state changes.
    const size_t start = sensorChanged || hitChanged ? 0 : appliedShapeCount_;
    for (size_t i = start; i < shapeFlags_.size(); ++i) {
        const auto& flags = shapeFlags_[i];
        if (!b2Shape_IsValid(flags.id)) continue;
        if (sensorChanged || i >= appliedShapeCount_)
            b2Shape_EnableSensorEvents(flags.id, flags.sensor || worldSensorListeners);
        if (hitChanged || i >= appliedShapeCount_)
            b2Shape_EnableHitEvents(flags.id, flags.hit || hit);
    }
    appliedShapeCount_ = shapeFlags_.size();
    appliedSensorListeners_ = worldSensorListeners;
    appliedHitListeners_ = hit;
}
b2ShapeId RigidBody2D::addCircle(float radius, const b2ShapeDef& def) {
    if (!b2Body_IsValid(id())) return b2_nullShapeId;
    if (!std::isfinite(radius) || radius <= 0.0f) {
        tc::logWarning() << "tcxBox2d3: addCircle needs a finite, positive radius in pixels.";
        return b2_nullShapeId;
    }
    b2Circle circle = {{0.0f, 0.0f}, radius / pixelsPerMeter};
    auto eventDef = def;
    eventDef.enableContactEvents = true;
    auto shape = b2CreateCircleShape(bodyId_, &eventDef, &circle);
    rememberShape(shape, def);
    return shape;
}
b2ShapeId RigidBody2D::addBox(float width, float height, const b2ShapeDef& def) {
    if (!b2Body_IsValid(id())) return b2_nullShapeId;
    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.0f || height <= 0.0f) {
        tc::logWarning() << "tcxBox2d3: addBox needs finite, positive dimensions in pixels.";
        return b2_nullShapeId;
    }
    auto polygon = b2MakeBox(width / (2.0f * pixelsPerMeter), height / (2.0f * pixelsPerMeter));
    auto eventDef = def;
    eventDef.enableContactEvents = true;
    auto shape = b2CreatePolygonShape(bodyId_, &eventDef, &polygon);
    rememberShape(shape, def);
    return shape;
}
std::vector<b2ShapeId> RigidBody2D::addPolygon(const tc::Path& path, const b2ShapeDef& def) {
    if (!b2Body_IsValid(id())) return {};
    for (const auto& p : path.getVertices()) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) { polygonWarning(); return {}; }
    }
    std::vector<b2Polygon> polygons;
    std::vector<b2Vec2> ring;
    b2Polygon polygon;
    if (convexRing(path, ring) && makePolygon(ring.data(), static_cast<int>(ring.size()), polygon)) {
        polygons.push_back(polygon);
    } else {
        const auto triangles = path.buildFillTriangles();
        bool skipped = false;
        for (size_t i = 0; i + 2 < triangles.size(); i += 3) {
            b2Vec2 tri[3];
            for (size_t j = 0; j < 3; ++j) tri[j] = meters({triangles[i + j][0], triangles[i + j][1]});
            if (makePolygon(tri, 3, polygon)) polygons.push_back(polygon);
            else skipped = true;
        }
        if (skipped || polygons.empty()) polygonWarning();
    }
    std::vector<b2ShapeId> created;
    auto batchDef = def;
    batchDef.enableContactEvents = true;
    batchDef.updateBodyMass = false;
    for (const auto& poly : polygons) {
        auto shape = b2CreatePolygonShape(bodyId_, &batchDef, &poly);
        created.push_back(shape);
        rememberShape(shape, def);
    }
    if (!created.empty()) b2Body_UpdateMassFromShapes(bodyId_);
    return created;
}
void RigidBody2D::setPosition(tc::Vec2 position) {
    if (!b2Body_IsValid(id())) return;
    b2Body_SetTransform(bodyId_, meters(position), b2Body_GetRotation(bodyId_));
    syncNode(b2Body_GetTransform(bodyId_));
}
tc::Vec2 RigidBody2D::getPosition() const {
    return b2Body_IsValid(id()) ? pixels(b2Body_GetPosition(bodyId_)) : tc::Vec2{};
}
void RigidBody2D::setAngle(float radians) {
    if (!b2Body_IsValid(id())) return;
    b2Body_SetTransform(bodyId_, b2Body_GetPosition(bodyId_), b2MakeRot(radians));
    syncNode(b2Body_GetTransform(bodyId_));
}
float RigidBody2D::getAngle() const { return b2Body_IsValid(id()) ? b2Rot_GetAngle(b2Body_GetRotation(bodyId_)) : 0.0f; }
void RigidBody2D::setVelocity(tc::Vec2 velocity) {
    if (b2Body_IsValid(id())) b2Body_SetLinearVelocity(bodyId_, meters(velocity));
}
tc::Vec2 RigidBody2D::getVelocity() const {
    return b2Body_IsValid(id()) ? pixels(b2Body_GetLinearVelocity(bodyId_)) : tc::Vec2{};
}
void RigidBody2D::setAngularVelocity(float radiansPerSecond) {
    if (b2Body_IsValid(id())) b2Body_SetAngularVelocity(bodyId_, radiansPerSecond);
}
float RigidBody2D::getAngularVelocity() const {
    return b2Body_IsValid(id()) ? b2Body_GetAngularVelocity(bodyId_) : 0.0f;
}
void RigidBody2D::syncNode(b2Transform transform) {
    // Hold the Node while transform observers run. They may remove this Mod,
    // so compute everything first and never touch this after setting position.
    auto keepAlive = getOwner()->weak_from_this().lock();
    auto* node = getOwner();
    if (node->isDead()) return;
    const auto pos = pixels(transform.p);
    auto parent = node->getParent();
    auto local = parent ? parent->globalToLocal({pos.x, pos.y, 0.0f}) : tc::Vec3(pos.x, pos.y, 0.0f);
    const float rotation = b2Rot_GetAngle(transform.q) - parentAngle(*node);
    const float z = node->getZ();
    node->setPos(local.x, local.y, z);
    if (!node->isDead()) node->setRot(rotation);
}
} // namespace tcx::box2d
