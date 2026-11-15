#include "tcxBox2dWorld.h"

namespace tcx::box2d {

World::~World() {
    if (b2World_IsValid(worldId_)) b2DestroyWorld(worldId_);
}

void World::setup() {
    if (b2World_IsValid(worldId_)) b2DestroyWorld(worldId_);
    auto def = b2DefaultWorldDef();
    def.gravity = {gravity_.x / 30.0f, gravity_.y / 30.0f};
    def.workerCount = workerCount_;
    worldId_ = b2CreateWorld(&def);
}

void World::update() {
    if (b2World_IsValid(worldId_)) b2World_Step(worldId_, 1.0f / 60.0f, subSteps_);
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
