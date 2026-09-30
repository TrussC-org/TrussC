// =============================================================================
// tcxCollisionManager.cpp - Collision Event Management Implementation
// =============================================================================

#include "tcxCollisionManager.h"
#include "tcxBox2dWorld.h"
#include "tcxBox2dBody.h"
#include <algorithm>
#include <iterator>

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
    flushPendingExits();

    // Listeners may destroy or disable bodies, which ends contacts right away
    // (EndContact): pairs are then emptied, not erased, until the loops end,
    // and each pair is looked up again by index after every notify.
    dispatching_ = true;

    // World-level Stay (Mod layer): once per touching body pair.
    for (size_t i = 0; i < worldPairs_.size(); ++i) {
        if (b2Contact* contact = touchingContact(worldPairs_[i].contacts)) {
            WorldContact wc = makeWorldContact(contact);
            contactStay.notify(wc);
        }
    }

    // Dispatch onCollisionStay once per touching collider pair
    for (size_t i = 0; i < activeContacts_.size(); ++i) {
        b2Contact* contact = touchingContact(activeContacts_[i].contacts);
        Collider2D* a = activeContacts_[i].a;
        Collider2D* b = activeContacts_[i].b;
        if (!a || !b || !contact) continue;

        // Notify A about collision with B
        CollisionEvent eventA = createEvent(contact, a, b);
        a->notifyStay(eventA);

        // Notify B about collision with A, if A's listener left them touching
        contact = touchingContact(activeContacts_[i].contacts);
        if (!contact) continue;
        CollisionEvent eventB = createEvent(contact, b, a);
        b->notifyStay(eventB);
    }

    dispatching_ = false;

    // Drop the pairs emptied during the loops.
    auto emptied = [](const auto& pair) { return pair.contacts.empty() && !pair.exitPending; };
    worldPairs_.erase(std::remove_if(worldPairs_.begin(), worldPairs_.end(), emptied), worldPairs_.end());
    activeContacts_.erase(std::remove_if(activeContacts_.begin(), activeContacts_.end(), emptied),
                          activeContacts_.end());
}

void CollisionManager::flushPendingExits() {
    // Take the pending pairs out first: the listeners may end more contacts.
    std::vector<BodyPair> bodies;
    std::vector<ColliderPair> colliders;
    auto take = [](auto& pairs, auto& out) {
        auto it = std::stable_partition(pairs.begin(), pairs.end(),
                                        [](const auto& pair) { return !pair.exitPending; });
        std::move(it, pairs.end(), std::back_inserter(out));
        pairs.erase(it, pairs.end());
    };
    take(worldPairs_, bodies);
    take(activeContacts_, colliders);

    for (auto& pair : bodies) contactEnded.notify(pair.exit);
    for (auto& pair : colliders) {
        pair.a->notifyExit(pair.exit.a);
        pair.b->notifyExit(pair.exit.b);
    }
}

// =============================================================================
// b2ContactListener Implementation
// =============================================================================

// True inside b2World::Step() (contact callbacks from Collide / SolveTOI).
// EndContact also comes outside a step, from DestroyBody(), SetEnabled(false)
// or SetType(): those Exits fire at once.
static bool inStep(b2Contact* contact) {
    return contact->GetFixtureA()->GetBody()->GetWorld()->IsLocked();
}

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
    const bool stepping = inStep(contact);

    // World-level Ended (Mod layer), regardless of Collider2D: when the body
    // pair's last contact ends.
    if (BodyPair* pair = removeContact(worldPairs_, fixtureA->GetBody(), fixtureB->GetBody(), contact)) {
        WorldContact wc = makeWorldContact(contact);
        if (endPair(worldPairs_, pair, wc, stepping)) contactEnded.notify(wc);
    }

    Collider2D* colliderA = getColliderFromFixture(fixtureA);
    Collider2D* colliderB = getColliderFromFixture(fixtureB);

    if (!colliderA || !colliderB) return;

    // Untrack the contact; only the collider pair's last one is an Exit
    ColliderPair* pair = removeContact(activeContacts_, colliderA, colliderB, contact);
    if (!pair) return;

    // Dispatch onCollisionExit (after the step when inside one)
    ColliderExit exit{createEvent(contact, pair->a, pair->b), createEvent(contact, pair->b, pair->a)};
    Collider2D* a = pair->a;
    Collider2D* b = pair->b;
    if (!endPair(activeContacts_, pair, exit, stepping)) return;
    a->notifyExit(exit.a);
    b->notifyExit(exit.b);
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

template<typename T, typename Exit>
bool CollisionManager::addContact(std::vector<ContactPair<T, Exit>>& pairs, T* a, T* b, b2Contact* contact) {
    for (auto& pair : pairs) {
        if (pair.is(a, b)) {
            // Empty and not pending: a pair ended during update()'s dispatch,
            // so this contact starts it again.
            const bool starts = pair.contacts.empty() && !pair.exitPending;
            pair.exitPending = false;   // touching again within the step: no Exit
            pair.contacts.push_back(contact);
            return starts;
        }
    }
    ContactPair<T, Exit> pair;
    pair.a = a;
    pair.b = b;
    pair.contacts.push_back(contact);
    pairs.push_back(std::move(pair));
    return true;
}

template<typename T, typename Exit>
CollisionManager::ContactPair<T, Exit>* CollisionManager::removeContact(
        std::vector<ContactPair<T, Exit>>& pairs, T* a, T* b, b2Contact* contact) {
    for (auto& pair : pairs) {
        if (!pair.is(a, b)) continue;
        auto& cs = pair.contacts;
        auto it = std::find(cs.begin(), cs.end(), contact);
        if (it == cs.end()) return nullptr;
        cs.erase(it);
        return cs.empty() ? &pair : nullptr;
    }
    return nullptr;
}

template<typename T, typename Exit>
bool CollisionManager::endPair(std::vector<ContactPair<T, Exit>>& pairs, ContactPair<T, Exit>* pair,
                               const Exit& exit, bool stepping) {
    if (stepping) {
        pair->exitPending = true;
        pair->exit = exit;
        return false;
    }
    if (!dispatching_) pairs.erase(pairs.begin() + (pair - pairs.data()));
    return true;
}

} // namespace tcx::box2d
