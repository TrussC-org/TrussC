// =============================================================================
// tcxCollider2D.h - 2D Collider Component
// =============================================================================

#pragma once

#include "tcxCollisionEvent.h"
#include <tc/events/tcEvent.h>
#include <box2d/box2d.h>

namespace tcx::box2d {

// Forward declarations
class Body;
class CollisionManager;

// =============================================================================
// Collider2D - Base collider component
// =============================================================================
// Attach to a Body to receive collision callbacks.
// Use isTrigger for detection-only (no physics response).
// =============================================================================
class Collider2D {
public:
    // -------------------------------------------------------------------------
    // Collision Events (use tc::Event<T> pattern)
    // -------------------------------------------------------------------------
    trussc::Event<CollisionEvent> onCollisionEnter;  // Called when collision starts
    trussc::Event<CollisionEvent> onCollisionStay;   // Called each frame during collision
    trussc::Event<CollisionEvent> onCollisionExit;   // Called when collision ends

    // -------------------------------------------------------------------------
    // Sensor Mode
    // -------------------------------------------------------------------------
    // When true, collision is detected but no physics response occurs.
    // Useful for triggers, pickups, detection zones, etc.
    bool isTrigger = false;

    // -------------------------------------------------------------------------
    // Collision Filtering (wraps Box2D b2Filter)
    // -------------------------------------------------------------------------
    // Category: what this collider "is" (bitmask)
    // Mask: what this collider collides "with" (bitmask)
    // Collision occurs when: (A.category & B.mask) && (B.category & A.mask)

    void setCategoryBits(uint16_t bits);
    void setMaskBits(uint16_t bits);
    void setGroupIndex(int16_t index);

    uint16_t getCategoryBits() const;
    uint16_t getMaskBits() const;
    int16_t getGroupIndex() const;

    // -------------------------------------------------------------------------
    // Accessors
    // -------------------------------------------------------------------------
    Body* getBody() const { return body_; }
    b2Fixture* getFixture() const { return fixture_; }

    virtual ~Collider2D() = default;

protected:
    friend class Body;
    friend class CollisionManager;

    Body* body_ = nullptr;
    b2Fixture* fixture_ = nullptr;

    // Internal: called by CollisionManager
    void notifyEnter(CollisionEvent& e) { onCollisionEnter.notify(e); }
    void notifyStay(CollisionEvent& e) { onCollisionStay.notify(e); }
    void notifyExit(CollisionEvent& e) { onCollisionExit.notify(e); }

    // Apply isTrigger to fixture
    void applyTriggerMode();
};

// =============================================================================
// CircleCollider2D - Circle-shaped collider
// =============================================================================
class CircleCollider2D : public Collider2D {
public:
    float getRadius() const { return radius_; }
    void setRadius(float r) { radius_ = r; }

private:
    float radius_ = 0.0f;
};

// =============================================================================
// BoxCollider2D - Rectangle-shaped collider
// =============================================================================
class BoxCollider2D : public Collider2D {
public:
    float getWidth() const { return width_; }
    float getHeight() const { return height_; }
    void setSize(float w, float h) { width_ = w; height_ = h; }

private:
    float width_ = 0.0f;
    float height_ = 0.0f;
};

// =============================================================================
// PolygonCollider2D - Polygon-shaped collider
// =============================================================================
class PolygonCollider2D : public Collider2D {
public:
    int getVertexCount() const { return vertexCount_; }
    void setVertexCount(int count) { vertexCount_ = count; }

private:
    int vertexCount_ = 0;
};

} // namespace tcx::box2d
