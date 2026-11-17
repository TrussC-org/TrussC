#include "tcxBox2dMod.h"

namespace tcx::box2d {

World::~World() {
    ++owners_->epoch;
    owners_->bodies.clear();
    if (b2World_IsValid(worldId_)) b2DestroyWorld(worldId_);
}

void World::setup() {
    if (b2World_IsValid(worldId_)) b2DestroyWorld(worldId_);
    ++owners_->epoch;
    owners_->bodies.clear();
    auto def = b2DefaultWorldDef();
    def.gravity = {gravity_.x / 30.0f, gravity_.y / 30.0f};
    def.workerCount = workerCount_;
    worldId_ = b2CreateWorld(&def);
    owners_->worldId = worldId_;
}

void World::update() {
    // Keep the guard outside World: a callback may delete this object.
    const auto owners = owners_;
    if (owners->updating) {
        tc::logWarning() << "tcxBox2d3: re-entrant World::update refused; step next frame instead.";
        return;
    }
    if (!b2World_IsValid(worldId_)) return;
    owners->updating = true;
    struct StepGuard {
        std::shared_ptr<Owners> owners;
        ~StepGuard() { owners->updating = false; }
    } guard{owners};
    const auto steppedWorld = worldId_;
    const auto epoch = owners->epoch;
    const auto live = [&] { return owners->epoch == epoch && b2World_IsValid(steppedWorld); };
    bool sensorListeners = false;
    for (auto* body : owners->bodies)
        sensorListeners |= body->onSensorBegin.listenerCount() || body->onSensorEnd.listenerCount();
    for (auto* body : owners->bodies) body->updateEventFlags(sensorListeners);
    b2World_Step(steppedWorld, 1.0f / 60.0f, subSteps_);

    enum class Kind { ContactBegin, ContactEnd, Hit, SensorBegin, SensorEnd };
    struct Pending {
        Kind kind;
        b2ShapeId a, b;
        tc::Vec2 point{}, normal{};
        float speed = 0.0f;
    };
    std::vector<Pending> pending;
    const auto contacts = b2World_GetContactEvents(steppedWorld);
    const auto sensors = b2World_GetSensorEvents(steppedWorld);
    const auto pixels = [](b2Vec2 v) { return tc::Vec2(v.x * 30.0f, v.y * 30.0f); };
    for (int i = 0; i < contacts.beginCount; ++i) {
        const auto& e = contacts.beginEvents[i];
        const auto manifold = b2Contact_GetData(e.contactId).manifold;
        // Zero-point manifolds use the midpoint between the two body origins.
        auto point = manifold.pointCount ? pixels(b2OffsetPos(b2Body_GetWorldCenter(b2Shape_GetBody(e.shapeIdA)), manifold.points[0].anchorA)) :
            (pixels(b2Body_GetPosition(b2Shape_GetBody(e.shapeIdA))) +
             pixels(b2Body_GetPosition(b2Shape_GetBody(e.shapeIdB)))) * 0.5f;
        pending.push_back({Kind::ContactBegin, e.shapeIdA, e.shapeIdB, point,
                           {manifold.normal.x, manifold.normal.y}});
    }
    for (int i = 0; i < contacts.endCount; ++i) {
        const auto& e = contacts.endEvents[i];
        pending.push_back({Kind::ContactEnd, e.shapeIdA, e.shapeIdB});
    }
    for (int i = 0; i < contacts.hitCount; ++i) {
        const auto& e = contacts.hitEvents[i];
        pending.push_back({Kind::Hit, e.shapeIdA, e.shapeIdB, pixels(e.point),
                           {e.normal.x, e.normal.y}, e.approachSpeed * 30.0f});
    }
    for (int i = 0; i < sensors.beginCount; ++i) {
        const auto& e = sensors.beginEvents[i];
        pending.push_back({Kind::SensorBegin, e.sensorShapeId, e.visitorShapeId});
    }
    for (int i = 0; i < sensors.endCount; ++i) {
        const auto& e = sensors.endEvents[i];
        pending.push_back({Kind::SensorEnd, e.sensorShapeId, e.visitorShapeId});
    }
    const auto events = b2World_GetBodyEvents(steppedWorld);
    std::vector<b2BodyMoveEvent> moves;
    if (events.moveCount) moves.assign(events.moveEvents, events.moveEvents + events.moveCount);
    for (const auto& event : moves) {
        if (!live()) return;
        auto* owner = static_cast<RigidBody2D*>(event.userData);
        if (owners->bodies.count(owner) && B2_ID_EQUALS(owner->id(), event.bodyId))
            owner->syncNode(event.transform);
    }
    // Resolve from generation-bearing ids for every side AND every listener.
    // GuardedEventDelivery owns Event's snapshot before calling user code.
    const auto resolve = [&](b2ShapeId shape) -> RigidBody2D* {
        if (!live() || !b2Shape_IsValid(shape)) return nullptr;
        auto bodyId = b2Shape_GetBody(shape);
        auto* body = static_cast<RigidBody2D*>(b2Body_GetUserData(bodyId));
        return owners->bodies.count(body) && B2_ID_EQUALS(body->id(), bodyId) ? body : nullptr;
    };
    for (const auto& e : pending) {
        for (int side = 0; side < 2; ++side) {
            if (!live()) return;
            const auto shape = side == 0 ? e.a : e.b;
            const auto otherShape = side == 0 ? e.b : e.a;
            auto* receiver = resolve(shape);
            if (!receiver) continue;
            const auto normal = e.normal * (side == 0 ? 1.0f : -1.0f);
            auto deliver = [&](auto& source, auto payload) {
                // Core dependency: tc/events/tcEvent.h retains the snapshot across owner destruction.
                tc::internal::GuardedEventDelivery<decltype(payload)>::notify(source, payload, [&] {
                    if (!live() || !resolve(shape)) return false;
                    payload.other = resolve(otherShape);
                    return true;
                });
            };
            switch (e.kind) {
            case Kind::ContactBegin:
                deliver(receiver->onContactBegin, ContactEvent{nullptr, shape, otherShape, e.point, normal}); break;
            case Kind::ContactEnd:
                deliver(receiver->onContactEnd, ContactEvent{nullptr, shape, otherShape}); break;
            case Kind::Hit:
                deliver(receiver->onHit, HitEvent{nullptr, shape, otherShape, e.point, normal, e.speed}); break;
            case Kind::SensorBegin:
                deliver(receiver->onSensorBegin, SensorEvent{nullptr, shape, otherShape}); break;
            case Kind::SensorEnd:
                deliver(receiver->onSensorEnd, SensorEvent{nullptr, shape, otherShape}); break;
            }
        }
    }
}

void World::setSubSteps(int count) {
    if (count > 0) subSteps_ = count;
}

void World::setGravity(const tc::Vec2& gravity) {
    gravity_ = gravity;
    if (b2World_IsValid(worldId_)) {
        b2World_SetGravity(worldId_, {gravity.x / 30.0f, gravity.y / 30.0f});
    }
}

void World::setWorkerCount(int count) {
    workerCount_ = count;
    if (b2World_IsValid(worldId_)) b2World_SetWorkerCount(worldId_, count);
}

} // namespace tcx::box2d
