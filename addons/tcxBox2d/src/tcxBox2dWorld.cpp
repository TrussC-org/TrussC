// =============================================================================
// tcxBox2dWorld.cpp - Box2D World Management
// =============================================================================

#include "tcxBox2dWorld.h"
#include "tcxBox2dBody.h"
// The Mod constructors initialize deprecated event aliases internally.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#include "tcxBox2dMod.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#include "tcxCollisionManager.h"

namespace tcx::box2d {

// Default scale: 30 pixels = 1 meter
float World::scale = 30.0f;

// =============================================================================
// Constructor / Destructor
// =============================================================================
namespace detail {
DispatchState::DispatchState() : manager(std::make_unique<CollisionManager>()) {}
DispatchState::~DispatchState() = default;
void DispatchState::SayGoodbye(b2Fixture* fixture) {
    b2Body* body = fixture->GetBody();
    auto* owner = reinterpret_cast<BodyOwner*>(body->GetUserData().pointer);
    body->GetUserData().pointer = 0;
    if (owner) owner->onWorldReleased();
    manager->forget(body); // includes ownerless endpoints and active Exit frames
    if (world && world->groundBody_ == body) world->groundBody_ = nullptr;
    if (world && world->dragAnchorBody_ == body) world->dragAnchorBody_ = nullptr;
    if (delegate) delegate->SayGoodbye(fixture);
}
void DispatchState::SayGoodbye(b2Joint* joint) {
    if (world && world->mouseJoint_ == joint) world->mouseJoint_ = nullptr;
    if (delegate) delegate->SayGoodbye(joint);
}
} // namespace detail

World::World() : dispatch_(std::make_shared<detail::DispatchState>()) {
    dispatch_->world = this;
    dispatch_->manager->state_ = dispatch_;
}

World::~World() {
    dispatch_->alive = false;
    dispatch_->token.reset();
    updateListener_.disconnect();
    contactRouter_.beganL.disconnect();
    contactRouter_.stayL.disconnect();
    contactRouter_.endedL.disconnect();
    clear();
    dispatch_->physics = nullptr;
    dispatch_->world = nullptr;
    world_.reset();
}

void World::ensureContactRouter() {
    if (contactRouter_.beganL) return;
    auto* cm = getCollisionManager();
    if (!cm) return;
    std::weak_ptr<detail::DispatchState> weak = dispatch_;
    // Capture only the World identity and independently retained dispatch state.
    // A routed listener may delete this World (including its router handles).
    contactRouter_.beganL = cm->contactBegan.listen([wp = this, weak](WorldContact& c) {
        if (auto s = weak.lock(); s && s->alive) detail::routeContact(wp, c, 0);
    });
    contactRouter_.stayL = cm->contactStay.listen([wp = this, weak](WorldContact& c) {
        if (auto s = weak.lock(); s && s->alive) detail::routeContact(wp, c, 1);
    });
    contactRouter_.endedL = cm->contactEnded.listen([wp = this, weak](WorldContact& c) {
        if (auto s = weak.lock(); s && s->alive) detail::routeContact(wp, c, 2);
    });
}

// =============================================================================
// Initialization
// =============================================================================
void World::setup(const tc::Vec2& gravity) {
    setup(gravity.x, gravity.y);
}

void World::setup(float gravityX, float gravityY) {
    // Clean up existing state if setup() is called multiple times
    if (world_) {
        clear();
        ++dispatch_->setupGeneration;
        world_.reset();
    }

    // Reset groundBody_ to ensure clean state
    groundBody_ = nullptr;

    // In Box2D coordinate system, down is positive, so use as is
    b2Vec2 g(gravityX / scale, gravityY / scale);
    world_ = std::make_unique<b2World>(g);

    // Create and register collision manager
    if (!dispatch_->manager) dispatch_->manager = std::make_unique<CollisionManager>();
    dispatch_->manager->state_ = dispatch_;
    dispatch_->physics = world_.get();
    world_->SetContactListener(dispatch_->manager.get());
    world_->SetDestructionListener(dispatch_.get());

    // Fresh stepping state, and (re)arm the auto-update listener.
    accumulator_ = 0.0f;
    lastStepFrame_ = UINT64_MAX;
    refreshAutoUpdate();
}

// =============================================================================
// Simulation
// =============================================================================
void World::update() {
    auto state = dispatch_;
    if (!world_ || state->depth) return;
    uint64_t frame = tc::getFrameCount();
    if (frame == lastStepFrame_) return;
    lastStepFrame_ = frame;
    accumulator_ += static_cast<float>(tc::getDeltaTime());
    const uint64_t generation = state->setupGeneration;
    ++state->manager->journal_.updateCounter;
    state->manager->drain(); // raw Steps, including a zero-sub-step update
    if (!state->alive) return;
    if (state->setupGeneration != generation) return;
    const int MAX_SUBSTEPS = 8;
    int n = 0;
    while (accumulator_ >= timeStep_ && n < MAX_SUBSTEPS) {
        // Only changed classic flags are synchronized. Per-fixture overrides
        // survive an unchanged flag, including the default false.
        for (auto* body = world_->GetBodyList(); body; body = body->GetNext()) {
            auto* owner = reinterpret_cast<detail::BodyOwner*>(body->GetUserData().pointer);
            if (auto* classic = dynamic_cast<Body*>(owner)) {
                if (auto* collider = classic->getCollider()) collider->syncTriggerMode();
            }
        }
        state->manager->beginStep();
        const float step = timeStep_;
        world_->Step(step, velocityIterations_, positionIterations_);
        state->manager->endStep(); // close BEFORE callbacks/wrapper teardown
        state->manager->drain();
        if (!state->alive) return; // never read this after listener deletion
        if (state->setupGeneration != generation) return;
        accumulator_ -= step;
        ++n;
    }
    if (n >= MAX_SUBSTEPS) accumulator_ = 0.0f;
    state->manager->dispatchStay();
}

void World::setAutoUpdate(bool on) {
    autoUpdate_ = on;
    refreshAutoUpdate();
}

void World::refreshAutoUpdate() {
    if (autoUpdate_ && world_) {
        // listen() replaces any prior listener (RAII), so this is idempotent.
        updateListener_ = tc::events().update.listen([this]() { update(); });
    } else {
        updateListener_ = tc::EventListener();  // unregister
    }
}

void World::setSimulationRate(float hz) {
    if (hz > 0) {
        timeStep_ = 1.0f / hz;
    }
}

void World::setFPS(float fps) {
    setSimulationRate(fps);
}

void World::setVelocityIterations(int n) {
    velocityIterations_ = n;
}

void World::setPositionIterations(int n) {
    positionIterations_ = n;
}

void World::setGravity(const tc::Vec2& gravity) {
    setGravity(gravity.x, gravity.y);
}

void World::setGravity(float x, float y) {
    if (world_) {
        world_->SetGravity(b2Vec2(x / scale, y / scale));
    }
}

tc::Vec2 World::getGravity() const {
    if (world_) {
        b2Vec2 g = world_->GetGravity();
        return tc::Vec2(g.x * scale, g.y * scale);
    }
    return tc::Vec2(0, 0);
}

// =============================================================================
// Bounds
// =============================================================================
void World::createBounds(float x, float y, float width, float height) {
    if (!world_) return;

    // Remove existing bounds
    if (groundBody_) {
        if (dispatch_->manager) dispatch_->manager->forget(groundBody_);
        world_->DestroyBody(groundBody_);
        if (dispatch_->manager) dispatch_->manager->forgetBody(groundBody_);
        groundBody_ = nullptr;
    }

    // Create static body
    b2BodyDef bodyDef;
    bodyDef.type = b2_staticBody;
    groundBody_ = world_->CreateBody(&bodyDef);

    // Create four sides
    float halfW = toBox2d(width / 2);
    float halfH = toBox2d(height / 2);
    float centerX = toBox2d(x + width / 2);
    float centerY = toBox2d(y + height / 2);

    b2EdgeShape edge;

    // Bottom edge
    edge.SetTwoSided(b2Vec2(centerX - halfW, centerY + halfH),
                     b2Vec2(centerX + halfW, centerY + halfH));
    groundBody_->CreateFixture(&edge, 0.0f);

    // Top edge
    edge.SetTwoSided(b2Vec2(centerX - halfW, centerY - halfH),
                     b2Vec2(centerX + halfW, centerY - halfH));
    groundBody_->CreateFixture(&edge, 0.0f);

    // Left edge
    edge.SetTwoSided(b2Vec2(centerX - halfW, centerY - halfH),
                     b2Vec2(centerX - halfW, centerY + halfH));
    groundBody_->CreateFixture(&edge, 0.0f);

    // Right edge
    edge.SetTwoSided(b2Vec2(centerX + halfW, centerY - halfH),
                     b2Vec2(centerX + halfW, centerY + halfH));
    groundBody_->CreateFixture(&edge, 0.0f);
}

void World::createBounds() {
    createBounds(0, 0, tc::getWindowWidth(), tc::getWindowHeight());
}

void World::createGround(float y, float width) {
    if (!world_) return;

    if (groundBody_) {
        if (dispatch_->manager) dispatch_->manager->forget(groundBody_);
        world_->DestroyBody(groundBody_);
        if (dispatch_->manager) dispatch_->manager->forgetBody(groundBody_);
        groundBody_ = nullptr;
    }

    b2BodyDef bodyDef;
    bodyDef.type = b2_staticBody;
    groundBody_ = world_->CreateBody(&bodyDef);

    b2EdgeShape edge;
    edge.SetTwoSided(b2Vec2(0, toBox2d(y)),
                     b2Vec2(toBox2d(width), toBox2d(y)));
    groundBody_->CreateFixture(&edge, 0.0f);
}

void World::createGround() {
    createGround(tc::getWindowHeight(), tc::getWindowWidth());
}

// =============================================================================
// Body Management
// =============================================================================
void World::attachOwner(b2Body* body, detail::BodyOwner& owner) {
    static uint64_t nextGeneration = 0;  // physics objects live on the main thread
    owner.generation = ++nextGeneration;
    body->GetUserData().pointer = reinterpret_cast<uintptr_t>(&owner);
}

detail::BodyOwner* World::findOwner(b2Body* body) const {
    // A listener can destroy a body, then create another at the same address.
    // Compare against live bodies before reading any user data.
    if (world_ && body) {
        for (b2Body* live = world_->GetBodyList(); live; live = live->GetNext()) {
            if (live == body) {
                return reinterpret_cast<detail::BodyOwner*>(live->GetUserData().pointer);
            }
        }
    }
    return nullptr;
}

void World::releaseBody(b2Body* body, ExitDelivery delivery) {
    if (!world_ || !body) return;
    auto state = dispatch_;
    auto operation = state->manager->mutation(delivery == ExitDelivery::Deferred);
    if (mouseJoint_ && mouseJoint_->GetBodyB() == body) endDrag();
    auto* owner = reinterpret_cast<detail::BodyOwner*>(body->GetUserData().pointer);
    body->GetUserData().pointer = 0;
    if (owner) owner->onWorldReleased();
    state->manager->forget(body); // also covers explicit fixture/owner release
    world_->DestroyBody(body);
    state->manager->forget(body);
    // No World/owner access after delivery; a listener can delete either.
    if (delivery == ExitDelivery::Immediate) state->manager->drain();
}

void World::clear() {
    // Cancel active/suspended payloads before the first object is released.
    dispatch_->manager->clearContacts();
    endDrag();
    if (world_) {
        // Invalidate every borrowed handle before Box2D releases any objects.
        for (b2Body* body = world_->GetBodyList(); body; body = body->GetNext()) {
            auto* owner = reinterpret_cast<detail::BodyOwner*>(body->GetUserData().pointer);
            body->GetUserData().pointer = 0;
            if (owner) owner->onWorldReleased();
        }
        // Clear is silent, including any Exits queued by earlier owner teardown.
        // Keep the world, its settings and its auto-update subscription intact.
        // Restore whatever listener was registered (a user proxy included).
        b2ContactListener* listener = world_->GetContactManager().m_contactListener;
        world_->SetContactListener(nullptr);
        while (b2Body* body = world_->GetBodyList()) world_->DestroyBody(body);
        world_->SetContactListener(listener);
    }
    groundBody_ = nullptr;
    dragAnchorBody_ = nullptr;
}

int World::getBodyCount() const {
    return world_ ? world_->GetBodyCount() : 0;
}

// =============================================================================
// Point Query
// =============================================================================
Body* World::getBodyAtPoint(const tc::Vec2& point) {
    return getBodyAtPoint(point.x, point.y);
}

Body* World::getBodyAtPoint(float px, float py) {
    if (!world_) return nullptr;

    b2Vec2 point = toBox2d(px, py);

    // Check all bodies (exclude static bodies)
    for (b2Body* b = world_->GetBodyList(); b; b = b->GetNext()) {
        if (b->GetType() != b2_dynamicBody) continue;

        for (b2Fixture* f = b->GetFixtureList(); f; f = f->GetNext()) {
            if (f->TestPoint(point)) {
                auto* owner = reinterpret_cast<detail::BodyOwner*>(b->GetUserData().pointer);
                if (auto* classic = dynamic_cast<Body*>(owner)) return classic;
            }
        }
    }
    return nullptr;
}

// =============================================================================
// Mouse Drag
// =============================================================================
void World::startDrag(Body* body, const tc::Vec2& target) {
    startDrag(body, target.x, target.y);
}

void World::startDrag(Body* body, float tx, float ty) {
    if (!world_ || !body || !body->getBody() || body->getBody()->GetWorld() != world_.get()) return;

    // Destroy existing joint if any
    endDrag();

    // Create static body for anchor (if not exists)
    if (!dragAnchorBody_) {
        b2BodyDef anchorDef;
        anchorDef.type = b2_staticBody;
        dragAnchorBody_ = world_->CreateBody(&anchorDef);
    }

    // Create mouse joint
    b2MouseJointDef jointDef;
    jointDef.bodyA = dragAnchorBody_;
    jointDef.bodyB = body->getBody();
    jointDef.target = toBox2d(tx, ty);
    jointDef.maxForce = 1000.0f * body->getBody()->GetMass();
    jointDef.stiffness = 50.0f;
    jointDef.damping = 0.9f;

    mouseJoint_ = static_cast<b2MouseJoint*>(world_->CreateJoint(&jointDef));

    // Wake up body during drag
    body->getBody()->SetAwake(true);
}

void World::updateDrag(const tc::Vec2& target) {
    updateDrag(target.x, target.y);
}

void World::updateDrag(float tx, float ty) {
    if (mouseJoint_) {
        mouseJoint_->SetTarget(toBox2d(tx, ty));
    }
}

void World::endDrag() {
    if (mouseJoint_ && world_) {
        world_->DestroyJoint(mouseJoint_);
        mouseJoint_ = nullptr;
    }
}

bool World::isDragging() const {
    return mouseJoint_ != nullptr;
}

tc::Vec2 World::getDragAnchor() const {
    if (mouseJoint_) {
        return toPixels(mouseJoint_->GetAnchorB());
    }
    return tc::Vec2(0, 0);
}

// =============================================================================
// Coordinate Conversion
// =============================================================================
b2Vec2 World::toBox2d(const tc::Vec2& v) {
    return b2Vec2(v.x / scale, v.y / scale);
}

b2Vec2 World::toBox2d(float x, float y) {
    return b2Vec2(x / scale, y / scale);
}

float World::toBox2d(float val) {
    return val / scale;
}

tc::Vec2 World::toPixels(const b2Vec2& v) {
    return tc::Vec2(v.x * scale, v.y * scale);
}

float World::toPixels(float val) {
    return val * scale;
}

} // namespace tcx::box2d
