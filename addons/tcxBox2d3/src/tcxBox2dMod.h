#pragma once

#include "tcxBox2dWorld.h"
#include <TrussC.h>
#include <vector>

namespace tcx::box2d {

struct ContactEvent {
    RigidBody2D* other = nullptr;
    b2ShapeId shape = b2_nullShapeId;
    b2ShapeId otherShape = b2_nullShapeId;
    tc::Vec2 point{}; // pixels; begin only (end leaves zero)
    tc::Vec2 normal{}; // unit vector toward other; begin only
};
struct SensorEvent {
    RigidBody2D* other = nullptr;
    b2ShapeId shape = b2_nullShapeId;
    b2ShapeId otherShape = b2_nullShapeId;
};
struct HitEvent {
    RigidBody2D* other = nullptr;
    b2ShapeId shape = b2_nullShapeId;
    b2ShapeId otherShape = b2_nullShapeId;
    tc::Vec2 point{}; // pixels
    tc::Vec2 normal{}; // unit vector toward other
    float approachSpeed = 0.0f; // pixels/second
};

// One owning v3 body per Node. Positions and linear velocities use pixels;
// angles and angular velocities use radians. Shapes use upstream definitions.
class RigidBody2D : public tc::Mod {
    friend class World;
public:
    RigidBody2D(World& world, b2BodyType type)
        : owners_(world.owners_), type_(type) {}
    ~RigidBody2D() override { onDestroy(); }
    RigidBody2D(const RigidBody2D&) = delete;
    RigidBody2D& operator=(const RigidBody2D&) = delete;
    RigidBody2D(RigidBody2D&&) = delete;
    RigidBody2D& operator=(RigidBody2D&&) = delete;

    // Invalid after the owning World is destroyed or setup() again.
    b2BodyId id() const;
    b2ShapeId addCircle(float radius, const b2ShapeDef& def = b2DefaultShapeDef());
    b2ShapeId addBox(float width, float height, const b2ShapeDef& def = b2DefaultShapeDef());
    std::vector<b2ShapeId> addPolygon(const tc::Path& path,
                                     const b2ShapeDef& def = b2DefaultShapeDef());
    const std::vector<b2ShapeId>& shapeIds() const { return shapes_; }

    tc::Event<ContactEvent> onContactBegin;
    tc::Event<ContactEvent> onContactEnd;
    tc::Event<SensorEvent> onSensorBegin;
    tc::Event<SensorEvent> onSensorEnd;
    tc::Event<HitEvent> onHit;

    void setPosition(tc::Vec2 position);
    tc::Vec2 getPosition() const;
    void setAngle(float radians);
    float getAngle() const;
    void setVelocity(tc::Vec2 velocity);
    tc::Vec2 getVelocity() const;
    void setAngularVelocity(float radiansPerSecond);
    float getAngularVelocity() const;

protected:
    void setup() override;
    void onDestroy() override;
    bool isExclusive() const override { return true; }

private:
    struct ShapeFlags { b2ShapeId id; bool sensor; bool hit; };
    void rememberShape(b2ShapeId shape, const b2ShapeDef& def);
    void updateEventFlags(bool worldSensorListeners);
    std::vector<ShapeFlags> shapeFlags_;
    size_t appliedShapeCount_ = 0;
    bool appliedSensorListeners_ = false;
    bool appliedHitListeners_ = false;
    void syncNode(b2Transform transform);
    std::weak_ptr<World::Owners> owners_;
    b2WorldId worldId_ = b2_nullWorldId;
    b2BodyId bodyId_ = b2_nullBodyId;
    b2BodyType type_;
    std::vector<b2ShapeId> shapes_;
};

} // namespace tcx::box2d
