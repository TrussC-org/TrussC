// =============================================================================
// tcxCollisionManager.cpp - Collision Event Management Implementation
// =============================================================================

#include "tcxCollisionManager.h"
#include "tcxBox2dWorld.h"
#include "tcxBox2dBody.h"
#include <algorithm>

namespace tcx::box2d {

// =============================================================================
// Update - Dispatch Stay Events
// =============================================================================

// The first still-touching contact of a pair (null if none).
static b2Contact* touchingContact(const std::vector<b2Contact*>& contacts) {
    for (b2Contact* c : contacts) {
        if (c && c->IsTouching()) return c;
    }
    return nullptr;
}

void CollisionManager::update() {
    // World-level Stay (Mod layer): once per touching body pair.
    for (auto& pair : worldPairs_) {
        if (b2Contact* contact = touchingContact(pair.contacts)) {
            WorldContact wc = makeWorldContact(contact);
            contactStay.notify(wc);
        }
    }

    // Dispatch onCollisionStay once per touching collider pair
    for (auto& pair : activeContacts_) {
        b2Contact* contact = touchingContact(pair.contacts);
        if (pair.a && pair.b && contact) {
            // Notify A about collision with B
            CollisionEvent eventA = createEvent(contact, pair.a, pair.b);
            pair.a->notifyStay(eventA);

            // Notify B about collision with A
            CollisionEvent eventB = createEvent(contact, pair.b, pair.a);
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

    // World-level Began (Mod layer), regardless of Collider2D: on the body
    // pair's first contact only.
    if (addContact(worldPairs_, fixtureA->GetBody(), fixtureB->GetBody(), contact)) {
        WorldContact wc = makeWorldContact(contact);
        contactBegan.notify(wc);
    }

    Collider2D* colliderA = getColliderFromFixture(fixtureA);
    Collider2D* colliderB = getColliderFromFixture(fixtureB);

    if (!colliderA || !colliderB) return;

    // Track the contact; only the collider pair's first one is an Enter
    if (!addContact(activeContacts_, colliderA, colliderB, contact)) return;

    // Dispatch onCollisionEnter
    CollisionEvent eventA = createEvent(contact, colliderA, colliderB);
    colliderA->notifyEnter(eventA);

    CollisionEvent eventB = createEvent(contact, colliderB, colliderA);
    colliderB->notifyEnter(eventB);
}

void CollisionManager::EndContact(b2Contact* contact) {
    b2Fixture* fixtureA = contact->GetFixtureA();
    b2Fixture* fixtureB = contact->GetFixtureB();

    // World-level Ended (Mod layer), regardless of Collider2D: when the body
    // pair's last contact ends.
    if (removeContact(worldPairs_, fixtureA->GetBody(), fixtureB->GetBody(), contact)) {
        WorldContact wc = makeWorldContact(contact);
        contactEnded.notify(wc);
    }

    Collider2D* colliderA = getColliderFromFixture(fixtureA);
    Collider2D* colliderB = getColliderFromFixture(fixtureB);

    if (!colliderA || !colliderB) return;

    // Untrack the contact; only the collider pair's last one is an Exit
    if (!removeContact(activeContacts_, colliderA, colliderB, contact)) return;

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

WorldContact CollisionManager::makeWorldContact(b2Contact* contact) {
    WorldContact wc;
    wc.a = contact->GetFixtureA()->GetBody();
    wc.b = contact->GetFixtureB()->GetBody();

    b2WorldManifold worldManifold;
    contact->GetWorldManifold(&worldManifold);
    if (contact->GetManifold()->pointCount > 0) {
        wc.point = World::toPixels(worldManifold.points[0]);
        wc.normal = tc::Vec2(worldManifold.normal.x, worldManifold.normal.y);
    }
    return wc;
}

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

template<typename T>
bool CollisionManager::addContact(std::vector<ContactPair<T>>& pairs, T* a, T* b, b2Contact* contact) {
    for (auto& pair : pairs) {
        if (pair.is(a, b)) {
            pair.contacts.push_back(contact);
            return false;
        }
    }
    ContactPair<T> pair;
    pair.a = a;
    pair.b = b;
    pair.contacts.push_back(contact);
    pairs.push_back(std::move(pair));
    return true;
}

template<typename T>
bool CollisionManager::removeContact(std::vector<ContactPair<T>>& pairs, T* a, T* b, b2Contact* contact) {
    for (auto it = pairs.begin(); it != pairs.end(); ++it) {
        if (!it->is(a, b)) continue;
        auto& cs = it->contacts;
        cs.erase(std::remove(cs.begin(), cs.end(), contact), cs.end());
        if (!cs.empty()) return false;
        pairs.erase(it);
        return true;
    }
    return false;
}

} // namespace tcx::box2d
