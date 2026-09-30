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
    const bool outer = !dispatching_;
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
        // (a destroyed body ends the contacts at once)
        contact = touchingContact(activeContacts_[i].contacts);
        if (!contact) continue;
        a = activeContacts_[i].a;
        b = activeContacts_[i].b;
        CollisionEvent eventB = createEvent(contact, b, a);
        b->notifyStay(eventB);
    }

    if (outer) {
        dispatching_ = false;
        dropEmptied();
    }
}

void CollisionManager::flushPendingExits() {
    // The pairs stay in place while the listeners run: they may destroy
    // bodies, which ends pairs (EndContact) or nulls a side out of them
    // (forget()). Each side is read from the pair right before its notify.
    const bool outer = !dispatching_;
    dispatching_ = true;

    for (size_t i = 0; i < worldPairs_.size(); ++i) {
        if (!worldPairs_[i].exitPending) continue;
        worldPairs_[i].exitPending = false;   // no contacts left: dropped below
        WorldContact wc = worldPairs_[i].exit;
        if (wc.a || wc.b) contactEnded.notify(wc);
    }
    for (size_t i = 0; i < activeContacts_.size(); ++i) {
        if (!activeContacts_[i].exitPending) continue;
        activeContacts_[i].exitPending = false;
        if (Collider2D* a = activeContacts_[i].a) {
            CollisionEvent e = activeContacts_[i].exit.a;
            a->notifyExit(e);
        }
        // A's listener may have destroyed B: forget() nulled it
        if (Collider2D* b = activeContacts_[i].b) {
            CollisionEvent e = activeContacts_[i].exit.b;
            b->notifyExit(e);
        }
    }

    if (outer) {
        dispatching_ = false;
        dropEmptied();
    }
}

void CollisionManager::dropEmptied() {
    auto emptied = [](const auto& pair) { return pair.contacts.empty() && !pair.exitPending; };
    worldPairs_.erase(std::remove_if(worldPairs_.begin(), worldPairs_.end(), emptied), worldPairs_.end());
    activeContacts_.erase(std::remove_if(activeContacts_.begin(), activeContacts_.end(), emptied),
                          activeContacts_.end());
}

void CollisionManager::forget(b2Body* body) {
    if (!body) return;

    // The body's colliders (a classic Body links one to every fixture)
    std::vector<Collider2D*> colliders;
    for (b2Fixture* f = body->GetFixtureList(); f; f = f->GetNext()) {
        Collider2D* c = getColliderFromFixture(f);
        if (c && std::find(colliders.begin(), colliders.end(), c) == colliders.end()) {
            colliders.push_back(c);
        }
    }

    // Pairs that still have contacts are ended by DestroyBody() (EndContact).
    for (auto& pair : worldPairs_) {
        if (!pair.contacts.empty()) continue;
        if (pair.a == body) pair.a = nullptr;
        if (pair.b == body) pair.b = nullptr;
        if (pair.exit.a == body) pair.exit.a = nullptr;
        if (pair.exit.b == body) pair.exit.b = nullptr;
    }
    for (auto& pair : activeContacts_) {
        if (!pair.contacts.empty()) continue;
        for (Collider2D* c : colliders) {
            if (pair.a == c) {
                pair.a = nullptr;
                pair.exit.b.other = nullptr;   // B's event about A
            }
            if (pair.b == c) {
                pair.b = nullptr;
                pair.exit.a.other = nullptr;
            }
        }
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
