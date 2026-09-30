// =============================================================================
// tcxCollisionManager.h - Collision Event Management
// =============================================================================

#pragma once

#include "tcxCollider2D.h"
#include "tcxCollisionEvent.h"
#include <TrussC.h>          // tc::Event, tc::Vec2
#include <box2d/box2d.h>
#include <vector>
#include <unordered_set>

namespace tcx::box2d {

// Forward declarations
class World;

// =============================================================================
// WorldContact - world-level contact info (raw b2Body pair)
// =============================================================================
// Emitted by CollisionManager regardless of whether the bodies carry a
// Collider2D. The Mod layer (RigidBody2D) routes these to per-node collision
// events; the legacy Collider2D dispatch below is independent and unchanged.
struct WorldContact {
    b2Body* a = nullptr;
    b2Body* b = nullptr;
    tc::Vec2 point;    // pixels (zero if no manifold points, e.g. on end)
    tc::Vec2 normal;   // world-space contact normal
};

// =============================================================================
// CollisionManager - Box2D Contact Listener
// =============================================================================
// Manages collision events and dispatches them to Collider2D components.
// Registered to b2World as the ContactListener.
// =============================================================================
class CollisionManager : public b2ContactListener {
public:
    CollisionManager() = default;
    ~CollisionManager() = default;

    // Non-copyable
    CollisionManager(const CollisionManager&) = delete;
    CollisionManager& operator=(const CollisionManager&) = delete;

    // -------------------------------------------------------------------------
    // World-level contact events (fired for ALL touching body pairs, whether or
    // not they carry a Collider2D). Used by the Mod layer (RigidBody2D).
    // Events are per body pair: when several fixtures touch (compound bodies,
    // or the four walls of World::createBounds(), which are one body), Began
    // fires on the first contact, Stay once per update, and Ended when the
    // last contact ends. The Collider2D events below count the same way.
    // A pair whose last contact ends inside b2World::Step() gets its Ended /
    // Exit after the step (World::update() dispatches it), so a body sliding
    // from one fixture onto another in one step never sees Ended + Began.
    // If you call b2World::Step() yourself, call update() right after it,
    // before creating or destroying bodies. A body destroyed before its
    // deferred Ended / Exit fires (Body::destroy(), a RigidBody2D's node
    // going away) gets none, and the other side still gets its own with that
    // body null (WorldContact::a / b, CollisionEvent::other,
    // Contact2D::other). Listeners of the deferred events and of Stay may
    // destroy bodies.
    // -------------------------------------------------------------------------
    tc::Event<WorldContact> contactBegan;   // started touching
    tc::Event<WorldContact> contactStay;    // still touching, once per World::update()
    tc::Event<WorldContact> contactEnded;   // stopped touching

    // -------------------------------------------------------------------------
    // Update (called each frame): dispatch the Ended / Exit events deferred
    // from the last step, then the Stay events.
    // -------------------------------------------------------------------------
    void update();

    // -------------------------------------------------------------------------
    // b2ContactListener Implementation
    // -------------------------------------------------------------------------
    void BeginContact(b2Contact* contact) override;
    void EndContact(b2Contact* contact) override;
    void PreSolve(b2Contact* contact, const b2Manifold* oldManifold) override;
    void PostSolve(b2Contact* contact, const b2ContactImpulse* impulse) override;

private:
    friend class World;         // calls flushPendingExits() after each step
    friend class Body;          // forget() before destroying its b2Body
    friend class RigidBody2D;   // same

    // -------------------------------------------------------------------------
    // Contact Pair Tracking
    // -------------------------------------------------------------------------
    // The touching contacts of one pair (colliders or bodies, unordered).
    // Enter/Began fires when the first one begins, Exit/Ended when the last
    // one ends, and Stay once per update with the first contact.
    //   - The last contact ending inside b2World::Step() only marks the pair
    //     exitPending, with the Exit payload built from that contact (it may
    //     be freed before the step ends). A contact of the pair that begins
    //     later in the same step clears the mark, so a hand-over from one
    //     fixture to another is no Exit + Enter; flushPendingExits() fires
    //     the rest after the step.
    //   - While update() or flushPendingExits() dispatches, a pair whose
    //     last contact ends (a listener destroyed or disabled a body) is left
    //     in place with no contacts, since the loops walk the vector by
    //     index, and dropped after them. Each side is read from the pair
    //     again right before it is notified.
    //   - A body about to be destroyed is nulled out of its pairs with no
    //     contacts left (forget()): they can outlive it, and their Exit must
    //     not reach it, or a new body allocated at its address.
    template<typename T, typename Exit>
    struct ContactPair {
        T* a = nullptr;
        T* b = nullptr;
        std::vector<b2Contact*> contacts;
        bool exitPending = false;
        Exit exit;   // valid while exitPending

        bool is(const T* x, const T* y) const {
            return (a == x && b == y) || (a == y && b == x);
        }
    };

    // Exit payload of a collider pair: the event each side receives.
    struct ColliderExit {
        CollisionEvent a;   // for pair.a, about pair.b
        CollisionEvent b;   // for pair.b, about pair.a
    };

    using ColliderPair = ContactPair<Collider2D, ColliderExit>;
    using BodyPair = ContactPair<b2Body, WorldContact>;

    // Collider pairs (Collider2D events)
    std::vector<ColliderPair> activeContacts_;

    // Body pairs (world-level events), independent of colliders.
    std::vector<BodyPair> worldPairs_;

    // True while update() iterates the pair vectors (see ContactPair).
    bool dispatching_ = false;

    // Fire the Ended / Exit events deferred inside the last step.
    void flushPendingExits();

    // `body` is about to be destroyed (b2World::DestroyBody()). Null it and
    // its Collider2D out of every pair with no contacts left: a pending Exit
    // then reaches only the other side, with this body null in its payload.
    // Pairs that still have contacts are ended by DestroyBody() itself.
    void forget(b2Body* body);

    // Drop the pairs left empty by a dispatch (see ContactPair).
    void dropEmptied();

    // -------------------------------------------------------------------------
    // Helper Methods
    // -------------------------------------------------------------------------

    // Build a WorldContact (raw body pair + manifold) from a b2Contact.
    static WorldContact makeWorldContact(b2Contact* contact);

    // Get Collider2D from fixture (stored in UserData)
    static Collider2D* getColliderFromFixture(b2Fixture* fixture);

    // Create CollisionEvent from contact
    static CollisionEvent createEvent(b2Contact* contact, Collider2D* self, Collider2D* other);

    // Add a contact to its pair. Returns true if it starts the pair (Enter),
    // false if the pair already touches or had an Exit pending (cancelled).
    template<typename T, typename Exit>
    static bool addContact(std::vector<ContactPair<T, Exit>>& pairs, T* a, T* b, b2Contact* contact);

    // Remove a contact from its pair. Returns the pair if it was the pair's
    // last contact (still in `pairs`; see endPair()), else null.
    template<typename T, typename Exit>
    static ContactPair<T, Exit>* removeContact(std::vector<ContactPair<T, Exit>>& pairs,
                                               T* a, T* b, b2Contact* contact);

    // A pair lost its last contact: outside a step, drop it (or leave it
    // empty while update() dispatches) and return true to fire Exit now;
    // inside a step, mark it exitPending with `exit` and return false.
    template<typename T, typename Exit>
    bool endPair(std::vector<ContactPair<T, Exit>>& pairs, ContactPair<T, Exit>* pair,
                 const Exit& exit, bool stepping);
};

} // namespace tcx::box2d
