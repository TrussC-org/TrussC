#include "tcxBox2dMod.h"

namespace tcx::box2d {

World::~World() {
    owners_->bodies.clear();
    if (b2World_IsValid(worldId_)) b2DestroyWorld(worldId_);
}

void World::setup() {
    if (b2World_IsValid(worldId_)) b2DestroyWorld(worldId_);
    owners_->bodies.clear();
    auto def = b2DefaultWorldDef();
    def.gravity = {gravity_.x / 30.0f, gravity_.y / 30.0f};
    def.workerCount = workerCount_;
    worldId_ = b2CreateWorld(&def);
    owners_->worldId = worldId_;
}

void World::update() {
    if (!b2World_IsValid(worldId_)) return;
    b2World_Step(worldId_, 1.0f / 60.0f, subSteps_);
    const auto events = b2World_GetBodyEvents(worldId_);
    // Node transform observers may destroy Mods, reset or destroy this World.
    std::vector<b2BodyMoveEvent> moves;
    if (events.moveCount) moves.assign(events.moveEvents, events.moveEvents + events.moveCount);
    const auto owners = owners_;
    const auto steppedWorld = worldId_;
    for (const auto& event : moves) {
        if (!b2World_IsValid(steppedWorld)) break;
        auto* owner = static_cast<RigidBody2D*>(event.userData);
        // Raw bodies can have arbitrary userData; never dereference it unless
        // it belongs to a live Mod in this world.
        if (owners->bodies.count(owner) && B2_ID_EQUALS(owner->id(), event.bodyId)) {
            owner->syncNode(event.transform);
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
