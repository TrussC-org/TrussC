// =============================================================================
// tcxCollisionManager.cpp - Collision Event Management Implementation
// =============================================================================

#include "tcxCollisionManager.h"
#include "tcxBox2dWorld.h"
#include "tcxBox2dBody.h"

namespace tcx::box2d {

// =============================================================================
// Update - Dispatch Stay Events
// =============================================================================

void CollisionManager::update() {
    // Dispatch onCollisionStay for all active contacts
    for (auto& pair : activeContacts_) {
        if (pair.a && pair.b && pair.contact) {
            // Notify A about collision with B
            CollisionEvent eventA = createEvent(pair.contact, pair.a, pair.b);
            pair.a->notifyStay(eventA);

            // Notify B about collision with A
            CollisionEvent eventB = createEvent(pair.contact, pair.b, pair.a);
            pair.b->notifyStay(eventB);
        }
    }
}

// =============================================================================
// b2ContactListener Implementation
// =============================================================================

void CollisionManager::BeginContact(b2Contact* contact) {
    b2Fixture* fixtureA = contact->GetFixtureA();
    b2Fixture* fixtureB = contact->GetFixtureB();

    Collider2D* colliderA = getColliderFromFixture(fixtureA);
    Collider2D* colliderB = getColliderFromFixture(fixtureB);

    if (!colliderA || !colliderB) return;

    // Add to active contacts for Stay events
    activeContacts_.push_back({colliderA, colliderB, contact});

    // Dispatch onCollisionEnter
    CollisionEvent eventA = createEvent(contact, colliderA, colliderB);
    colliderA->notifyEnter(eventA);

    CollisionEvent eventB = createEvent(contact, colliderB, colliderA);
    colliderB->notifyEnter(eventB);
}

void CollisionManager::EndContact(b2Contact* contact) {
    b2Fixture* fixtureA = contact->GetFixtureA();
    b2Fixture* fixtureB = contact->GetFixtureB();

    Collider2D* colliderA = getColliderFromFixture(fixtureA);
    Collider2D* colliderB = getColliderFromFixture(fixtureB);

    if (!colliderA || !colliderB) return;

    // Remove from active contacts
    removeContactPair(colliderA, colliderB);

    // Dispatch onCollisionExit
    CollisionEvent eventA = createEvent(contact, colliderA, colliderB);
    colliderA->notifyExit(eventA);

    CollisionEvent eventB = createEvent(contact, colliderB, colliderA);
    colliderB->notifyExit(eventB);
}

void CollisionManager::PreSolve(b2Contact* contact, const b2Manifold* oldManifold) {
    // Can be used to disable contact or modify friction/restitution
    // For now, just pass through
    (void)contact;
    (void)oldManifold;
}

void CollisionManager::PostSolve(b2Contact* contact, const b2ContactImpulse* impulse) {
    // Could update impulse data here for more accurate collision info
    // For now, just pass through
    (void)contact;
    (void)impulse;
}

// =============================================================================
// Helper Methods
// =============================================================================

Collider2D* CollisionManager::getColliderFromFixture(b2Fixture* fixture) {
    if (!fixture) return nullptr;

    uintptr_t ptr = fixture->GetUserData().pointer;
    if (ptr == 0) return nullptr;

    return reinterpret_cast<Collider2D*>(ptr);
}

CollisionEvent CollisionManager::createEvent(b2Contact* contact, Collider2D* self, Collider2D* other) {
    CollisionEvent event;
    event.other = other ? other->getBody() : nullptr;

    // Get contact point and normal
    b2WorldManifold worldManifold;
    contact->GetWorldManifold(&worldManifold);

    if (contact->GetManifold()->pointCount > 0) {
        event.contactPoint = World::toPixels(worldManifold.points[0]);
        event.normal = tc::Vec2(worldManifold.normal.x, worldManifold.normal.y);
    } else {
        // Fallback: use other body's position if no contact points available
        if (event.other) {
            event.contactPoint = event.other->getPhysicsPosition();
        }
    }

    return event;
}

void CollisionManager::removeContactPair(Collider2D* a, Collider2D* b) {
    activeContacts_.erase(
        std::remove_if(activeContacts_.begin(), activeContacts_.end(),
            [a, b](const ContactPair& pair) {
                return (pair.a == a && pair.b == b) ||
                       (pair.a == b && pair.b == a);
            }),
        activeContacts_.end()
    );
}

} // namespace tcx::box2d
