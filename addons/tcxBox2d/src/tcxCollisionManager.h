// =============================================================================
// tcxCollisionManager.h - Collision Event Management
// =============================================================================

#pragma once

#include "tcxCollider2D.h"
#include "tcxCollisionEvent.h"
#include <box2d/box2d.h>
#include <vector>
#include <unordered_set>

namespace tcx::box2d {

// Forward declarations
class World;

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
    // Contact Pair Tracking (for Stay events)
    // -------------------------------------------------------------------------
    struct ContactPair {
        Collider2D* a = nullptr;
        Collider2D* b = nullptr;
        b2Contact* contact = nullptr;

        bool operator==(const ContactPair& other) const {
            return (a == other.a && b == other.b) ||
                   (a == other.b && b == other.a);
        }
    };

    struct ContactPairHash {
        size_t operator()(const ContactPair& p) const {
            // Order-independent hash
            auto ha = std::hash<void*>{}(p.a);
            auto hb = std::hash<void*>{}(p.b);
            return ha ^ hb;
        }
    };

    std::vector<ContactPair> activeContacts_;

    // -------------------------------------------------------------------------
    // Helper Methods
    // -------------------------------------------------------------------------

    // Get Collider2D from fixture (stored in UserData)
    static Collider2D* getColliderFromFixture(b2Fixture* fixture);

    // Create CollisionEvent from contact
    static CollisionEvent createEvent(b2Contact* contact, Collider2D* self, Collider2D* other);

    // Find and remove contact pair
    void removeContactPair(Collider2D* a, Collider2D* b);
};

} // namespace tcx::box2d
