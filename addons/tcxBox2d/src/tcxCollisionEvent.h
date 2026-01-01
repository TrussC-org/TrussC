// =============================================================================
// tcxCollisionEvent.h - Collision Event Data
// =============================================================================

#pragma once

#include <TrussC.h>

namespace tcx::box2d {

// Forward declaration
class Body;

// =============================================================================
// CollisionEvent - Data passed to collision callbacks
// =============================================================================
struct CollisionEvent {
    Body* other = nullptr;         // The other body in the collision
    tc::Vec2 contactPoint;         // Contact point in pixels
    tc::Vec2 normal;               // Collision normal (direction of impact)
    float impulse = 0.0f;          // Impact magnitude (available in PostSolve)

    // Convenience methods
    bool hasContactPoint() const { return other != nullptr; }
};

} // namespace tcx::box2d
