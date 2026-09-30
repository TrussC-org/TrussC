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
    // Events are per body pair: when several fixtures touch (compound bodies),
    // Began fires on the first contact, Stay once per update, and Ended when
    // the last contact ends. The Collider2D events below count the same way.
    // -------------------------------------------------------------------------
    tc::Event<WorldContact> contactBegan;   // started touching
    tc::Event<WorldContact> contactStay;    // still touching, every step
    tc::Event<WorldContact> contactEnded;   // stopped touching

    // -------------------------------------------------------------------------
    // Update (called each frame to dispatch Stay events)
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
    // -------------------------------------------------------------------------
    // Contact Pair Tracking
    // -------------------------------------------------------------------------
    // The touching contacts of one pair (colliders or bodies, unordered).
    // Enter/Began fires when the first one begins, Exit/Ended when the last
    // one ends, and Stay once per update with the first contact.
    template<typename T>
    struct ContactPair {
        T* a = nullptr;
        T* b = nullptr;
        std::vector<b2Contact*> contacts;

        bool is(const T* x, const T* y) const {
            return (a == x && b == y) || (a == y && b == x);
        }
    };

    // Collider pairs (Collider2D events)
    std::vector<ContactPair<Collider2D>> activeContacts_;

    // Body pairs (world-level events), independent of colliders.
    std::vector<ContactPair<b2Body>> worldPairs_;

    // -------------------------------------------------------------------------
    // Helper Methods
    // -------------------------------------------------------------------------

    // Build a WorldContact (raw body pair + manifold) from a b2Contact.
    static WorldContact makeWorldContact(b2Contact* contact);

    // Get Collider2D from fixture (stored in UserData)
    static Collider2D* getColliderFromFixture(b2Fixture* fixture);

    // Create CollisionEvent from contact
    static CollisionEvent createEvent(b2Contact* contact, Collider2D* self, Collider2D* other);

    // Add a contact to its pair. Returns true if it is the pair's first.
    template<typename T>
    static bool addContact(std::vector<ContactPair<T>>& pairs, T* a, T* b, b2Contact* contact);

    // Remove a contact from its pair. Returns true if it was the pair's last
    // (the pair is then dropped).
    template<typename T>
    static bool removeContact(std::vector<ContactPair<T>>& pairs, T* a, T* b, b2Contact* contact);
};

} // namespace tcx::box2d
